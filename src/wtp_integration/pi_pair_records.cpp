// SPDX-License-Identifier: MIT
#include "pi_pair_records.hpp"
#include "pi_pair_crypto.hpp"
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::invalid_argument("invalid_pi_pair_record");}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto name:names)if(!value.contains(name))return false;
    return true;
}
bool hex(const Json &value,std::size_t size){return value.is_string()&&
    value.get_ref<const std::string &>().size()==size&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
std::uint64_t counter(const Json &value,bool positive=false){
    require(value.is_string());const auto &text=value.get_ref<const std::string &>();
    std::uint64_t number{};const auto result=std::from_chars(text.data(),text.data()+text.size(),number);
    require(!text.empty()&&(text.size()==1||text.front()!='0')&&result.ec==std::errc{}&&
        result.ptr==text.data()+text.size()&&(!positive||number));return number;
}
void secret(const Json &value){require(value.is_string());(void)PiPairSecret::from_storage(value.get_ref<const std::string &>());}
std::uint64_t epoch(const Json &value){
    require(fields(value,{"epoch","tls_psk","service_ms","connections"}));
    const auto number=counter(value["epoch"],true);secret(value["tls_psk"]);
    (void)counter(value["service_ms"]);(void)counter(value["connections"]);return number;
}
void record(const Json &value,const std::string &self,std::uint64_t generation,bool retired){
    require(fields(value,{"schema","self_id","peer_id","pair_id","authorization_generation","role","state",
        "current","pending","previous","receipt","retirement_verifier"})&&
        value["schema"]=="pi-management-pair/1"&&value["self_id"]==self&&hex(value["peer_id"],32)&&
        value["peer_id"]!=self&&hex(value["pair_id"],32)&&value["role"]=="manager");
    const auto own=counter(value["authorization_generation"],true);
    require(retired?own<=generation:own==generation);
    if(!value["receipt"].is_null())require(fields(value["receipt"],{"operation_id","request_digest","result_digest"})&&
        hex(value["receipt"]["operation_id"],32)&&hex(value["receipt"]["request_digest"],64)&&hex(value["receipt"]["result_digest"],64));
    if(!value["retirement_verifier"].is_null())secret(value["retirement_verifier"]);
    const auto &current=value["current"],&pending=value["pending"],&previous=value["previous"];
    if(retired){
        require(value["state"]=="revoked"&&current.is_null()&&pending.is_null()&&previous.is_null());return;
    }
    require(!value["retirement_verifier"].is_null());
    if(value["state"]=="pending"){
        require(current.is_null()&&previous.is_null()&&!pending.is_null()&&epoch(pending)==1);return;
    }
    require(value["state"]=="active"&&!current.is_null()&&(pending.is_null()||previous.is_null()));
    const auto now=epoch(current);
    if(!pending.is_null()){const auto next=epoch(pending);require(next>now&&next-now==1);}
    if(!previous.is_null()){const auto before=epoch(previous);require(now>before&&now-before==1);}
}
}
bool valid_pi_pair_catalog(const Json &value,const std::string &self) noexcept {
    try{
        require(hex(self,32)&&fields(value,{"schema","self_id","authorization_generation","active","retired"})&&
            value["schema"]=="pi-management-pairs/1"&&value["self_id"]==self&&value["retired"].is_array()&&value["retired"].size()<=32);
        const auto generation=counter(value["authorization_generation"]);
        std::set<std::string> ids;
        if(!value["active"].is_null()){
            record(value["active"],self,generation,false);ids.insert(value["active"]["pair_id"].get<std::string>());
        }
        for(const auto &row:value["retired"]){record(row,self,generation,true);require(ids.insert(row["pair_id"].get<std::string>()).second);}
        return value.dump().size()<=1048576;
    }catch(...){return false;}
}
Json parse_pi_pair_catalog(const std::string &bytes,const std::string &self){
    require(!bytes.empty()&&bytes.size()<=1048576);
    try{
        std::vector<std::set<std::string>> objects;
        auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &part){
            require(depth>=0&&depth<=8);
            if(event==Json::parse_event_t::object_start)objects.emplace_back();
            if(event==Json::parse_event_t::key)require(objects.back().insert(part.get<std::string>()).second);
            if(event==Json::parse_event_t::object_end)objects.pop_back();
            return true;
        });
        require(valid_pi_pair_catalog(value,self));return value;
    }catch(...){throw std::invalid_argument("invalid_pi_pair_record");}
}
} // namespace wsprrypi
