// SPDX-License-Identifier: MIT
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/browser_api.hpp"
#include <iostream>
using namespace wsprrypi;
using Json=nlohmann::json;
#define CHECK(x) do{if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
 try{
  const std::string device(32,'a'),boot(32,'b');const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
  Json wifi{{"contract","Wsprry-Control/1"},{"schema","wifi"},{"target",target},{"wifi",nullptr},{"generation","0"},{"pending_restart",false},{"storage_healthy",true}};
  Json name{{"contract","Wsprry-Control/1"},{"schema","hostname"},{"target",target},{"saved","pico.local"},{"active","pico.local"},{"default","pico.local"},{"source","network_profile"},{"writable",true},{"reason",""},{"generation","0"},{"pending_restart",false},{"storage_healthy",true}};
  Json logging{{"contract","Wsprry-Control/1"},{"schema","logging"},{"target",target},{"saved",{{"enabled",true},{"maximum_severity",7}}},{"active",{{"enabled",true},{"maximum_severity",7},{"streaming",true},{"connected",true},{"queued",0},{"dropped","0"}}}};
  std::string revision="\"r0\"",last_body;unsigned writes=0,reads=0;bool malformed_reply=false;
  const PicoManagementExchange exchange=[&](const auto &path,const auto &method,const auto &body,const auto &etag){
   CHECK(path=="/api/usb/v1/wifi"||path=="/api/usb/v1/ap"||path=="/api/control/v1/hostname"||path=="/api/control/v1/logging"||path=="/api/control/v1/restart");
   auto *value=path.ends_with("wifi")?&wifi:path.ends_with("logging")?&logging:&name;
   if(method=="GET"){++reads;return PicoHttpResponse{200,value->dump(),revision};}
   ++writes;last_body=body;CHECK(etag==revision);revision="\"r"+std::to_string(writes)+"\"";
   const auto input=strict_browser_json(body);
   if(path.ends_with("restart")||path.ends_with("/ap"))return PicoHttpResponse{202,Json{{"contract","Wsprry-Control/1"},{path.ends_with("restart")?"restart_requested":"requested",true}}.dump(),revision};
   if(path.ends_with("wifi")){wifi["wifi"]=input["wifi"];if(!wifi["wifi"].is_null())wifi["wifi"]["password"]=nullptr;wifi["generation"]=std::to_string(writes);}
   if(path.ends_with("hostname"))name["saved"]=input["hostname"].is_null()?Json("pico.local"):input["hostname"];
   if(path.ends_with("logging")){logging["saved"]=input["logging"];logging["active"]["enabled"]=input["logging"]["enabled"];logging["active"]["maximum_severity"]=input["logging"]["maximum_severity"];}
   auto result=*value;if(malformed_reply)result["unexpected"]="must not be trusted";
   return PicoHttpResponse{200,result.dump(),revision};
  };
  const auto call=[&](const std::string &resource,const std::string &method,const Json &body=Json(nullptr),const std::string &etag=std::string{}){return pico_usb_settings_request(exchange,device,boot,resource,method,body.is_null()?std::string{}:body.dump(),etag);};
  CHECK(call("wifi","GET").status==200&&reads==1&&writes==0);
  Json edit{{"contract","Wsprry-Control/1"},{"schema","wifi"},{"target",target},{"wifi",{{"ssid","Network"},{"password","password"},{"time_server","pool.ntp.org"}}}};
  CHECK(call("wifi","PUT",edit,revision).status==200&&writes==1&&last_body==edit.dump());
  edit["wifi"]["password"]=nullptr;CHECK(call("wifi","PUT",edit,revision).status==200&&writes==2);
  const auto rejected=[&](Json value,unsigned status=400){const auto count=writes;CHECK(call("wifi","PUT",value,revision).status==status&&writes==count);};
  auto bad=edit;bad["wifi"]["ssid"]="Other";rejected(bad);
  bad=edit;bad["wifi"]["password"]="short";rejected(bad);
  bad=edit;bad["target"]["boot_id"]=std::string(32,'c');rejected(bad,409);
  bad=edit;bad["wifi"]["extra"]=true;rejected(bad);
  for(const char *server:{"127.0.0.1","0x7f000001","0177.0.0.1","1.2.3.999","foo..org","-foo.org"}){bad=edit;bad["wifi"]["time_server"]=server;rejected(bad);}
  CHECK(call("wifi","PUT",edit,"\"stale\"").status==412&&writes==2);
  const auto saved_wifi=wifi;wifi["wifi"]["password"]="must-not-leak";CHECK(call("wifi","GET").status==502);wifi=saved_wifi;
  wifi["generation"]="18446744073709551616";CHECK(call("wifi","GET").status==502);wifi=saved_wifi;
  Json hostname{{"contract","Wsprry-Control/1"},{"schema","hostname"},{"target",target},{"hostname","NEW.local."}};
  CHECK(call("hostname","PUT",hostname,revision).status==200&&last_body==hostname.dump());
  const auto count=writes;hostname["hostname"]="not.local.invalid";CHECK(call("hostname","PUT",hostname,revision).status==400&&writes==count);
  hostname["hostname"]=nullptr;CHECK(call("hostname","PUT",hostname,revision).status==200);
  Json log{{"contract","Wsprry-Control/1"},{"schema","logging"},{"target",target},{"logging",{{"enabled",false},{"maximum_severity",2}}}};
  CHECK(call("logging","PUT",log,revision).status==200);
  const auto logging_count=writes;log["logging"]["maximum_severity"]=2.0;CHECK(call("logging","PUT",log,revision).status==400&&writes==logging_count);
  logging["active"]["maximum_severity"]=2.0;CHECK(call("logging","GET").status==502);logging["active"]["maximum_severity"]=2;
  CHECK(call("restart","POST",Json::object(),revision).status==409);
  CHECK(call("restart","POST",Json{{"contract","Wsprry-Control/1"}},revision).status==202);
  CHECK(call("ap","POST",Json{{"contract","Wsprry-Control/1"},{"schema","ap-request"},{"target",target}},revision).status==202);
  malformed_reply=true;const auto unknown=call("wifi","PUT",edit,revision);CHECK(unknown.status==503&&strict_browser_json(unknown.body)["error"]["code"]=="management_outcome_unknown");
  malformed_reply=false;edit["wifi"]=nullptr;CHECK(call("wifi","DELETE",edit,revision).status==200&&wifi["wifi"].is_null());
  std::cout<<"PASS: closed USB name/Wi-Fi/logging/actions, preserved native payloads, identity/revision refusals before writes, redaction and uncertain malformed replies\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
