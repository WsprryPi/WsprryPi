// SPDX-License-Identifier: MIT
#include "pi_pair_peers.hpp"
#include "pi_pair_records.hpp"
#include "management_resolver.hpp"
#include "browser_api.hpp"
#include <charconv>
#include <algorithm>
#include <cstdint>
#include <set>
#include <map>
#include <string_view>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::invalid_argument("invalid_pi_pair_peers");}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto name:names)if(!value.contains(name))return false;
    return true;
}
bool hex(const Json &value,std::size_t size){return value.is_string()&&value.get_ref<const std::string &>().size()==size&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
void generation(const Json &value){
    require(value.is_string());const auto &text=value.get_ref<const std::string &>();std::uint64_t number{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    require(!text.empty()&&text.front()!='0'&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&number);
}
void endpoint(const Json &value){
    require(fields(value,{"host","port"})&&value["host"].is_string()&&value["port"].is_number_integer()&&value["port"]==31418);
    const auto &host=value["host"].get_ref<const std::string &>();require(!host.empty()&&host.size()<=253);
    // Numeric addresses must already be private unicast. Named peers still
    // require fresh complete resolution/self checks before each connection.
    if(host.find(':')!=std::string::npos||host.find_first_not_of("0123456789.")==std::string::npos){
        require(private_management_address(host,{}));return;
    }
    require(host.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.")==std::string::npos);
    const auto end=host.back()=='.'?host.size()-1:host.size();require(end&&host[end-1]!='.');
    std::size_t begin=0;
    while(begin<end){
        const auto dot=host.find('.',begin),stop=std::min(dot==std::string::npos?end:dot,end);
        require(stop>begin&&stop-begin<=63&&host[begin]!='-'&&host[stop-1]!='-');begin=stop+1;
    }
}
void pair(const Json &value,const std::string &self,const Json &target,bool revoked){
    require(value.is_object()&&value.contains("authorization_generation")&&value.contains("peer_id")&&value["peer_id"]==target);
    const Json catalog{{"schema","pi-management-pairs/1"},{"self_id",self},{"authorization_generation",value["authorization_generation"]},
        {"active",revoked?Json(nullptr):value},{"retired",revoked?Json::array({value}):Json::array()}};
    require(valid_pi_pair_catalog(catalog,self));
}
}
Json empty_pi_pair_peers(const std::string &self){
    auto result=Json{{"schema","pi-management-peers/1"},{"self_id",self},{"profiles",Json::array()},{"retired",Json::array()},{"uncertain",Json::array()}};
    require(valid_pi_pair_peers(result,self));return result;
}
bool valid_pi_pair_peers(const Json &value,const std::string &self) noexcept {
    try{
        require(hex(self,32)&&fields(value,{"schema","self_id","profiles","retired","uncertain"})&&value["schema"]=="pi-management-peers/1"&&
            value["self_id"]==self&&value["profiles"].is_array()&&value["profiles"].size()<=64&&value["retired"].is_array()&&
            value["uncertain"].is_array()&&value["retired"].size()+value["uncertain"].size()<=32);
        std::set<std::string> profiles,targets,pairs,initiations;
        std::map<std::string,Json> current;
        std::map<std::string,std::string> profile_targets;
        std::set<std::pair<std::string,std::string>> revisions;
        const auto bind_profile=[&](const Json &row){
            const auto profile=row["profile_id"].get<std::string>(),target=row["target_id"].get<std::string>();
            const auto [found,inserted]=profile_targets.emplace(profile,target);require(inserted||found->second==target);
        };
        for(const auto *group:{"profiles","uncertain"})for(const auto &row:value[group]){
            require(fields(row,{"profile_id","profile_generation","target_id","endpoint","pair","stage","initial_operation_id","initial_request_digest"})&&
                hex(row["profile_id"],32)&&hex(row["target_id"],32)&&row["target_id"]!=self&&hex(row["initial_operation_id"],32)&&hex(row["initial_request_digest"],64));
            generation(row["profile_generation"]);endpoint(row["endpoint"]);
            bind_profile(row);require(revisions.emplace(row["profile_id"].get<std::string>(),row["profile_generation"].get<std::string>()).second);
            const bool uncertain=std::string_view(group)=="uncertain";
            if(uncertain){
                require(row["stage"]=="awaiting_commit");
                const auto active=current.find(row["target_id"].get<std::string>());
                if(active!=current.end()){
                    const auto &newer=active->second;require(newer["profile_id"]==row["profile_id"]);
                    const auto before=row["profile_generation"].get<std::string>(),after=newer["profile_generation"].get<std::string>();
                    require(after.size()>before.size()||(after.size()==before.size()&&after>before));
                }
            }else{
                require(profiles.insert(row["profile_id"].get<std::string>()).second&&targets.insert(row["target_id"].get<std::string>()).second);
                current.emplace(row["target_id"].get<std::string>(),row);
            }
            require(initiations.insert(row["initial_operation_id"].get<std::string>()).second);
            const bool removed=row["stage"]=="access_removed";pair(row["pair"],self,row["target_id"],removed);
            require((row["stage"]=="awaiting_commit"&&row["pair"]["state"]=="pending")||
                (row["stage"]=="active"&&row["pair"]["state"]=="active")||(removed&&row["pair"]["state"]=="revoked"));
            require(pairs.insert(row["pair"]["pair_id"].get<std::string>()).second);
        }
        for(const auto &row:value["retired"]){
            require(fields(row,{"profile_id","target_id","endpoint","pair"})&&hex(row["profile_id"],32)&&hex(row["target_id"],32)&&row["target_id"]!=self);
            bind_profile(row);
            endpoint(row["endpoint"]);pair(row["pair"],self,row["target_id"],true);
            require(pairs.insert(row["pair"]["pair_id"].get<std::string>()).second);
        }
        return value.dump().size()<=1048576;
    }catch(...){return false;}
}
Json parse_pi_pair_peers(const std::string &bytes,const std::string &self){
    require(!bytes.empty()&&bytes.size()<=1048576);
    try{auto value=strict_browser_json(bytes,8);require(valid_pi_pair_peers(value,self));return value;}
    catch(...){throw std::invalid_argument("invalid_pi_pair_peers");}
}
Json supersede_pi_pair_pending(const Json &before,const std::string &self,const Json &replacement){
    require(valid_pi_pair_peers(before,self));auto after=before;
    require(replacement.is_object()&&replacement.contains("target_id")&&replacement.contains("profile_id")&&
        replacement.contains("profile_generation")&&replacement.contains("stage")&&replacement["stage"]=="awaiting_commit");
    bool found=false;
    for(auto &row:after["profiles"])if(row["target_id"]==replacement["target_id"]){
        require(!found&&row["stage"]=="awaiting_commit"&&row["profile_id"]==replacement["profile_id"]);found=true;
        generation(replacement["profile_generation"]);
        const auto previous=row["profile_generation"].get<std::string>();std::uint64_t number{};
        const auto parsed=std::from_chars(previous.data(),previous.data()+previous.size(),number);
        require(parsed.ec==std::errc{}&&number<UINT64_MAX&&replacement["profile_generation"]==std::to_string(number+1));
        after["uncertain"].push_back(row);row=replacement;
    }
    require(found&&valid_pi_pair_peers(after,self));return after;
}
} // namespace wsprrypi
