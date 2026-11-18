// SPDX-License-Identifier: MIT
#include "pi_pair_operations.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_crypto.hpp"
#include "pi_pair_rotation.hpp"
#include "pi_management_journal.hpp"
#include "controller_contract.hpp"
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::invalid_argument("invalid_pi_pair_operations");}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto name:names)if(!value.contains(name))return false;
    return true;
}
bool hex(const Json &value,std::size_t size){return value.is_string()&&value.get_ref<const std::string &>().size()==size&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
std::uint64_t counter(const Json &value,bool positive=false){
    require(value.is_string());const auto &text=value.get_ref<const std::string &>();std::uint64_t number{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    require(!text.empty()&&(text.size()==1||text.front()!='0')&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&(!positive||number));return number;
}
void rotation(const Json &value,const Json &entry,const std::string &self){
    require(fields(value,{"kind","self_id","peer_id","profile_id","pair_id","authorization_generation","operation_id",
        "expected_epoch","stage","prepare","prepared","private_key"})&&value["kind"]=="rotation-intent"&&value["self_id"]==self&&
        hex(value["peer_id"],32)&&value["peer_id"]!=self&&hex(value["profile_id"],32)&&value["pair_id"]==entry["pair_id"]&&
        value["authorization_generation"]==entry["authorization_generation"]&&value["operation_id"]==entry["operation_id"]&&
        entry["unresolved"]==true&&entry["state"]!="rejected"&&(entry["scope"]=="device"||entry["scope"]=="paired"));
    require(valid_pi_pair_document(value["prepare"],"rotate-prepare")&&value["prepare"]["operation_id"]==value["operation_id"]&&
        value["prepare"]["pair_id"]==value["pair_id"]&&value["prepare"]["expected_epoch"]==value["expected_epoch"]);
    const auto before=counter(value["expected_epoch"],true);require(before<UINT64_MAX);
    require(PiPairRotationKey::valid_public_point(value["prepare"]["pA"].get_ref<const std::string &>()));
    if(entry["scope"]=="device")require(entry["principal_id"]==self&&entry["target_id"]==value["peer_id"]);
    else require(entry["principal_id"]==value["peer_id"]&&entry["target_id"]==self);
    if(value["stage"]=="awaiting_prepare"){
        require(entry["scope"]=="device"&&(entry["state"]=="pending"||entry["state"]=="unknown")&&value["prepared"].is_null()&&value["private_key"].is_string());
        require(PiPairRotationKey::valid_private_pair(value["private_key"].get_ref<const std::string &>(),value["prepare"]["pA"].get_ref<const std::string &>()));
    }else {
        require(value["stage"]=="awaiting_commit"&&value["private_key"].is_null()&&
            valid_pi_pair_document(value["prepared"],"rotate-prepared")&&value["prepared"]["operation_id"]==value["operation_id"]);
        const auto after=counter(value["prepared"]["next_epoch"],true);require(after>before&&after-before==1);
        require(PiPairRotationKey::valid_public_point(value["prepared"]["pB"].get_ref<const std::string &>()));
    }
}
void authority_registration(const Json &value,const Json &entry,const std::string &self){
    require(fields(value,{"kind","self_id","peer_id","profile_id","profile_generation","stage","request"})&&
        value["kind"]=="authority-registration-intent"&&value["self_id"]==self&&hex(value["peer_id"],32)&&value["peer_id"]!=self&&
        hex(value["profile_id"],32)&&counter(value["profile_generation"],true)>0&&
        (value["stage"]=="prepared"||value["stage"]=="awaiting_receipt")&&valid_pi_pair_document(value["request"],"authority-register")&&
        entry["scope"]=="device"&&entry["principal_id"]==self&&entry["target_id"]==value["peer_id"]&&entry["unresolved"]==true&&
        (entry["state"]=="pending"||entry["state"]=="unknown")&&entry["receipt"].is_null());
    const auto &request=value["request"];
    require(request["controller_id"]==self&&request["target_id"]==value["peer_id"]&&request["pair_id"]==entry["pair_id"]&&
        request["authorization_generation"]==entry["authorization_generation"]&&request["operation_id"]==entry["operation_id"]&&
        entry["request_digest"]==PiManagementJournal::digest(canonical_pi_management_json(request)));
}
void entries(const Json &values,const std::string &self,std::size_t maximum,bool extra){
    require(hex(self,32)&&values.is_array()&&values.size()<=maximum);unsigned unresolved=0,terminal=0;std::set<std::string> ids;
    for(const auto &value:values){
        require(fields(value,{"scope","principal_id","target_id","pair_id","authorization_generation","operation_id","request_digest",
            "state","result_digest","unresolved","receipt","continuation"})&&
            (value["scope"]=="local"||value["scope"]=="paired"||value["scope"]=="retirement"||value["scope"]=="device")&&
            hex(value["principal_id"],32)&&hex(value["target_id"],32)&&hex(value["operation_id"],32)&&hex(value["request_digest"],64)&&
            value["unresolved"].is_boolean()&&ids.insert(value["operation_id"].get<std::string>()).second);
        const auto generation=counter(value["authorization_generation"]);
        if(value["pair_id"].is_null())require(value["scope"]=="local");
        else require(hex(value["pair_id"],32)&&generation>0);
        if(value["scope"]!="local")require(!value["pair_id"].is_null());
        if(value["scope"]=="device")require(value["principal_id"]==self);
        if(value["scope"]=="paired"||value["scope"]=="retirement")require(value["target_id"]==self&&value["principal_id"]!=self);
        require(value["state"]=="pending"||value["state"]=="committed"||value["state"]=="rejected"||value["state"]=="unknown");
        if(value["unresolved"]==true)++unresolved;else {++terminal;require(value["state"]=="committed"||value["state"]=="rejected");}
        if(value["state"]=="committed")require(hex(value["result_digest"],64));else require(value["result_digest"].is_null());
        if(!value["receipt"].is_null())require(valid_controller_document(value["receipt"],"target-operation")&&
            value["receipt"]["binding"]["device_id"]==value["target_id"]&&value["receipt"]["operation_id"]==value["operation_id"]&&
            value["receipt"]["request_digest"]==value["request_digest"]&&value["receipt"]["state"]==value["state"]);
        if(value["state"]=="committed"&&!value["receipt"].is_null())
            require(value["result_digest"]==PiManagementJournal::digest(canonical_pi_management_json(value["receipt"])));
        if(!value["receipt"].is_null()){
            const auto &receipt=value["receipt"];
            if(value["state"]=="committed")require(hex(receipt["revision"],64)&&receipt["error"].is_null());
            else require(receipt["revision"].is_null());
            if(value["state"]=="pending")require(receipt["error"].is_null());
            if(value["state"]=="rejected")require(!receipt["error"].is_null());
        }
        if(!value["continuation"].is_null()){
            if(value["continuation"].is_object()&&value["continuation"].value("kind",Json(nullptr))=="authority-registration-intent")authority_registration(value["continuation"],value,self);
            else rotation(value["continuation"],value,self);
        }
    }
    require(unresolved<=32&&terminal<=256+static_cast<unsigned>(extra));
}
Json document(const std::string &self,const Json &values){return {{"schema","pi-management-pair-operations/1"},{"self_id",self},{"entries",values}};}
}
bool valid_pi_pair_operations(const Json &value,const std::string &self) noexcept {
    try{
        require(fields(value,{"schema","self_id","entries"})&&value["schema"]=="pi-management-pair-operations/1"&&value["self_id"]==self);
        entries(value["entries"],self,288,false);return value.dump().size()<=1048576;
    }catch(...){return false;}
}
Json parse_pi_pair_operations(const std::string &bytes,const std::string &self){
    require(!bytes.empty()&&bytes.size()<=1048576);
    try{
        std::vector<std::set<std::string>> objects;
        auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &part){
            require(depth>=0&&depth<=12);
            if(event==Json::parse_event_t::object_start)objects.emplace_back();
            if(event==Json::parse_event_t::key)require(objects.back().insert(part.get<std::string>()).second);
            if(event==Json::parse_event_t::object_end)objects.pop_back();
            return true;
        });require(valid_pi_pair_operations(value,self));return value;
    }catch(...){throw std::invalid_argument("invalid_pi_pair_operations");}
}
PiPairOperationsUpdate pi_pair_operations(const std::string &self,const Json &values){
    entries(values,self,289,true);auto kept=values;unsigned terminal=0;for(const auto &value:values)if(value["unresolved"]==false)++terminal;
    bool evicted=false;
    if(terminal>256){for(auto item=kept.begin();item!=kept.end();++item)if((*item)["unresolved"]==false){kept.erase(item);evicted=true;break;}}
    auto result=document(self,kept);require(valid_pi_pair_operations(result,self));return {std::move(result),evicted};
}
} // namespace wsprrypi
