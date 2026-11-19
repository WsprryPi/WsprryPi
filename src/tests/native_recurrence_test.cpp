// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "native_recurrence.hpp"
#include "recurrence_admission.hpp"
#include "runtime_config_bridge.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_pi_control.hpp"
#include "wspr_transmit.hpp"
#include "gpio_output.hpp"
#include <cassert>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <future>
#include <unistd.h>
using namespace wsprrypi;
namespace {
std::uint64_t utc(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
struct Fixture {
    std::string root,self=std::string(32,'a');NativeSolarSnapshot snapshot;
    std::uint64_t clock_generation=1;
    Fixture(){
        char pattern[]="/tmp/pi-native-recurrence-XXXXXX";auto directory=mkdtemp(pattern);assert(directory);root=directory;
        init_default_config();clear_current_wspr_runtime_state_for_test();reset_current_transmission_request_for_test();reset_current_controller_request_for_test();reset_managed_reload_runtime_for_test();
        GPIOOutput::setTestMode(true);
        clear_wtp_pi_control_hooks();set_wtp_pi_management_readiness({});reset_native_recurrence_observation();
        ini_reload_pending=false;ppm_reload_pending=false;exiting_wspr=false;web_test_tone=false;
        config.use_ini=false;config.transmit=true;config.mode=ModeType::WSPR;config.use_offset=false;
        config.use_system_clock_frequency_estimate=config.gpio_use_system_clock_frequency_estimate=false;
        config.transmit_backend=TransmitBackendKind::SIMULATED;config.simulated_backend_override=true;
        config.use_amp=config.use_led=config.use_shutdown=false;
        config.callsign=config.wspr.callsign="AA0NT/12";config.grid_square=config.wspr.grid_square="EM18IG";config.power_dbm=config.wspr.power_dbm=20;config.frequencies=config.wspr.frequencies="20m 30m";
        config.wspr_planner_preference=config.wspr.planner_preference=WsprPlannerPreference::RequirePaired;
        resolve_backend_specific_config(config);assert(set_frequencies(config));
        wsprTransmitter.selectBackend(BackendKind::SIMULATED);config_to_json();
        set_band_gpio_selector_for_test(false,false);set_scheduler_execution_suppressed_for_test(true);
        snapshot.native=true;snapshot.device_id=self;snapshot.generation=snapshot.authority_generation=1;
        snapshot.revision=std::string(64,'b');snapshot.intent_revision=std::string(64,'c');
        snapshot.progress=std::make_shared<RecurrenceProgress>(root+"/transaction",root+"/progress.json",self);
        RecurrenceListDigests empty;empty.fill(std::string(64,'d'));snapshot.progress->recover(true,empty);
        set_native_solar_snapshot_provider([this]{auto value=snapshot;value.ordinary=config;return value;});
        set_legacy_recurrence_admission([]{return LegacyRecurrenceAdmission::Native;});
        testing::set_native_clock_reader([this](bool){return SolarClockObservation{SolarClockState::Synchronized,utc(),clock_generation,1000,0,true};});
    }
    ~Fixture(){
        transmitter_set_callbacks({});transmitter_stop_and_join();finish_native_occurrence(true);reset_native_recurrence_observation();
        set_native_solar_snapshot_provider({});set_legacy_recurrence_admission({});testing::set_native_clock_reader({});
        testing::set_native_wspr_start({});
        set_scheduler_execution_suppressed_for_test(false);reset_active_wspr_plan_state();std::filesystem::remove_all(root);
        GPIOOutput::setTestMode(false);
    }
};
void complete(){transmitter_cb(WsprTransmissionCallbackEvent::COMPLETE,WsprTransmitLogLevel::INFO,"native fixture",0);}
void pair_and_disable(){
    Fixture f;assert(set_config(true));auto plan=active_native_occurrence();assert(plan&&plan->accepted&&active_wspr_plan_in_progress);
    const auto saved_request=current_transmission_request;const auto count=f.snapshot.progress->publication_count();assert(count==2); // initialization + reservation
    const auto reserved=f.snapshot.progress->snapshot();assert(reserved["cursors"]["ordinary"]["index"]==0);
    assert(reserved["in_flight"]["last_start_ns"]==std::to_string(plan->starts.back())&&plan->starts.size()==2);
    f.snapshot.policy.schedule_enabled=false;++f.snapshot.generation;f.snapshot.revision=std::string(64,'e');
    complete();assert(active_native_occurrence()==plan&&active_wspr_frame_index==1&&f.snapshot.progress->publication_count()==count);
    assert(current_transmission_request.actual_rf_frequency_hz==saved_request.actual_rf_frequency_hz);
    complete();assert(!native_occurrence_active()&&f.snapshot.progress->publication_count()==count+1);
    assert(native_recurrence_observation(f.self)["next_start_utc_ns"].is_null());
    assert(native_recurrence_observation(f.self)["forecast"]=="unknown");
    assert(f.snapshot.progress->snapshot()["cursors"]["ordinary"]["index"]==1);
    const auto bytes=f.snapshot.progress->snapshot();
    for(unsigned i=0;i<50;++i)assert(set_config(false));
    assert(f.snapshot.progress->snapshot()==bytes&&f.snapshot.progress->publication_count()==count+1);
}
void holds_and_generations(){
    Fixture f;f.snapshot.policy.solar_enabled=true;f.snapshot.policy.locator="JJ00";
    f.snapshot.policy.day.allow=f.snapshot.policy.night.allow=false;
    const auto initial=f.snapshot.progress->publication_count();
    assert(set_config(true)&&!native_occurrence_active()&&current_transmission_request.payload.empty());
    assert(f.snapshot.progress->publication_count()==initial);
    f.snapshot.policy.day.allow=f.snapshot.policy.night.allow=true;
    const std::array<std::uint64_t,1> starts{utc()+5'000'000'000ULL};
    auto plan=prepare_native_occurrence(config,starts);assert(plan->evaluation.outcome==SolarOutcome::Allowed);
    ++f.clock_generation;assert(!reserve_native_occurrence(plan,nlohmann::json{{"intent","clock-race"}}));
    assert(native_recurrence_observation(f.self)["forecast"]=="unknown");
    assert(f.snapshot.progress->publication_count()==initial);
    plan=prepare_native_occurrence(config,starts);assert(reserve_native_occurrence(plan,nlohmann::json{{"intent","disable-race"}}));
    f.snapshot.policy.schedule_enabled=false;bool dispatched=false;
    assert(!accept_native_occurrence(plan,[&]{dispatched=true;}));assert(!dispatched&&plan->resolved);
    assert(f.snapshot.progress->snapshot()["cursors"]["ordinary"]["index"]==0);
    assert(f.snapshot.progress->publication_count()==initial+2);
    f.snapshot.policy.schedule_enabled=true;
    auto replay=prepare_native_occurrence(config,starts);assert(replay->evaluation.outcome==SolarOutcome::Held);
}
void refused_committed_launch_and_file_fence(){
    Fixture f;
    config.use_ini=true;config.ini_filename=f.root+"/native.ini";
    {std::ofstream output(config.ini_filename);output<<"protected fixture\n";}
    std::filesystem::permissions(config.ini_filename,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);
    const std::array<std::uint64_t,1> starts{utc()+5'000'000'000ULL};
    auto plan=prepare_native_occurrence(config,starts);
    assert(reserve_native_occurrence(plan,nlohmann::json{{"file","unchanged"}}));
    assert(accept_native_occurrence(plan,[]{}));
    ++ini_reload_generation;assert(native_launch_current(plan));
    const auto replacement=config.ini_filename+".replacement";
    std::filesystem::copy_file(config.ini_filename,replacement);
    std::filesystem::rename(replacement,config.ini_filename);
    assert(!native_launch_current(plan));
    refuse_native_occurrence(plan);
    assert(plan->resolved&&!native_occurrence_active());
    assert(f.snapshot.progress->snapshot()["cursors"]["ordinary"]["index"]==0);
    assert(f.snapshot.progress->snapshot()["in_flight"].is_null());
    const std::array<std::uint64_t,1> later{starts.front()+120'000'000'000ULL};
    auto next=prepare_native_occurrence(config,later);
    assert(reserve_native_occurrence(next,nlohmann::json{{"retry","new slot"}}));
    assert(accept_native_occurrence(next,[]{}));finish_native_occurrence(true);
    assert(!native_occurrence_active());
}
void disabled_poll_has_no_preparation(){
    Fixture f;unsigned preparations=0;
    WtpPiControlHooks hooks;hooks.begin_configuration=[&]{++preparations;return false;};
    set_wtp_pi_control_hooks(std::move(hooks));
    const auto writes=f.snapshot.progress->publication_count();
    for(const auto scheduling:{false,true}){
        f.snapshot.policy.schedule_enabled=scheduling;config.transmit=!scheduling;
        reset_native_recurrence_observation();poll_native_recurrence();
        assert(preparations==0&&f.snapshot.progress->publication_count()==writes);
    }
    clear_wtp_pi_control_hooks();
}
void state_matrix(){
    Fixture f;f.snapshot.policy.locator="EM18";std::uint64_t sequence=0;
    for(auto boot:{EnableOnBootBehavior::Never,EnableOnBootBehavior::Follow,EnableOnBootBehavior::Always})for(unsigned bits=0;bits<128;++bits){
        const bool scheduling=bits&1,solar=bits&2,transmit=bits&4,healthy=bits&8,owned=bits&16,ready=bits&32,authority=bits&64;
        config.enable_on_boot=boot;config.transmit=transmit;f.snapshot.policy.schedule_enabled=scheduling;f.snapshot.policy.solar_enabled=solar;
        WtpPiControlHooks hooks;hooks.local_effective=[owned]{return !owned;};set_wtp_pi_control_hooks(std::move(hooks));
        set_wtp_pi_management_readiness([ready]{return ready;});
        set_legacy_recurrence_admission([authority]{return authority?LegacyRecurrenceAdmission::Native:LegacyRecurrenceAdmission::Held;});
        testing::set_native_clock_reader([healthy](bool){return SolarClockObservation{healthy?SolarClockState::Synchronized:SolarClockState::Unavailable,utc(),1,1000,0,healthy};});
        const std::array<std::uint64_t,1> starts{utc()+10'000'000'000ULL+(++sequence)*1'000'000'000ULL};
        const auto plan=prepare_native_occurrence(config,starts);const auto writes=f.snapshot.progress->publication_count();
        const bool admitted=reserve_native_occurrence(plan,nlohmann::json{{"matrix",bits}});
        const bool expected=scheduling&&transmit&&ready&&!owned&&authority&&(!solar||healthy);
        assert(admitted==expected);
        if(admitted){assert(accept_native_occurrence(plan,[]{}));finish_native_occurrence(true);assert(f.snapshot.progress->publication_count()==writes+2);}
        else assert(f.snapshot.progress->publication_count()==writes);
    }
    clear_wtp_pi_control_hooks();set_wtp_pi_management_readiness({});set_legacy_recurrence_admission([]{return LegacyRecurrenceAdmission::Native;});
}
bool real_message(bool virtual_time,bool reject,bool force_late=false){
    Fixture f;config.mode=ModeType::QRSS;config.qrss.message="E";config.qrss.frequency_hz=14097000;config.modulation_dot_seconds=config.qrss.dot_seconds=.02;resolve_backend_specific_config(config);
    WsprTransmitter::SimulatedRuntimeConfig sim;sim.virtual_time=virtual_time;sim.trace_path=f.root+"/trace.json";
    wsprTransmitter.selectBackend(BackendKind::SIMULATED,WsprTransmitter::Si5351RuntimeConfig{},sim);
    set_scheduler_execution_suppressed_for_test(false);
    std::mutex mutex;std::condition_variable cv;bool done=false,started=false,terminal_current=false;std::string terminal;
    std::chrono::system_clock::time_point terminal_utc;
    std::shared_ptr<NativeOccurrence> plan;
    if(force_late){
        f.snapshot.policy.solar_enabled=true;f.snapshot.policy.locator="EM18";
        f.snapshot.policy.day.allow=f.snapshot.policy.night.allow=true;
        testing::set_native_clock_reader([&](bool fresh){
            // An actual slow admission observation must not move the production
            // launch window. The transmitter rechecks lateness after the guard.
            if(fresh&&plan&&plan->accepted)std::this_thread::sleep_for(std::chrono::milliseconds(80));
            return SolarClockObservation{SolarClockState::Synchronized,utc(),f.clock_generation,1000,0,true};
        });
    }
    transmitter_set_callbacks([&](auto event,auto level,const auto &message,double value){
        if(event==WsprTransmissionCallbackEvent::STARTING)started=true;
        const bool ended=event==WsprTransmissionCallbackEvent::COMPLETE||event==WsprTransmissionCallbackEvent::CANCELLED||event==WsprTransmissionCallbackEvent::FAILED;
        if(ended){terminal_utc=std::chrono::system_clock::now();terminal_current=native_launch_current(plan);}
        transmitter_cb(event,level,message,value);
        if(ended){std::lock_guard lock(mutex);terminal=message;done=true;cv.notify_all();}
    });
    // Include preparation and durable reservation in the scheduled delay;
    // loaded runners can spend much of the lead time in those operations.
    const auto before=std::chrono::steady_clock::now();
    const auto start=utc()+2'000'000'000ULL;const std::array<std::uint64_t,1> starts{start};
    plan=prepare_native_occurrence(config,starts);assert(plan->evaluation.outcome==SolarOutcome::Allowed);
    const auto &tones=plan->evaluation.intent->nominal_tones_nhz;
    assert(reserve_native_occurrence(plan,nlohmann::json{{"message","E"}},tones));
    const auto when=std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(start)));
    assert(start_non_wspr_transmission_now(config,when,plan)&&plan->accepted);
    {std::lock_guard lock(config_update_mutex());
        f.snapshot.policy.schedule_enabled=false;
        if(reject)f.snapshot.intent_revision=std::string(64,'f');
    }
    {std::unique_lock lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(4),[&]{return done;}));}
    // Reap the worker before callback captures or fixture state can be destroyed.
    transmitter_stop_and_join();transmitter_set_callbacks({});
    const bool late=!reject&&!started&&terminal_current&&terminal=="Prepared occurrence is no longer current."&&
        terminal_utc>=when&&!ScheduledLaunch{when,{}}.current(terminal_utc);
    if((started==reject&&!late)||native_occurrence_active())
        std::cerr<<"Native message result: virtual="<<virtual_time<<" reject="<<reject
                 <<" started="<<started<<" active="<<native_occurrence_active()
                 <<" terminal="<<terminal<<" observation="<<native_recurrence_observation(f.self).dump()<<'\n';
    assert((started!=reject||late)&&!native_occurrence_active());
    if(force_late)assert(late&&!started);
    const auto elapsed=std::chrono::steady_clock::now()-before;
    assert(elapsed>=std::chrono::milliseconds(200)&&elapsed<std::chrono::seconds(4));
    assert(terminal_utc>=when);
    if(!reject){
        std::ifstream input(sim.trace_path);nlohmann::json trace;input>>trace;assert(!trace.is_null());
        if(late){
            assert(std::all_of(trace["events"].begin(),trace["events"].end(),[](const auto &event){return !event.value("rf_on",false);}));
            std::cout<<"PASS: host missed the unchanged launch window; refusal kept output off and reconciled the occurrence\n";
        }
    }
    assert(f.snapshot.progress->publication_count()==3);
    return started;
}
void real_message_launch(bool virtual_time){
    // Each late refusal must independently prove safe cleanup. Never retry a
    // committed occurrence: each attempt owns a fresh private fixture/slot.
    // A successful actual launch is still mandatory; unexplained refusals fail.
    for(unsigned attempt=0;attempt<8;++attempt)if(real_message(virtual_time,false))return;
    assert(false&&"No actual native message launch completed within eight fresh occurrences");
}
void scheduled_override(ModeType mode){
    Fixture f;config.mode=mode;config.modulation_dot_seconds=.02;config.modulation_fsk_offset_hz=2;
    config.qrss={"E",14097000,.02};config.fskcw={"E",14097002,14097000,.02};config.dfcw={"E",14097000,14097002,.02};
    // Keep minute/hour rollover in the same future timestamp and leave room
    // for the production planner to prepare before admission closes.
    const auto start=std::time(nullptr)+3;std::tm local{};localtime_r(&start,&local);
    config.schedule_start_minute=local.tm_min;config.schedule_start_second=local.tm_sec;config.schedule_repeat_minutes=1;
    f.snapshot.policy.solar_enabled=true;f.snapshot.policy.locator="EM18";
    f.snapshot.policy.day.cw_base_nhz=f.snapshot.policy.night.cw_base_nhz=14'098'000'000'000'000ULL;
    WsprTransmitter::SimulatedRuntimeConfig sim;sim.trace_path=f.root+"/override.json";
    wsprTransmitter.selectBackend(BackendKind::SIMULATED,WsprTransmitter::Si5351RuntimeConfig{},sim);
    set_scheduler_execution_suppressed_for_test(false);
    std::mutex mutex;std::condition_variable cv;bool done=false;
    transmitter_set_callbacks([&](auto event,auto level,const auto &message,double value){
        transmitter_cb(event,level,message,value);
        if(event==WsprTransmissionCallbackEvent::COMPLETE||event==WsprTransmissionCallbackEvent::FAILED||event==WsprTransmissionCallbackEvent::CANCELLED){std::lock_guard lock(mutex);done=true;cv.notify_all();}
    });
    assert(set_config(true));const auto plan=active_native_occurrence();assert(plan&&plan->accepted);
    assert(current_transmission_request.actual_rf_frequency_hz==(mode==ModeType::FSKCW?14098002:14098000));
    {std::lock_guard lock(config_update_mutex());f.snapshot.policy.schedule_enabled=false;}
    {std::unique_lock lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(6),[&]{return done;}));}
    transmitter_stop_and_join();transmitter_set_callbacks({});
    assert(!native_occurrence_active()&&f.snapshot.progress->publication_count()==3);
    std::ifstream input(sim.trace_path);nlohmann::json trace;input>>trace;
    assert(trace["mode"]==(mode==ModeType::QRSS?"QRSS":mode==ModeType::FSKCW?"FSKCW":"DFCW"));
}
bool real_wspr(bool reject){
    Fixture f;config.callsign=config.wspr.callsign="AA0NT";config.grid_square=config.wspr.grid_square="EM18";
    config.wspr_planner_preference=config.wspr.planner_preference=WsprPlannerPreference::Auto;
    WsprTransmitter::SimulatedRuntimeConfig sim;sim.trace_path=f.root+"/wspr.json";
    wsprTransmitter.selectBackend(BackendKind::SIMULATED,WsprTransmitter::Si5351RuntimeConfig{},sim);set_scheduler_execution_suppressed_for_test(false);
    std::mutex mutex;std::condition_variable cv;bool done=false,started=false,terminal_current=false;
    std::chrono::system_clock::time_point terminal_utc;std::string terminal;
    std::shared_ptr<NativeOccurrence> plan;
    transmitter_set_callbacks([&](auto event,auto level,const auto &message,double value){
        if(event==WsprTransmissionCallbackEvent::STARTING)started=true;
        const bool ended=event==WsprTransmissionCallbackEvent::COMPLETE||event==WsprTransmissionCallbackEvent::CANCELLED||event==WsprTransmissionCallbackEvent::FAILED;
        if(ended){terminal_utc=std::chrono::system_clock::now();terminal_current=native_launch_current(plan);}
        transmitter_cb(event,level,message,value);
        if(ended){std::lock_guard lock(mutex);terminal=message;done=true;cv.notify_all();}
    });
    // The production WSPR scheduler stages its worker two seconds before
    // launch. Leave that lead plus preparation time instead of starting the
    // fixture inside the worker's staging interval. The 50 ms deadline stays.
    const auto start=utc()+3'000'000'000ULL;const std::array<std::uint64_t,1> starts{start};plan=prepare_native_occurrence(config,starts);
    PreparedWsprTransmission paired;std::size_t index=0;double dial=0;WsprFrequencyEntry entry;bool in_progress=false;std::optional<WsprPlanPlacement> placement;::TransmissionRequest request;
    const auto selected=*plan->evaluation.intent->wspr_entry;
    assert(configure_current_wspr_transmission(config,0,selected.dial_frequency_hz,selected,paired,index,dial,entry,in_progress,placement,request));
    assert(reserve_native_occurrence(plan,nlohmann::json{{"center",request.actual_rf_frequency_hz}}));
    const auto when=std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(start)));
    assert(accept_native_occurrence(plan,[&]{commit_execution_request(request,when,[plan]{return native_launch_current(plan);},native_execution_callback(plan));}));
    {std::lock_guard lock(config_update_mutex());f.snapshot.policy.schedule_enabled=false;if(reject)f.snapshot.intent_revision=std::string(64,'f');}
    transmitter_start_async();
    {std::unique_lock lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(4),[&]{return done;}));}
    transmitter_stop_and_join();transmitter_set_callbacks({});
    const bool late=!reject&&!started&&terminal_current&&terminal=="Prepared occurrence is no longer current."&&
        terminal_utc>=when&&!ScheduledLaunch{when,{}}.current(terminal_utc);
    assert((started!=reject||late)&&!native_occurrence_active()&&f.snapshot.progress->publication_count()==3);
    if(!reject){
        std::ifstream input(sim.trace_path);nlohmann::json trace;input>>trace;assert(trace["mode"]=="WSPR");
        if(late){
            assert(std::all_of(trace["events"].begin(),trace["events"].end(),[](const auto &event){return !event.value("rf_on",false);}));
            std::cout<<"PASS: late WSPR launch refused with output off and occurrence reconciled; lateness_ns="
                     <<std::chrono::duration_cast<std::chrono::nanoseconds>(terminal_utc-when).count()<<'\n';
        }else{unsigned symbols=0;for(const auto &event:trace["events"])if(event["kind"]=="event")++symbols;assert(symbols==162);}
    }
    return started;
}
void real_wspr_launch(){
    for(unsigned attempt=0;attempt<8;++attempt)if(real_wspr(false))return;
    assert(false&&"No actual native WSPR launch completed within eight fresh occurrences");
}
void rotations_skip_and_fault(){
    Fixture f;f.snapshot.policy.solar_enabled=true;f.snapshot.policy.locator="EM18";
    SolarWsprList day;day.entries={{"40m",7038600,-1,false,true},{"20m",14095600,-1,false,true}};day.frequencies="40m 20m";
    f.snapshot.policy.day.wspr_override=day;
    const auto site=*solar_locator_center("EM18");
    const auto next=[&](std::uint64_t after,SolarPeriod wanted)->std::uint64_t{for(unsigned i=1;i<100;++i){auto start=after+i*1800'000'000'000ULL;const auto position=solar_position(site,start);if(position&&solar_period(*position,{},{})->period==wanted)return start;}assert(false);return after;};
    auto previous=utc();
    for(const auto wanted:{SolarPeriod::Day,SolarPeriod::Night,SolarPeriod::Day}){
        const auto start=next(previous,wanted);previous=start;const std::array<std::uint64_t,1> starts{start};const auto plan=prepare_native_occurrence(config,starts);
        assert(plan->evaluation.selected_period==wanted&&reserve_native_occurrence(plan,nlohmann::json{{"period",int(wanted)}}));
        assert(accept_native_occurrence(plan,[]{}));finish_native_occurrence(true);
    }
    auto progress=f.snapshot.progress->snapshot();assert(progress["cursors"]["day"]["index"]==0&&progress["cursors"]["ordinary"]["index"]==1);
    // Terminal persistence uncertainty keeps the committed in-flight image and
    // prevents memory-only advancement or another admission.
    const std::array<std::uint64_t,1> starts{previous+3600'000'000'000ULL};auto plan=prepare_native_occurrence(config,starts);
    assert(reserve_native_occurrence(plan,nlohmann::json{{"fault",true}})&&accept_native_occurrence(plan,[]{}));
    const auto count=f.snapshot.progress->publication_count();assert(!unlink((f.root+"/progress.json").c_str()));
    finish_native_occurrence(true);assert(f.snapshot.progress->held()&&!plan->resolved&&f.snapshot.progress->publication_count()==count);
    assert(native_recurrence_observation(f.self)["waiting_reason"]=="storage");
}
void explicit_skip(){
    Fixture f;config.wspr_frequency_entries={{"0",0,-1,false,true},{"20m",14095600,-1,false,true}};config.frequencies="0 20m";
    const auto start=utc()+100'000'000ULL;const std::array<std::uint64_t,2> starts{start,start+120'000'000'000ULL};
    const auto plan=prepare_native_occurrence(config,starts);assert(plan->evaluation.outcome==SolarOutcome::Skipped&&plan->starts.size()==1);
    const auto count=f.snapshot.progress->publication_count();assert(!reserve_native_occurrence(plan,nlohmann::json{{"skip",true}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    assert(reserve_native_occurrence(plan,nlohmann::json{{"skip",true}})&&plan->resolved&&!native_occurrence_active());
    const auto saved=f.snapshot.progress->snapshot();assert(saved["in_flight"].is_null()&&saved["cursors"]["ordinary"]["index"]==1&&saved["consumed_through_utc_ns"]==std::to_string(start));
    assert(f.snapshot.progress->publication_count()==count+1);
}
void queued_callback_binding(){
    Fixture f;std::promise<void> entered,release;auto unblock=release.get_future().share();std::atomic_int old{0},replacement{0};
    wsprTransmitter.setTransmissionCallbacks([&](auto event,auto,const auto &,double){
        if(event==WsprTransmissionCallbackEvent::LOGGING){entered.set_value();assert(unblock.wait_for(std::chrono::seconds(3))==std::future_status::ready);}
        if(event==WsprTransmissionCallbackEvent::COMPLETE)++old;
    });
    wsprTransmitter.backendFireTransmitCallback(WsprTransmissionCallbackEvent::LOGGING,WsprTransmitLogLevel::INFO,"queued",0);
    assert(entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready);
    wsprTransmitter.backendFireTransmitCallback(WsprTransmissionCallbackEvent::COMPLETE,WsprTransmitLogLevel::INFO,"old request",0);
    wsprTransmitter.setTransmissionCallbacks([&](auto,auto,const auto &,double){++replacement;});release.set_value();
    for(unsigned i=0;i<300&&old.load()==0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(old==1&&replacement==0);
}
void complete_pair_refusal_before_effects(){
    Fixture f;f.snapshot.policy.solar_enabled=true;f.snapshot.policy.locator="JJ00";
    config.wspr_frequency_entries={{"20m",14095600,5,true,false}};
    const auto site=*solar_locator_center("JJ00");std::uint64_t boundary=0;
    const auto now=utc();
    for(unsigned i=1;i<1440;++i){
        const auto start=now+i*120'000'000'000ULL;
        const auto first=solar_period(*solar_position(site,start),{},{}),last=solar_period(*solar_position(site,start+120'000'000'000ULL),{},{});
        if(first->period!=last->period){boundary=start;break;}
    }
    assert(boundary);testing::set_native_wspr_start([boundary]{return boundary;});
    GPIOOutput::clearTestEvents();const auto count=f.snapshot.progress->publication_count();
    assert(set_config(true));assert(!native_occurrence_active()&&!active_wspr_plan_in_progress);
    assert(GPIOOutput::testEvents().empty()&&f.snapshot.progress->publication_count()==count);
    assert(native_recurrence_observation(f.self)["waiting_reason"]=="solar");
}
}
int main(){pair_and_disable();holds_and_generations();refused_committed_launch_and_file_fence();disabled_poll_has_no_preparation();state_matrix();real_message_launch(true);real_message_launch(false);assert(!real_message(true,true));assert(!real_message(true,false,true));for(auto mode:{ModeType::QRSS,ModeType::FSKCW,ModeType::DFCW})scheduled_override(mode);real_wspr_launch();assert(!real_wspr(true));rotations_skip_and_fault();explicit_skip();queued_callback_binding();complete_pair_refusal_before_effects();std::cout<<"PASS: native production planner, pair freeze/drain/refusal before effects, state matrix, reservation races, bounded writes, rotations/skips/faults, callback binding, and virtual/real-time simulated launch\n";}
