// SPDX-License-Identifier: MIT
#include "local_browser_principal.hpp"
#include <array>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <utility>
namespace wsprrypi {
namespace {
PiPairSecret fresh_key(){
    std::array<unsigned char,32> bytes{};
    struct Wipe{std::array<unsigned char,32> &value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{bytes};
    if(RAND_bytes(bytes.data(),bytes.size())!=1)throw std::runtime_error("local_authorization_unavailable");
    return PiPairSecret(bytes);
}
std::array<unsigned char,32> seal(const PiPairSecret &key,std::string_view body){
    std::array<unsigned char,32> result{};std::size_t size{};
    if(!EVP_Q_mac(nullptr,"HMAC",nullptr,"SHA256",nullptr,key.bytes().data(),key.bytes().size(),
        reinterpret_cast<const unsigned char *>(body.data()),body.size(),result.data(),result.size(),&size)||size!=result.size())
        throw std::runtime_error("local_authorization_unavailable");
    return result;
}
}
LocalBrowserPrincipal::LocalBrowserPrincipal(const BrowserRequest &request,std::string session,std::weak_ptr<std::atomic_bool> lease)
    :body_key_(fresh_key()),method_(request.method),path_(request.path),revision_(request.revision),member_context_(request.member_context),
     session_(std::move(session)),body_mac_(seal(body_key_,request.body)),lease_(std::move(lease)),
     deadline_(std::chrono::steady_clock::now()+std::chrono::seconds(30)){}
bool LocalBrowserPrincipal::matches(const BrowserRequest &request) const {
    try{
    const auto lease=lease_.lock();
    return lease&&lease->load()&&std::chrono::steady_clock::now()<deadline_&&request.mutation_context&&!session_.empty()&&request.method==method_&&request.path==path_&&
        request.revision==revision_&&request.member_context==member_context_&&
        CRYPTO_memcmp(seal(body_key_,request.body).data(),body_mac_.data(),body_mac_.size())==0;
    }catch(...){return false;}
}
} // namespace wsprrypi
