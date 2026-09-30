// Private, opt-in qualification harness. No application fault switches.
#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/plain_listener.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_endpoint/system_clock.hpp"
#include "wtp_endpoint/tone_engine.hpp"
#include "wtp_endpoint/native_bridge.hpp"
#include "wtp_endpoint/capabilities.hpp"
#include "Singleton/src/singleton.hpp"
#include "WSPR-Transmitter/src/wspr_transmit_backend_rpi.hpp"
#include "json.hpp"
#include <poll.h>
#include <iostream>
#include <csignal>
using namespace wsprrypi;
namespace wtp=wsprrypico::wtp;
static volatile sig_atomic_t ending=0;
static void signal_end(int){ending=1;}
class FaultClock final:public wtp::Clock {
public:
 std::atomic<bool> lost{false}; WtpPiSystemClock physical;
 wtp::ClockSnapshot snapshot() const override {
  auto s=physical.snapshot(); if(lost){s.state=wtp::ClockState::Unsynchronized;s.leap=wtp::LeapState::Unknown;} return s;
 }
};
class ConfirmationAccess final:public IRpiStartupQuiesceAccess {
public:
 std::atomic<bool> deny{false}; std::shared_ptr<IRpiStartupQuiesceAccess> actual=makeProductionRpiStartupQuiesceAccess();
 bool supportedPlatform(std::string&e)override{return actual->supportedPlatform(e);}
 bool discoverPeripheralBase(std::uint32_t&b,std::string&e)override{return actual->discoverPeripheralBase(b,e);}
 bool open(std::string&e)override{return actual->open(e);}
 bool map(std::uint32_t b,std::size_t s,std::string&e)override{return actual->map(b,s,e);}
 bool read(RpiStartupQuiesceRegister r,std::uint32_t&v,std::string&e)override{return actual->read(r,v,e);}
 bool write(RpiStartupQuiesceRegister r,std::uint32_t v,std::string&e)override{return actual->write(r,v,e);}
 bool unmap(std::size_t s,std::string&e)override{return actual->unmap(s,e);}
 bool close(std::string&e)override{bool okay=actual->close(e);if(deny){e="Injected confirmation failure after native hardware shutdown";return false;}return okay;}
};
int main(int argc,char**argv) {
 if(argc!=2 || std::string(argv[1])!="--enable-physical-gpio4")return 2;
 SingletonProcess guard(1234);if(!guard())return 3;
 std::signal(SIGTERM,signal_end);std::signal(SIGINT,signal_end);
 FaultClock clock;WtpPiNativeBridge bridge;auto access=std::make_shared<ConfirmationAccess>();
 WsprRpiBackend backend(bridge,access,4,LegacyGpioProcessorProfile::Bcm2711);
 BackendExecutionInputs inputs;inputs.tx_gpio=inputs.configured_tx_gpio=4;inputs.power_level=7;
 WtpPiToneEngine* execution=nullptr;
 backend.setScheduledExecutionHooks({[&]{return execution->admit_output_enable();},[&]{execution->observe_output_enable();},[&]{if(!execution->admit_output_enable())return false;execution->observe_output_enable();return true;},true});
 WtpPiToneEngine engine(backend,inputs,BackendKind::RPI_CLOCK_GPIO,clock,[&](const wtp::Job&)->std::optional<std::vector<std::uint64_t>> {
  std::vector<std::uint64_t> values;for(double f:backend.realizedEventFrequencies()){if(f==0){values.push_back(0);continue;}auto n=wtp_frequency_nhz(f);if(!n)return {};values.push_back(*n);}return values;
 },0,[&]{bridge.reset_stop();},true,[&]{bridge.request_stop();},false,false,HardwareProfile::BCM2711);
 execution=&engine;if(!engine.startup_safe())return 4;
 WtpPiBootIdentity identity;wtp::JobService service(clock,engine,identity,wtp_backend_caps(backend));WtpPiAuthority authority(service);
 WtpPlainListener listener(authority,wtp_pi_device_id(),"private-fault-harness");if(!listener.start("192.168.1.120",31418))return 5;
 std::cout<<"{\"ready\":true,\"port\":31418}"<<std::endl;
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(360);
 while(!ending && std::chrono::steady_clock::now()<end) {
  pollfd input{STDIN_FILENO,POLLIN,0};int n=::poll(&input,1,100);if(n<=0)continue;
  std::string command;if(!std::getline(std::cin,command))break;
  if(command=="clock-lost")clock.lost=true;
  else if(command=="clock-restored")clock.lost=false;
  else if(command=="confirm-failed")access->deny=true;
  else if(command=="confirm-restored")access->deny=false;
  else if(command=="enable-local")authority.enable_noninteractive();
  else if(command=="disable-local")authority.disable_local();
  else if(command=="reconcile")authority.recover_remote_output();
  else if(command=="quit")break;
  auto s=authority.snapshot();auto remote=s.remote;
  nlohmann::json state={{"command",command},{"output_unknown",s.output_unknown},{"local_requested",s.requested_local_enable},{"local_effective",s.effective_local_enable},{"local_work_allowed",authority.begin_test_tone()},{"output_active",remote.output_active},{"boot_id",remote.boot_id}};
  // Admission probe reserves no hardware. Return it immediately if admitted.
  if(state["local_work_allowed"].get<bool>())authority.end_local_work(true);
  std::cout<<state.dump()<<std::endl;
 }
 access->deny=false;clock.lost=false;listener.stop();bridge.request_stop();
 return engine.disable(clock.snapshot().monotonic_now_ns+5000000000ULL)?0:6;
}
