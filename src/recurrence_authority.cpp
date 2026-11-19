// SPDX-License-Identifier: MIT
#include "recurrence_authority.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <set>
#include <map>
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
bool registration(const Json &value,std::string_view self,bool wire=true){
    if(!fields(value,{"contract","schema","operation_id","pair_id","authorization_generation","target_id","controller_id",
        "controller_incarnation","controller_generation","assignments","endpoint"})||value["contract"]!="Wsprry-Pair/1"||value["schema"]!="authority-register"||
        !hex(value["operation_id"],32)||!hex(value["pair_id"],32)||!generation(value["authorization_generation"])||value["target_id"]!=self||
        !hex(value["controller_id"],32)||value["controller_id"]==self||!hex(value["controller_incarnation"],32)||!generation(value["controller_generation"])||
        !bounded_rows(value["assignments"])||!fields(value["endpoint"],{"host","port"})||value["endpoint"]["port"]!=31418||
        value["endpoint"]["host"]!="wsprry-management-"+value["controller_id"].get<std::string>()+".local."||(wire&&value.dump().size()>4096))return false;
    std::set<std::string> ids;
    for(const auto &row:value["assignments"]){
        if(!fields(row,{"assignment_id","assignment_revision","state"})||!hex(row["assignment_id"],32)||!hex(row["assignment_revision"],64)||
            (row["state"]!="legacy"&&row["state"]!="retiring"&&row["state"]!="retired"&&row["state"]!="rollback"&&row["state"]!="held")||
            !ids.insert(row["assignment_id"].get<std::string>()).second)return false;
    }
    return true;
}
bool pair_operation(const Json &value){
    return fields(value,{"contract","schema","operation_id","pair_id","authorization_generation","request_digest","state","result_digest"})&&
        value["contract"]=="Wsprry-Pair/1"&&value["schema"]=="operation"&&hex(value["operation_id"],32)&&hex(value["pair_id"],32)&&
        generation(value["authorization_generation"])&&hex(value["request_digest"],64)&&value["state"]=="committed"&&hex(value["result_digest"],64);
}
bool handover(const Json &,std::string_view);
bool resume(const Json &,std::string_view);
bool management(const Json &stored,std::string_view self){
    auto value=stored;value.erase("handover");value.erase("resume");
    if(!fields(value,{"history_id","complete","authorities","operations"})||!hex(value["history_id"],32)||!value["complete"].is_boolean()||
        !bounded_rows(value["authorities"])||!value["operations"].is_array()||value["operations"].size()>256)return false;
    std::set<std::string> peers,operations;std::size_t count=0;
    for(const auto &row:value["authorities"]){
        if(!fields(row,{"registration","assignments"})||!registration(row["registration"],self)||!bounded_rows(row["assignments"]))return false;
        const auto &request=row["registration"];
        if(!peers.insert(request["controller_id"].get<std::string>()+request["pair_id"].get<std::string>()+request["authorization_generation"].get<std::string>()).second)return false;
        auto retained=request;retained["assignments"]=row["assignments"];if(!registration(retained,self,false))return false;
        count+=row["assignments"].size();if(count>32)return false;
        for(const auto &current:request["assignments"]){
            bool found=false;for(const auto &saved:row["assignments"])if(saved==current)found=true;if(!found)return false;
        }
    }
    for(const auto &row:value["operations"]){
        if(!fields(row,{"controller_id","receipt"})||!hex(row["controller_id"],32)||row["controller_id"]==self||!pair_operation(row["receipt"])||
            !operations.insert(row["receipt"]["operation_id"].get<std::string>()).second)return false;
        bool found=false;for(const auto &authority:value["authorities"]){const auto &r=authority["registration"];
            if(r["controller_id"]==row["controller_id"]&&r["pair_id"]==row["receipt"]["pair_id"]&&r["authorization_generation"]==row["receipt"]["authorization_generation"])found=true;
        }if(!found)return false;
    }
    return true;
}
std::uint64_t number(const Json &value){const auto text=value.get<std::string>();std::uint64_t out{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),out);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw std::invalid_argument("invalid_authority_generation");
    return out;
}

}
bool valid_recurrence_authority_record(const Json &value,std::string_view self) noexcept {
    try{
        auto core=value;core.erase("management");
        if(!hex(std::string(self),32)||!fields(core,{"schema","device_id","generation","state","transition_id","controllers","retirements","jobs","staged_revision","clean_proof"})||
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
        if(value.contains("management")&&(!management(value["management"],self)||
            (value["management"]["complete"]==true&&proof.is_null())||
            (value["management"].contains("handover")&&!handover(value,self))||
            (value["management"].contains("resume")&&!resume(value,self))))return false;
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
Json recurrence_authority_registration(const Json &record,const Json &request,const Json &receipt,std::string_view history){
    const auto require=[](bool value){if(!value)throw std::invalid_argument("authority_registration_refused");};
    require(record.is_object()&&record.contains("device_id")&&record["device_id"].is_string());
    const auto self=record["device_id"].get<std::string>();
    require(valid_recurrence_authority_record(record,self)&&hex(std::string(history),32)&&registration(request,self)&&pair_operation(receipt));
    require(receipt["operation_id"]==request["operation_id"]&&receipt["pair_id"]==request["pair_id"]&&receipt["authorization_generation"]==request["authorization_generation"]);
    auto result=record;
    if(!result.contains("management"))result["management"]={{"history_id",record["clean_proof"].is_null()?Json(history):record["clean_proof"]["installation_id"]},
        {"complete",!record["clean_proof"].is_null()},{"authorities",Json::array()},{"operations",Json::array()}};
    auto &ledger=result["management"];auto &operations=ledger["operations"];
    for(const auto &operation:operations)if(operation["receipt"]["operation_id"]==request["operation_id"]){
        require(operation["controller_id"]==request["controller_id"]&&operation["receipt"]==receipt);return record;
    }
    Json *selected=nullptr;
    for(auto &authority:ledger["authorities"]){const auto &previous=authority["registration"];
        if(previous["controller_id"]==request["controller_id"]&&previous["pair_id"]==request["pair_id"]&&previous["authorization_generation"]==request["authorization_generation"])selected=&authority;
    }
    if(!selected){require(ledger["authorities"].size()<32);ledger["authorities"].push_back({{"registration",request},{"assignments",request["assignments"]}});selected=&ledger["authorities"].back();}
    else {
        const auto &previous=(*selected)["registration"];
        require(number(request["controller_generation"])>=number(previous["controller_generation"]));
        // This is the source retirement-book counter, not a substitute for
        // the immutable assignment digest. Ordinary unfrozen assignment edits
        // may change that digest without a retirement-counter increment.
        // Known exact operation replay is handled above without reapplying it.
        auto retained=(*selected)["assignments"];
        for(const auto &assignment:request["assignments"]){
            bool found=false;
            for(auto &old:retained){if(old["assignment_id"]==assignment["assignment_id"]){
                // Registration cannot undo a retirement or change a frozen
                // assignment. Explicit resume is a separate owner transition.
                if(old["state"]=="retiring"||old["state"]=="retired"){
                    const auto resumed=record["management"].value("resume",Json(nullptr));
                    const bool authorized=!resumed.is_null()&&resumed["state"]=="resumed"&&record["state"]=="rollback"&&
                        (!record["management"].contains("handover")||number(record["management"]["handover"]["generation"])<=number(resumed["grant"]["authority_generation"]))&&
                        request["controller_id"]==resumed["grant"]["controller_id"]&&request["pair_id"]==resumed["transport"]["pair_id"]&&
                        request["authorization_generation"]==resumed["transport"]["authorization_generation"]&&assignment["assignment_id"]==resumed["grant"]["assignment_id"]&&
                        assignment["state"]=="rollback"&&number(request["controller_generation"])>=number(resumed["acknowledgement"]["controller_generation"]);
                    require(authorized||(assignment["assignment_revision"]==old["assignment_revision"]&&
                        (assignment["state"]==old["state"]||(old["state"]=="retiring"&&assignment["state"]=="retired"))));
                }
                old=assignment;found=true;break;
            }}
            if(!found)retained.push_back(assignment);
        }
        (*selected)["registration"]=request;(*selected)["assignments"]=retained;
    }
    if(ledger.contains("resume")){
        auto &resumed=ledger["resume"];
        if(resumed["state"]=="resumed"&&record["state"]=="rollback"&&
            (!ledger.contains("handover")||number(ledger["handover"]["generation"])<=number(resumed["grant"]["authority_generation"]))&&
            request["controller_id"]==resumed["grant"]["controller_id"]&&request["pair_id"]==resumed["transport"]["pair_id"]&&
            request["authorization_generation"]==resumed["transport"]["authorization_generation"]&&
            number(request["controller_generation"])>=number(resumed["acknowledgement"]["controller_generation"]))
            for(const auto &assignment:request["assignments"])if(assignment["assignment_id"]==resumed["grant"]["assignment_id"]&&assignment["state"]=="rollback")
                resumed["registered"]={{"pair_id",request["pair_id"]},{"authorization_generation",request["authorization_generation"]},
                    {"assignment_revision",assignment["assignment_revision"]},{"controller_generation",request["controller_generation"]},{"controller_incarnation",request["controller_incarnation"]}};
    }
    operations.push_back({{"controller_id",request["controller_id"]},{"receipt",receipt}});
    if(operations.size()>256)operations.erase(operations.begin());
    require(valid_recurrence_authority_record(result,self));return result;
}
bool recurrence_retirement_scope_closed(const Json &record,std::string_view pair,std::string_view generation){
    if(!valid_recurrence_authority_record(record,record.value("device_id",std::string{}))||!record.contains("management"))return false;
    const auto &managed=record["management"];
    if(managed["complete"]!=true||(record["state"]!="staged"&&record["state"]!="native")||!record["jobs"].empty())return false;
    // A terminal ACK is not evidence that its delivery was confirmed. Keep
    // the current grant's exact verifier until a later completed handover
    // proves the source permission permanently fenced again.
    const auto closes_resume=[&](const Json &resumed){
        if(resumed.is_null())return true;
        const auto &grant=resumed["grant"];
        const bool original=grant["pair_id"]==pair&&grant["authorization_generation"]==generation;
        const bool transport=!resumed["transport"].is_null()&&resumed["transport"]["pair_id"]==pair&&resumed["transport"]["authorization_generation"]==generation;
        if(!original&&!transport)return true;
        if(resumed["state"]!="resumed"||!managed.contains("handover"))return false;
        for(const auto &row:managed["handover"]["requests"])if(row["state"]=="confirmed"&&
            row["request"]["controller_id"]==grant["controller_id"]&&row["request"]["assignment_id"]==grant["assignment_id"]&&
            number(row["request"]["retirement_generation"])>number(grant["authority_generation"]))return true;
        return false;
    };
    if(managed.contains("resume")&&!closes_resume(managed["resume"]))return false;
    if(managed.contains("handover")&&!closes_resume(managed["handover"]["resume_basis"]))return false;
    bool known=false;
    for(const auto &authority:managed["authorities"]){const auto &registered=authority["registration"];
        if(registered["pair_id"]!=pair||registered["authorization_generation"]!=generation)continue;
        known=true;
        for(const auto &assignment:authority["assignments"]){
            bool confirmed=false;
            if(managed.contains("handover"))for(const auto &row:managed["handover"]["requests"]){
                if(row["state"]!="confirmed"||row["request"]["controller_id"]!=registered["controller_id"]||row["request"]["assignment_id"]!=assignment["assignment_id"])continue;
                for(const auto &covered:row["covered"])if(covered["pair_id"]==pair&&covered["authorization_generation"]==generation&&
                    covered["assignment_revision"]==assignment["assignment_revision"])confirmed=true;
            }
            if(!confirmed)return false;
        }
    }
    return known;
}
Json recurrence_retirement_basis(const Json &record){
    if(!record.contains("management"))return nullptr;
    const auto &managed=record["management"];
    if(managed.contains("resume")&&managed["resume"]["state"]=="resumed")return managed["resume"];
    if(managed.contains("handover")&&!managed["handover"]["resume_basis"].is_null()&&managed["handover"]["resume_basis"]["state"]=="resumed")
        return managed["handover"]["resume_basis"];
    return nullptr;
}
bool recurrence_remote_resume_active(const Json &record){
    if(record["state"]!="rollback"||!record.contains("management")||!record["management"].contains("resume"))return false;
    const auto &managed=record["management"],&resumed=managed["resume"];
    return resumed["state"]=="resumed"&&(!managed.contains("handover")||number(managed["handover"]["generation"])<=number(resumed["grant"]["authority_generation"]));
}
Json recurrence_reusable_retirement(const Json &record,const Json &planned){
    if(!record.contains("management")||!record["management"].contains("handover"))return nullptr;
    const auto basis=recurrence_retirement_basis(record);
    for(const auto &row:record["management"]["handover"]["requests"]){
        if(row["state"]!="confirmed")continue;
        const auto &request=row["request"];
        if(!basis.is_null()&&basis["state"]=="resumed"&&basis["grant"]["controller_id"]==request["controller_id"]&&
            basis["grant"]["assignment_id"]==request["assignment_id"]&&number(basis["grant"]["authority_generation"])>=number(request["retirement_generation"]))continue;
        if(number(request["retirement_generation"])>=number(planned["retirement_generation"]))continue;
        auto normalized=request;normalized.erase("proof");normalized["transition_id"]=planned["transition_id"];normalized["retirement_generation"]=planned["retirement_generation"];
        if(normalized==planned)return row;
    }
    return nullptr;
}
Json recurrence_retirement_plan(const Json &record,std::string_view transition){
    const auto require=[](bool value){if(!value)throw std::invalid_argument("retirement_inventory_unavailable");};
    require(record.is_object()&&record.contains("device_id")&&record["device_id"].is_string());const auto self=record["device_id"].get<std::string>();
    require(valid_recurrence_authority_record(record,self)&&hex(std::string(transition),32)&&record.contains("management")&&
        record["management"]["complete"]==true&&number(record["generation"])<UINT64_MAX);
    const auto generation=std::to_string(number(record["generation"])+1);
    using Identity=std::pair<std::string,std::string>;std::map<Identity,Json> groups;Json controllers=Json::array();
    std::map<std::string,std::size_t> order;std::size_t sequence=0;
    for(const auto &operation:record["management"]["operations"])order[operation["receipt"]["operation_id"].get<std::string>()]=++sequence;

    for(const auto &controller:record["controllers"])if(controller["controller_id"]==self)controllers.push_back(controller);
    require(controllers.size()<=1);
    for(const auto &authority:record["management"]["authorities"]){const auto &registration=authority["registration"];
        for(const auto &assignment:authority["assignments"]){
            const Identity identity{registration["controller_id"].get<std::string>(),assignment["assignment_id"].get<std::string>()};
            auto found=groups.find(identity);
            if(found==groups.end())found=groups.emplace(identity,Json{{"endpoint",registration["endpoint"]},{"pair_id",registration["pair_id"]},
                {"authorization_generation",registration["authorization_generation"]},{"covered",Json::array()}}).first;
            // The original scope proves continuity; another pair is not an
            // implicit fallback. A future verified resume disposition must
            // explicitly select its own scope before a later handover.
            auto &group=found->second;const auto current=order.find(registration["operation_id"].get<std::string>());
            const auto rank=current==order.end()?0:current->second;
            if(!group.contains("assignment_revision")){group["assignment_revision"]=assignment["assignment_revision"];group["order"]=rank;}
            else if(group["assignment_revision"]!=assignment["assignment_revision"]){
                // Scope insertion order is not update order. Only retained
                // authenticated registration order can choose a newer digest.
                // An evicted ambiguous ordering requires fresh source evidence.
                const auto previous=group["order"].get<std::size_t>();require(previous||rank);
                if(rank>previous){group["assignment_revision"]=assignment["assignment_revision"];group["order"]=rank;}
            }else if(rank>group["order"].get<std::size_t>())group["order"]=rank;
            group["covered"].push_back({{"pair_id",registration["pair_id"]},{"authorization_generation",registration["authorization_generation"]},
                {"assignment_revision",assignment["assignment_revision"]}});
        }
    }
    if(record["management"].contains("resume"))require(record["management"]["resume"]["state"]!="pending");
    const auto resumed=recurrence_retirement_basis(record);
    if(!resumed.is_null()){
        require(!resumed["registered"].is_null());const auto &scope=resumed["transport"];
        const Identity identity{resumed["grant"]["controller_id"].get<std::string>(),resumed["grant"]["assignment_id"].get<std::string>()};
        const auto found=groups.find(identity);require(found!=groups.end());bool covered=false;
        for(const auto &row:found->second["covered"])if(row["pair_id"]==scope["pair_id"]&&row["authorization_generation"]==scope["authorization_generation"])covered=true;
        require(covered);found->second["pair_id"]=scope["pair_id"];found->second["authorization_generation"]=scope["authorization_generation"];
    }
    for(const auto &controller:record["controllers"])if(controller["controller_id"]!=self)
        require(groups.contains({controller["controller_id"].get<std::string>(),controller["assignment_id"].get<std::string>()}));
    require(controllers.size()+groups.size()<=32);Json requests=Json::array();
    for(const auto &[identity,group]:groups){
        controllers.push_back({{"controller_id",identity.first},{"assignment_id",identity.second},{"assignment_revision",group["assignment_revision"]}});
        requests.push_back({{"endpoint",group["endpoint"]},{"covered",group["covered"]},{"request",{{"contract","Wsprry-Pair/1"},{"schema","retirement"},
            {"target_id",self},{"controller_id",identity.first},{"assignment_id",identity.second},{"transition_id",std::string(transition)},
            {"assignment_revision",group["assignment_revision"]},{"retirement_generation",generation},{"pair_id",group["pair_id"]},
            {"authorization_generation",group["authorization_generation"]}}}});
    }
    return {{"generation",generation},{"controllers",controllers},{"requests",requests}};
}
namespace {
bool opaque_proof(const Json &value){return value.is_string()&&value.get_ref<const std::string &>().size()==43&&
    value.get_ref<const std::string &>().find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos;}
bool retired_document(const Json &value,std::string_view self,bool response){
    auto body=value;if(response)body.erase("receipt_digest");
    return fields(body,{"contract","schema","target_id","controller_id","assignment_id","transition_id","assignment_revision","retirement_generation","pair_id","authorization_generation","proof"})&&
        body["contract"]=="Wsprry-Pair/1"&&body["schema"]==(response?"retired":"retirement")&&body["target_id"]==self&&hex(body["controller_id"],32)&&body["controller_id"]!=self&&
        hex(body["assignment_id"],32)&&hex(body["transition_id"],32)&&hex(body["assignment_revision"],64)&&generation(body["retirement_generation"])&&
        hex(body["pair_id"],32)&&generation(body["authorization_generation"])&&opaque_proof(body["proof"])&&(!response||hex(value["receipt_digest"],64));
}
bool resume(const Json &record,std::string_view self){
    try{
        const auto &value=record["management"]["resume"];
        if(record["management"]["complete"]!=true)return false;
        if(!fields(value,{"schema","grant","disabled_snapshot","retirement","state","acknowledgement","transport","registered"})||value["schema"]!="pi-remote-resume/1"||
            (value["state"]!="pending"&&value["state"]!="resumed"&&value["state"]!="refused"))return false;
        const auto &grant=value["grant"],&disabled=value["disabled_snapshot"],&proof=value["retirement"];
        if(!fields(grant,{"contract","schema","operation_id","pair_id","authorization_generation","target_id","target_boot_id","controller_id","controller_incarnation", "assignment_id",
            "assignment_revision","transition_id","retirement_receipt_digest","authority_generation","disabled_revision","disable_receipt_digest","candidate_digest","grant_digest","wtp_binding"})||
            grant["contract"]!="Wsprry-Pair/1"||grant["schema"]!="resume-grant"||grant["target_id"]!=self||grant["controller_id"]==self)return false;
        for(const auto *name:{"operation_id","pair_id","target_boot_id","controller_id","controller_incarnation","assignment_id","transition_id"})if(!hex(grant[name],32))return false;
        for(const auto *name:{"assignment_revision","retirement_receipt_digest","disabled_revision","disable_receipt_digest","candidate_digest","grant_digest"})if(!hex(grant[name],64))return false;
        if(!generation(grant["authorization_generation"])||!generation(grant["authority_generation"])||number(grant["authority_generation"])>number(record["generation"]))return false;
        const auto &binding=grant["wtp_binding"];
        if(!fields(binding,{"revocation_generation","wtp_port","management_port"})||!(binding["revocation_generation"]=="0"||generation(binding["revocation_generation"])))return false;
        for(const auto *name:{"wtp_port","management_port"})if(!binding[name].is_number_integer()||binding[name]<1||binding[name]>65535)return false;
        if(!fields(disabled,{"candidate_digest","configuration_generation","receipt"})||disabled["candidate_digest"]!=grant["candidate_digest"]||!generation(disabled["configuration_generation"]))return false;
        const auto &receipt=disabled["receipt"];
        if(!fields(receipt,{"contract","schema","binding","operation_id","request_digest","state","revision","error"})||receipt["contract"]!="Wsprry-Control/1"||
            receipt["schema"]!="target-operation"||receipt["state"]!="committed"||!receipt["error"].is_null()||receipt["revision"]!=grant["disabled_revision"]||
            !hex(receipt["operation_id"],32)||!hex(receipt["request_digest"],64)||!fields(receipt["binding"],{"device_id","boot_id","profile_generation","connection_generation"})||
            receipt["binding"]["device_id"]!=self||!hex(receipt["binding"]["boot_id"],32)||!generation(receipt["binding"]["profile_generation"])||!generation(receipt["binding"]["connection_generation"]))return false;
        if(!fields(proof,{"request","response"})||!retired_document(proof["request"],self,false)||!retired_document(proof["response"],self,true))return false;
        auto expected=proof["request"];expected.erase("proof");expected["schema"]="retired";auto response=proof["response"];response.erase("proof");response.erase("receipt_digest");
        if(expected!=response||proof["response"]["receipt_digest"]!=grant["retirement_receipt_digest"]||number(proof["request"]["retirement_generation"])>=number(grant["authority_generation"]))return false;
        for(const auto *name:{"controller_id","assignment_id","assignment_revision","transition_id"})if(proof["request"][name]!=grant[name])return false;
        const auto &ack=value["acknowledgement"],&transport=value["transport"],&registered=value["registered"];
        if(value["state"]=="pending")return ack.is_null()&&transport.is_null()&&registered.is_null()&&record["state"]=="reconciling"&&record["transition_id"]==grant["operation_id"]&&record["jobs"].empty();
        if(!fields(ack,{"contract","schema","operation_id","pair_id","authorization_generation","target_id","controller_id","assignment_id","grant_digest","outcome","controller_generation","receipt_digest","proof"})||
            ack["contract"]!="Wsprry-Pair/1"||ack["schema"]!="resume-ack"||ack["outcome"]!=value["state"]||!generation(ack["controller_generation"])||!hex(ack["receipt_digest"],64)||!opaque_proof(ack["proof"]))return false;
        for(const auto *name:{"operation_id","pair_id","authorization_generation","target_id","controller_id","assignment_id","grant_digest"})if(ack[name]!=grant[name])return false;
        if(!fields(transport,{"pair_id","authorization_generation"})||!hex(transport["pair_id"],32)||!generation(transport["authorization_generation"]))return false;
        if(value["state"]=="refused")return registered.is_null();
        if(!registered.is_null()){
            if(!fields(registered,{"pair_id","authorization_generation","assignment_revision","controller_generation","controller_incarnation"})||registered["pair_id"]!=transport["pair_id"]||
                registered["authorization_generation"]!=transport["authorization_generation"]||!hex(registered["assignment_revision"],64)||!hex(registered["controller_incarnation"],32)||
                !generation(registered["controller_generation"])||number(registered["controller_generation"])<number(ack["controller_generation"]))return false;
            bool found=false;for(const auto &authority:record["management"]["authorities"]){const auto &source=authority["registration"];
                if(source["controller_id"]!=grant["controller_id"]||source["pair_id"]!=registered["pair_id"]||source["authorization_generation"]!=registered["authorization_generation"]||
                    number(source["controller_generation"])<number(registered["controller_generation"]))continue;
                for(const auto &assignment:authority["assignments"])if(assignment["assignment_id"]==grant["assignment_id"]&&assignment["assignment_revision"]==registered["assignment_revision"])found=true;
            }if(!found)return false;
        }
        // A resumed assignment's old retirement proof cannot qualify native
        // authority again. Only a fresh higher-generation confirmed fence can.
        if(record["state"]=="native"||record["state"]=="staged"){
            bool fresh=false;for(const auto &fence:record["retirements"])if(fence["controller_id"]==grant["controller_id"]&&fence["assignment_id"]==grant["assignment_id"]&&
                number(fence["retirement_generation"])>number(grant["authority_generation"]))fresh=true;
            if(!fresh)return false;
        }
        return true;
    }catch(...){return false;}
}
bool handover(const Json &record,std::string_view self){
    try{
        if(record["device_id"]!=self)return false;
        const auto &saved=record["management"]["handover"];
        if(!fields(saved,{"schema","generation","transition_id","candidate_digest","inventory","operations","resume_basis","controllers","requests"})||
            saved["schema"]!="pi-remote-handover/1"||!generation(saved["generation"])||number(saved["generation"])<=1||
            number(saved["generation"])>number(record["generation"])||!hex(saved["transition_id"],32)||!hex(saved["candidate_digest"],64)||
            !bounded_rows(saved["inventory"])||!bounded_rows(saved["controllers"])||!bounded_rows(saved["requests"])||
            saved["controllers"]!=record["controllers"])return false;
        if(saved["transition_id"]!=record["transition_id"]&&(!record["management"].contains("resume")||
            record["management"]["resume"]["grant"]["operation_id"]!=record["transition_id"]||
            number(record["management"]["resume"]["grant"]["authority_generation"])<=number(saved["generation"])))return false;
        auto baseline=record;baseline["management"].erase("handover");baseline["management"].erase("resume");
        if(!saved["resume_basis"].is_null())baseline["management"]["resume"]=saved["resume_basis"];
        baseline["management"]["authorities"]=saved["inventory"];
        baseline["management"]["operations"]=saved["operations"];baseline["generation"]=std::to_string(number(saved["generation"])-1);
        baseline["controllers"]=saved["controllers"];
        const auto expected=recurrence_retirement_plan(baseline,saved["transition_id"].get<std::string>());
        if(expected["controllers"]!=saved["controllers"]||expected["requests"].size()!=saved["requests"].size())return false;
        for(std::size_t index=0;index<saved["requests"].size();++index){
            const auto &row=saved["requests"][index];const auto &plan=expected["requests"][index];
            if(!fields(row,{"endpoint","covered","request","state","response"})||row["endpoint"]!=plan["endpoint"]||row["covered"]!=plan["covered"]||
                !retired_document(row["request"],self,false)||
                (row["state"]!="prepared"&&row["state"]!="awaiting_receipt"&&row["state"]!="confirmed"))return false;
            auto request=row["request"];request.erase("proof");
            if(request!=plan["request"]){
                if(row["state"]!="confirmed"||!generation(request["retirement_generation"])||
                    number(request["retirement_generation"])>=number(saved["generation"]))return false;
                const auto &basis=saved["resume_basis"];
                if(!basis.is_null()&&basis["state"]=="resumed"&&basis["grant"]["controller_id"]==request["controller_id"]&&
                    basis["grant"]["assignment_id"]==request["assignment_id"]&&number(basis["grant"]["authority_generation"])>=number(request["retirement_generation"]))return false;
                auto normalized=request;normalized["transition_id"]=plan["request"]["transition_id"];normalized["retirement_generation"]=plan["request"]["retirement_generation"];
                if(normalized!=plan["request"])return false;
            }
            if(row["state"]!="confirmed"){if(!row["response"].is_null())return false;}
            else{
                auto response=row["response"];
                if(!response.is_object()||!response.contains("proof")||!opaque_proof(response["proof"])||
                    !response.contains("receipt_digest")||!hex(response["receipt_digest"],64))return false;
                response.erase("proof");response.erase("receipt_digest");request["schema"]="retired";if(response!=request)return false;
                bool found=false;for(const auto &proof:record["retirements"])if(proof==Json{{"controller_id",request["controller_id"]},
                    {"assignment_id",request["assignment_id"]},{"transition_id",request["transition_id"]},
                    {"retirement_generation",request["retirement_generation"]},{"receipt_digest",row["response"]["receipt_digest"]}})found=true;
                if(!found)return false;
            }
        }
        return true;
    }catch(...){return false;}
}
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
