// SPDX-License-Identifier: MIT
// Actual Fleet owner/client -> actual Pico TLS or framed endpoint subprocess;
// second output uses the production Pi engine and explicit simulated backend.
#include "wtp_integration/fleet_test.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/capabilities.hpp"
#include "wtp_endpoint/tone_engine.hpp"
#include "wtp_endpoint/execution_context.hpp"
#include "WTP-Server/include/wtp_server/endpoint.hpp"
#include "WSPR-Transmitter/src/simulated_transmit_backend.hpp"
#include "wtp_settings_json.hpp"
#include "config_handler.hpp"
#include <csignal>
#include <deque>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
namespace server=wsprrypico::wtp;
namespace {
class Clock final:public WtpScheduleClock,public server::Clock {
public:
 std::uint64_t now_ms()const override{return WtpSteadyClock{}.now_ms();}
 std::optional<std::uint64_t> utc_now_ns()const override{return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
 void wait_ms(std::uint64_t ms)override{std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
 server::ClockSnapshot snapshot()const override{return {server::ClockState::Synchronized,*utc_now_ns(),now_ms()*1000000,1000,0,server::LeapState::Normal,{}};}
};
class Identity final:public server::IdentitySource {public:std::string new_boot_id()override{return std::string(32,'c');}};
class PiPeer final:public wtp::ByteStream {
 Clock& clock_;Identity identity_;WtpPiSimulationContext context_;
 SimulatedTransmitBackend backend_{context_,{false,{}}};
 WtpPiToneEngine engine_{backend_,{},BackendKind::SIMULATED,clock_,
  [](const server::Job& job)->std::optional<std::vector<std::uint64_t>>{std::vector<std::uint64_t> result;for(const auto& e:job.events)result.push_back(e.frequency_nhz.value_or(0));return result;},
  0.0,[this]{context_.reset();},false,[this]{context_.stop();},true};
 server::JobService service_{clock_,engine_,identity_,wtp_backend_caps(backend_)};
 WtpPiAuthority authority_{service_};
 server::Endpoint endpoint_{authority_,std::string(32,'b'),"fleet-simulated","WsprryPi"};
public:
 explicit PiPeer(Clock& clock):clock_(clock){}
 bool open(){endpoint_.disconnect();endpoint_.connect("local-network");return true;}
 Json host(){return {{"schema","wsprrypi-wtp-endpoint/1"},{"device_id",std::string(32,'b')},{"boot_id",std::string(32,'c')},
  {"revocation_generation","0"},{"revocation_unavailable",false},{"local_requested",false},{"output_unknown",false}};}
 wtp::IoResult read(std::span<std::uint8_t> bytes)override{
  endpoint_.poll(clock_.now_ms());const auto source=endpoint_.output();
  const auto count=std::min(bytes.size(),source.size());std::copy_n(source.begin(),count,bytes.begin());endpoint_.consume_output(count,clock_.now_ms());
  return {count?wtp::IoState::Progress:wtp::IoState::WouldBlock,count};
 }
 wtp::IoResult write(std::span<const std::uint8_t> bytes)override{const auto count=endpoint_.receive(bytes,clock_.now_ms());return {count?wtp::IoState::Progress:wtp::IoState::WouldBlock,count};}
 void close()noexcept override{endpoint_.disconnect();}
};
class Resolver final:public TlsResolver {
public:
 bool begin(const std::string&,unsigned)override{return true;}
 std::optional<std::vector<std::string>> poll()override{return std::vector<std::string>{"127.0.0.1"};}
 void cancel()noexcept override{}
};
class Driver {
 int input_{-1},output_{-1};pid_t child_{-1};std::string buffer_;
public:
 explicit Driver(const std::string& path,bool tls) {
  int to[2],from[2];if(pipe(to)||pipe(from))throw std::runtime_error("pipe");
  child_=fork();if(child_<0)throw std::runtime_error("fork");
  if(!child_){dup2(to[0],0);dup2(from[1],1);::close(to[0]);::close(to[1]);::close(from[0]);::close(from[1]);
   if(tls)execl(path.c_str(),path.c_str(),"0","a","blank",nullptr);else execl(path.c_str(),path.c_str(),nullptr);_exit(127);}
  ::close(to[0]);::close(from[1]);input_=from[0];output_=to[1];
  if(tls && !line().starts_with("READY "))throw std::runtime_error("Pico TLS server not ready");
 }
 ~Driver(){if(input_>=0)::close(input_);if(output_>=0)::close(output_);if(child_>0){kill(child_,SIGTERM);waitpid(child_,nullptr,0);}}
 std::string line(){
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(true){const auto newline=buffer_.find('\n');if(newline!=std::string::npos){auto result=buffer_.substr(0,newline);buffer_.erase(0,newline+1);return result;}
   if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("Pico driver deadline");
   pollfd p{input_,POLLIN,0};if(::poll(&p,1,100)<0)throw std::runtime_error("Pico driver poll");
   if(p.revents&POLLIN){char bytes[8192];const auto count=::read(input_,bytes,sizeof(bytes));if(count<=0)throw std::runtime_error("Pico driver closed");buffer_.append(bytes,count);if(buffer_.size()>1024*1024)throw std::runtime_error("Pico driver reply limit");}
   if(p.revents&(POLLHUP|POLLERR))throw std::runtime_error("Pico driver closed");
  }
 }
 Json command(const Json& body){const auto data=body.dump()+"\n";std::size_t sent=0;while(sent<data.size()){const auto n=::write(output_,data.data()+sent,data.size()-sent);if(n<=0)throw std::runtime_error("Pico driver write");sent+=n;}return Json::parse(line());}
};
class FramedUsb final:public wtp::ByteStream {
 Driver& peer_;std::deque<std::uint8_t> bytes_;
 void ingest(const Json& value){const auto hex=value.at("hex").get<std::string>();const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};for(std::size_t i=0;i<hex.size();i+=2)bytes_.push_back((digit(hex[i])<<4)|digit(hex[i+1]));}
public:
 explicit FramedUsb(Driver& peer):peer_(peer){}
 bool open(){bytes_.clear();ingest(peer_.command({{"action","connect"},{"principal","usb-physical"}}));return true;}
 wtp::IoResult read(std::span<std::uint8_t> out)override{if(bytes_.empty())ingest(peer_.command({{"action","poll"}}));const auto count=std::min(out.size(),bytes_.size());for(std::size_t i=0;i<count;++i){out[i]=bytes_.front();bytes_.pop_front();}return {count?wtp::IoState::Progress:wtp::IoState::WouldBlock,count};}
 wtp::IoResult write(std::span<const std::uint8_t> input)override{static constexpr char h[]="0123456789abcdef";std::string hex;for(auto b:input){hex+=h[b>>4];hex+=h[b&15];}ingest(peer_.command({{"action","send"},{"hex",hex}}));return {wtp::IoState::Progress,input.size()};}
 void close()noexcept override{try{(void)peer_.command({{"action","disconnect"}});}catch(...) {}}
};
}
int main(int argc,char** argv) {
 if(argc!=4)return 2;
 signal(SIGPIPE,SIG_IGN);
 try {
  const bool network=std::string(argv[1])=="network";Driver pico(argv[2],network);
  Clock clock;PiPeer pi(clock);FramedUsb usb(pico);
  TlsStream tls([&]{return clock.now_ms();},TlsStream::Access::LoopbackTest,std::make_unique<Resolver>());
  const std::string directory=argv[3];
  const TlsSelection selection{"wsprrypico-"+std::string(32,'a')+".local","",directory+"/client-ca.crt",directory+"/client.crt",directory+"/client.key",18443};
  auto credentials=network?std::make_shared<TlsCredentials>(selection):nullptr;
  char temporary[]="/tmp/wsprrypi-fleet-operations.XXXXXX";if(!mkdtemp(temporary))throw std::runtime_error("temporary directory");
  const std::string ini=std::string(temporary)+"/test.ini";
  struct Cleanup{std::string path;~Cleanup(){stop_wtp_fleet();std::filesystem::remove_all(path);}}cleanup{temporary};
  auto policy=::config;policy.allow_unqualified_frequency=true;copy_runtime_config(policy,::config);
  Json rows=Json::array();
  for(bool local:{false,true}) {
   WtpSettings settings;settings.device_id=std::string(32,local?'b':network?'a':'d');settings.allow_frequency_adjustment=true;
   settings.transport=local?"network_plain":network?"network":"usb";settings.hostname=local?"127.0.0.1":selection.host;
   settings.tcp_port=local?31417:18443;settings.tls_ca=selection.ca_file;settings.tls_certificate=selection.certificate_file;settings.tls_key=selection.key_file;
   if(!local && !network){settings.path="/dev/fixture-only";settings.usb_serial="fixture-only";settings.vendor_id=0xcafe;settings.product_id=0x4012;}
   rows.push_back({{"device_id",settings.device_id},{"settings",wtp_settings_json(settings)},{"name",local?"Simulated Pi":network?"TLS Pico":"USB Pico"},
    {"schedule",{{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",120},{"phase_seconds",0}}},
    {"enabled",false},{"product",local?"WsprryPi":"WsprryPico"},{"management_port",31415},
    {"revocation_generation",local?Json("0"):Json(nullptr)},{"last_start_ns","0"},{"in_flight",false}});
  }
  WtpFleetStore store(ini+".wtp-assignments.json",std::string(32,'f'));store.update({},[&](Json& d){d["assignments"]=rows;});
  WtpFleetTestHooks hooks;
  hooks.revocation_status=[&](const WtpSettings&,unsigned){return pi.host();};
  hooks.create_target=[&](WtpSettings settings,std::function<bool()> admission,std::shared_ptr<void> lease){
   const bool local=settings.device_id==std::string(32,'b');wtp::ByteStream& carrier=local?static_cast<wtp::ByteStream&>(pi):network?static_cast<wtp::ByteStream&>(tls):static_cast<wtp::ByteStream&>(usb);
   auto open=[&,local]{if(local)return pi.open();if(!network)return usb.open();if(!tls.begin_open(selection,credentials))return false;while(tls.opening()){tls.poll_open();clock.wait_ms(1);}return tls.ready();};
   auto result=std::make_unique<WtpTargetRuntime>(settings,clock,carrier,wtp::SessionOptions{wtp_random_identity(),wtp_random_identity(),settings.device_id},open,std::move(admission));
   if(lease){result->output_lease=std::move(lease);result->output_lease_device=settings.device_id;}else (void)result->reserve_output(settings.device_id);return result;
  };
  start_wtp_fleet_for_test(ini,std::string(32,'f'),hooks);
  std::cout<<"READY"<<std::endl;
  std::string line;while(std::getline(std::cin,line)) {
   try {const auto request=Json::parse(line);
    if(request.value("replace",false))start_wtp_fleet_for_test(ini,std::string(32,'f'),hooks);
    const auto result=wtp_fleet_api(request.value("method","GET"),request.value("body",""),request.value("etag",""),request.value("resource","operations"),request.value("ids",""));
    std::cout<<Json{{"status",result.status},{"body",Json::parse(result.body)},{"etag",result.etag}}.dump()<<std::endl;
   }catch(const std::exception& e){std::cout<<Json{{"error",e.what()}}.dump()<<std::endl;}
  }
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
