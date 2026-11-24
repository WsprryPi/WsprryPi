// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_crypto.hpp"
#include "wtp_integration/pi_pair_contract.hpp"
namespace pairing_vector {
#include "pairing_provider/profile_vector.hpp"
}
#include <botan/auto_rng.h>
#include <botan/hex.h>
#include <botan/kdf.h>
#include <botan/spake2p.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <algorithm>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
namespace {
using Json=nlohmann::json;
using Bytes=std::vector<unsigned char>;
unsigned checks{};
void check(bool v){++checks;if(!v)throw std::runtime_error("pair_crypto_test_failed");}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(const wsprrypi::PiPairCryptoError &e){rejected=true;check(std::string_view(e.what())=="pairing_crypto_failed");}check(rejected);}
std::span<const unsigned char> as_bytes(std::string_view value){return {reinterpret_cast<const unsigned char *>(value.data()),value.size()};}
std::string encode(std::span<const unsigned char> value){
    std::string result((value.size()+2)/3*4,'\0');EVP_EncodeBlock(reinterpret_cast<unsigned char *>(result.data()),value.data(),static_cast<int>(value.size()));
    while(result.ends_with('='))result.pop_back();
    std::replace(result.begin(),result.end(),'+','-');std::replace(result.begin(),result.end(),'/','_');return result;
}
Bytes decode(std::string value,std::size_t n){
    std::replace(value.begin(),value.end(),'-','+');std::replace(value.begin(),value.end(),'_','/');while(value.size()%4)value+='=';
    Bytes result(value.size()/4*3);check(EVP_DecodeBlock(result.data(),reinterpret_cast<const unsigned char *>(value.data()),static_cast<int>(value.size()))==static_cast<int>(result.size()));result.resize(n);return result;
}
void keys_match(const wsprrypi::PiPairKeys &actual,std::span<const unsigned char> secret){
    const auto salt=Botan::hex_decode(pairing_vector::salt_hex);
    const auto derived=[&](const char *label){return Botan::KDF::create_or_throw("HKDF(SHA-256)")->derive_key(32,secret,salt,as_bytes(label));};
    for(const auto &[key,label]:{std::pair{&actual.tls,"Wsprry-Pair/1/spake2plus/tls-psk"},
        std::pair{&actual.commit,"Wsprry-Pair/1/spake2plus/commit"},std::pair{&actual.retirement,"Wsprry-Pair/1/spake2plus/retirement"}}){
        const auto expected=derived(label);check(CRYPTO_memcmp(expected.data(),key->bytes().data(),32)==0);
    }
}
}
int main(){
    try{
        using namespace wsprrypi;
        static_assert(!std::is_copy_constructible_v<PiPairSecret>);
        static_assert(!std::is_copy_constructible_v<PiPairCode>);
        static_assert(!std::is_constructible_v<Json,PiPairSecret>);
        const auto code=PiPairCode::normalize(" 2345-ab cd ");check(code.display()=="2345-ABCD");
        for(const auto input:{"2345ABCI","2345ABCL","2345ABCO","2345ABCU","2345ABC","2345ABCDE","2345\tABCD","2345ABCé"})
            refuses([&]{(void)PiPairCode::normalize(input);});
        const auto generated=PiPairCode::generate();check(generated.display().size()==9);check(PiPairCode::normalize(generated.display()).display()==generated.display());
        auto hello=Json::parse(pairing_vector::context);const auto target_nonce=hello.at("target_nonce");hello.erase("target_nonce");
        const Json challenge{{"contract","Wsprry-Pair/1"},{"schema","challenge"},{"suite",hello["suite"]},{"pair_id",hello["pair_id"]},{"target_nonce",target_nonce}};
        const auto params=Botan::SPAKE2p::SystemParameters::rfc9383_p256_sha256();
        const auto scalar_bytes=Botan::hex_decode(pairing_vector::scalars_hex);
        const auto secret=Botan::SPAKE2p::ProverSecret::deserialize(params,scalar_bytes);
        Botan::AutoSeeded_RNG rng;
        // Independent fixture scalars, not another copy of the application's
        // password mapping, drive the peer in each direction.
        PiPairExchange controller(PiPairExchange::Role::Controller,code,hello,challenge);
        check(controller.request_digest()==pairing_vector::salt_hex);refuses([&]{(void)controller.keys();});
        refuses([&]{(void)controller.confirmed_controller_hello();});
        Botan::SPAKE2p::VerifierContext peer(params,secret.registration_record(rng),as_bytes(pairing_vector::prover_id),as_bytes(pairing_vector::verifier_id),as_bytes(pairing_vector::context));
        const auto initial=controller.start_controller();check(valid_pi_pair_document(initial,"exchange-request"));
        check(PiPairExchange::valid_peer_point(initial["pA"].get<std::string>()));
        const auto reply=peer.process_message(decode(initial["pA"],65),rng);
        const Json response{{"contract","Wsprry-Pair/1"},{"schema","exchange"},{"pair_id",hello["pair_id"]},
            {"pB",encode(std::span(reply).first(65))},{"cB",encode(std::span(reply).last(32))}};
        const auto proof=controller.controller_confirmation(response);refuses([&]{(void)controller.keys();});
        peer.verify_confirmation(decode(proof["cA"],32));
        const Json ack{{"contract","Wsprry-Pair/1"},{"schema","confirmed"},{"pair_id",hello["pair_id"]}};
        controller.controller_confirmed(ack);const auto controller_keys=controller.keys();keys_match(controller_keys,peer.shared_secret());
        check(controller.confirmed_controller_hello()==hello);
        const auto mac=pi_pair_mac(controller_keys.commit,"request/"+controller.request_digest());
        check(pi_pair_verify_mac(controller_keys.commit,"request/"+controller.request_digest(),mac));
        check(!pi_pair_verify_mac(controller_keys.tls,"request/"+controller.request_digest(),mac));
        check(!pi_pair_verify_mac(controller_keys.commit,"changed",mac));
        check(!pi_pair_verify_mac(controller_keys.commit,"request/"+controller.request_digest(),std::string(42,'A')+"B"));
        const auto restored=PiPairSecret::from_storage(controller_keys.tls.storage_encoding());
        check(CRYPTO_memcmp(restored.bytes().data(),controller_keys.tls.bytes().data(),32)==0);
        refuses([&]{(void)PiPairSecret::from_storage(std::string(42,'A')+"B");});
        PiPairExchange target(PiPairExchange::Role::Target,code,hello,challenge);
        Botan::SPAKE2p::ProverContext other(params,secret,as_bytes(pairing_vector::prover_id),as_bytes(pairing_vector::verifier_id),as_bytes(pairing_vector::context));
        const auto share=other.generate_message(rng);
        auto request=initial;request["pA"]=encode(share);const auto target_reply=target.target_exchange(request);
        auto combined=decode(target_reply["pB"],65);const auto cb=decode(target_reply["cB"],32);combined.insert(combined.end(),cb.begin(),cb.end());
        const auto ca=other.process_message(combined,rng);auto confirmation=proof;confirmation["cA"]=encode(ca);
        refuses([&]{(void)target.keys();});check(target.target_confirmation(confirmation)==ack);
        const auto target_keys=target.keys();keys_match(target_keys,other.shared_secret());
        refuses([&]{(void)target.confirmed_controller_hello();});
        const auto wrong=PiPairCode::normalize("2345ABCE");
        PiPairExchange bad_target(PiPairExchange::Role::Target,wrong,hello,challenge);
        PiPairExchange good_controller(PiPairExchange::Role::Controller,code,hello,challenge);
        const auto bad_response=bad_target.target_exchange(good_controller.start_controller());
        refuses([&]{(void)good_controller.controller_confirmation(bad_response);});
        refuses([&]{(void)good_controller.keys();});refuses([&]{(void)good_controller.controller_confirmation(response);});
        PiPairExchange malformed_target(PiPairExchange::Role::Target,code,hello,challenge);
        auto malformed=initial;Bytes zero(65);zero.front()=4;malformed["pA"]=encode(zero);
        check(!PiPairExchange::valid_peer_point(malformed["pA"].get<std::string>()));
        refuses([&]{(void)malformed_target.target_exchange(malformed);});
        refuses([&]{(void)malformed_target.target_exchange(initial);});refuses([&]{(void)malformed_target.keys();});
        auto changed=challenge;changed["pair_id"]=std::string(32,'1');
        refuses([&]{PiPairExchange invalid(PiPairExchange::Role::Target,code,hello,changed);});
        // Context/role disagreement must fail actual peer authentication, not
        // merely compare two copies of the application's own derived output.
        changed=challenge;changed["target_nonce"]=std::string(42,'A')+"E";
        PiPairExchange other_context(PiPairExchange::Role::Target,code,hello,changed);
        PiPairExchange original_context(PiPairExchange::Role::Controller,code,hello,challenge);
        const auto context_response=other_context.target_exchange(original_context.start_controller());
        refuses([&]{(void)original_context.controller_confirmation(context_response);});
        refuses([&]{(void)original_context.keys();});
        PiPairExchange invalid_confirmation(PiPairExchange::Role::Target,code,hello,challenge);
        (void)invalid_confirmation.target_exchange(initial);
        auto tampered=proof;tampered["cA"]=std::string(43,'A');
        refuses([&]{(void)invalid_confirmation.target_confirmation(tampered);});
        refuses([&]{(void)invalid_confirmation.keys();});
        // Exact wire replay belongs to the owner's retained response. Calling
        // a completed crypto step again cannot leave reusable ephemeral state.
        refuses([&]{(void)controller.start_controller();});
        refuses([&]{(void)controller.keys();});
        // Hold the actual reserved slot at a typed checkpoint. A competing
        // derivation must return Busy immediately rather than allocate another
        // scrypt workspace or wait behind the first operation.
        std::mutex gate;std::condition_variable progress;bool entered=false,released=false;std::exception_ptr worker_failure;
        testing::set_pi_pair_derivation_checkpoint([&]{
            std::unique_lock lock(gate);entered=true;progress.notify_all();
            if(!progress.wait_for(lock,std::chrono::seconds(5),[&]{return released;}))throw std::runtime_error("fixture checkpoint deadline");
        });
        std::thread worker([&]{try{PiPairExchange exchange(PiPairExchange::Role::Controller,code,hello,challenge);}
            catch(...){worker_failure=std::current_exception();}});
        bool slot_entered=false;
        {std::unique_lock lock(gate);slot_entered=progress.wait_for(lock,std::chrono::seconds(3),[&]{return entered;});}
        bool busy=false,checkpoint_busy=false;
        if(slot_entered){
            try{PiPairExchange competing(PiPairExchange::Role::Target,code,hello,challenge);}
            catch(const PiPairCryptoError &e){busy=e.code()==PiPairCryptoFailure::Busy;}
            try{testing::set_pi_pair_derivation_checkpoint({});}
            catch(const PiPairCryptoError &e){checkpoint_busy=e.code()==PiPairCryptoFailure::Busy;}
        }
        {std::lock_guard lock(gate);released=true;}progress.notify_all();worker.join();
        testing::set_pi_pair_derivation_checkpoint({});
        check(slot_entered&&busy&&checkpoint_busy&&!worker_failure);
        PiPairExchange after_busy(PiPairExchange::Role::Controller,code,hello,challenge);
        check(valid_pi_pair_document(after_busy.start_controller(),"exchange-request"));
        std::cout<<"PASS production Pair/1 crypto: independent scalar peers in both directions, exact transcript/keys, normalization, confirmation, canonical encodings and terminal failures; "<<checks<<" checks\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
