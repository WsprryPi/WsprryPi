// SPDX-License-Identifier: MIT
// Production Fleet, session, framing, Endpoint, and JobService with typed clocks.
#include "wtp_integration/fleet_test.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "wtp_endpoint/authority.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "WTP-Server/include/wtp_server/endpoint.hpp"
#include "wtp_settings_json.hpp"
#include "config_handler.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <thread>
#include <unistd.h>
#ifdef __linux__
#include <sys/resource.h>
#endif
using namespace wsprrypi;
using Json=nlohmann::json;
namespace server=wsprrypico::wtp;
namespace {
constexpr std::uint64_t utc_base=1800000000000000000ULL; // exact minute boundary
void check(bool okay,const std::string& why){if(!okay)throw std::runtime_error(why);}
class Clock final:public WtpScheduleClock,public server::Clock {
 mutable std::mutex mutex_;std::condition_variable cv_;
 std::uint64_t time_=1000,horizon_=std::numeric_limits<std::uint64_t>::max();
 bool held_=false,waiting_=false;
public:
 std::uint64_t now_ms()const override{std::lock_guard lock(mutex_);return time_;}
 std::optional<std::uint64_t> utc_now_ns()const override{return utc_base+now_ms()*1000000;}
 server::ClockSnapshot snapshot()const override{const auto time=now_ms();return {server::ClockState::Synchronized,utc_base+time*1000000,time*1000000,1000,0,server::LeapState::Normal,{}};}
 void wait_ms(std::uint64_t ms)override{
  std::unique_lock lock(mutex_);
  waiting_=true;
  cv_.wait(lock,[&]{return !held_ && time_<horizon_;});
  waiting_=false;time_=std::min(time_+std::max<std::uint64_t>(1,ms),horizon_);
 }
 void wake()noexcept override{std::lock_guard lock(mutex_);held_=false;horizon_=std::numeric_limits<std::uint64_t>::max();cv_.notify_all();}
 void horizon(std::uint64_t value){std::lock_guard lock(mutex_);check(value>=time_,"logical time cannot regress");horizon_=value;cv_.notify_all();}
 void advance_paused(std::uint64_t value){std::lock_guard lock(mutex_);check(value>=time_,"paused clock cannot regress");time_=value;}
 void hold(bool value){std::lock_guard lock(mutex_);held_=value;cv_.notify_all();}
 bool held()const{std::lock_guard lock(mutex_);return held_;}
 bool at_barrier()const{std::lock_guard lock(mutex_);return waiting_ && !held_ && time_==horizon_;}
};
class Identity final:public server::IdentitySource {public:std::string new_boot_id()override{return wtp_random_identity();}};
class Engine final:public server::RfEngine {
 server::InhibitedRfEngine engine_;
public:
 std::atomic<unsigned> starts{0};std::atomic<std::uint64_t> launched{0};
 server::PrepareResult prepare(const server::Job& job)override{return engine_.prepare(job);}
 bool begin(const server::Job& job,std::uint64_t when)override{++starts;launched=when;return engine_.begin(job,when);}
 server::EngineReport poll(std::uint64_t now)override{return engine_.poll(now);}
 bool disable(std::uint64_t deadline)override{return engine_.disable(deadline);}
 bool output_active()const override{return false;}
};
struct Peer {
 Clock clock;Identity identity;Engine engine;
 server::JobService service{clock,engine,identity};WtpPiAuthority authority{service};
 std::string id,product;std::atomic<bool> online{true};std::atomic<std::uint64_t> events{0};
 Peer(std::string device,bool pico):id(std::move(device)),product(pico?"WsprryPico":"WsprryPi"){}
 Json revocation(){const auto s=authority.snapshot();return {{"schema","wsprrypi-wtp-endpoint/1"},{"device_id",id},{"revocation_unavailable",false},{"local_requested",s.requested_local_enable},{"output_unknown",s.output_unknown},{"revocation_generation",std::to_string(s.revocation_generation)}};}
};
// Real protocol endpoint and framing; only the byte carrier is an in-memory
// fixture. Each runtime owns a fresh connection, sharing the actual job service.
class MemoryStream final:public wtp::ByteStream {
 Peer& peer_;server::Endpoint endpoint_;bool open_=false;
public:
 explicit MemoryStream(Peer& peer):peer_(peer),endpoint_(peer.authority,peer.id,"fleet-test",peer.product,[&peer](std::string_view){return peer.events++;}){}
 bool open(){endpoint_.disconnect();open_=peer_.online;if(open_)endpoint_.connect("local-network");return open_;}
 wtp::IoResult read(std::span<std::uint8_t> bytes)override{
  if(!open_ || !peer_.online)return {wtp::IoState::Closed,0};
  if(peer_.clock.held())return {wtp::IoState::WouldBlock,0};
  endpoint_.poll(peer_.clock.now_ms());const auto source=endpoint_.output();
  if(source.empty())return {endpoint_.closed()?wtp::IoState::Closed:wtp::IoState::WouldBlock,0};
  const auto count=std::min(bytes.size(),source.size());std::copy_n(source.begin(),count,bytes.begin());endpoint_.consume_output(count,peer_.clock.now_ms());return {wtp::IoState::Progress,count};
 }
 wtp::IoResult write(std::span<const std::uint8_t> bytes)override{
  if(!open_ || !peer_.online)return {wtp::IoState::Closed,0};
  if(peer_.clock.held())return {wtp::IoState::WouldBlock,0};
  endpoint_.poll(peer_.clock.now_ms());
  const auto count=endpoint_.receive(bytes.first(std::min<std::size_t>(64,bytes.size())),peer_.clock.now_ms());
  return {count?wtp::IoState::Progress:wtp::IoState::WouldBlock,count};
 }
 void close()noexcept override{open_=false;endpoint_.disconnect();}
};
Json snapshot(){const auto result=wtp_fleet_api("GET","","");check(result.status==200,result.body);return Json::parse(result.body);}
PicoHttpResponse command(const Json& body){for(unsigned attempt=0;;++attempt){const auto before=wtp_fleet_api("GET","","");auto result=wtp_fleet_api("POST",body.dump(),before.etag);if(result.status!=412 || attempt==20)return result;std::this_thread::yield();}}
void succeeds(const Json& body){const auto result=command(body);check(result.status==200,body.dump()+": "+result.body);}
// Wall time bounds a hung test only. It never advances a protocol clock or
// chooses a launch instant. Logical clocks move only through explicit gates.
template<class F>void await(F predicate,const char* why){const auto watchdog=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(!predicate()){if(std::chrono::steady_clock::now()>=watchdog){std::cerr<<snapshot().dump(2)<<'\n';throw std::runtime_error(why);}std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
}
int main(){
 ArgParserConfig policy=::config;policy.allow_unqualified_frequency=true;copy_runtime_config(policy,::config);
 char path[]="/tmp/wsprrypi-fleet-capacity.XXXXXX";check(mkdtemp(path),"temporary directory");
 const auto ini=std::string(path)+"/fleet.ini";const std::string self(32,'f');
 std::map<std::string,std::shared_ptr<Peer>> peers;
 std::vector<std::shared_ptr<Peer>> ordered;
 for(unsigned i=0;i<32;++i){auto id=std::string(30,'0')+"0123456789abcdef"[i/16]+"0123456789abcdef"[i%16];auto peer=std::make_shared<Peer>(id,i%2!=0);peers.emplace(id,peer);ordered.push_back(std::move(peer));}
 std::mutex streams_mutex;std::vector<std::shared_ptr<MemoryStream>> streams;
 WtpFleetTestHooks hooks;
 hooks.create_target=[&](WtpSettings settings,std::function<bool()> admission,std::shared_ptr<void> lease){
  auto& peer=*peers.at(settings.device_id);auto stream=std::make_shared<MemoryStream>(peer);
  auto target=std::make_unique<WtpTargetRuntime>(settings,peer.clock,*stream,
   wtp::SessionOptions{wtp_random_identity(),wtp_random_identity(),settings.device_id},[stream]{return stream->open();},std::move(admission));
  if(lease){target->output_lease=std::move(lease);target->output_lease_device=settings.device_id;}
  else check(target->reserve_output(settings.device_id),"fixture preserves identity exclusion");
  {std::lock_guard lock(streams_mutex);streams.push_back(std::move(stream));}return target;
 };
 hooks.revocation_status=[&](const WtpSettings& settings,unsigned){return peers.at(settings.device_id)->revocation();};
 struct Cleanup{std::string path;~Cleanup(){stop_wtp_fleet();std::filesystem::remove_all(path);}}cleanup{path};
 start_wtp_fleet_for_test(ini,self,hooks);check(snapshot().at("maximum_outputs")==32,"advertised capacity is 32");
 Json schedule={{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",60},{"phase_seconds",0}};
 for(const auto& peer:ordered){WtpSettings settings;settings.transport="network_plain";settings.hostname="127.0.0.1";settings.tcp_port=31417;settings.device_id=peer->id;settings.allow_frequency_adjustment=true;
  succeeds({{"operation","assign"},{"name","Output "+peer->id},{"settings",wtp_settings_json(settings)},{"schedule",schedule},{"enabled",false},{"consent_plain",true},{"management_port",31415}});}
 const auto full=wtp_fleet_api("GET","","");
 auto extra=Json{{"operation","assign"},{"name","33rd"},{"settings",snapshot().at("assignments")[0].at("settings")},{"schedule",schedule},{"enabled",false},{"consent_plain",true},{"management_port",1}};
 extra["settings"]["Device ID"]=std::string(32,'e');
 const auto refused=command(extra);check(refused.status==409 && refused.body.find("at most 32")!=std::string::npos,"33rd refused before unknown peer factory");check(wtp_fleet_api("GET","","").etag==full.etag,"33rd preserves persistent revision");
 stop_wtp_fleet();start_wtp_fleet_for_test(ini,self,hooks);check(snapshot().at("assignments").size()==32,"32 assignments reload");
 auto all_armed=[&](unsigned count){for(unsigned i=0;i<count;++i){const auto& peer=*ordered[i];if(!peer.clock.at_barrier() || peer.authority.snapshot().remote.state!=server::State::Armed)return false;}return true;};
 auto all_complete=[&](unsigned first,unsigned end){const auto state=snapshot();for(unsigned i=first;i<end;++i){const auto& wtp=state.at("outputs").at(ordered[i]->id).at("wtp");if(!wtp.contains("last_report") || wtp.at("last_report").is_null() || wtp.at("last_report").at("outcome")!="complete")return false;}return true;};
 // Let current jobs finish and reconcile, but hold before the next dispatch.
 // Derive that barrier from the production scheduler's published plan.
 auto drain_current=[&](unsigned first,unsigned end){const auto state=snapshot();for(unsigned i=first;i<end;++i){const auto& output=state.at("outputs").at(ordered[i]->id).at("wtp");const auto dispatch=wtp_fleet_decimal(output.at("dispatch_utc_ns"));const auto next_dispatch=dispatch+schedule.at("period_seconds").get<std::uint64_t>()*1000000000ULL;ordered[i]->clock.horizon((next_dispatch-utc_base)/1000000-1);}};

 for(auto& peer:ordered){peer->clock.horizon(59999);succeeds({{"operation","enable"},{"device_id",peer->id}});}
 await([&]{return all_armed(32);},"all 32 acknowledge ARM before time advances");
#ifdef __linux__
 const auto threads=std::distance(std::filesystem::directory_iterator("/proc/self/task"),std::filesystem::directory_iterator{});
 const auto descriptors=std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});
#endif
 for(auto& peer:ordered){check(peer->engine.starts==0,"arming never launches early");peer->clock.horizon(60000);}
 await([&]{for(auto& peer:ordered)if(peer->engine.starts!=1)return false;return true;},"32 launches at shared logical boundary");
 for(auto& peer:ordered)check(peer->engine.launched==60000000000ULL,"exact first monotonic launch");
 drain_current(0,32);
 await([&]{return all_complete(0,32);},"all 32 complete through production session cleanup");
 for(auto& peer:ordered)succeeds({{"operation","pause"},{"device_id",peer->id}});
 await([]{const auto state=snapshot();for(const auto& row:state.at("assignments"))if(row.at("in_flight")==true)return false;return true;},"pause/completion race cannot retain a confirmed barrier");
 for(auto& peer:ordered){check(peer->engine.starts==1,"pause prevents another job");peer->clock.advance_paused(120000);peer->clock.horizon(179999);}
 // The slow peer cannot advance its own protocol exchange until the other 29
 // healthy targets acknowledge ARM. No sleep or latency allowance models it.
 ordered[28]->clock.hold(true);ordered[30]->online=false;
 check(ordered[31]->authority.recover_remote_output(),"Pico fixture restart establishes a new boot");
 for(auto& peer:ordered)succeeds({{"operation","enable"},{"device_id",peer->id}});
 await([&]{for(unsigned i=0;i<30;++i)if(i!=28 && (!ordered[i]->clock.at_barrier() || ordered[i]->authority.snapshot().remote.state!=server::State::Armed))return false;return true;},"held peer cannot block healthy ARM admission");
 check(ordered[28]->engine.starts==1,"held peer cannot launch");ordered[28]->clock.hold(false);
 await([&]{return all_armed(30);},"released slow peer acknowledges the same intended slot");
 for(unsigned i=0;i<30;++i)ordered[i]->clock.horizon(180000);
 await([&]{for(unsigned i=0;i<30;++i)if(ordered[i]->engine.starts!=2)return false;return true;},"30 healthy/slow peers launch at second logical boundary");
 for(unsigned i=0;i<30;++i)check(ordered[i]->engine.launched==180000000000ULL,"exact second launch independent of CPU scheduling");
 check(ordered[30]->engine.starts==1 && ordered[31]->engine.starts==1,"disconnect and changed boot cannot rearm");
 ordered[0]->online=false;ordered[0]->clock.wake();check(ordered[0]->authority.enable_noninteractive()==WtpPiAuthority::EnableResult::Enabled,"local takeover");
 drain_current(1,30);
 await([&]{return all_complete(1,30);},"healthy peers complete despite takeover and failed targets");
 for(unsigned i=1;i<32;++i)succeeds({{"operation","pause"},{"device_id",ordered[i]->id}});
 await([&]{const auto state=snapshot();return state.at("assignments").size()==31 && state.at("outputs").contains(ordered[0]->id) && state.at("outputs").at(ordered[0]->id).value("revoked",false);},"unreachable removed context is retained");
 const auto retained=command(extra);check(retained.status==409 && retained.body.find("at most 32")!=std::string::npos,"31 assignments plus retained context refuse another");
 check(command({{"operation","recover"},{"device_id",ordered[0]->id}}).status==409,"unreachable retired context cannot release capacity");ordered[0]->online=true;
 await([&]{return command({{"operation","recover"},{"device_id",ordered[0]->id}}).status==200;},"successful cleanup reclaims capacity");
 succeeds({{"operation","recover"},{"device_id",ordered[31]->id}});
 await([&]{return snapshot().at("outputs").at(ordered[31]->id).value("message","").find("Output reconciled")!=std::string::npos;},"explicit changed-boot recovery");
 const auto recovered=snapshot();
 for(const auto& row:recovered.at("assignments"))if(row.at("device_id")==ordered[31]->id)
  check(row.at("enabled")==false && row.at("in_flight")==false,"reconciled restart remains paused without replay");
 check(ordered[31]->engine.starts==1,"recovery never launches a job");
 succeeds({{"operation","remove"},{"device_id",ordered[2]->id}});
 stop_wtp_fleet();WtpFleetStore stopped(ini+".wtp-assignments.json",self);check(stopped.snapshot().first.at("assignments").size()==30,"takeover and removal persist");
#ifdef __linux__
 rusage usage{};getrusage(RUSAGE_SELF,&usage);
 std::cout<<"Fixture resources: threads="<<threads<<"; descriptors="<<descriptors<<"; max_rss_kib="<<usage.ru_maxrss<<"; user_s="<<usage.ru_utime.tv_sec<<"; system_s="<<usage.ru_stime.tv_sec<<'\n';
#endif
 std::cout<<"Deterministic Fleet: 32 ARM acknowledgments, exact 60 s launch and completion; 29 healthy ARM acknowledgments while one peer is held; 30 exact 180 s launches; failed-peer exclusion, pause, takeover, retained capacity, reconciliation, and shutdown passed.\n";
}
