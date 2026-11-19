// SPDX-License-Identifier: MIT
// Independent and paired-Pi TLS management -> real context/journal, USB
// framing/ACK and the explicitly selected software Pico driver. Fault controls
// exist only on this fixture's stdin; no physical-carrier command exists.
#include "wtp_integration/pico_management_context.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
#include "wtp_integration/pi_pair_server.hpp"
#include "wtp_integration/pi_pair_client.hpp"
#include "wtp_integration/pi_pair_service_lifetime.hpp"
#include "wtp_integration/pi_pair_machine_backend.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include "wtp_integration/pi_pair_remote_controller.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_integration/browser_http.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/pico_target_controller.hpp"
#include "wtp_settings_json.hpp"
#include "privileged_network_runtime.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
namespace {
std::string identity(char value){return std::string(32,value);}
PiPairSecret fixture_key(){std::array<unsigned char,32> bytes{};bytes.fill(19);return PiPairSecret(bytes);}
std::shared_ptr<PiPairHistory> paired_history(const std::string &directory,bool outgoing=false){
 const auto self=identity(outgoing?'e':'d'),peer=identity(outgoing?'d':'e');
 std::filesystem::create_directories(directory);std::filesystem::permissions(directory,std::filesystem::perms::owner_all);
 std::shared_ptr<PiPairHistory> history;
 {ControllerRetirementBook provision(directory+"/retirement",directory+"/assignments.json",self,nlohmann::json::array(),{},
  [&](PiPairInitializationProof proof){history=std::shared_ptr<PiPairHistory>(PiPairHistory::initialize(directory+"/pairing",std::move(proof)));});}
 if(!history)throw std::runtime_error("pair_fixture_provisioning");
 const auto before=history->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self);
 catalog["authorization_generation"]="1";
 nlohmann::json pair{{"schema","pi-management-pair/1"},{"self_id",self},{"peer_id",peer},{"pair_id",identity('c')},
  {"authorization_generation","1"},{"role","manager"},{"state","active"},{"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},
  {"retirement_verifier",fixture_key().storage_encoding()},
  {"current",{{"epoch","1"},{"tls_psk",fixture_key().storage_encoding()},{"service_ms","0"},{"connections","0"}}}};
 if(outgoing){
  catalog["authorization_generation"]="0";auto peers=parse_pi_pair_peers(before.peers->bytes,self);
  peers["profiles"].push_back({{"profile_id",identity('9')},{"profile_generation","1"},{"target_id",peer},{"stage","active"},
   {"endpoint",{{"host","fixture.pi.lan"},{"port",31418}}},{"initial_operation_id",identity('1')},{"initial_request_digest",std::string(64,'2')},{"pair",pair}});
  history->publish(before,catalog,parse_pi_pair_operations(before.operations.bytes,self),peers);
 }else{catalog["active"]=pair;history->publish(before,catalog);}
 return history;
}
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class Driver final:public wtp::ByteStream {
 int input{-1},output{-1};pid_t child{-1};
 bool resume_fault_{};std::string program_;
 public:
 std::atomic_uint64_t writes{0};
 bool block_mutation_once{};
 std::function<void()> before_resume;
 void wait(unsigned ms){if(resume_fault_){resume_fault_=false;if(before_resume)before_resume();}std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
 explicit Driver(const char *program):program_(program){launch();}
 void launch(){
  const auto program=program_.c_str();
  int to[2],from[2];if(pipe(to))throw std::runtime_error("driver_pipe");
  if(pipe(from)){::close(to[0]);::close(to[1]);throw std::runtime_error("driver_pipe");}
  child=fork();if(child<0){for(int fd:{to[0],to[1],from[0],from[1]})::close(fd);throw std::runtime_error("driver_fork");}
  if(!child){dup2(to[0],STDIN_FILENO);dup2(from[1],STDOUT_FILENO);for(int fd:{to[0],to[1],from[0],from[1]})::close(fd);execl(program,program,nullptr);_exit(127);}
  ::close(to[0]);::close(from[1]);input=from[0];output=to[1];fcntl(input,F_SETFL,O_NONBLOCK);fcntl(output,F_SETFL,O_NONBLOCK);
 }
 void restart(){close();if(child>0){kill(child,SIGTERM);while(waitpid(child,nullptr,0)<0&&errno==EINTR){}}child=-1;launch();}
 ~Driver(){close();if(child>0){kill(child,SIGTERM);while(waitpid(child,nullptr,0)<0&&errno==EINTR){}}}
 wtp::IoResult read(std::span<std::uint8_t> bytes)override{const auto n=::read(input,bytes.data(),std::min<std::size_t>(bytes.size(),17));if(n>0)return {wtp::IoState::Progress,std::size_t(n)};return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Closed,0};}
 wtp::IoResult write(std::span<const std::uint8_t> bytes)override{
  ++writes;
  if(block_mutation_once&&bytes.size()>=8&&std::string_view(reinterpret_cast<const char *>(bytes.data()),4)=="UMP1"){
   const auto frame=nlohmann::json::parse(bytes.begin()+8,bytes.end());
   if(frame.value("operation","")=="request"&&frame.value("method","")!="GET"){
    block_mutation_once=false;resume_fault_=true;return {wtp::IoState::WouldBlock,0};
   }
  }
  const auto n=::write(output,bytes.data(),std::min<std::size_t>(bytes.size(),23));if(n>0)return {wtp::IoState::Progress,std::size_t(n)};return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};}
 void close()noexcept override{if(input>=0)::close(input);if(output>=0)::close(output);input=output=-1;}
};
}
int main(int argc,char **argv){
 if(argc!=2&&!(argc==3&&std::string_view(argv[2])=="--paired-pi"))return 2;
 try{
  const bool paired=argc==3;
  signal(SIGPIPE,SIG_IGN);Driver driver(argv[1]);
  char pattern[]="/tmp/pico-independent-usb-XXXXXX";const auto directory=mkdtemp(pattern);if(!directory)throw std::runtime_error("private_directory");
  struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{directory};
  WtpSettings settings;settings.transport="usb";settings.path="/dev/fixture-wtp";settings.usb_serial="fixture-independent";settings.vendor_id=0xcafe;settings.product_id=0x4012;settings.device_id=std::string(32,'a');
  std::atomic_bool utc_qualified{false};
  auto channel=std::make_shared<PicoUsbManagementChannel>(settings);
  channel->utc_observer=[&]{
    const auto mono=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return HostUtcObservation{utc_qualified.load(),static_cast<std::uint64_t>(mono),
      1760000000000000000ULL+static_cast<std::uint64_t>(mono),200000};
  };
  channel->factory_for_test=[&]{return std::make_unique<UsbManagementClient>(driver,now,[&](unsigned ms){driver.wait(ms);});};
  const auto journal=std::string(directory)+(paired?"/attached-management":"/journal"),controller=identity(paired?'d':'c');
  PicoManagementContext context(settings,controller,std::string(directory)+"/journal","1","1",{{},{},channel},PicoManagementHistoryAuthority::isolated());
  const auto connected=context.connect("usb_physical");if(connected.status!=200)throw std::runtime_error(connected.body);
  PiPairAdmission incoming,outgoing;ManagementBrowserSessions sessions;
  std::shared_ptr<PiPairHistory> history;std::unique_ptr<PiPairServiceLifetime> lifetime;
  std::shared_ptr<PiPairHistory> controller_history;ManagementBrowserSessions controller_sessions;
  std::unique_ptr<PiPairServiceLifetime> controller_lifetime;std::unique_ptr<WtpCatalog> controller_catalog;
  std::unique_ptr<PiPairRemoteController> remote_owner;
  int inventory_fault=0;
  std::unique_ptr<WtpCatalog> attached_catalog;std::shared_ptr<PicoTargetController> attached;
  std::unique_ptr<PicoManagementContext> foreign_context;
  std::unique_ptr<PiPairMachineBackend> backend;std::unique_ptr<PiPairMachineService> machine;
  std::string gateway_context;nlohmann::json gateway_binding;
  std::atomic_bool revoke_before_usb{false},wrong_parent{false},cancel_at_handoff{false},operation_cancelled{false},lose_owner_reply{false},malform_owner_reply{false},malform_owner_error{false};
  PiPairClient client(outgoing,PiPairClient::Access::LoopbackTest);
  const PiPairTlsIdentity tls{PiPairTlsPurpose::Management,identity('c'),"1","1"};
  // Destroy/join the listener before any object captured by its handlers.
  std::unique_ptr<PiPairServer> server;
  if(paired){
   initialize_privileged_network_runtime("enforced");history=paired_history(directory);
   foreign_context=std::make_unique<PicoManagementContext>(settings,identity('f'),std::string(directory)+"/foreign-management","1","1",
    PicoManagementTarget::Fixtures{{},{},channel},PicoManagementHistoryAuthority::isolated());
   if(foreign_context->connect("usb_physical").status!=200)throw std::runtime_error("pair_fixture_foreign_context");
   driver.before_resume=[&]{
    if(cancel_at_handoff.exchange(false)){operation_cancelled.store(true);return;}
    const auto before=history->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,identity('d'));auto retired=catalog["active"];
    retired["state"]="revoked";retired["current"]=nullptr;retired["pending"]=nullptr;retired["previous"]=nullptr;
    catalog["active"]=nullptr;catalog["retired"].push_back(std::move(retired));catalog["authorization_generation"]="2";
    history->publish(before,catalog);sessions.retire();
   };
   attached_catalog=std::make_unique<WtpCatalog>(std::string(directory)+"/attached-catalog.json");
   const auto [empty,catalog_revision]=attached_catalog->snapshot({});(void)catalog_revision;
   if(!empty["profiles"].empty())throw std::runtime_error("pair_fixture_catalog_must_start_empty");
   attached=std::make_shared<PicoTargetController>(*attached_catalog,[]{return std::optional<WtpSettings>{};},std::string(directory)+"/attached-management",identity('d'),
    [&](const auto &){return PicoManagementTarget::Fixtures{{},{},channel};},identity('b'));
   lifetime=std::make_unique<PiPairServiceLifetime>(history,incoming,sessions);
   if(!lifetime->open())throw std::runtime_error("pair_fixture_lifetime");
   backend=std::make_unique<PiPairMachineBackend>([&]{return history;},*attached_catalog,attached,[]{return std::optional<WtpSettings>{};},nullptr,
    PiPairMachineBackend::Fixtures{[&]{auto observed=settings;observed.device_id=inventory_fault==2?identity('9'):std::string{};auto inventory=nlohmann::json{{"scope","pico-management-usb/1"},{"available",true},{"reason",""},
      {"candidates",nlohmann::json::array({{{"id","usb-"+identity('f')},{"instance","Software Pico"},{"binding","usb"},{"state","online"},{"settings",management_profile_settings_json(observed)}}})}};
      if(inventory_fault==1)inventory["candidates"][0]["unexpected"]=true;
      return inventory;},
     [&](const auto &){return channel;}});
   machine=std::make_unique<PiPairMachineService>(*history,sessions,identity('d'),
    [&](const auto &principal,const auto &cancelled){return backend->targets(principal,cancelled);},
    [&](const auto &principal,const auto &request,const auto &cancelled){return backend->request(principal,request,cancelled);});
   server=std::make_unique<PiPairServer>(incoming,[&](const auto &requested){return history->management_credential(requested);},
    [&](const auto &principal){return history->accept_finished(principal,&sessions)==PiPairHistory::ManagementAccess::Current;},
    [&](const auto &principal,const httplib::Request &request,auto &connection,const auto &cancelled){
     const auto cancelled_request=[&]{return cancelled()||operation_cancelled.load();};
     auto call=lifetime->enter();if(!call||cancelled_request())throw std::runtime_error("pair_fixture_cancelled");
     if(revoke_before_usb.exchange(false)||cancel_at_handoff.load())driver.block_mutation_once=true;
     if(wrong_parent.exchange(false)){
      const auto browser=wtp_browser_http_request(request);if(!browser||browser->mutation_context)throw std::runtime_error("pair_fixture_browser_impersonation");
      auto cancellation=foreign_context->scoped_cancellation(cancelled_request);
      const auto before=driver.writes.load();const auto result=foreign_context->request(*browser,foreign_context->id(),request.get_header_value("X-WsprryPi-Operation-ID"),principal,*history);
      if(driver.writes.load()!=before)throw std::runtime_error("pair_fixture_cross_parent_dispatch");
      return PiPairHttpReply{result,{},{},{},{},{}};
     }
     auto reply=machine->handle(principal,request,connection,cancelled_request);
     if(lose_owner_reply.exchange(false)&&request.method!="GET"&&reply.payload.status==200)throw std::runtime_error("fixture lost gateway write reply");
     if(malform_owner_reply.exchange(false)&&request.method!="GET"&&reply.payload.status==200){
      auto body=strict_browser_json(reply.payload.body);body["unexpected_reply_field"]=true;reply.payload.body=body.dump();
     }
     if(malform_owner_error.exchange(false)&&request.method!="GET"&&reply.payload.status==200){
      reply.payload=controller_target_error(409,"candidate_invalid");auto body=strict_browser_json(reply.payload.body);
      body["unexpected_reply_field"]=true;reply.payload.body=body.dump();
     }
     return reply;
    },PiPairServer::Access::LoopbackTest);
   if(!server->start())throw std::runtime_error("pair_fixture_listener");
   const auto observe=[&](){return client.exchange({"127.0.0.1",server->port()},identity('d'),[&](const auto &requested)->std::shared_ptr<const PiPairTlsCredential>{
     if(requested!=tls)return {};
     return std::make_shared<PiPairTlsCredential>(PiPairTlsCredential{tls,identity('e'),identity('d'),fixture_key()});},tls,
    [&](const auto &principal){return principal.self_id()==identity('e')&&principal.peer_id()==identity('d')&&principal.identity()==tls;},"GET","/api/control/v1/targets");
   };
   for(int fault:{1,2}){
    inventory_fault=fault;const auto before_writes=driver.writes.load();const auto refused=observe();
    if(!refused.reply||refused.reply->payload.status!=(fault==1?503u:200u))throw std::runtime_error("pair_fixture_bad_discovery_status");
    if(fault==2&&!strict_browser_json(refused.reply->payload.body)["targets"].empty())throw std::runtime_error("pair_fixture_unverified_target_advertised");
    if(!attached_catalog->snapshot({}).first["profiles"].empty())throw std::runtime_error("pair_fixture_discovery_before_identity");
    if(fault==1&&driver.writes.load()!=before_writes)throw std::runtime_error("pair_fixture_invalid_discovery_usb_write");
   }
   inventory_fault=0;const auto inventory=observe();
   if(!inventory.reply||inventory.reply->payload.status!=200)throw std::runtime_error("pair_fixture_automatic_usb_inventory");
   const auto targets=strict_browser_json(inventory.reply->payload.body);
   if(!valid_controller_document(targets,"targets")||targets["targets"].size()!=1||targets["targets"][0]["device_id"]!=settings.device_id)
    throw std::runtime_error("pair_fixture_automatic_usb_identity");
   const auto [enrolled,catalog_etag]=attached_catalog->snapshot({});(void)catalog_etag;
   if(enrolled["profiles"].size()!=1||parse_management_profile_settings(enrolled["profiles"][0]["settings"]).device_id!=settings.device_id)
    throw std::runtime_error("pair_fixture_verified_automatic_enrollment");
   controller_history=paired_history(std::string(directory)+"/controller",true);
   controller_lifetime=std::make_unique<PiPairServiceLifetime>(controller_history,outgoing,controller_sessions);
   if(!controller_lifetime->open())throw std::runtime_error("pair_fixture_controller_lifetime");
   controller_catalog=std::make_unique<WtpCatalog>(std::string(directory)+"/controller-catalog.json");
   remote_owner=std::make_unique<PiPairRemoteController>([&]{return controller_history;},outgoing,identity('e'),*controller_catalog,
    std::string(directory)+"/controller-management",PiPairRemoteController::LoopbackTest{{"127.0.0.1",server->port()}});
   if(!remote_owner->observe(identity('9')))throw std::runtime_error("pair_fixture_controller_observation");
  }
  std::cout<<"READY"<<std::endl;std::string line;
  while(std::getline(std::cin,line)){
   const auto command=strict_browser_json(line);operation_cancelled.store(false);PicoHttpResponse result;std::string reply_readback,reply_recovery_operation,reply_recovery_resource,reply_operation;
   if(command.value("control","")=="journal")result={200,PicoManagementJournal(journal,controller,settings.device_id,paired?attached_catalog->management_history_authority({}):PicoManagementHistoryAuthority::isolated()).snapshot().dump(),{}};
   else if(command.value("control","")=="time-owner"){
    utc_qualified=command.value("qualified",false);channel->start_time_refresh();
    result={200,channel->client->observation().dump(),{}};
   }
   else if(command.value("control","")=="time-status")result={200,channel->client->observation().dump(),{}};
   else if(command.value("control","")=="operations")result=channel->client->request("/api/control/v1/operations","GET",{},{});
   else if(command.value("control","")=="status")result=channel->client->request("/api/control/v1/status","GET",{},{});
   else if(command.value("control","")=="reconnect-usb"){channel->client->close();driver.restart();result={200,R"({"ok":true})",{}};}
   else if(command.value("control","")=="seed-foreign-receipt"&&paired){
    const auto operation=command.at("operation_id").get<std::string>();
    PicoManagementJournal evidence(journal,controller,settings.device_id,attached_catalog->management_history_authority({}));
    if(gateway_binding.is_null())throw std::runtime_error("pair_fixture_verified_binding_required");
    if(evidence.reserve(identity('e'),"/api/control/v1/targets/"+settings.device_id+"/pico/hostname","PUT",gateway_binding,"1",operation,std::string(64,'f')))
      throw std::runtime_error("pair_fixture_foreign_receipt_reuse");
    evidence.finish(operation,command.value("unknown",false)?PicoHttpResponse{503,R"({"error":{"code":"management_outcome_unknown"}})",{}}:
        controller_target_error(409,"candidate_invalid"));result={200,R"({"ok":true})",{}};
   }
   else if(command.value("control","")=="inspect-wifi")result=channel->client->request("/api/usb/v1/wifi","GET",{},{});
   else if(command.value("control","")=="owner-reopen"&&paired){
    remote_owner.reset();
    remote_owner=std::make_unique<PiPairRemoteController>([&]{return controller_history;},outgoing,identity('e'),*controller_catalog,
      std::string(directory)+"/controller-management",PiPairRemoteController::LoopbackTest{{"127.0.0.1",server->port()}});
    result=remote_owner->observe(identity('9'))?PicoHttpResponse{200,R"({"ok":true})",{}}:controller_target_error(503,"transport_unavailable");
   }
   else if(command.value("control","")=="owner-journal"&&paired)result={200,PicoManagementJournal(std::string(directory)+"/controller-management",identity('e'),settings.device_id,controller_catalog->management_history_authority({})).snapshot().dump(),{}};
   else if(command.value("control","")=="owner"&&paired){
    const auto resource=command.at("resource").get<std::string>(),method=command.value("method",std::string("GET"));
    const auto path="/api/control/v1/targets/"+settings.device_id+(resource=="context"||resource.starts_with("operations/")?"/"+resource:"/pico/"+resource);
    BrowserRequest request{method,path,command.value("body",std::string{}),command.value("revision",std::string{}),true};
    if(command.value("fault",std::string{})=="lose-reply")lose_owner_reply=true;
    if(command.value("fault",std::string{})=="malformed-reply")malform_owner_reply=true;
    if(command.value("fault",std::string{})=="malformed-error")malform_owner_error=true;
    PiPairHttpReply reply;
    if(resource=="targets")reply={remote_owner->targets(),{},{}};
    else if(resource=="inventory")reply={remote_owner->observe(identity('9'))?remote_owner->targets():controller_target_error(503,"transport_unavailable"),{},{}};
    else if(method=="GET")reply=command.contains("context")?
      remote_owner->read(request,identity('9'),[]{return false;},command["context"].get<std::string>()):remote_owner->read(request,identity('9'));
    else if(resource.ends_with("/review"))reply=remote_owner->review(request,identity('9'));
    else reply=remote_owner->write_pico(request,identity('9'),command.value("context",std::string{}),command.value("operation_id",std::string{}));
    result=reply.payload;reply_operation=reply.operation;reply_readback=reply.readback_revision;reply_recovery_resource=reply.recovery_resource;
    if(reply.location.size()>=32)reply_recovery_operation=reply.location.substr(reply.location.size()-32);
    if(!reply.context.empty())gateway_context=reply.context;
    if(result.status==200&&resource=="context")gateway_binding=strict_browser_json(result.body).at("binding");
    if(result.status==200&&strict_browser_json(result.body).value("schema",std::string{})=="target-operation-reviewed")gateway_binding=strict_browser_json(result.body).at("binding");
   }
   else if(command.value("control","")=="reconcile"&&!paired)result=context.reconcile(command.at("operation_id"),command.at("readback_revision"));
   else if(paired){
    if(command.value("fault","")=="cancel-before-handoff")cancel_at_handoff.store(true);
    if(command.value("fault","")=="revoke-before-handoff")revoke_before_usb.store(true);
    if(command.value("fault","")=="wrong-parent-context")wrong_parent.store(true);
    const bool reconcile=command.value("control","")=="reconcile",inventory=command.value("control","")=="inventory";
    const auto method=inventory?std::string("GET"):reconcile?std::string("POST"):command.at("method").get<std::string>();
    const auto resource=inventory?std::string{}:reconcile?std::string("management"):command.at("resource").get<std::string>();
    const auto body=reconcile?nlohmann::json{{"contract","Wsprry-Control/1"},{"action","reconcile"},{"operation_id",command.at("operation_id")},{"readback_revision",command.at("readback_revision")}}.dump():command.value("body","");
    const auto response=client.exchange({"127.0.0.1",server->port()},identity('d'),[&](const auto &requested)->std::shared_ptr<const PiPairTlsCredential>{
      if(requested!=tls)return {};
      return std::make_shared<PiPairTlsCredential>(PiPairTlsCredential{tls,identity('e'),identity('d'),fixture_key()});},tls,
     [&](const auto &principal){return principal.self_id()==identity('e')&&principal.peer_id()==identity('d')&&principal.identity()==tls;},
     method,inventory?std::string("/api/control/v1/targets"):"/api/control/v1/targets/"+settings.device_id+(resource=="context"?"/context":resource.starts_with("operations/")?"/"+resource:"/pico/"+resource),body,
     {{"If-Match",command.value("revision","")},{"X-WsprryPi-Target-Context",command.value("context",gateway_context)},{"X-WsprryPi-Operation-ID",command.value("operation_id","")}});
    if(!response.reply)throw std::runtime_error("paired_usb_exchange_unavailable");
    result=response.reply->payload;
    if(!response.reply->context.empty())gateway_context=response.reply->context;
    reply_operation=response.reply->operation;reply_readback=response.reply->readback_revision;reply_recovery_resource=response.reply->recovery_resource;
    if(!reply_recovery_resource.empty()&&response.reply->location.size()>=32)reply_recovery_operation=response.reply->location.substr(response.reply->location.size()-32);
    if(resource=="context"&&result.status==200)gateway_binding=strict_browser_json(result.body).at("binding");
    if(result.status==200&&strict_browser_json(result.body).value("schema",std::string{})=="target-operation-reviewed")gateway_binding=strict_browser_json(result.body).at("binding");
   }
   else result=context.request({command.at("method"),"/api/control/v1/targets/"+settings.device_id+"/pico/"+command.at("resource").get<std::string>(),command.value("body",""),command.value("revision",""),true,{}},command.value("context",context.id()),command.value("operation_id",""));
   const auto recovery=context.recovery_hint();
   std::cout<<nlohmann::json{{"status",result.status},{"operation",reply_operation},{"body",strict_browser_json(result.body)},{"etag",result.etag},{"binding",paired?gateway_binding:context.binding()},{"context",paired?gateway_context:context.id()},{"readback_revision",paired?reply_readback:context.readback_revision()},{"recovery_operation",paired?reply_recovery_operation:recovery.first},{"recovery_resource",paired?reply_recovery_resource:recovery.second},{"execution_selected",wtp_runtime_selected()},{"execution_ready",wtp_runtime_ready()},{"response_valid",command.value("validate_response",false)?nlohmann::json(pico_management_response_valid(command.at("resource").get<std::string>(),command.value("method",std::string("GET")),result,gateway_binding,command.value("body",std::string{}))):nlohmann::json(nullptr)}}.dump()<<std::endl;
  }
  if(paired){controller_lifetime->close();lifetime->close();server->stop();controller_lifetime->drain();lifetime->drain();}
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
