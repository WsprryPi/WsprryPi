// SPDX-License-Identifier: MIT
// Production Fleet holds and actual Pico management. No device nodes or RF.
#include "wtp_integration/fleet_test.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/management_identity.hpp"
#include "wtp_integration/usb_management.hpp"
#include "wtp_integration/network_http.hpp"
#include "wtp_integration/configuration.hpp"
#include "wtp_settings_json.hpp"
#include <csignal>
#include <thread>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
namespace {
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class Peer final:public wtp::ByteStream {
 int input_{-1},output_{-1};pid_t child_{-1};
public:
 Peer(const char* path,bool tls){
  int to[2],from[2];if(pipe(to)||pipe(from))throw std::runtime_error("pipe");
  child_=fork();if(child_<0)throw std::runtime_error("fork");
  if(!child_){dup2(to[0],0);dup2(from[1],1);for(int fd:{to[0],to[1],from[0],from[1]})::close(fd);
   if(tls)execl(path,path,"0","a","blank",nullptr);else execl(path,path,nullptr);_exit(127);}
  ::close(to[0]);::close(from[1]);input_=from[0];output_=to[1];
  fcntl(input_,F_SETFL,O_NONBLOCK);fcntl(output_,F_SETFL,O_NONBLOCK);
  if(tls){std::string line;const auto deadline=now()+10000;while(now()<deadline&&line.find('\n')==std::string::npos){
    char value;const auto n=::read(input_,&value,1);if(n==1)line+=value;else std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }if(!line.starts_with("READY "))throw std::runtime_error("Pico TLS readiness");}
 }
 ~Peer(){close();if(child_>0){kill(child_,SIGTERM);while(waitpid(child_,nullptr,0)<0&&errno==EINTR){}}}
 wtp::IoResult read(std::span<std::uint8_t> bytes)override{const auto n=::read(input_,bytes.data(),std::min<std::size_t>(17,bytes.size()));if(n>0)return {wtp::IoState::Progress,std::size_t(n)};return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Closed,0};}
 wtp::IoResult write(std::span<const std::uint8_t> bytes)override{const auto n=::write(output_,bytes.data(),std::min<std::size_t>(23,bytes.size()));if(n>0)return {wtp::IoState::Progress,std::size_t(n)};return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};}
 void close()noexcept override{if(input_>=0)::close(input_);if(output_>=0)::close(output_);input_=output_=-1;}
};
struct Resolver: TlsResolver {
 bool begin(const std::string&,unsigned)override{return true;}
 auto poll()->std::optional<std::vector<std::string>>override{return std::vector<std::string>{"127.0.0.1"};}
 void cancel()noexcept override{}
};
std::string bytes(const std::string& path){std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};}
}
int main(int argc,char** argv){
 if(argc<3||argc>4)return 2;
 const std::string_view carrier=argv[1];
 if(carrier!="network"&&carrier!="usb")return 2;
 const bool network=carrier=="network";
 if(network&&(argc!=4||!argv[3][0]))return 2;
 signal(SIGPIPE,SIG_IGN);
 try{
  Peer peer(argv[2],network);
  UsbManagementClient usb(peer,now,[](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));});
  TlsStream tls(now,TlsStream::Access::LoopbackTest,std::make_unique<Resolver>());
  TlsSelection selection{};
  std::shared_ptr<TlsCredentials> keys;
  if(network){
   const std::string credentials=argv[3];
   selection={"wsprrypico-"+std::string(32,'a')+".local","",credentials+"/client-ca.crt",credentials+"/client.crt",credentials+"/client.key",18443};
   keys=std::make_shared<TlsCredentials>(selection);
  }
  if(!network&&!usb.hello(std::string(32,'a')))throw std::runtime_error("Actual USB management hello");
  const auto read_peer=[&]{return network?pico_http_request(tls,selection,keys,now,"status","GET",{},{}):usb.request("/api/control/v1/status","GET");};
  const auto initial=read_peer();if(initial.status!=200)throw std::runtime_error("Actual management status");
  const auto boot=Json::parse(initial.body)["job"]["boot_id"].get<std::string>();
  if(!pico_native_status_valid(Json::parse(initial.body),std::string(32,'a'),boot))throw std::runtime_error("Invalid native status");
  char pattern[]="/tmp/fleet-retirement-pair-XXXXXX";if(!mkdtemp(pattern))throw std::runtime_error("temporary directory");
  const std::string root=pattern,ini=root+"/fleet.ini",self(32,'f');
  struct Cleanup{std::string path;~Cleanup(){stop_wtp_fleet();std::filesystem::remove_all(path);}}cleanup{root};
  WtpFleetStore store(ini+".wtp-assignments.json",self);
  store.update({},[&](Json& document){for(char id:{'a','b'}){
   WtpSettings settings;settings.device_id=std::string(32,id);settings.transport="network_plain";settings.hostname="preserved.local";settings.tcp_port=31417;
   document["assignments"].push_back({{"device_id",settings.device_id},{"settings",wtp_settings_json(settings)},{"name","Duplicate name"},
    {"schedule",{{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",120},{"phase_seconds",0}}},
    {"enabled",id=='a'},{"product",id=='a'?"WsprryPico":"WsprryPi"},{"management_port",31415},
    {"revocation_generation",id=='a'?Json(nullptr):Json("0")},{"last_start_ns","120000000000"},{"in_flight",id=='a'}});
  }});
  const auto original=bytes(ini+".wtp-assignments.json");unsigned contacts=0;
  WtpFleetTestHooks hooks;
  hooks.before_execution_contact=[&]{++contacts;throw std::runtime_error("Retired execution contact");};

  start_wtp_fleet_for_test(ini,self,hooks);std::cout<<"READY"<<std::endl;
  std::string line;while(std::getline(std::cin,line)){
   try{
    const auto request=Json::parse(line);
    if(request.value("peer_status",false)){
     const auto result=read_peer();std::cout<<Json{{"status",result.status},{"body",Json::parse(result.body)},{"legacy_contacts",contacts},{"saved_unchanged",bytes(ini+".wtp-assignments.json")==original}}.dump()<<std::endl;continue;
    }
    if(request.value("fail_replace",false)){
     const auto broken=ini+".broken";{std::ofstream file(broken+".wtp-assignments.json");file<<"{broken";}chmod((broken+".wtp-assignments.json").c_str(),0600);
     start_wtp_fleet_private_storage_for_test(broken,self,broken+".controller-retirements");
    }
    if(request.value("replace",false))start_wtp_fleet_for_test(ini,self,hooks);
    const auto result=wtp_fleet_api(request.value("method","GET"),request.value("body",""),request.value("etag",""),request.value("resource","operations"),request.value("ids",""));
    std::cout<<Json{{"status",result.status},{"body",Json::parse(result.body)},{"etag",result.etag}}.dump()<<std::endl;
   }catch(const std::exception& error){std::cout<<Json{{"error",error.what()}}.dump()<<std::endl;}
  }
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
