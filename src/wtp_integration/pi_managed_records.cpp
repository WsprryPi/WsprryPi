// SPDX-License-Identifier: MIT
#include "pi_managed_records.hpp"
#include "controller_contract.hpp"
#include "pi_management_journal.hpp"
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool condition){if(!condition)throw std::invalid_argument("invalid_pi_managed_record");}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(auto name:names)if(!value.contains(name))return false;
    return true;
}
bool hex(const Json &value,std::size_t size){
    return value.is_string()&&value.get_ref<const std::string&>().size()==size&&
        value.get_ref<const std::string&>().find_first_not_of("0123456789abcdef")==std::string::npos;
}
bool decimal(const Json &value,bool positive){
    if(!value.is_string())return false;
    const auto &text=value.get_ref<const std::string&>();std::uint64_t number{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return !text.empty()&&(text.size()==1||text[0]!='0')&&parsed.ec==std::errc{}&&
        parsed.ptr==text.data()+text.size()&&(!positive||number);
}
std::string hash(const Json &value){return PiManagementJournal::digest(canonical_pi_management_json(value));}
std::string family(const std::string &self,const std::string &route){
    if(route=="/api/control/v1/host/configuration-recovery")return "configuration-recovery";
    const auto prefix="/api/control/v1/targets/"+self+"/";
    if(route==prefix+"settings")return "target-save";
    if(route==prefix+"enable")return "target-enable";
    if(route==prefix+"disable")return "target-action";
    if(route==prefix+"stop")return "target-stop";
    if(route==prefix+"migration")return "target-migration";
    if(route==prefix+"migration/rollback")return "target-migration-rollback";
    return {};
}
bool unresolved(const Json &entry){return entry["receipt"]["state"]=="pending"||entry["receipt"]["state"]=="unknown";}
Json parse_record(const std::string &bytes){
    require(!bytes.empty()&&bytes.size()<=1048576);
    try{
        std::vector<std::set<std::string>> objects;
        return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &value){
            require(depth>=0&&depth<=16);
            if(event==Json::parse_event_t::object_start)objects.emplace_back();
            if(event==Json::parse_event_t::key)require(objects.back().insert(value.get<std::string>()).second);
            if(event==Json::parse_event_t::object_end)objects.pop_back();
            return true;
        });
    }catch(...){throw std::invalid_argument("invalid_pi_managed_record");}
}
void validate_entries(const Json &receipts,const std::string &self,std::size_t maximum){
    require(hex(self,32)&&receipts.is_array()&&receipts.size()<=maximum);
    std::set<std::string> ids;unsigned pending=0;
    for(const auto &entry:receipts){
        require(fields(entry,{"principal_id","route","authorization_generation","request_digest","receipt"}));
        require(hex(entry["principal_id"],32)&&entry["route"].is_string()&&
            !family(self,entry["route"].get<std::string>()).empty()&&decimal(entry["authorization_generation"],false)&&hex(entry["request_digest"],64));
        const auto &receipt=entry["receipt"];
        require(valid_controller_document(receipt,"target-operation")&&receipt["binding"]["device_id"]==self&&
            receipt["request_digest"]==entry["request_digest"]&&ids.insert(receipt["operation_id"].get<std::string>()).second);
        if(unresolved(entry)){++pending;require(receipt["revision"].is_null());}
        if(receipt["state"]=="pending")require(receipt["error"].is_null());
        if(receipt["state"]=="committed")require(hex(receipt["revision"],64)&&receipt["error"].is_null());
    }
    require(pending<=32);
}
}
nlohmann::json pi_managed_settings_record(const std::string &self,const std::string &generation,
    const Json &candidate,const std::string &private_digest){
    require(hex(self,32)&&decimal(generation,true)&&hex(private_digest,64)&&valid_pi_candidate_shape(candidate));
    require(canonical_pi_management_json(candidate).size()<=32768);
    const auto candidate_digest=hash(candidate);
    const auto revision=hash(Json{{"device_id",self},{"configuration_generation",generation},
        {"candidate_digest",candidate_digest},{"private_state_digest",private_digest}});
    return {{"schema","pi-managed-settings/1"},{"device_id",self},{"configuration_generation",generation},
        {"configuration",candidate["configuration"]},{"scheduling",candidate["scheduling"]},
        {"candidate_digest",candidate_digest},{"revision",revision}};
}
bool valid_pi_managed_settings_record(const Json &record,const std::string &self,const std::string &private_digest) noexcept {
    try{
        if(!fields(record,{"schema","device_id","configuration_generation","configuration","scheduling","candidate_digest","revision"})||
            record["schema"]!="pi-managed-settings/1"||record["device_id"]!=self||!decimal(record["configuration_generation"],true)||
            !hex(record["candidate_digest"],64)||!hex(record["revision"],64))return false;
        return record==pi_managed_settings_record(self,record["configuration_generation"].get<std::string>(),
            Json{{"configuration",record["configuration"]},{"scheduling",record["scheduling"]}},private_digest);
    }catch(...){return false;}
}
PiOperationsRecord pi_operations_record(const std::string &self,const Json &receipts){
    validate_entries(receipts,self,289);
    std::size_t terminal=0;for(const auto &entry:receipts)if(!unresolved(entry))++terminal;
    const bool evicted=terminal>256;Json retained=Json::array();
    for(const auto &entry:receipts){if(terminal>256&&!unresolved(entry)){--terminal;continue;}retained.push_back(entry);}
    return {Json{{"schema","pi-management-operations/1"},{"device_id",self},{"receipts",retained}},evicted};
}
bool valid_pi_operations_record(const Json &record,const std::string &self) noexcept {
    try{
        if(!fields(record,{"schema","device_id","receipts"})||record["schema"]!="pi-management-operations/1"||record["device_id"]!=self)return false;
        validate_entries(record["receipts"],self,288);
        const auto retained=pi_operations_record(self,record["receipts"]);
        return !retained.evicted&&retained.document==record;
    }catch(...){return false;}
}
nlohmann::json parse_pi_managed_settings_record(const std::string &bytes,const std::string &self,const std::string &private_digest){
    auto record=parse_record(bytes);require(valid_pi_managed_settings_record(record,self,private_digest));return record;
}
nlohmann::json parse_pi_operations_record(const std::string &bytes,const std::string &self){
    auto record=parse_record(bytes);require(valid_pi_operations_record(record,self));return record;
}
std::string pi_managed_request_digest(const std::string &self,const std::string &principal,
    const std::string &authorization,const std::string &route,const Json &request){
    auto name=family(self,route);
    if(name=="configuration-recovery"){
        require(request.is_object()&&request.contains("schema")&&request["schema"].is_string());
        name=request["schema"].get<std::string>();
        require(name=="configuration-recovery-save"||name=="configuration-recovery-action");
    }
    require(hex(self,32)&&hex(principal,32)&&decimal(authorization,false)&&!name.empty()&&
        valid_controller_document(request,name)&&request["binding"]["device_id"]==self);
    return hash(Json{{"principal_id",principal},{"authorization_generation",authorization},
        {"method",name=="target-save"||name=="configuration-recovery-save"?"PUT":"POST"},{"route",route},{"request",request}});
}
} // namespace wsprrypi
