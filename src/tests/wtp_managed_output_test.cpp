// SPDX-License-Identifier: MIT
#include "wtp_managed_output.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_pi_control.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "wtp_backend_peer.hpp"
#include <chrono>
#include <thread>
#include <future>
#include <filesystem>
#include <unistd.h>
using namespace std::chrono_literals;
using backend_test::checks;
namespace {
struct Clock:wsprrypi::WtpScheduleClock {
    backend_test::Peer peer;
    std::uint64_t now_ms()const override{return peer.now;}
    std::optional<std::uint64_t> utc_now_ns()const override{return peer.utc_base+peer.mono();}
    void wait_ms(std::uint64_t value)override{peer.advance(peer.now+value);}
};
template<class F>bool refuses(F action){try{action();return false;}catch(...){return true;}}
}
int main(){
 try{
    Clock clock;
    struct Reset {~Reset(){(void)wtp_runtime_stop();select_wtp_runtime(std::nullopt);}} reset;
    WtpSettings settings;settings.transport="network_plain";settings.hostname="127.0.0.1";
    settings.tcp_port=31417;settings.device_id=backend_test::device;
    set_wtp_runtime_for_test(settings,clock,clock.peer,{backend_test::sid,backend_test::owner_id,backend_test::device},[&]{clock.peer.open();return true;});
    const auto original=clock.peer.operations;
    auto offline=wsprrypi::wtp_runtime_managed_output();
    CHECK(offline&&!offline->destination_id&&offline->output.generation&&clock.peer.operations==original);
    CHECK(offline->reserve_configuration&&offline->reserve_configuration());
    CHECK(!wtp_runtime_switch_allowed());
    wsprrypi::TransmissionRequest request;request.mode=wsprrypi::TransmissionMode::TONE;
    request.output.backend=wsprrypi::BackendKind::WTP;request.payload=wsprrypi::TonePayload{14097100,200ms,{}};
    request.policy.allow_unqualified_frequency=true;
    CHECK(refuses([&]{wtp_runtime_prepare(request);})&&refuses([]{wtp_runtime_start();}));
    bool other_refused=false;std::thread other([&]{other_refused=refuses([]{select_wtp_runtime(std::nullopt);});});other.join();
    CHECK(other_refused&&clock.peer.operations==original);
    offline->release_configuration();
    CHECK(wtp_runtime_inspect().ok);
    const auto read_only=clock.peer.operations;
    auto idle=wsprrypi::wtp_runtime_managed_output();
    CHECK(idle&&idle->destination_id==backend_test::device&&idle->output.generation>offline->output.generation&&
        !idle->output.owner_id&&!idle->output.job_id&&clock.peer.operations==read_only);
    clock.peer.advance(clock.peer.now+5001);wtp_runtime_poll_idle();
    const auto idle_refresh=wsprrypi::wtp_runtime_managed_output();
    CHECK(idle_refresh&&idle_refresh->output==idle->output);
    char pattern[]="/tmp/wtp-managed-native-XXXXXX";const auto temporary=mkdtemp(pattern);CHECK(temporary);
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{temporary};
    wsprrypi::testing::set_wtp_pi_private_state_path(std::string(temporary)+"/revocation-v1");
    init_default_config();config.use_ini=true;config.ini_filename=std::string(temporary)+"/settings.ini";
    config.transmit_backend=TransmitBackendKind::WTP;config.wtp=settings;config.wtp_server.enabled=false;config.transmit=false;
    std::string error;CHECK(wsprrypi::start_wtp_pi_endpoint(config,&error));
    struct StopNative {~StopNative(){wsprrypi::stop_wtp_pi_endpoint();}} stop_native;
    auto native=wsprrypi::wtp_pi_management_observation();
    CHECK(native&&native->destination_id==backend_test::device&&native->output==idle->output);
    wtp_pi_end_local(false);
    CHECK(!wsprrypi::wtp_pi_external_output_admitted()&&refuses([&]{wtp_runtime_prepare(request);}));
    CHECK(wtp_runtime_recover().ok&&wsprrypi::wtp_pi_external_output_admitted());
    CHECK(wtp_pi_begin_test_tone()); // Permission reservation only; no output.
    const auto host_pending=wtp_runtime_recover();
    CHECK(!host_pending.ok&&host_pending.error.find("could not confirm")!=std::string::npos);
    wtp_pi_end_local(true);
    CHECK(wtp_runtime_recover().ok);
    const auto reconciled=wsprrypi::wtp_runtime_managed_output();CHECK(reconciled);
    wtp_runtime_prepare(request);
    CHECK(wsprrypi::wtp_pi_reconcile_external_output(*reconciled)==wsprrypi::WtpExternalReconcileResult::Held);
    auto pending=wsprrypi::wtp_runtime_managed_output();
    CHECK(pending&&pending->output.generation>idle->output.generation&&pending->output.job_id&&
        pending->output.owner_id==backend_test::owner_id&&pending->reserve_configuration);
    CHECK(pending->reserve_configuration());
    const auto pending_operations=clock.peer.operations;
    CHECK(refuses([]{wtp_runtime_start();})&&clock.peer.operations==pending_operations);
    pending->release_configuration();
    CHECK(!idle->reserve_stop(idle->output));
    CHECK(pending->reserve_stop(pending->output));
    CHECK(refuses([]{wtp_runtime_start();})&&clock.peer.operations==pending_operations);
    CHECK(wtp_runtime_stop().ok);
    pending->release_stop();
    auto ended=wsprrypi::wtp_runtime_managed_output();
    CHECK(ended&&ended->output.generation>pending->output.generation&&!ended->output.job_id);
    CHECK(clock.peer.executions==0&&std::count(clock.peer.operations.begin(),clock.peer.operations.end(),wsprrypi::wtp::Operation::Arm)==0);
    // Pause an existing software job after LOAD. A host settings reservation
    // must fence new jobs without revoking this finite job's existing ARM.
    std::promise<void> loaded,continue_job;auto loaded_signal=loaded.get_future();auto continue_signal=continue_job.get_future();
    bool signalled=false;
    clock.peer.filter=[&](auto operation,std::string &){if(operation==wsprrypi::wtp::Operation::Load&&!signalled){signalled=true;loaded.set_value();continue_signal.wait();}};
    wtp_runtime_prepare(request);wtp_runtime_start();
    if(loaded_signal.wait_for(3s)!=std::future_status::ready){continue_job.set_value();(void)wtp_runtime_stop();throw std::runtime_error("Existing software job did not reach LOAD");}
    auto editing=wsprrypi::wtp_runtime_managed_output();
    const bool reserved=editing&&editing->reserve_configuration&&editing->reserve_configuration();
    continue_job.set_value();
    std::optional<wsprrypi::WtpScheduleReport> completion;
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(!completion&&std::chrono::steady_clock::now()<deadline){completion=wtp_runtime_completion();if(!completion)std::this_thread::sleep_for(1ms);}
    if(reserved)editing->release_configuration();
    CHECK(reserved&&completion&&completion->outcome==wsprrypi::WtpScheduleOutcome::Complete&&clock.peer.executions==1);
    clock.peer.filter={};
    const auto before_whole_job=wsprrypi::wtp_runtime_managed_output();CHECK(before_whole_job);
    wtp_runtime_prepare(request);wtp_runtime_start();completion.reset();
    const auto whole_deadline=std::chrono::steady_clock::now()+3s;
    while(!completion&&std::chrono::steady_clock::now()<whole_deadline){completion=wtp_runtime_completion();if(!completion)std::this_thread::sleep_for(1ms);}
    CHECK(completion&&completion->outcome==wsprrypi::WtpScheduleOutcome::Complete&&clock.peer.executions==2);
    const auto after_whole_job=wsprrypi::wtp_runtime_managed_output();
    CHECK(after_whole_job&&after_whole_job->output.owner_id==before_whole_job->output.owner_id&&
        after_whole_job->output.job_id==before_whole_job->output.job_id&&
        after_whole_job->output.generation>before_whole_job->output.generation&&
        !before_whole_job->reserve_stop(before_whole_job->output));
    set_wtp_pi_management_readiness([]{return false;});
    CHECK(refuses([&]{wtp_runtime_prepare(request);}));
    set_wtp_pi_management_readiness({});
    // A different controller's lease is observed, never cancelled or borrowed.
    clock.peer.owner=std::string(32,'9');clock.peer.expiry=std::numeric_limits<std::uint64_t>::max();
    clock.peer.job.reset();clock.peer.state=wsprrypi::wtp::State::Empty;clock.peer.active=false;
    clock.peer.advance(clock.peer.now+5001);wtp_runtime_poll_idle();
    const auto foreign_operations=clock.peer.operations;
    auto foreign=wsprrypi::wtp_runtime_managed_output();
    CHECK(foreign&&foreign->output.owner_id==std::string(32,'9')&&!foreign->cancellable&&!foreign->reserve_stop&&
        !ended->reserve_stop(ended->output)&&clock.peer.operations==foreign_operations);
    const auto foreign_native=wsprrypi::wtp_pi_management_observation();
    CHECK(foreign_native&&!foreign_native->reserve_stop&&foreign_native->destination_id==backend_test::device);
    clock.peer.owner.reset();clock.peer.advance(clock.peer.now+5001);wtp_runtime_poll_idle();
    select_wtp_runtime(std::nullopt);
    auto absent=wsprrypi::wtp_runtime_managed_output();
    CHECK(absent&&!absent->destination_id&&absent->output.generation>ended->output.generation);
    std::cout<<"PASS: typed outgoing WTP observations without transport writes, disconnected configuration, exact reservation/ABA refusal, cancellation fencing, preserved accepted software job during an edit, and managed-state dispatch readiness\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
