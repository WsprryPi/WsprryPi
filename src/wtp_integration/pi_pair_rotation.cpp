// SPDX-License-Identifier: MIT
#include "pi_pair_rotation.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/param_build.h>
#include <algorithm>
#include <array>
#include <charconv>
namespace wsprrypi {
namespace {
using Key=std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>;
void require(bool value,PiPairCryptoFailure error=PiPairCryptoFailure::InvalidShape){if(!value)throw PiPairCryptoError(error);}
struct Errors{Errors(){ERR_clear_error();}~Errors(){ERR_clear_error();}};
std::array<unsigned char,65> point(std::string_view value){
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    require(value.size()==87&&value.find_first_not_of(alphabet)==std::string_view::npos&&(alphabet.find(value.back())&3)==0);
    std::array<unsigned char,66> bytes{};std::array<char,88> encoded{};
    std::copy(value.begin(),value.end(),encoded.begin());encoded.back()='=';
    std::replace(encoded.begin(),encoded.end(),'-','+');std::replace(encoded.begin(),encoded.end(),'_','/');
    require(EVP_DecodeBlock(bytes.data(),reinterpret_cast<const unsigned char *>(encoded.data()),static_cast<int>(encoded.size()))==66&&bytes.front()==4);
    std::array<unsigned char,65> result{};std::copy_n(bytes.begin(),result.size(),result.begin());return result;
}
std::string encode(const std::array<unsigned char,65> &value){
    std::array<unsigned char,89> encoded{};require(EVP_EncodeBlock(encoded.data(),value.data(),static_cast<int>(value.size()))==88);
    std::string result(reinterpret_cast<const char *>(encoded.data()),87);
    std::replace(result.begin(),result.end(),'+','-');std::replace(result.begin(),result.end(),'/','_');return result;
}
Key public_key(std::string_view encoded){
    const auto bytes=point(encoded);
    std::unique_ptr<OSSL_PARAM_BLD,decltype(&OSSL_PARAM_BLD_free)> builder(OSSL_PARAM_BLD_new(),OSSL_PARAM_BLD_free);
    require(bool(builder),PiPairCryptoFailure::Unavailable);
    require(OSSL_PARAM_BLD_push_utf8_string(builder.get(),OSSL_PKEY_PARAM_GROUP_NAME,"prime256v1",0)==1&&
        OSSL_PARAM_BLD_push_octet_string(builder.get(),OSSL_PKEY_PARAM_PUB_KEY,bytes.data(),bytes.size())==1);
    std::unique_ptr<OSSL_PARAM,decltype(&OSSL_PARAM_free)> params(OSSL_PARAM_BLD_to_param(builder.get()),OSSL_PARAM_free);
    std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> context(EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr),EVP_PKEY_CTX_free);
    require(params&&context&&EVP_PKEY_fromdata_init(context.get())==1);EVP_PKEY *raw{};
    const auto imported=EVP_PKEY_fromdata(context.get(),&raw,EVP_PKEY_PUBLIC_KEY,params.get());Key value(raw,EVP_PKEY_free);
    require(imported==1&&value);std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> check(EVP_PKEY_CTX_new_from_pkey(nullptr,value.get(),nullptr),EVP_PKEY_CTX_free);
    require(check&&EVP_PKEY_public_check(check.get())==1);return value;
}
std::uint64_t epoch(const nlohmann::json &value){
    const auto text=value.get<std::string>();std::uint64_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&result);return result;
}
}
struct PiPairRotationKey::Impl {
    Key key{nullptr,EVP_PKEY_free};
    explicit Impl(Key value):key(std::move(value)){
        require(bool(key));std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> check(EVP_PKEY_CTX_new_from_pkey(nullptr,key.get(),nullptr),EVP_PKEY_CTX_free);
        require(check&&EVP_PKEY_private_check(check.get())==1&&EVP_PKEY_public_check(check.get())==1&&EVP_PKEY_pairwise_check(check.get())==1);
    }
};
PiPairRotationKey::PiPairRotationKey(std::unique_ptr<Impl> value):impl_(std::move(value)){}
PiPairRotationKey::~PiPairRotationKey()=default;
PiPairRotationKey::PiPairRotationKey(PiPairRotationKey &&) noexcept=default;
PiPairRotationKey &PiPairRotationKey::operator=(PiPairRotationKey &&) noexcept=default;
PiPairRotationKey PiPairRotationKey::generate(){
    Errors errors;Key value(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","P-256"),EVP_PKEY_free);require(bool(value),PiPairCryptoFailure::Unavailable);
    return PiPairRotationKey(std::make_unique<Impl>(std::move(value)));
}
PiPairRotationKey PiPairRotationKey::restore(const PiPairSecret &private_key,std::string_view public_point){
    Errors errors;const auto bytes=point(public_point);
    std::unique_ptr<BIGNUM,decltype(&BN_clear_free)> number(BN_secure_new(),BN_clear_free);require(bool(number),PiPairCryptoFailure::Unavailable);
    require(BN_bin2bn(private_key.bytes().data(),static_cast<int>(private_key.bytes().size()),number.get())==number.get()&&!BN_is_zero(number.get()));
    std::unique_ptr<OSSL_PARAM_BLD,decltype(&OSSL_PARAM_BLD_free)> builder(OSSL_PARAM_BLD_new(),OSSL_PARAM_BLD_free);require(bool(builder),PiPairCryptoFailure::Unavailable);
    require(OSSL_PARAM_BLD_push_utf8_string(builder.get(),OSSL_PKEY_PARAM_GROUP_NAME,"prime256v1",0)==1&&
        OSSL_PARAM_BLD_push_octet_string(builder.get(),OSSL_PKEY_PARAM_PUB_KEY,bytes.data(),bytes.size())==1&&
        OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_PRIV_KEY,number.get())==1);
    std::unique_ptr<OSSL_PARAM,decltype(&OSSL_PARAM_free)> params(OSSL_PARAM_BLD_to_param(builder.get()),OSSL_PARAM_free);require(bool(params),PiPairCryptoFailure::Unavailable);
    std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> context(EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr),EVP_PKEY_CTX_free);
    require(context&&EVP_PKEY_fromdata_init(context.get())==1);EVP_PKEY *value{};
    const auto imported=EVP_PKEY_fromdata(context.get(),&value,EVP_PKEY_KEYPAIR,params.get());
    if(imported!=1){EVP_PKEY_free(value);throw PiPairCryptoError(PiPairCryptoFailure::InvalidShape);}
    Key owned(value,EVP_PKEY_free);return PiPairRotationKey(std::make_unique<Impl>(std::move(owned)));
}
bool PiPairRotationKey::valid_private_pair(std::string_view secret,std::string_view public_point) noexcept {
    try{const auto key=PiPairSecret::from_storage(secret);(void)restore(key,public_point);return true;}catch(...){return false;}
}
bool PiPairRotationKey::valid_public_point(std::string_view encoded) noexcept {
    try{Errors errors;(void)public_key(encoded);return true;}catch(...){return false;}
}
PiPairSecret PiPairRotationKey::export_private() const {
    Errors errors;require(bool(impl_));BIGNUM *raw{};
    const auto exported=EVP_PKEY_get_bn_param(impl_->key.get(),OSSL_PKEY_PARAM_PRIV_KEY,&raw);
    std::unique_ptr<BIGNUM,decltype(&BN_clear_free)> number(raw,BN_clear_free);
    require(exported==1&&number&&!BN_is_zero(number.get())&&BN_num_bytes(number.get())<=32);
    std::array<unsigned char,32> bytes{};
    struct Wipe{std::array<unsigned char,32> &value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{bytes};
    require(BN_bn2binpad(number.get(),bytes.data(),static_cast<int>(bytes.size()))==32);return PiPairSecret(bytes);
}
std::string PiPairRotationKey::public_point() const {
    Errors errors;require(bool(impl_));std::array<unsigned char,65> bytes{};std::size_t size{};
    require(EVP_PKEY_get_octet_string_param(impl_->key.get(),OSSL_PKEY_PARAM_PUB_KEY,bytes.data(),bytes.size(),&size)==1&&size==65&&bytes.front()==4);
    return encode(bytes);
}
PiPairSecret PiPairRotationKey::derive_next(const PiPairSecret &current_psk,std::uint64_t current_epoch,
    const nlohmann::json &prepare,const nlohmann::json &prepared,Role role) const {
    Errors errors;require(impl_&&current_epoch&&current_epoch<UINT64_MAX&&(role==Role::Controller||role==Role::Target)&&
        valid_pi_pair_document(prepare,"rotate-prepare")&&valid_pi_pair_document(prepared,"rotate-prepared"));
    require(prepare["operation_id"]==prepared["operation_id"]&&epoch(prepare["expected_epoch"])==current_epoch&&epoch(prepared["next_epoch"])==current_epoch+1);
    const auto own=role==Role::Controller?prepare["pA"].get<std::string>():prepared["pB"].get<std::string>();require(public_point()==own);
    auto peer=public_key(role==Role::Controller?prepared["pB"].get<std::string>():prepare["pA"].get<std::string>());
    std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> agreement(EVP_PKEY_CTX_new_from_pkey(nullptr,impl_->key.get(),nullptr),EVP_PKEY_CTX_free);
    require(agreement&&EVP_PKEY_derive_init(agreement.get())==1&&EVP_PKEY_derive_set_peer(agreement.get(),peer.get())==1);
    std::array<unsigned char,32> shared{},salt{},output{};
    struct Wipe{std::array<unsigned char,32> &a,&b,&c;~Wipe(){OPENSSL_cleanse(a.data(),a.size());OPENSSL_cleanse(b.data(),b.size());OPENSSL_cleanse(c.data(),c.size());}} wipe{shared,salt,output};
    std::size_t size{};require(EVP_PKEY_derive(agreement.get(),nullptr,&size)==1&&size==shared.size());
    require(EVP_PKEY_derive(agreement.get(),shared.data(),&size)==1&&size==shared.size());
    std::copy(current_psk.bytes().begin(),current_psk.bytes().end(),salt.begin());
    auto info=std::string("Wsprry-Pair/1/rotation/")+canonical_pi_management_json(nlohmann::json::array({prepare,prepared}));
    std::unique_ptr<EVP_KDF,decltype(&EVP_KDF_free)> kdf(EVP_KDF_fetch(nullptr,"HKDF",nullptr),EVP_KDF_free);require(bool(kdf),PiPairCryptoFailure::Unavailable);
    std::unique_ptr<EVP_KDF_CTX,decltype(&EVP_KDF_CTX_free)> context(EVP_KDF_CTX_new(kdf.get()),EVP_KDF_CTX_free);require(bool(context),PiPairCryptoFailure::Unavailable);
    char digest[]="SHA256";int mode=EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND;
    OSSL_PARAM params[]={OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,digest,0),OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE,&mode),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,shared.data(),shared.size()),OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,salt.data(),salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,info.data(),info.size()),OSSL_PARAM_construct_end()};
    require(EVP_KDF_derive(context.get(),output.data(),output.size(),params)==1,PiPairCryptoFailure::Unavailable);return PiPairSecret(output);
}
} // namespace wsprrypi
