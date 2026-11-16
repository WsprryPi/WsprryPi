// SPDX-License-Identifier: MIT
// Real independent management Context/Journal -> real USB framing/ACK -> the
// explicitly selected software Pico driver. No physical-carrier command exists.
#include "wtp_integration/pico_management_context.hpp"
#include "wtp_runtime_bridge.hpp"
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
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class Driver final:public wtp::ByteStream {
 int input{-1},output{-1};pid_t child{-1};
 public:
 explicit Driver(const char *program){
  int to[2],from[2];if(pipe(to))throw std::runtime_error("driver_pipe");
  if(pipe(from)){::close(to[0]);::close(to[1]);throw std::runtime_error("driver_pipe");}
  child=fork();if(child<0){for(int fd:{to[0],to[1],from[0],from[1]})::close(fd);throw std::runtime_error("driver_fork");}
  if(!child){dup2(to[0],STDIN_FILENO);dup2(from[1],STDOUT_FILENO);for(int fd:{to[0],to[1],from[0],from[1]})::close(fd);execl(program,program,nullptr);_exit(127);}
  ::close(to[0]);::close(from[1]);input=from[0];output=to[1];fcntl(input,F_SETFL,O_NONBLOCK);fcntl(output,F_SETFL,O_NONBLOCK);
 }
 ~Driver(){close();if(child>0){kill(child,SIGTERM);while(waitpid(child,nullptr,0)<0&&errno==EINTR){}}}
 wtp::IoResult read(std::span<std::uint8_t> bytes)override{const auto n=::read(input,bytes.data(),std::min<std::size_t>(bytes.size(),17));if(n>0)return {wtp::IoState::Progress,std::size_t(n)};return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Closed,0};}
 wtp::IoResult write(std::span<const std::uint8_t> bytes)override{const auto n=::write(output,bytes.data(),std::min<std::size_t>(bytes.size(),23));if(n>0)return {wtp::IoState::Progress,std::size_t(n)};return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};}
 void close()noexcept override{if(input>=0)::close(input);if(output>=0)::close(output);input=output=-1;}
};
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 try{
  signal(SIGPIPE,SIG_IGN);Driver driver(argv[1]);
  char pattern[]="/tmp/pico-independent-usb-XXXXXX";const auto directory=mkdtemp(pattern);if(!directory)throw std::runtime_error("private_directory");
  struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{directory};
  WtpSettings settings;settings.transport="usb";settings.path="/dev/fixture-wtp";settings.usb_serial="fixture-independent";settings.vendor_id=0xcafe;settings.product_id=0x4012;settings.device_id=std::string(32,'a');
  auto channel=std::make_shared<PicoUsbManagementChannel>(settings);
  channel->factory_for_test=[&]{return std::make_unique<UsbManagementClient>(driver,now,[](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));});};
  const auto journal=std::string(directory)+"/journal",controller=std::string(32,'c');
  PicoManagementContext context(settings,controller,journal,"1","1",{{},{},channel},PicoManagementHistoryAuthority::isolated());
  const auto connected=context.connect("usb_physical");if(connected.status!=200)throw std::runtime_error(connected.body);
  std::cout<<"READY"<<std::endl;std::string line;
  while(std::getline(std::cin,line)){
   const auto command=strict_browser_json(line);PicoHttpResponse result;
   if(command.value("control","")=="journal")result={200,PicoManagementJournal(journal,controller,settings.device_id).snapshot().dump(),{}};
   else if(command.value("control","")=="status")result=channel->client->request("/api/control/v1/status","GET",{},{});
   else if(command.value("control","")=="reconcile")result=context.reconcile(command.at("operation_id"),command.at("readback_revision"));
   else result=context.request({command.at("method"),"/api/control/v1/targets/"+settings.device_id+"/pico/"+command.at("resource").get<std::string>(),command.value("body",""),command.value("revision",""),true,{}},command.value("context",context.id()),command.value("operation_id",""));
   const auto recovery=context.recovery_hint();
   std::cout<<nlohmann::json{{"status",result.status},{"body",strict_browser_json(result.body)},{"etag",result.etag},{"binding",context.binding()},{"context",context.id()},{"readback_revision",context.readback_revision()},{"recovery_operation",recovery.first},{"recovery_resource",recovery.second},{"execution_ready",wtp_runtime_ready()}}.dump()<<std::endl;
  }
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
