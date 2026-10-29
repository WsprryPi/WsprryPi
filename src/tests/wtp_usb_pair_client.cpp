#include "udp_logging/management.hpp"
// SPDX-License-Identifier: MIT
// Hardware-free pipe carrier, real Pi client/browser/configuration adapters and
// real Pico USB endpoint/application/time handlers. No serial or network opens.
#include "wtp_integration/usb_management.hpp"
#include "wtp_integration/usb_cdc.hpp"
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/browser_api.hpp"
#include "tests/network/protected_browser_fixture.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
namespace {
std::uint64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
class Pipe : public wtp::ByteStream {
public:
  int input{-1},output{-1};pid_t child{-1};
  explicit Pipe(const char *driver) {
    int to[2],from[2];if(pipe(to)||pipe(from))throw std::runtime_error("pipe");
    child=fork();if(child<0)throw std::runtime_error("fork");
    if(!child) {dup2(to[0],STDIN_FILENO);dup2(from[1],STDOUT_FILENO);::close(to[0]);::close(to[1]);::close(from[0]);::close(from[1]);execl(driver,driver,nullptr);_exit(127);}
    ::close(to[0]);::close(from[1]);input=from[0];output=to[1];
    fcntl(input,F_SETFL,O_NONBLOCK);fcntl(output,F_SETFL,O_NONBLOCK);
  }
  ~Pipe(){close();if(child>0){kill(child,SIGTERM);waitpid(child,nullptr,0);}}
  wtp::IoResult read(std::span<std::uint8_t> bytes) override {
    auto n=::read(input,bytes.data(),std::min<std::size_t>(bytes.size(),17));
    if(n>0)return {wtp::IoState::Progress,std::size_t(n)};
    return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Closed,0};
  }
  wtp::IoResult write(std::span<const std::uint8_t> bytes) override {
    auto n=::write(output,bytes.data(),std::min<std::size_t>(bytes.size(),23));
    if(n>0)return {wtp::IoState::Progress,std::size_t(n)};
    return {n<0&&(errno==EAGAIN||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};
  }
  void close() noexcept override {if(input>=0)::close(input);if(output>=0)::close(output);input=output=-1;}
};
}
int main(int argc,char **argv) {
 const bool physical=argc==5 && std::string(argv[1])=="--usb";
 if(argc!=2 && !physical)return 2;
 try {
  signal(SIGPIPE,SIG_IGN);
  PosixCdcSystem system;UsbCdcStream usb(system);std::unique_ptr<Pipe> pipe;
  wtp::ByteStream *selected{};
  const auto expected=physical?std::string(argv[4]):std::string(32,'a');
  if(physical) {
    const auto management=paired_linux_cdc({argv[2],argv[3],0xcafe,0x4012},PicoCdcRole::Management);
    if(!usb.begin_open(management,now_ms()))throw std::runtime_error(usb.diagnostic());
    while(usb.state()==CdcState::Resetting){usb.poll_open(now_ms());std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    if(usb.state()!=CdcState::Ready)throw std::runtime_error(usb.diagnostic());
    selected=&usb;
  } else {pipe=std::make_unique<Pipe>(argv[1]);selected=pipe.get();}
  UsbManagementClient client(*selected,now_ms,[](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));});
  if(!client.hello(expected))throw std::runtime_error(client.observation().dump());
  const auto exchange=[&](const std::string &path,const std::string &method,const std::string &body,const std::string &revision) {
    return client.request(path.starts_with("/")?path:"/api/v1/"+path,method,body,revision);
  };
  WtpBrowserApi api([&]{
      const auto result=exchange("/api/v1/status","GET",{},{});
      if(result.status!=200)throw std::runtime_error("Pico status unavailable");
      const auto pico=strict_browser_json(result.body);
      return Json{{"selected",true},{"transport","usb"},{"pico",pico},{"remote",pico.value("job",Json(nullptr))},{"usb",client.observation()}};
    },
    [&](auto resource,auto method,auto body,auto revision,auto) {
      if(resource=="member/management")return PicoHttpResponse{200,Json{{"carrier","usb_physical"},{"selection_id","pipe-selection"},
        {"connected",true},{"host",""},{"session",nullptr}}.dump(),{}};
      if(resource=="member/configuration")return pico_configuration_request(exchange,client.device(),client.boot(),method,body,revision,"usb_physical");
      if(resource=="member/application"||resource=="member/station"||resource=="member/hardware"||(resource=="member/gpio-drive"||resource=="member/amplifier-timing"||resource=="member/filter-selector")||resource=="member/gpio-drive-progress"||resource=="member/pins"||resource=="member/pins/validate")
        return pico_resource_request(exchange,client.device(),client.boot(),resource.substr(7),method,body,revision,"usb_physical");
      if(resource=="member/udp-logging"||resource=="member/udp-logging/capabilities"||resource=="member/udp-logging/test"||resource=="member/udp-logging/test/capabilities")return pico_udp_logging_request(exchange,client.device(),client.boot(),resource.substr(7),method,body,revision);
      if(resource.starts_with("member/progress"))return pico_progress_request(exchange,client.device(),client.boot(),resource.substr(7),method,body,revision,"usb_physical");
      return exchange(resource=="member/wifi"?"/api/usb/v1/wifi":resource=="member/hostname"?"/api/v1/hostname":resource=="member/ap"?"/api/usb/v1/ap":resource=="member/restart"?"/api/v1/restart":resource,method,body,revision);
    },[](auto){return PicoHttpResponse{409,"{}",{}};});
  std::string line;
  while(std::getline(std::cin,line)) {
    try {
      const auto command=strict_browser_json(line);
      if(command.contains("raw_resource")) {
        const auto r=exchange(command.at("raw_resource"),command.value("method","GET"),command.value("body",""),command.value("revision",""));
        std::cout<<Json{{"status",r.status},{"body",strict_browser_json(r.body)},{"etag",r.etag},{"bytes",r.body.size()}}.dump()<<std::endl;
        continue;
      }
      if(command.contains("synchronize")) {
        const bool accepted=physical?client.synchronize():client.synchronize([] {
          const auto mono=std::chrono::steady_clock::now().time_since_epoch();
          const auto utc=std::chrono::system_clock::now().time_since_epoch();
          return HostUtcObservation{true,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(mono).count()),
            std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(utc).count()),1000000};
        });
        std::cout<<Json{{"accepted",accepted},{"observation",client.observation()}}.dump()<<std::endl;
        continue;
      }
      auto result=protected_browser_fixture(api,command);
      std::cout<<Json{{"status",result.status},{"body",strict_browser_json(result.body)},{"etag",result.etag}}.dump()<<std::endl;
    }catch(const std::exception &e){std::cout<<Json{{"failure",e.what()}}.dump()<<std::endl;}
  }
 } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
