// SPDX-License-Identifier: MIT
#include "wire_bytes.hpp"
#include "local_transport.hpp"
#include <charconv>
#include <openssl/evp.h>
namespace wsprrypi {
namespace {void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}}
std::string encode_wire_chunk(const std::string &bytes){
    require(bytes.size()<=65536);if(bytes.empty())return {};
    std::string encoded(4*((bytes.size()+2)/3),'\0');
    require(EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),reinterpret_cast<const unsigned char *>(bytes.data()),static_cast<int>(bytes.size()))==static_cast<int>(encoded.size()));
    return encoded;
}
std::string decode_wire_chunk(const std::string &encoded){
    require(encoded.size()<=4*((65536+2)/3)&&encoded.size()%4==0);
    if(encoded.empty())return {};
    require(encoded.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")==std::string::npos);
    std::string decoded(encoded.size()/4*3,'\0');
    const auto count=EVP_DecodeBlock(reinterpret_cast<unsigned char *>(decoded.data()),reinterpret_cast<const unsigned char *>(encoded.data()),static_cast<int>(encoded.size()));
    require(count>=0);std::size_t padding=encoded.ends_with("==")?2:encoded.ends_with("=")?1:0;
    require(static_cast<std::size_t>(count)>=padding);decoded.resize(count-padding);
    require(decoded.size()<=65536&&encode_wire_chunk(decoded)==encoded);return decoded;
}
std::uint64_t wire_offset(const std::string &text){
    require(!text.empty()&&(text.size()==1||text.front()!='0'));
    std::uint64_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size());return value;
}
}
