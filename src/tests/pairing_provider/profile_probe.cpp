// MIT. Hardware-free qualification only; all password/vector inputs are public fixtures.
#include <botan/auto_rng.h>
#include <botan/ec_scalar.h>
#include <botan/exceptn.h>
#include <botan/hex.h>
#include <botan/pwdhash.h>
#include <botan/spake2p.h>
#include <botan/version.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#include "profile_vector.hpp"
#ifdef PROVIDER_WITH_OPENSSL
#include <openssl/crypto.h>
#endif
using namespace Botan;
using namespace Botan::SPAKE2p;
#ifndef EXPECTED_BOTAN_PATCH
#define EXPECTED_BOTAN_PATCH 1
#endif
namespace {
unsigned checks=0;
void require(bool value){++checks;if(!value)throw std::runtime_error("provider qualification failed");}
template<typename F> void rejected(F action){bool refused=false;try{action();}catch(const Botan::Exception&){refused=true;}require(refused);}
std::span<const uint8_t> bytes(std::string_view value){return {reinterpret_cast<const uint8_t*>(value.data()),value.size()};}
}
int main(){
 try{
  const auto start=std::chrono::steady_clock::now();
  require(version_major()==3&&version_minor()==14&&version_patch()==EXPECTED_BOTAN_PATCH);
  const auto params=SystemParameters::rfc9383_p256_sha256();
  require(params.share_size()==65&&params.confirmation_size()==32);
  AutoSeeded_RNG rng;
  const auto framed=hex_decode(framed_hex),salt=hex_decode(salt_hex);
  secure_vector<uint8_t> derived(96);
  const auto kdf=PasswordHashFamily::create_or_throw("Scrypt")->from_params(32768,8,1);
  require(kdf->total_memory_usage()<=32*1024*1024+4096);
  kdf->derive_key(derived.data(),derived.size(),reinterpret_cast<const char*>(framed.data()),framed.size(),salt.data(),salt.size());
  require(hex_encode(derived)==hex_encode(hex_decode(derived_hex)));
  const auto w0=EC_Scalar::from_bytes_mod_order(params.group(),std::span(derived).first(48));
  const auto w1=EC_Scalar::from_bytes_mod_order(params.group(),std::span(derived).last(48));
  const auto secret=ProverSecret::from_prehashed(w0,w1);
  require(hex_encode(secret.serialize())==hex_encode(hex_decode(scalars_hex)));
  const auto record=secret.registration_record(rng);
  ProverContext prover(params,secret,bytes(prover_id),bytes(verifier_id),bytes(context));
  VerifierContext verifier(params,record,bytes(prover_id),bytes(verifier_id),bytes(context));
  rejected([&]{(void)prover.shared_secret();});
  rejected([&]{(void)verifier.shared_secret();});
  const auto a=prover.generate_message(rng);
  const auto b=verifier.process_message(a,rng);
  require(a.size()==65&&b.size()==97);
  const auto confirmation=prover.process_message(b,rng);
  rejected([&]{(void)verifier.shared_secret();});
  verifier.verify_confirmation(confirmation);
  require(prover.shared_secret().size()==32&&prover.shared_secret()==verifier.shared_secret());
  rejected([&]{(void)prover.generate_message(rng);});
  auto wrong_input=framed;wrong_input[8]='3';secure_vector<uint8_t> wrong_derived(96);
  kdf->derive_key(wrong_derived.data(),wrong_derived.size(),reinterpret_cast<const char*>(wrong_input.data()),wrong_input.size(),salt.data(),salt.size());
  const auto wrong_secret=ProverSecret::from_prehashed(
      EC_Scalar::from_bytes_mod_order(params.group(),std::span(wrong_derived).first(48)),
      EC_Scalar::from_bytes_mod_order(params.group(),std::span(wrong_derived).last(48)));
  ProverContext wrong_prover(params,wrong_secret,bytes(prover_id),bytes(verifier_id),bytes(context));
  VerifierContext correct_verifier(params,record,bytes(prover_id),bytes(verifier_id),bytes(context));
  const auto wrong_message=correct_verifier.process_message(wrong_prover.generate_message(rng),rng);
  rejected([&]{(void)wrong_prover.process_message(wrong_message,rng);});
  rejected([&]{(void)wrong_prover.shared_secret();});
  for(const auto altered:{0,1}){
   ProverContext p(params,secret,bytes(altered?prover_id:verifier_id),bytes(verifier_id),bytes(altered?"wrong-context":context));
   VerifierContext v(params,record,bytes(prover_id),bytes(verifier_id),bytes(context));
   const auto message=v.process_message(p.generate_message(rng),rng);
   rejected([&]{(void)p.process_message(message,rng);});
   rejected([&]{(void)p.shared_secret();});
  }
  VerifierContext bad_point(params,record,bytes(prover_id),bytes(verifier_id),bytes(context));
  std::vector<uint8_t> malformed(65);malformed.front()=4;
  rejected([&]{(void)bad_point.process_message(malformed,rng);});
  rejected([&]{(void)bad_point.process_message(a,rng);});
  VerifierContext bad_confirmation(params,record,bytes(prover_id),bytes(verifier_id),bytes(context));
  ProverContext p(params,secret,bytes(prover_id),bytes(verifier_id),bytes(context));
  auto proof=p.process_message(bad_confirmation.process_message(p.generate_message(rng),rng),rng);proof.front()^=1;
  rejected([&]{bad_confirmation.verify_confirmation(proof);});
  rejected([&]{(void)bad_confirmation.shared_secret();});
  const auto zero=EC_Scalar::from_bytes_mod_order(params.group(),std::vector<uint8_t>(48));
  rejected([&]{(void)ProverSecret::from_prehashed(zero,w1);});
  rejected([&]{(void)ProverSecret::from_prehashed(w0,zero);});
#ifdef PROVIDER_WITH_OPENSSL
  require(OpenSSL_version_num()!=0);
#endif
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  require(elapsed<30000);
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS usage{};usage.cb=sizeof usage;
  require(GetProcessMemoryInfo(GetCurrentProcess(),&usage,sizeof usage)!=0);
  const auto rss=usage.PeakWorkingSetSize;
#else
  rusage usage{};require(getrusage(RUSAGE_SELF,&usage)==0);
#ifdef __APPLE__
  const auto rss=usage.ru_maxrss;
#else
  const auto rss=usage.ru_maxrss*1024ULL;
#endif
#endif
  std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"elapsed_ms\":"<<elapsed<<",\"scrypt_memory_bytes\":"<<kdf->total_memory_usage()<<",\"peak_rss_bytes\":"<<rss<<"}\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
