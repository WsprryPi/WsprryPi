// SPDX-License-Identifier: MIT
#include "recurrence_authority.hpp"
#include <array>
#include <charconv>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto *name:names)if(!value.contains(name))return false;
    return true;
}
bool hex(const Json &value,std::size_t length){
    return value.is_string()&&value.get_ref<const std::string &>().size()==length&&
        value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;
}
bool generation(const Json &value){
    if(!value.is_string())return false;
    const auto &text=value.get_ref<const std::string &>();std::uint64_t number{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return !text.empty()&&text.front()!='0'&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&number;
}
bool bounded_rows(const Json &value){return value.is_array()&&value.size()<=32;}
}
bool valid_recurrence_authority_record(const Json &value,std::string_view self) noexcept {
    try{
        if(!hex(std::string(self),32)||!fields(value,{"schema","device_id","generation","state","transition_id","controllers","retirements","jobs","staged_revision","clean_proof"})||
            value.at("schema")!="pi-recurrence-authority/1"||value.at("device_id")!=self||!generation(value.at("generation")))return false;
        const std::array states{"unclassified","legacy","retiring","reconciling","staged","native","rollback","held"};
        bool known=false;for(const auto *state:states)if(value.at("state")==state)known=true;
        if(!known||(!value.at("transition_id").is_null()&&!hex(value.at("transition_id"),32))||
            (!value.at("staged_revision").is_null()&&!hex(value.at("staged_revision"),64)))return false;
        for(const auto *name:{"controllers","retirements","jobs"})if(!bounded_rows(value.at(name)))return false;
        std::set<std::pair<std::string,std::string>> controllers,retirements,jobs;
        for(const auto &row:value.at("controllers")){
            if(!fields(row,{"controller_id","assignment_id","assignment_revision"})||!hex(row.at("controller_id"),32)||
                !hex(row.at("assignment_id"),32)||!hex(row.at("assignment_revision"),64)||
                !controllers.emplace(row.at("controller_id").get<std::string>(),row.at("assignment_id").get<std::string>()).second)return false;
        }
        for(const auto &row:value.at("retirements")){
            if(!fields(row,{"controller_id","assignment_id","transition_id","retirement_generation","receipt_digest"})||
                !hex(row.at("controller_id"),32)||!hex(row.at("assignment_id"),32)||!hex(row.at("transition_id"),32)||
                !generation(row.at("retirement_generation"))||!hex(row.at("receipt_digest"),64)||
                !retirements.emplace(row.at("controller_id").get<std::string>(),row.at("assignment_id").get<std::string>()).second)return false;
        }
        for(const auto &row:value.at("jobs")){
            if(!fields(row,{"controller_id","job_id","boot_id","state"})||!hex(row.at("controller_id"),32)||
                !hex(row.at("job_id"),32)||!hex(row.at("boot_id"),32)||
                (row.at("state")!="pending"&&row.at("state")!="accepted"&&row.at("state")!="unknown")||
                !jobs.emplace(row.at("controller_id").get<std::string>(),row.at("job_id").get<std::string>()).second)return false;
        }
        const auto &proof=value.at("clean_proof");
        if(!proof.is_null()&&(!fields(proof,{"installation_id","kind","attestation_id"})||
            !hex(proof.at("installation_id"),32)||proof.at("kind")!="never_initialized"||!hex(proof.at("attestation_id"),32)))return false;
        // Validate UTF-8 and the private-record size, without granting authority.
        return value.dump().size()<=1048576;
    }catch(...){return false;}
}
Json parse_recurrence_authority_record(std::string_view bytes,std::string_view self){
    if(bytes.empty()||bytes.size()>1048576)throw std::invalid_argument("invalid_recurrence_authority");
    std::vector<std::set<std::string>> keys;
    auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        if(depth<0||depth>16)throw std::invalid_argument("invalid_recurrence_authority");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        if(event==Json::parse_event_t::key&&!keys.back().insert(item.get<std::string>()).second)
            throw std::invalid_argument("invalid_recurrence_authority");
        if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
    if(!valid_recurrence_authority_record(value,self))throw std::invalid_argument("invalid_recurrence_authority");
    return value;
}
Json unclassified_recurrence_authority(std::string_view self){
    Json value{{"schema","pi-recurrence-authority/1"},{"device_id",self},{"generation","1"},{"state","unclassified"},
        {"transition_id",nullptr},{"controllers",Json::array()},{"retirements",Json::array()},{"jobs",Json::array()},
        {"staged_revision",nullptr},{"clean_proof",nullptr}};
    if(!valid_recurrence_authority_record(value,self))throw std::invalid_argument("invalid_recurrence_identity");
    return value;
}
Json recurrence_bootstrap_record(std::string_view self,std::string_view binding,std::string_view installation){
    if(!hex(std::string(self),32)||!hex(std::string(binding),64)||!hex(std::string(installation),32))
        throw std::invalid_argument("invalid_recurrence_bootstrap");
    return {{"schema","pi-recurrence-bootstrap/1"},{"device_id",self},{"source_binding",binding},{"installation_id",installation}};
}
Json parse_recurrence_storage_record(std::string_view bytes,std::string_view self,std::string_view binding){
    if(bytes.empty()||bytes.size()>1048576)throw std::invalid_argument("invalid_recurrence_storage");
    std::vector<std::set<std::string>> keys;
    auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        if(depth<0||depth>16)throw std::invalid_argument("invalid_recurrence_storage");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        if(event==Json::parse_event_t::key&&!keys.back().insert(item.get<std::string>()).second)throw std::invalid_argument("invalid_recurrence_storage");
        if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
    if(valid_recurrence_authority_record(value,self))return value;
    if(!fields(value,{"schema","device_id","source_binding","installation_id"})||!hex(value.at("installation_id"),32)||
        value!=recurrence_bootstrap_record(self,binding,value.at("installation_id").get<std::string>()))
        throw std::invalid_argument("invalid_recurrence_storage");
    return value;
}
}
