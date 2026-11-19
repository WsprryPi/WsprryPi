// SPDX-License-Identifier: MIT
#include "pi_pair_machine_service.hpp"
#include "pi_pair_contract.hpp"
#include "control_contract.hpp"
#include "controller_contract.hpp"
#include "browser_http.hpp"
#include "privileged_network_runtime.hpp"
namespace wsprrypi {
namespace {
bool identity(std::string_view value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
bool native_route(std::string_view resource,std::string_view method){
    if(method=="GET")return resource=="context"||resource=="settings"||resource=="status"||resource=="migration";
    if(method=="PUT")return resource=="settings";
    return method=="POST"&&(resource=="enable"||resource=="disable"||resource=="stop"||resource=="migration"||resource=="migration/rollback");
}
PiPairHttpReply refusal(const PiPairPrincipal &principal,bool pair,unsigned status,const char *code){
    if(!pair)return {controller_target_error(status,code),{},{}};
    return {{status,nlohmann::json{{"contract","Wsprry-Pair/1"},{"schema","result"},{"pair_id",principal.identity().pair_id},
        {"state","rejected"},{"code","invalid_proof"},{"retryable",false}}.dump(),{}},{},{}};
}
}
PiPairMachineService::PiPairMachineService(PiPairHistory &history,ManagementBrowserSessions &sessions,std::string self,Targets targets,Target target)
    :history_(history),sessions_(sessions),self_(std::move(self)),targets_(std::move(targets)),target_(std::move(target)){
    if(!identity(self_)||!targets_||!target_)throw std::invalid_argument("pairing_machine_owner_required");
}
PiPairHttpReply PiPairMachineService::handle(const PiPairPrincipal &principal,const httplib::Request &request,
    PiPairAdmission::Connection &connection,const std::function<bool()> &cancelled){
    const bool pair=request.path.starts_with("/api/pair/");
    const auto denied=[&](unsigned status=403,const char *code="unauthorized"){return refusal(principal,pair,status,code);};
    const auto policy=privileged_network_runtime_state();
    if(cancelled()||connection.cancelled()||principal.self_id()!=self_||
        !policy.active_known||policy.active!=PrivilegedNetworkMode::enforced||policy.configured!=PrivilegedNetworkMode::enforced)return denied();
    const auto browser=wtp_browser_http_request(request);if(!browser||request.target!=request.path)return denied(400,"invalid_shape");
    if(principal.identity().purpose==PiPairTlsPurpose::Retirement){
        constexpr std::string_view prefix="/api/pair/v1/";
        if(!request.path.starts_with(prefix)||!pi_pair_route(PiPairRoutePurpose::Retirement,request.method,std::string_view(request.path).substr(prefix.size()))||
            !history_.retirement_finished(principal,true))return denied();
        try{
            const auto body=parse_pi_pair_document(request.body,request.path.ends_with("/status")?"retirement-status":"retirement");
            if(!history_.authorize_retirement(principal,body))return denied();
        }catch(...){return denied(400,"invalid_shape");}
        auto mutation=connection.try_mutation(self_);if(!mutation)return denied(409,"management_busy");
        return target_(principal,request,cancelled);
    }
    if(principal.identity().purpose!=PiPairTlsPurpose::Management)return denied();
    if(pair){
        constexpr std::string_view prefix="/api/pair/v1/";
        if(!request.path.starts_with(prefix))return denied();
        const auto resource=std::string_view(request.path).substr(prefix.size());
        if(!pi_pair_route(PiPairRoutePurpose::Management,request.method,resource))return denied();
        if(resource.starts_with("operations/")){
            const auto operation=history_.pair_operation(principal,std::string(resource.substr(11)));
            if(operation)return {{200,operation->dump(),{}},{},{}};
            if(!history_.management_authorization(principal))return denied();
            return target_(principal,request,cancelled);
        }
        if(resource!="rotate/prepare"&&resource!="rotate/commit"){
            if((resource!="authorities/register"&&resource!="resume"&&resource!="resume/ack")||!history_.management_authorization(principal))return denied();
            if(request.method=="GET")return request.body.empty()?target_(principal,request,cancelled):denied(400,"invalid_shape");
            auto mutation=connection.try_mutation(self_);if(!mutation)return denied(409,"management_busy");
            return target_(principal,request,cancelled);
        }
        auto mutation=connection.try_mutation(self_);if(!mutation)return denied(409,"management_busy");
        try{
            const auto body=parse_pi_pair_document(request.body,resource=="rotate/prepare"?"rotate-prepare":"rotate-commit");
            // A current namespace has one operation meaning even though the
            // native and key owners use independently atomic local WALs.
            if(resource=="rotate/prepare"){
            auto lookup=request;lookup.method="GET";lookup.path="/api/pair/v1/operations/"+body["operation_id"].get<std::string>();lookup.target=lookup.path;lookup.body.clear();
            lookup.headers.erase("Content-Type");lookup.headers.erase("Content-Length");
            const auto native=target_(principal,lookup,cancelled);
            if(native.payload.status==200)return denied(409,"request_id_reuse");
            if(native.payload.status!=404)return denied(503,"storage_unavailable");
            }
            if(cancelled())return denied();
            const auto result=history_.rotate(principal,body,sessions_);
            return result?PiPairHttpReply{{200,result->dump(),{}},{},{}}:denied();
        }catch(...){return denied(400,"invalid_shape");}
    }
    if(!history_.management_authorization(principal))return denied();
    if(request.path=="/api/control/v1/targets"){
        if(request.method!="GET"||!request.body.empty())return denied();
        auto result=targets_(principal,cancelled);
        if(result.status==200){
            try{if(!valid_controller_document(strict_browser_json(result.body),"targets"))return denied(503,"invalid_shape");}
            catch(...){return denied(503,"invalid_shape");}
        }
        return {std::move(result),{},{}};
    }
    constexpr std::string_view prefix="/api/control/v1/targets/";
    if(!request.path.starts_with(prefix))return denied();
    const auto suffix=std::string_view(request.path).substr(prefix.size());
    if(suffix.size()<34||suffix[32]!='/'||!identity(suffix.substr(0,32)))return denied();
    const auto resource=suffix.substr(33);const auto device=std::string(suffix.substr(0,32));
    const bool pico=resource.starts_with("pico/");
    const bool receipt=resource.starts_with("operations/")&&identity(resource.substr(11))&&request.method=="GET";
    const bool review=request.method=="POST"&&resource.size()==50&&resource.starts_with("operations/")&&resource.ends_with("/review")&&identity(resource.substr(11,32));
    if(!(pico?controller_pico_route(resource.substr(5),request.method):native_route(resource,request.method)||receipt||review))return denied();
    // A different Pi cannot masquerade as this host. Only the typed physical
    // Pico gateway can resolve a child identity through the current parent.
    if(!pico&&!receipt&&!review&&!(request.method=="GET"&&resource=="context")&&device!=self_)return denied(409,"target_mismatch");
    std::optional<PiPairAdmission::Mutation> mutation;
    if(request.method!="GET"){
        mutation=connection.try_mutation(device);if(!mutation)return denied(409,"management_busy");
    }
    if(cancelled()||connection.cancelled()||!history_.management_authorization(principal))return denied();
    return target_(principal,request,cancelled);
}
}
