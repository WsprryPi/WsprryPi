// SPDX-License-Identifier: MIT
#include "pi_pair_crypto.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
#include "pi_management_journal.hpp"
#include <botan/auto_rng.h>
#include <botan/ec_apoint.h>
#include <botan/ec_scalar.h>
#include <botan/hash.h>
#include <botan/kdf.h>
#include <botan/mac.h>
#include <botan/pwdhash.h>
#include <botan/spake2p.h>
#include <algorithm>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
using Bytes=std::vector<unsigned char>;
using Secure=Botan::secure_vector<unsigned char>;
constexpr std::string_view alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
std::mutex derivation_gate;
std::function<void()> derivation_checkpoint;
[[noreturn]] void fail(PiPairCryptoFailure code){throw PiPairCryptoError(code);}
void require(bool value,PiPairCryptoFailure code=PiPairCryptoFailure::InvalidShape){if(!value)fail(code);}
std::span<const unsigned char> bytes(std::string_view text){return {reinterpret_cast<const unsigned char *>(text.data()),text.size()};}
std::string encode(std::span<const unsigned char> input){
    std::string out((input.size()+2)/3*4,'\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()),input.data(),input.size());
    while(out.ends_with('='))out.pop_back();
    std::replace(out.begin(),out.end(),'+','-');std::replace(out.begin(),out.end(),'/','_');return out;
}
Secure decode(std::string_view input,std::size_t expected){
    require(input.size()==(expected*8+5)/6&&input.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::string_view::npos);
    constexpr std::string_view digits="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const auto tail=(expected*8)%6;
    require(!tail||(digits.find(input.back())&((1U<<(6-tail))-1))==0);
    std::string encoded(input);std::replace(encoded.begin(),encoded.end(),'-','+');std::replace(encoded.begin(),encoded.end(),'_','/');
    struct Wipe{std::string &value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{encoded};
    while(encoded.size()%4)encoded+='=';
    Secure result(encoded.size()/4*3);
    require(EVP_DecodeBlock(result.data(),reinterpret_cast<const unsigned char *>(encoded.data()),encoded.size())==static_cast<int>(result.size()));
    result.resize(expected);return result;
}
Secure hash(std::string_view input){return Botan::HashFunction::create_or_throw("SHA-256")->process(bytes(input));}
Secure derive(std::span<const unsigned char> secret,std::span<const unsigned char> salt,std::string_view label){
    return Botan::KDF::create_or_throw("HKDF(SHA-256)")->derive_key<Secure>(32,secret,salt,bytes(label));
}
Json message(const char *schema,const std::string &pair){return {{"contract","Wsprry-Pair/1"},{"schema",schema},{"pair_id",pair}};}
}
PiPairSecret::PiPairSecret(std::span<const unsigned char> value){require(value.size()==value_.size());std::copy(value.begin(),value.end(),value_.begin());}
PiPairSecret::~PiPairSecret(){OPENSSL_cleanse(value_.data(),value_.size());}
PiPairSecret::PiPairSecret(PiPairSecret &&other) noexcept:value_(other.value_){OPENSSL_cleanse(other.value_.data(),other.value_.size());}
PiPairSecret &PiPairSecret::operator=(PiPairSecret &&other) noexcept{if(this!=&other){value_=other.value_;OPENSSL_cleanse(other.value_.data(),other.value_.size());}return *this;}
std::string PiPairSecret::storage_encoding() const{return encode(value_);}
PiPairSecret PiPairSecret::from_storage(std::string_view value){const auto decoded=decode(value,32);return PiPairSecret(decoded);}
PiPairCode::PiPairCode(std::array<unsigned char,8> value):value_(value){OPENSSL_cleanse(value.data(),value.size());}
PiPairCode::~PiPairCode(){OPENSSL_cleanse(value_.data(),value_.size());}
PiPairCode::PiPairCode(PiPairCode &&other) noexcept:value_(other.value_){OPENSSL_cleanse(other.value_.data(),other.value_.size());}
PiPairCode &PiPairCode::operator=(PiPairCode &&other) noexcept{if(this!=&other){value_=other.value_;OPENSSL_cleanse(other.value_.data(),other.value_.size());}return *this;}
PiPairCode PiPairCode::generate(){
    std::array<unsigned char,8> raw{};
    struct Wipe{std::array<unsigned char,8> &value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{raw};
    require(RAND_bytes(raw.data(),raw.size())==1,PiPairCryptoFailure::Unavailable);
    for(auto &c:raw)c=alphabet[c&31];
    return PiPairCode(raw);
}
PiPairCode PiPairCode::normalize(std::string_view input){
    require(input.size()<=4096);std::array<unsigned char,8> code{};std::size_t count=0;
    struct Wipe{std::array<unsigned char,8> &code;~Wipe(){OPENSSL_cleanse(code.data(),code.size());}} wipe{code};
    for(unsigned char c:input){if(c==' '||c=='-')continue;if(c>='a'&&c<='z')c-=32;
        require(count<code.size()&&alphabet.find(c)!=std::string_view::npos);code[count++]=c;}
    require(count==code.size());return PiPairCode(code);
}
std::string PiPairCode::display() const{return std::string(value_.begin(),value_.begin()+4)+"-"+std::string(value_.begin()+4,value_.end());}
struct PiPairExchange::Impl {
    Role role;
    enum class State {Fresh,Share,Confirm,Complete,Failed};
    State state{State::Fresh};
    std::string pair,context,digest;
    Json hello;
    Secure salt;
    Botan::SPAKE2p::SystemParameters params=Botan::SPAKE2p::SystemParameters::rfc9383_p256_sha256();
    std::unique_ptr<Botan::AutoSeeded_RNG> rng;
    std::unique_ptr<Botan::SPAKE2p::ProverContext> prover;
    std::unique_ptr<Botan::SPAKE2p::VerifierContext> verifier;
    Impl(Role selected,const PiPairCode &code,const Json &hello,const Json &challenge):role(selected),hello(hello){
        require(valid_pi_pair_document(hello,"hello")&&valid_pi_pair_document(challenge,"challenge")&&
            hello["pair_id"]==challenge["pair_id"]&&hello["suite"]==challenge["suite"]);
        require(role==Role::Controller||role==Role::Target);
        require(std::all_of(code.bytes().begin(),code.bytes().end(),[](unsigned char c){return alphabet.find(c)!=std::string_view::npos;}));
        rng=std::make_unique<Botan::AutoSeeded_RNG>();
        pair=hello["pair_id"].get<std::string>();auto transcript=hello;transcript["target_nonce"]=challenge["target_nonce"];
        context=canonical_pi_management_json(transcript);digest=PiManagementJournal::digest(context);salt=hash(context);
        const auto a="Wsprry-Pair/1/controller/"+context,b="Wsprry-Pair/1/target/"+context;
        Secure framed;
        const auto append=[&](std::span<const unsigned char> data){for(unsigned i=0;i<8;++i)framed.push_back(static_cast<std::uint64_t>(data.size())>>(8*i));framed.insert(framed.end(),data.begin(),data.end());};
        append(code.bytes());append(bytes(a));append(bytes(b));
        Secure derived(96);
        {
            std::unique_lock lock(derivation_gate,std::try_to_lock);if(!lock.owns_lock())fail(PiPairCryptoFailure::Busy);
            if(derivation_checkpoint)derivation_checkpoint();
            const auto kdf=Botan::PasswordHashFamily::create_or_throw("Scrypt")->from_params(32768,8,1);
            require(kdf->total_memory_usage()<=32*1024*1024+4096,PiPairCryptoFailure::Unavailable);
            kdf->derive_key(derived.data(),derived.size(),reinterpret_cast<const char *>(framed.data()),framed.size(),salt.data(),salt.size());
        }
        auto w0=Botan::EC_Scalar::from_bytes_mod_order(params.group(),std::span(derived).first(48));
        auto w1=Botan::EC_Scalar::from_bytes_mod_order(params.group(),std::span(derived).last(48));
        require(w0.is_nonzero()&&w1.is_nonzero(),PiPairCryptoFailure::InvalidProof);
        const auto secret=Botan::SPAKE2p::ProverSecret::from_prehashed(std::move(w0),std::move(w1));
        if(role==Role::Controller)prover=std::make_unique<Botan::SPAKE2p::ProverContext>(params,secret,bytes(a),bytes(b),bytes(context));
        else verifier=std::make_unique<Botan::SPAKE2p::VerifierContext>(params,secret.registration_record(*rng),bytes(a),bytes(b),bytes(context));
    }
    void failed() noexcept{prover.reset();verifier.reset();state=State::Failed;}
    void expect(Role expected,State stage){
        if(role!=expected||state!=stage){failed();fail(PiPairCryptoFailure::InvalidShape);}
    }
};
PiPairExchange::PiPairExchange(Role role,const PiPairCode &code,const Json &hello,const Json &challenge){
    try{impl_=std::make_unique<Impl>(role,code,hello,challenge);}catch(const PiPairCryptoError &){throw;}catch(...){fail(PiPairCryptoFailure::Unavailable);}
}
PiPairExchange::~PiPairExchange()=default;
void testing::set_pi_pair_derivation_checkpoint(std::function<void()> checkpoint){
    std::unique_lock lock(derivation_gate,std::try_to_lock);if(!lock.owns_lock())fail(PiPairCryptoFailure::Busy);
    derivation_checkpoint=std::move(checkpoint);
}
PiPairExchange::PiPairExchange(PiPairExchange &&) noexcept=default;
PiPairExchange &PiPairExchange::operator=(PiPairExchange &&) noexcept=default;
Json PiPairExchange::start_controller(){
    require(bool(impl_));impl_->expect(Role::Controller,Impl::State::Fresh);
    try{auto out=message("exchange",impl_->pair);out["pA"]=encode(impl_->prover->generate_message(*impl_->rng));impl_->state=Impl::State::Share;return out;}
    catch(...){impl_->failed();fail(PiPairCryptoFailure::Unavailable);}
}
Json PiPairExchange::target_exchange(const Json &request){
    require(bool(impl_));impl_->expect(Role::Target,Impl::State::Fresh);
    try{
        require(valid_pi_pair_document(request,"exchange-request")&&request["pair_id"]==impl_->pair);
        const auto share=decode(request["pA"].get<std::string>(),65);
        const auto response=impl_->verifier->process_message(share,*impl_->rng);
        auto out=message("exchange",impl_->pair);out["pB"]=encode(std::span(response).first(65));out["cB"]=encode(std::span(response).last(32));
        impl_->state=Impl::State::Share;return out;
    }catch(...){impl_->failed();fail(PiPairCryptoFailure::InvalidProof);}
}
Json PiPairExchange::controller_confirmation(const Json &request){
    require(bool(impl_));impl_->expect(Role::Controller,Impl::State::Share);
    try{
        require(valid_pi_pair_document(request,"exchange-response")&&request["pair_id"]==impl_->pair);
        auto response=decode(request["pB"].get<std::string>(),65);const auto confirmation=decode(request["cB"].get<std::string>(),32);
        response.insert(response.end(),confirmation.begin(),confirmation.end());
        auto out=message("confirm",impl_->pair);out["cA"]=encode(impl_->prover->process_message(response,*impl_->rng));impl_->state=Impl::State::Confirm;return out;
    }catch(...){impl_->failed();fail(PiPairCryptoFailure::InvalidProof);}
}
Json PiPairExchange::target_confirmation(const Json &request){
    require(bool(impl_));impl_->expect(Role::Target,Impl::State::Share);
    try{
        require(valid_pi_pair_document(request,"confirm")&&request["pair_id"]==impl_->pair);
        impl_->verifier->verify_confirmation(decode(request["cA"].get<std::string>(),32));impl_->state=Impl::State::Complete;
        return message("confirmed",impl_->pair);
    }catch(...){impl_->failed();fail(PiPairCryptoFailure::InvalidProof);}
}
void PiPairExchange::controller_confirmed(const Json &request){
    require(bool(impl_));impl_->expect(Role::Controller,Impl::State::Confirm);
    try{require(valid_pi_pair_document(request,"confirmed")&&request["pair_id"]==impl_->pair);impl_->state=Impl::State::Complete;}
    catch(...){impl_->failed();fail(PiPairCryptoFailure::InvalidProof);}
}
PiPairKeys PiPairExchange::keys() const{
    require(impl_&&impl_->state==Impl::State::Complete);
    const auto secret=impl_->role==Role::Controller?impl_->prover->shared_secret():impl_->verifier->shared_secret();
    return {PiPairSecret(derive(secret,impl_->salt,"Wsprry-Pair/1/spake2plus/tls-psk")),
        PiPairSecret(derive(secret,impl_->salt,"Wsprry-Pair/1/spake2plus/commit")),
        PiPairSecret(derive(secret,impl_->salt,"Wsprry-Pair/1/spake2plus/retirement"))};
}
const std::string &PiPairExchange::request_digest() const{require(bool(impl_));return impl_->digest;}
const Json &PiPairExchange::confirmed_controller_hello() const{
    require(bool(impl_)&&impl_->role==Role::Controller&&impl_->state==Impl::State::Complete);return impl_->hello;
}
bool PiPairExchange::valid_peer_point(std::string_view encoded){
    try{const auto decoded=decode(encoded,65);if(decoded.front()!=4)return false;
        const auto params=Botan::SPAKE2p::SystemParameters::rfc9383_p256_sha256();
        return bool(Botan::EC_AffinePoint::deserialize_uncompressed(params.group(),decoded));
    }catch(...){return false;}
}
std::string pi_pair_mac(const PiPairSecret &key,std::string_view input){
    auto mac=Botan::MessageAuthenticationCode::create_or_throw("HMAC(SHA-256)");mac->set_key(key.bytes());mac->update(bytes(input));return encode(mac->final());
}
bool pi_pair_verify_mac(const PiPairSecret &key,std::string_view input,std::string_view proof){
    try{auto mac=Botan::MessageAuthenticationCode::create_or_throw("HMAC(SHA-256)");mac->set_key(key.bytes());mac->update(bytes(input));return mac->verify_mac(decode(proof,32));}
    catch(...){return false;}
}
} // namespace wsprrypi
