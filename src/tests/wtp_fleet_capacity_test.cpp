// SPDX-License-Identifier: MIT
// Production Fleet coordinator and WTP sessions over isolated loopback endpoints.
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/application.hpp"
#include "wtp_endpoint/plain_listener.hpp"
#include "wtp_endpoint/authority.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "wtp_settings_json.hpp"
#include "config_handler.hpp"
#include "httplib.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <array>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/timex.h>
// Test-only Linux clock qualification; time still advances on the host clocks.
// Linked only into this fixture, never the application or another test.
extern "C" int __wrap_adjtimex(struct timex* observation) {
 *observation={};observation->maxerror=1;return TIME_OK;
}
#endif
using namespace wsprrypi;
using Json=nlohmann::json;
namespace server=wsprrypico::wtp;

#ifdef __linux__
namespace {
void check(bool okay,const std::string& why){if(!okay)throw std::runtime_error(why);}
class Clock final:public server::Clock {
 static std::uint64_t monotonic(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
 const std::uint64_t mono_=monotonic();
 const std::uint64_t utc_=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
public:
 server::ClockSnapshot snapshot() const override {
  const auto now=monotonic();
  return {server::ClockState::Synchronized,utc_+now-mono_,now,1000000,0,server::LeapState::Normal,{}};
 }
};
class Identity final:public server::IdentitySource {
public: std::string new_boot_id() override{return wtp_random_identity();}
};
class Engine final:public server::RfEngine {
 server::InhibitedRfEngine engine;
public:
 std::atomic<unsigned> starts{0};
 server::PrepareResult prepare(const server::Job& job) override{return engine.prepare(job);}
 bool begin(const server::Job& job,std::uint64_t when) override{++starts;return engine.begin(job,when);}
 server::EngineReport poll(std::uint64_t now) override{return engine.poll(now);}
 bool disable(std::uint64_t deadline) override{return engine.disable(deadline);}
 bool output_active() const override{return false;}
};
server::ServiceConfig config(){server::ServiceConfig c;c.maximum_arm_uncertainty_ns=500000000;return c;}
// Byte-carrier fixture around the production Pico Endpoint and JobService.
// The Pi half uses its shipped WtpPlainListener. Neither carrier drives RF.
class PicoListener {
 struct Client {
  int fd;server::Endpoint endpoint;
  Client(int socket,server::IJobService& service,const std::string& id,
         server::Endpoint::EventIdSource events):fd(socket),endpoint(service,id,"fleet-test","WsprryPico",events){endpoint.connect("local-network");}
  ~Client(){close(fd);}
 };
 server::IJobService& service_;std::string id_;int fd_=-1;unsigned port_=0;
 std::atomic<bool> running_{false};std::thread thread_;std::uint64_t events_=0;
 std::vector<std::unique_ptr<Client>> clients_;
 static void nonblocking(int fd){check(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)==0,"nonblocking fixture socket");}
public:
 PicoListener(server::IJobService& service,std::string id):service_(service),id_(std::move(id)){}
 ~PicoListener(){stop();}
 bool start(unsigned port=0){
  fd_=socket(AF_INET,SOCK_STREAM,0);check(fd_>=0,"Pico loopback socket");
  int reuse=1;setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
  check(bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 && listen(fd_,4)==0,"Pico loopback bind");
  socklen_t length=sizeof(address);getsockname(fd_,reinterpret_cast<sockaddr*>(&address),&length);port_=ntohs(address.sin_port);nonblocking(fd_);
  running_=true;thread_=std::thread([this]{
   while(running_){
    const int accepted=accept(fd_,nullptr,nullptr);
    if(accepted>=0){nonblocking(accepted);clients_.push_back(std::make_unique<Client>(accepted,service_,id_,[this](std::string_view){return events_++;}));}
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    service_.poll();
    for(auto it=clients_.begin();it!=clients_.end();){auto& client=**it;bool closed=false;
     client.endpoint.poll(now);
     if(client.endpoint.can_receive()){
      std::array<std::uint8_t,64> bytes{};const auto n=recv(client.fd,bytes.data(),bytes.size(),0);
      if(n==0)closed=true;
      else if(n>0)check(client.endpoint.receive(std::span(bytes.data(),static_cast<std::size_t>(n)),now)==static_cast<std::size_t>(n),"Pico input consumed");
      else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)closed=true;
     }
     const auto bytes=client.endpoint.output();
     if(!bytes.empty()){const auto n=send(client.fd,bytes.data(),bytes.size(),MSG_NOSIGNAL);if(n>0)client.endpoint.consume_output(n,now);else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)closed=true;}
     if(closed || client.endpoint.closed())it=clients_.erase(it);else ++it;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
  });return true;
 }
 void stop(){running_=false;if(thread_.joinable())thread_.join();clients_.clear();if(fd_>=0)close(fd_);fd_=-1;}
 unsigned bound_port()const{return port_;}
};
struct Endpoint {
 Clock clock; Identity identity; Engine engine;
 server::JobService service{clock,engine,identity,config()};
 WtpPiAuthority authority{service};
 std::string id;
 WtpPlainListener listener;
 std::unique_ptr<PicoListener> pico;
 bool start_listener(unsigned port=0){return pico?pico->start(port):listener.start("127.0.0.1",port);}
 void stop_listener(){if(pico)pico->stop();else listener.stop();}
 unsigned bound_port()const{return pico?pico->bound_port():listener.bound_port();}
 httplib::Server http;
 std::thread http_thread;
 int port{};
 std::atomic<unsigned> delay_ms{0};
 Endpoint(std::string device,unsigned wtp_port=0,int http_port=0,bool is_pico=false):id(std::move(device)),listener(authority,id,"fleet-capacity-test") {
  if(is_pico)pico=std::make_unique<PicoListener>(authority,id);
  check(start_listener(wtp_port),"start loopback WTP endpoint");
  http.new_task_queue=[] {return new httplib::ThreadPool(1);};
  http.Get("/api/control/v1/host/wtp-endpoint",[this](const auto&,auto& response){
   std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms.load()));
   const auto state=authority.snapshot();
   response.set_content(Json{{"schema","wsprrypi-wtp-endpoint/1"},{"device_id",id},
    {"revocation_unavailable",false},{"local_requested",state.requested_local_enable},
    {"output_unknown",state.output_unknown},{"revocation_generation",std::to_string(state.revocation_generation)}}.dump(),"application/json");
  });
  port=http_port ? (http.bind_to_port("127.0.0.1",http_port)?http_port:0) : http.bind_to_any_port("127.0.0.1");check(port>0,"start loopback status endpoint");
  http_thread=std::thread([this]{http.listen_after_bind();});
  http.wait_until_ready();
 }
 ~Endpoint(){http.stop();if(http_thread.joinable())http_thread.join();stop_listener();}
};
Json snapshot(){auto r=wtp_fleet_api("GET","","");check(r.status==200,r.body);return Json::parse(r.body);}
PicoHttpResponse command(Json body){
 // Worker completion legitimately advances the shared revision between GET and
 // POST. Re-read only after a proven pre-mutation CAS refusal; never retry 409.
 for(unsigned attempt=0;;++attempt){
  const auto before=wtp_fleet_api("GET","","");
  auto result=wtp_fleet_api("POST",body.dump(),before.etag);
  if(result.status!=412 || attempt==20)return result;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
}
void succeeds(Json body){const auto result=command(body);check(result.status==200,body.dump()+": "+result.body);}
template<class F> void await(F predicate,unsigned seconds,const char* why){
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
 while(!predicate()){if(std::chrono::steady_clock::now()>=end){std::cerr<<snapshot().dump(2)<<"\n";throw std::runtime_error(why);}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
}
}
#endif
int main(){
#ifdef __linux__
 // Run only inside a network-isolated host fixture. All addresses are numeric
 // loopback, every server uses the production inhibited RF engine, no devices.
 ArgParserConfig policy=::config;policy.allow_unqualified_frequency=true;
 copy_runtime_config(policy,::config); // publish explicit test planning policy
 for(const auto& interface:std::filesystem::directory_iterator("/sys/class/net")) {
  std::ifstream flags(interface.path()/"flags");unsigned value=0;flags>>std::hex>>value;
  check(interface.path().filename()=="lo" || !(value & 1U),"capacity fixture requires all non-loopback interfaces down");
 }
 check(wtp_host_utc_valid(),"fixture clock qualification must be selected explicitly");
 unsetenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
 char path[]="/tmp/wsprrypi-fleet-capacity.XXXXXX";check(mkdtemp(path),"temporary directory");
 struct Cleanup{std::string path;~Cleanup(){stop_wtp_fleet();std::filesystem::remove_all(path);}} cleanup{path};
 const auto ini=std::string(path)+"/fleet.ini";
 const auto self=std::string(32,'f');
 start_wtp_fleet(ini,self);
 check(snapshot().at("maximum_outputs")==32,"advertised capacity is 32");
 std::vector<std::unique_ptr<Endpoint>> endpoints;
 for(unsigned i=0;i<32;++i){
  auto id=std::string(30,'0')+"0123456789abcdef"[i/16]+"0123456789abcdef"[i%16];
  endpoints.push_back(std::make_unique<Endpoint>(id,0,0,i%2!=0));
  auto& peer=*endpoints.back();
  WtpSettings settings;settings.transport="network_plain";settings.hostname=i%4==0?"localhost":"127.0.0.1";
  settings.tcp_port=peer.bound_port();settings.device_id=id;settings.allow_frequency_adjustment=true;
  succeeds({{"operation","assign"},{"name","Output "+std::to_string(i+1)},{"settings",wtp_settings_json(settings)},
   {"schedule",{{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",60},{"phase_seconds",0}}},
   {"enabled",false},{"consent_plain",true},{"management_port",peer.port}});
 }
 const auto full=wtp_fleet_api("GET","","");
 auto extra=Json{{"operation","assign"},{"name","33rd"},{"settings",Json::parse(full.body).at("assignments")[0].at("settings")},
  {"schedule",Json::parse(full.body).at("assignments")[0].at("schedule")},{"enabled",false},{"consent_plain",true},{"management_port",1}};
 extra["settings"]["Device ID"]=std::string(32,'e');extra["settings"]["TCP Port"]=1;
 const auto refused=wtp_fleet_api("POST",extra.dump(),full.etag);
 check(refused.status==409 && refused.body.find("at most 32")!=std::string::npos,"33rd refused before connecting to unusable endpoint");
 check(wtp_fleet_api("GET","","").etag==full.etag,"33rd refusal leaves persistent revision unchanged");
 stop_wtp_fleet();start_wtp_fleet(ini,self);
 check(snapshot().at("assignments").size()==32,"32 saved assignments restart");
 const auto utc=endpoints[0]->clock.snapshot().utc_now_ns;
 const auto phase=(utc/1000000000ULL+25)%60;
 for(auto& peer:endpoints){
  auto schedule=Json::parse(full.body).at("assignments")[0].at("schedule");schedule["phase_seconds"]=phase;
  succeeds({{"operation","schedule"},{"device_id",peer->id},{"schedule",schedule}});
  succeeds({{"operation","enable"},{"device_id",peer->id}});
 }
 await([&]{for(auto& peer:endpoints)if(peer->engine.starts.load()!=1)return false;return true;},85,"all 32 targets must receive their scheduled job");
 auto state=snapshot();std::string slot;
 const auto thread_count=std::distance(std::filesystem::directory_iterator("/proc/self/task"),std::filesystem::directory_iterator{});
 const auto fd_count=std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});
 for(const auto& row:state.at("assignments")){
  if(slot.empty())slot=row.at("last_start_ns");
  check(row.at("last_start_ns")==slot,"32 independent workers share the intended UTC boundary");
 }
 await([&]{for(const auto& row:snapshot().at("assignments"))if(row.at("in_flight")==true)return false;return true;},15,"32 completion barriers clear only after cleanup");
 // A second common boundary mixes healthy, slow, disconnected, and restarted
 // targets. Failures retain their own barriers and cannot delay healthy jobs.
 for(auto& peer:endpoints)succeeds({{"operation","pause"},{"device_id",peer->id}});
 await([]{const auto current=snapshot();for(const auto& row:current.at("assignments"))if(row.at("in_flight")==true)return false;return true;},15,"pause must complete cleanup before changing schedules");
 endpoints[28]->delay_ms=150;
 endpoints[31]->stop_listener();
 const auto restart_id=endpoints[30]->id;
 const auto restart_port=endpoints[30]->bound_port();
 const auto restart_http=endpoints[30]->port;
 endpoints[30].reset();
 endpoints[30]=std::make_unique<Endpoint>(restart_id,restart_port,restart_http);
 const auto second_phase=(endpoints[0]->clock.snapshot().utc_now_ns/1000000000ULL+25)%60;
 for(auto& peer:endpoints){
  auto schedule=Json::parse(full.body).at("assignments")[0].at("schedule");schedule["phase_seconds"]=second_phase;
  succeeds({{"operation","schedule"},{"device_id",peer->id},{"schedule",schedule}});
  succeeds({{"operation","enable"},{"device_id",peer->id}});
 }
 await([&]{for(unsigned i=0;i<30;++i)if(endpoints[i]->engine.starts.load()!=2)return false;return true;},85,"healthy and slow outputs must reach second boundary despite failed peers");
 check(endpoints[30]->engine.starts.load()==0 && endpoints[31]->engine.starts.load()==1,"restart and disconnect never silently rearm");
 const auto response_started=std::chrono::steady_clock::now();
 const auto mixed=snapshot();
 check(std::chrono::steady_clock::now()-response_started<std::chrono::seconds(2),"Fleet status remains responsive with failed outputs");
 std::string second_slot;
 for(unsigned i=0;i<30;++i)for(const auto& row:mixed.at("assignments"))if(row.at("device_id")==endpoints[i]->id){
  if(second_slot.empty())second_slot=row.at("last_start_ns");
  check(row.at("last_start_ns")==second_slot && second_slot!=slot,"healthy peers share the second boundary");
 }
 const auto retired_id=endpoints[0]->id;
 const auto retired_port=endpoints[0]->bound_port();
 endpoints[0]->stop_listener();
 check(endpoints[0]->authority.enable_noninteractive()==WtpPiAuthority::EnableResult::Enabled,"takeover aborts the inhibited job while its controller transport is absent");
 for(unsigned i=1;i<32;++i)succeeds({{"operation","pause"},{"device_id",endpoints[i]->id}});
 await([&]{const auto current=snapshot();return current.at("assignments").size()==31 && current.at("outputs").contains(retired_id) && current.at("outputs").at(retired_id).value("revoked",false);},20,"unconfirmed cleanup retains the removed context");
 await([]{const auto current=snapshot();for(const auto& row:current.at("assignments"))if(row.at("device_id")!=std::string(30,'0')+"1e" && row.at("device_id")!=std::string(30,'0')+"1f" && row.at("in_flight")==true)return false;return true;},15,"healthy pauses complete cleanup");
 const auto retained_revision=wtp_fleet_api("GET","","").etag;
 const auto retained_refusal=command(extra);
 check(retained_refusal.status==409 && retained_refusal.body.find("at most 32")!=std::string::npos,"31 assignments plus retained context refuse another assignment: "+retained_refusal.body);
 check(wtp_fleet_api("GET","","").etag==retained_revision,"retained-capacity refusal performs no persistent mutation");
 check(command({{"operation","recover"},{"device_id",retired_id}}).status==409,"unreachable retired output cannot release capacity");
 check(endpoints[0]->start_listener(retired_port),"restore same-boot retired endpoint");
 await([&]{return command({{"operation","recover"},{"device_id",retired_id}}).status==200;},15,"successful retired cleanup reclaims capacity");
 check(!snapshot().at("outputs").contains(retired_id),"reconciled removed context is released");
 succeeds({{"operation","recover"},{"device_id",restart_id}});
 await([&]{return snapshot().at("outputs").at(restart_id).value("message","").find("Output reconciled")!=std::string::npos;},15,"explicit restart reconciliation succeeds");
 check(snapshot().at("assignments")[29].at("enabled")==false,"reconciled restart stays paused");
 std::cout<<"Mixed workload: 30 healthy/slow targets reached common second slot "<<second_slot<<"; disconnected and restarted peers refused automatic dispatch\n";
 succeeds({{"operation","recover"},{"device_id",endpoints[1]->id}});
 succeeds({{"operation","remove"},{"device_id",endpoints[2]->id}});
 const auto began=std::chrono::steady_clock::now();stop_wtp_fleet();
 const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();
 WtpFleetStore restarted(ini+".wtp-assignments.json",self);
 check(restarted.snapshot().first.at("assignments").size()==30,"takeover and explicit removal persist");
 rusage usage{};getrusage(RUSAGE_SELF,&usage);
 std::cout<<"32 production Fleet sessions (16 Pi, 16 Pico identities) dispatched once at UTC ns "<<slot<<"; shutdown_ms="<<elapsed
  <<"; threads_observed="<<thread_count<<"; fds_observed="<<fd_count<<"; max_rss_kib="<<usage.ru_maxrss<<"; user_s="<<usage.ru_utime.tv_sec<<"; system_s="<<usage.ru_stime.tv_sec<<"\n";
#else
 std::cout<<"Fleet concurrency fixture requires isolated Linux and synchronized host UTC\n";
 return 77;
#endif
}
