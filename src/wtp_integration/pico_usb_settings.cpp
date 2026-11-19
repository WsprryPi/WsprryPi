// SPDX-License-Identifier: MIT
#include "configuration.hpp"
#include "browser_api.hpp"
#include <algorithm>
#include <charconv>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
// Match the existing Pico LocalSettings, HostnameSettings, logging_resource,
// and physical AP/restart handlers. Native payloads pass through unchanged;
// malformed readbacks never become browser data or replay receipts.
struct Refusal {unsigned status;const char *code;};
void require(bool value,const char *code="invalid_remote_resource",unsigned status=502){if(!value)throw Refusal{status,code};}
bool fields(const Json &v,std::initializer_list<const char *> names){if(!v.is_object()||v.size()!=names.size())return false;for(auto name:names)if(!v.contains(name))return false;return true;}
bool id(const std::string &v){return v.size()==32&&v.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool text(const Json &v,std::size_t low,std::size_t high){if(!v.is_string())return false;const auto &s=v.get_ref<const std::string&>();return s.size()>=low&&s.size()<=high&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32&&c<127;});}
bool integer(const Json &v,unsigned high){return v.is_number_integer()&&v>=0&&v<=high;}
bool sequence(const Json &v){if(!v.is_string())return false;const auto &s=v.get_ref<const std::string&>();std::uint64_t n{};const auto r=std::from_chars(s.data(),s.data()+s.size(),n);return !s.empty()&&(s.size()==1||s[0]!='0')&&r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
bool etag(const std::string &s){return s.size()>=2&&s.size()<=128&&s.front()=='"'&&s.back()=='"'&&std::all_of(s.begin()+1,s.end()-1,[](unsigned char c){return c>=33&&c<127&&c!='"';});}
bool hostname(const Json &v){
    if(!text(v,7,70))return false;
    auto s=v.get<std::string>();if(s.back()=='.')s.pop_back();
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;});
    if(s.size()>69||!s.ends_with(".local"))return false;
    s.resize(s.size()-6);
    return !s.empty()&&s.size()<=63&&s.front()!='-'&&s.back()!='-'&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';});
}
bool time_server(const Json &v){
    if(!text(v,1,253))return false;
    auto s=v.get<std::string>();
    bool ipv4=true;std::size_t start=0;unsigned first=0;
    for(unsigned i=0;i<4;++i){const auto end=s.find('.',start);const auto p=s.substr(start,end==std::string::npos?end:end-start);unsigned n{};const auto r=std::from_chars(p.data(),p.data()+p.size(),n);if(p.empty()||p.size()>3||(p.size()>1&&p[0]=='0')||r.ec!=std::errc{}||r.ptr!=p.data()+p.size()||n>255||((i<3)!=(end!=std::string::npos))){ipv4=false;break;}if(!i)first=n;if(i<3)start=end+1;}
    if(ipv4)return first>0&&first<224&&first!=127;
    bool numeric=true;start=0;
    while(start<s.size()){const auto end=s.find('.',start);auto label=s.substr(start,end==std::string::npos?end:end-start);const bool hex=label.starts_with("0x")||label.starts_with("0X");if(hex)label.erase(0,2);numeric=numeric&&!label.empty()&&std::all_of(label.begin(),label.end(),[hex](unsigned char c){return (c>='0'&&c<='9')||(hex&&((c>='a'&&c<='f')||(c>='A'&&c<='F')));});if(end==std::string::npos)break;start=end+1;}
    if(numeric)return false;
    if(s.back()=='.')s.pop_back();
    start=0;
    while(start<s.size()){const auto end=s.find('.',start);const auto label=s.substr(start,end==std::string::npos?end:end-start);if(label.empty()||label.size()>63||label.front()=='-'||label.back()=='-'||!std::all_of(label.begin(),label.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';}))return false;if(end==std::string::npos)return true;start=end+1;}
    return false;
}
PicoHttpResponse error(unsigned status,const char *code){return {status,Json{{"contract","Wsprry-Control/1"},{"error",{{"code",code}}}}.dump(),{}};}
Json document(const PicoHttpResponse &r){require(r.body.size()<=4096);auto v=strict_browser_json(r.body,16);require(v.is_object()&&v.value("contract",Json(nullptr))=="Wsprry-Control/1");v.erase("contract");return v;}
void success(const PicoHttpResponse &r){
    if(r.status==200)return;
    // Preserve only finite native refusal codes, never arbitrary peer strings.
    const auto v=document(r);std::string code;
    if(v.contains("error")&&v["error"].is_object()&&v["error"].contains("code")&&v["error"]["code"].is_string())code=v["error"]["code"].get<std::string>();
    for(const char *known:{"revision_conflict","target_mismatch","busy","storage_fault","invalid_wifi","password_required","invalid_hostname","credential_bound_deployment","profile_unavailable","hostname_reconcile_required","unsupported_logging","logging_storage_fault","logging_save_unconfirmed","restart_blocked","restart_unavailable","ap_unavailable","usb_control_unavailable","not_found"})if(code==known)throw Refusal{r.status,known};
    throw Refusal{r.status>=400&&r.status<500?r.status:503U,"remote_configuration_rejected"};
}
void readback(const Json &v,const std::string &resource,const Json &target){
    require(v.is_object()&&v.contains("schema")&&v["schema"]==resource&&v.contains("target")&&v["target"]==target);
    if(resource=="wifi"){
        require(fields(v,{"schema","target","wifi","generation","pending_restart","storage_healthy"})&&sequence(v["generation"])&&v["pending_restart"].is_boolean()&&v["storage_healthy"].is_boolean());
        if(!v["wifi"].is_null())require(fields(v["wifi"],{"ssid","password","time_server"})&&text(v["wifi"]["ssid"],1,32)&&v["wifi"]["password"].is_null()&&time_server(v["wifi"]["time_server"]));
    }else if(resource=="hostname"){
        require(fields(v,{"schema","target","saved","active","default","source","writable","reason","pending_restart","generation","storage_healthy"})&&hostname(v["saved"])&&hostname(v["default"])&&(v["active"]==""||hostname(v["active"]))&&
            (v["source"]=="consumer_profile"||v["source"]=="network_profile"||v["source"]=="deployment")&&v["writable"].is_boolean()&&v["pending_restart"].is_boolean()&&v["storage_healthy"].is_boolean()&&sequence(v["generation"])&&
            (v["reason"]==""||v["reason"]=="storage_fault"||v["reason"]=="profile_unavailable"||v["reason"]=="credential_bound_deployment"));
    }else{
        require(fields(v,{"schema","target","saved","active"})&&fields(v["saved"],{"enabled","maximum_severity"})&&v["saved"]["enabled"].is_boolean()&&integer(v["saved"]["maximum_severity"],7)&&
            fields(v["active"],{"enabled","maximum_severity","streaming","connected","queued","dropped"})&&v["active"]["enabled"].is_boolean()&&integer(v["active"]["maximum_severity"],7)&&v["active"]["enabled"]==v["saved"]["enabled"]&&v["active"]["maximum_severity"]==v["saved"]["maximum_severity"]&&
            v["active"]["streaming"].is_boolean()&&v["active"]["connected"].is_boolean()&&integer(v["active"]["queued"],16)&&sequence(v["active"]["dropped"]));
    }
}
}
bool pico_usb_settings_response_valid(const std::string &resource,const PicoHttpResponse &response,const Json &target){
    try{
        const auto value=document(response);require(etag(response.etag));
        if(resource=="restart"||resource=="ap"){
            const auto key=resource=="restart"?"restart_requested":"requested";
            return response.status==202&&fields(value,{key})&&value[key]==true;
        }
        if(resource!="wifi"&&resource!="hostname"&&resource!="logging")return false;
        require(response.status==200);readback(value,resource,target);return true;
    }catch(...){return false;}
}
PicoHttpResponse pico_usb_settings_request(const PicoManagementExchange &exchange,const std::string &device,const std::string &boot,const std::string &resource,const std::string &method,const std::string &body,const std::string &revision){
    bool attempted=false;
    try{
        require(id(device)&&id(boot),"target_mismatch",409);
        const bool action=resource=="restart"||resource=="ap";
        require((action&&method=="POST")||(!action&&(resource=="wifi"||resource=="hostname"||resource=="logging")&&(method=="GET"||method=="PUT"||(resource=="wifi"&&method=="DELETE"))),"not_found",404);
        require(body.size()<=(resource=="wifi"?1024U:512U),"resource_body_too_large",413);
        require(method!="GET"||body.empty(),"invalid_json",400);
        const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
        Json input;
        if(method!="GET"){
            require(etag(revision),"revision_required",428);
            try{input=strict_browser_json(body,16);}catch(...){throw Refusal{400,"invalid_json"};}
            if(resource=="restart"){
                require(input.is_object()&&input.value("contract",Json(nullptr))=="Wsprry-Control/1","unsupported_control_contract",409);
                require(fields(input,{"contract"}),"invalid_restart",400);
            }
            else{
                require(input.is_object()&&input.value("contract",Json(nullptr))=="Wsprry-Control/1","unsupported_control_contract",409);input.erase("contract");
                require(input.contains("target")&&input["target"]==target,"target_mismatch",409);
                if(resource=="ap")require(fields(input,{"schema","target"})&&input["schema"]=="ap-request","invalid_ap_request",400);
                else require(fields(input,{"schema","target",resource.c_str()})&&input["schema"]==resource,"invalid_resource",400);
                if(resource=="hostname")require(input[resource].is_null()||hostname(input[resource]),"invalid_hostname",400);
                if(resource=="wifi"){
                    require((method=="DELETE")==input[resource].is_null(),"invalid_wifi",400);
                    if(!input[resource].is_null()){const auto &w=input[resource];require(fields(w,{"ssid","password","time_server"})&&text(w["ssid"],1,32)&&(w["password"].is_null()||text(w["password"],8,63))&&time_server(w["time_server"]),"invalid_wifi",400);}
                }
                if(resource=="logging")require(fields(input[resource],{"enabled","maximum_severity"})&&input[resource]["enabled"].is_boolean()&&integer(input[resource]["maximum_severity"],7),"invalid_logging_settings",400);
            }
        }
        const auto read_resource=action?"hostname":resource;
        const auto path=[](const std::string &r){return (r=="wifi"||r=="ap"?"/api/usb/v1/":"/api/control/v1/")+r;};
        auto current=exchange(path(read_resource),"GET",{},{});success(current);const auto value=document(current);readback(value,read_resource,target);require(etag(current.etag));
        if(method=="GET")return current;
        require(current.etag==revision,"revision_conflict",412);
        if(read_resource!="logging")require(value["storage_healthy"]==true,"storage_fault",503);
        if(resource=="hostname")require(value["writable"]==true,"credential_bound_deployment",409);
        if(resource=="wifi"&&!input[resource].is_null()&&input[resource]["password"].is_null())require(!value["wifi"].is_null()&&value["wifi"]["ssid"]==input[resource]["ssid"],"password_required",400);
        attempted=true;const auto result=exchange(path(resource),method,body,revision);
        if(result.status>=400&&result.status<500){attempted=false;success(result);}
        if(action){require(result.status==202,"management_outcome_unknown",503);const auto accepted=document(result);const auto key=resource=="restart"?"restart_requested":"requested";require(fields(accepted,{key})&&accepted[key]==true&&etag(result.etag),"management_outcome_unknown",503);}
        else{require(result.status==200,"management_outcome_unknown",503);readback(document(result),resource,target);require(etag(result.etag),"management_outcome_unknown",503);}
        return result;
    }catch(const Refusal &r){return error(attempted?503:r.status,attempted?"management_outcome_unknown":r.code);}
     catch(...){return error(attempted?503:502,attempted?"management_outcome_unknown":"invalid_remote_resource");}
}
} // namespace wsprrypi
