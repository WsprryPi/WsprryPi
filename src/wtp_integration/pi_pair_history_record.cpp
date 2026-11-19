// SPDX-License-Identifier: MIT
#include "pi_pair_history_record.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_management_journal.hpp"
#include "controller_contract.hpp"
#include "browser_api.hpp"
#include <charconv>
#include <cstdint>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::invalid_argument("invalid_pi_pair_history");}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto name:names)if(!value.contains(name))return false;
    return true;
}
bool hex(const Json &value,std::size_t size){return value.is_string()&&value.get_ref<const std::string &>().size()==size&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
bool counter(const Json &value){
    if(!value.is_string())return false;
    const auto &text=value.get_ref<const std::string &>();std::uint64_t number{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return !text.empty()&&(text.size()==1||text.front()!='0')&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size();
}
Json sealed(Json value){value.erase("checksum");value["checksum"]=PiManagementJournal::digest(canonical_pi_management_json(value));return value;}
}
bool valid_pi_pair_history_record(const Json &value,const std::string &self) noexcept {
    try{
        require(hex(self,32)&&fields(value,{"schema","self_id","namespace_id","initialization","authorization_generation",
            "catalog_digest","operations_digest","peers_digest","clean_shutdown","checksum"})&&value["schema"]=="pi-management-pair-history/1"&&
            value["self_id"]==self&&hex(value["namespace_id"],32)&&counter(value["authorization_generation"])&&
            hex(value["catalog_digest"],64)&&hex(value["operations_digest"],64)&&hex(value["peers_digest"],64)&&value["clean_shutdown"].is_boolean()&&hex(value["checksum"],64));
        const auto &initial=value["initialization"];
        require(initial.is_object()&&initial.contains("kind"));
        if(initial["kind"]=="first"){
            require(fields(initial,{"kind","operation_id","installation_id","source"})&&
                hex(initial["operation_id"],32)&&hex(initial["installation_id"],32)&&
                (initial["source"]=="transmitter-installation"||initial["source"]=="controller-establishment"));
        }else {
            require(fields(initial,{"kind","operation_id","principal_id","request_digest","previous_namespace","previous_selector_digest"})&&
                initial["kind"]=="access-recovery"&&hex(initial["operation_id"],32)&&hex(initial["principal_id"],32)&&hex(initial["request_digest"],64)&&
                (initial["previous_namespace"].is_null()||hex(initial["previous_namespace"],32))&&
                (initial["previous_selector_digest"].is_null()||hex(initial["previous_selector_digest"],64)));
            require(initial["previous_namespace"].is_null()||!initial["previous_selector_digest"].is_null());
            require(initial["previous_namespace"]!=value["namespace_id"]);
        }
        return sealed(value)==value;
    }catch(...){return false;}
}
Json pi_pair_history_record(const std::string &self,const std::string &namespace_id,const Json &initialization,
    const Json &catalog,const Json &operations,const Json &peers,bool clean_shutdown){
    require(valid_pi_pair_catalog(catalog,self)&&valid_pi_pair_operations(operations,self)&&valid_pi_pair_peers(peers,self));
    auto value=sealed(Json{{"schema","pi-management-pair-history/1"},{"self_id",self},{"namespace_id",namespace_id},
        {"initialization",initialization},{"authorization_generation",catalog["authorization_generation"]},
        {"catalog_digest",PiManagementJournal::digest(catalog.dump())},{"operations_digest",PiManagementJournal::digest(operations.dump())},
        {"peers_digest",PiManagementJournal::digest(peers.dump())},
        {"clean_shutdown",clean_shutdown}});
    require(valid_pi_pair_history_record(value,self));return value;
}
Json parse_pi_pair_history_record(const std::string &bytes,const std::string &self){
    require(!bytes.empty()&&bytes.size()<=4096);
    try{auto value=strict_browser_json(bytes,8);require(valid_pi_pair_history_record(value,self));return value;}
    catch(...){throw std::invalid_argument("invalid_pi_pair_history");}
}
} // namespace wsprrypi
