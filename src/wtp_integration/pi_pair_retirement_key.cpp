// SPDX-License-Identifier: MIT
#include "pi_pair_tls.hpp"
#include "controller_contract.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <array>
#include <charconv>
namespace wsprrypi {
namespace {
void require(bool value,PiPairCryptoFailure error=PiPairCryptoFailure::InvalidShape){if(!value)throw PiPairCryptoError(error);}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
struct Errors{Errors(){ERR_clear_error();}~Errors(){ERR_clear_error();}};
}
PiPairSecret pi_pair_retirement_tls_key(const PiPairSecret &verifier,const std::string &target,
    const std::string &controller,const std::string &pair,const std::string &generation){
    Errors errors;std::uint64_t number{};
    const auto parsed=std::from_chars(generation.data(),generation.data()+generation.size(),number);
    require(id(target)&&id(controller)&&target!=controller&&id(pair)&&!generation.empty()&&generation.front()!='0'&&
        parsed.ec==std::errc{}&&parsed.ptr==generation.data()+generation.size()&&number);
    const auto context=canonical_pi_management_json(nlohmann::json{{"purpose","Wsprry-Pair/1/retirement-tls"},
        {"target_id",target},{"controller_id",controller},{"pair_id",pair},{"authorization_generation",generation},
        {"client_role","target"},{"server_role","controller"}});
    std::array<unsigned char,32> salt{},output{};
    struct Wipe{std::array<unsigned char,32> &value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{output};
    std::size_t size{};
    require(EVP_Q_digest(nullptr,"SHA256",nullptr,context.data(),context.size(),salt.data(),&size)==1&&size==salt.size(),PiPairCryptoFailure::Unavailable);
    std::unique_ptr<EVP_KDF,decltype(&EVP_KDF_free)> kdf(EVP_KDF_fetch(nullptr,"HKDF",nullptr),EVP_KDF_free);
    require(bool(kdf),PiPairCryptoFailure::Unavailable);
    std::unique_ptr<EVP_KDF_CTX,decltype(&EVP_KDF_CTX_free)> derivation(EVP_KDF_CTX_new(kdf.get()),EVP_KDF_CTX_free);
    require(bool(derivation),PiPairCryptoFailure::Unavailable);
    char digest[]="SHA256",info[]="Wsprry-Pair/1/retirement-tls-psk";int mode=EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND;
    // OpenSSL parameters are input-only here; the retained verifier is never
    // copied into a JSON/string buffer or supplied as a writable output.
    OSSL_PARAM params[]={OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,digest,0),OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE,&mode),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,const_cast<unsigned char *>(verifier.bytes().data()),verifier.bytes().size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,salt.data(),salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,info,sizeof(info)-1),OSSL_PARAM_construct_end()};
    require(EVP_KDF_derive(derivation.get(),output.data(),output.size(),params)==1,PiPairCryptoFailure::Unavailable);
    return PiPairSecret(output);
}
} // namespace wsprrypi
