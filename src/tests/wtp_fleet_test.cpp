#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/fleet_limits.hpp"
#include "wtp_integration/fleet_schedule.hpp"
#include "wtp_integration/output_lease.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/fleet_recovery.hpp"
#include "wtp_integration/fleet_test.hpp"
#include "wtp_integration/fleet_observation_age.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "wtp_settings_json.hpp"
#include "WSPR-Transmitter/src/execution_plan_compiler.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>
#include <thread>
#include <condition_variable>
using namespace wsprrypi;
using Json=nlohmann::json;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class F> void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
Json assignment(char id) {
    WtpSettings settings;settings.transport="network_plain";settings.hostname="wspr4.local";
    settings.tcp_port=31417;settings.device_id=std::string(32,id);settings.allow_frequency_adjustment=true;
    return {{"device_id",settings.device_id},{"name",std::string("Output ")+id},{"settings",wtp_settings_json(settings)},
        {"product","WsprryPi"},{"revocation_generation","7"},{"management_port",31415},
        {"enabled",true},{"last_start_ns","0"},{"in_flight",false},
        {"schedule",{{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",60},{"phase_seconds",0}}}};
}
void restarted_output_admission() {
    const std::string device(32,'a'),old_boot(32,'1'),new_boot(32,'2');
    WtpRuntimeStatus previous,fresh;
    previous.session_phase=wtp::SessionPhase::IdentityChanged;
    previous.identity=wtp::HelloResponse{device,old_boot,"WsprryPi","test"};
    fresh.session_phase=wtp::SessionPhase::Ready;
    fresh.identity=wtp::HelloResponse{device,new_boot,"WsprryPi","test"};
    fresh.remote=wtp::Status{new_boot,wtp::State::Empty,false,{},{},{}};
    fresh.capabilities=wtp::Capabilities{};fresh.status_observed_ms=1;
    const auto allowed=[&](const WtpRuntimeStatus& p,const WtpRuntimeStatus& f){
        return wtp_fleet_restarted_output_safe(p,f,device,"WsprryPi");};
    check(allowed(previous,fresh),"fresh pinned restarted output may be reconciled");
    for(const auto state:{wtp::State::Complete,wtp::State::Aborted,wtp::State::Missed}) {
        auto candidate=fresh;candidate.remote->state=state;
        check(allowed(previous,candidate),"unowned inactive terminal state is safe");
    }
    const auto denied=[&](auto change){auto candidate=fresh;change(candidate);check(!allowed(previous,candidate),"unsafe restart cannot erase dispatch barrier");};
    denied([&](auto& s){s.identity->device_id=std::string(32,'b');});
    denied([&](auto& s){s.identity->product="WsprryPico";});
    denied([&](auto& s){s.identity->boot_id=old_boot;s.remote->boot_id=old_boot;});
    denied([&](auto& s){s.remote->boot_id=old_boot;});
    denied([](auto& s){s.remote->owner_id=std::string(32,'f');});
    denied([](auto& s){s.remote->output_active=true;});
    for(const auto state:{wtp::State::Loaded,wtp::State::Armed,wtp::State::Running,wtp::State::Failed})
        denied([&](auto& s){s.remote->state=state;});
    denied([](auto& s){s.uncertain=true;});denied([](auto& s){s.safety_fault=true;});
    denied([](auto& s){s.owns=true;});denied([](auto& s){s.remote.reset();});
    denied([](auto& s){s.identity.reset();});denied([](auto& s){s.capabilities.reset();});
    denied([](auto& s){s.status_observed_ms.reset();});
    denied([](auto& s){s.session_phase=wtp::SessionPhase::Disconnected;});
    denied([](auto& s){s.phase=WtpSchedulePhase::Blocked;});
    auto fault=previous;fault.session_phase=wtp::SessionPhase::Fault;
    check(!allowed(fault,fresh),"protocol faults remain latched");
    auto different=previous;different.identity->device_id=std::string(32,'b');
    check(!allowed(different,fresh),"retired identity must match the assignment");
}
void concurrent_pause_publication(const std::string& ini,const std::string& self) {
    auto a=assignment('a'),b=assignment('b');a["in_flight"]=true;b["in_flight"]=true;
    WtpFleetStore disk(ini+".wtp-assignments.json",self);
    disk.update({},[&](Json& data){data["assignments"]=Json::array({a,b});});
    std::mutex mutex;std::condition_variable changed;unsigned before=0,after=0;bool release=false;
    WtpFleetTestHooks hooks;
    hooks.create_target=[](auto,auto,auto)->std::unique_ptr<WtpTargetRuntime>{throw std::runtime_error("Crash barriers forbid carrier contact");};
    hooks.revocation_status=[](auto,auto)->Json{throw std::runtime_error("Crash barriers forbid host contact");};
    hooks.before_persist=[&]{std::lock_guard lock(mutex);++before;changed.notify_all();};
    hooks.after_persist=[&]{std::unique_lock lock(mutex);++after;changed.notify_all();if(after==1)changed.wait(lock,[&]{return release;});};
    std::atomic_bool shutdown_seen{false};
    hooks.before_shutdown_join=[&]{
        if(shutdown_seen.exchange(true))return;
        const auto read=Json::parse(wtp_fleet_api("GET","","").body);
        check(read.at("replacement_pending")==true,"shutdown fences HTTP admission before its first executor join");
        for(const auto& output:read.at("outputs"))check(output.at("can_resume")==false && output.at("can_pause")==false,"shutdown retires context action authority before waiting");
    };
    start_wtp_fleet_for_test(ini,self,hooks);
    struct Cleanup {std::mutex& mutex;std::condition_variable& changed;bool& release;~Cleanup(){ {std::lock_guard lock(mutex);release=true;changed.notify_all();}stop_wtp_fleet();}} cleanup{mutex,changed,release};
    {
        const auto read=wtp_fleet_api("GET","","");const auto data=Json::parse(read.body),candidate=assignment('c');
        Json command={{"operation","assign"},{"settings",candidate.at("settings")},{"name","Invalid port"},{"schedule",candidate.at("schedule")},
            {"enabled",false},{"consent_plain",true},{"management_port",4294967297ULL},{"action_id",wtp_random_identity()},
            {"controller_incarnation",data.at("controller_incarnation")},{"assignment_generation",nullptr},{"reviewed_control_revision",nullptr},
            {"reviewed_boot_id",nullptr},{"accept_before_ms",std::to_string(wtp_fleet_decimal(data.at("now_ms"))+30000)}};
        check(wtp_fleet_api("POST",command.dump(),read.etag).status==409,"oversized management port refuses before narrowed conversion or carrier admission");
    }
    const auto submit_pause=[&](char id) {
        const auto read=wtp_fleet_api("GET","","");const auto data=Json::parse(read.body);const auto device=std::string(32,id);
        const auto& output=data.at("outputs").at(device);const auto action=wtp_random_identity();
        const Json command={{"operation","pause"},{"device_id",device},{"action_id",action},{"controller_incarnation",data.at("controller_incarnation")},
            {"assignment_generation",output.at("assignment_generation")},{"reviewed_control_revision",output.at("control_revision")},
            {"reviewed_boot_id",output.at("boot_id")},{"accept_before_ms",std::to_string(wtp_fleet_decimal(data.at("now_ms"))+30000)}};
        check(wtp_fleet_api("POST",command.dump(),read.etag).status==202,"Pause admitted before unrelated publication");return action;
    };
    const auto first=submit_pause('a');
    {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return after==1;}),"first commit held before publication");}
    const auto second=submit_pause('b');
    {std::unique_lock lock(mutex);
     check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return before>=2;}),"second owner reaches persistence independently");
     changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return after>=2;});
     check(after==1,"later commit cannot overtake unpublished earlier commit");release=true;changed.notify_all();}
    Json result;
    for(unsigned attempt=0;attempt<5000;++attempt) {
        result=Json::parse(wtp_fleet_api("GET","","","actions",first+","+second).body).at("actions");
        if(!result.at(first).at("work_pending").get<bool>() && !result.at(second).at("work_pending").get<bool>())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for(const auto& id:{first,second})check(result.at(id).at("postconditions").value("schedule_paused",false),"accepted protective Pause continues despite another output's changed ETag");
    const auto cached=wtp_fleet_api("GET","","");WtpFleetStore restored(ini+".wtp-assignments.json",self);
    check(Json::parse(cached.body).at("assignments")==restored.snapshot().first.at("assignments") && cached.etag==restored.snapshot().second,"last published assignments and ETag equal latest durable state");
}
void unbound_takeover_observation(const std::string& ini,const std::string& self) {
    auto row=assignment('a');row["enabled"]=false;
    WtpFleetStore store(ini+".wtp-assignments.json",self);store.update({},[&](Json& d){d["assignments"].push_back(row);});
    struct ClosedStream:wtp::ByteStream {
        wtp::IoResult read(std::span<std::uint8_t>)override{return {wtp::IoState::Closed,0};}
        wtp::IoResult write(std::span<const std::uint8_t>)override{return {wtp::IoState::Closed,0};}
        void close()noexcept override{}
    } stream;
    struct Clock:WtpScheduleClock {
        std::uint64_t now_ms()const override{return WtpSteadyClock{}.now_ms();}
        std::optional<std::uint64_t> utc_now_ns()const override{return std::nullopt;}
        void wait_ms(std::uint64_t ms)override{std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
    } clock;
    std::atomic<unsigned> host_reads{0};WtpFleetTestHooks hooks;
    hooks.create_target=[&](WtpSettings settings,std::function<bool()> admission,std::shared_ptr<void>){
        return std::make_unique<WtpTargetRuntime>(settings,clock,stream,
            wtp::SessionOptions{wtp_random_identity(),wtp_random_identity(),settings.device_id},[]{return false;},std::move(admission));
    };
    hooks.revocation_status=[&](const WtpSettings&,unsigned)->Json{
        ++host_reads;return {{"schema","wsprrypi-wtp-endpoint/1"},{"device_id",std::string(32,'a')},{"boot_id",std::string(32,'2')},
            {"revocation_unavailable",false},{"local_requested",false},{"output_unknown",false},{"revocation_generation","8"}};
    };
    start_wtp_fleet_for_test(ini,self,hooks);struct Stop {~Stop(){stop_wtp_fleet();}} stop;
    Json data;
    for(unsigned attempt=0;attempt<5000;++attempt) {
        data=Json::parse(wtp_fleet_api("GET","","").body);
        if(host_reads && (data.at("assignments").empty() || data.at("outputs").at(std::string(32,'a')).value("message",std::string{}).find("Pi takeover boot differs")!=std::string::npos))break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(host_reads>0,"Pi takeover HTTP observation was attempted after failed WTP inspection");
    check(data.at("assignments").size()==1,"unbound Pi HTTP boot cannot remove an assignment");
    check(data.at("outputs").at(std::string(32,'a')).value("message",std::string{}).find("Pi takeover boot differs")!=std::string::npos,"unbound host result was consumed and explicitly refused");
    WtpFleetJournal journal(ini+".wtp-fleet-contexts.json",self);
    check(journal.snapshot().at("contexts").empty(),"unbound HTTP observation cannot create retirement authority");
}
void failed_manager_replacement(const std::string& ini,const std::string& self) {
    start_wtp_fleet(ini,self);struct Stop {~Stop(){stop_wtp_fleet();}} stop;
    const auto previous=Json::parse(wtp_fleet_api("GET","","").body).at("controller_incarnation");
    const auto broken=ini+".broken";
    {std::ofstream file(broken+".wtp-assignments.json");file<<"{broken";}
    check(chmod((broken+".wtp-assignments.json").c_str(),0600)==0,"private corrupt assignment fixture");
    start_wtp_fleet(broken,self);
    const auto failed=wtp_fleet_api("GET","","");
    check(failed.status==503,"failed replacement cannot masquerade as a healthy stopped manager");
    const auto error=Json::parse(failed.body).at("error");
    check(error.at("code")=="fleet_unavailable" && !error.at("message").get<std::string>().empty(),"failed replacement exposes its bounded startup cause");
    start_wtp_fleet(ini,self);
    const auto fresh=wtp_fleet_api("GET","","");
    check(fresh.status==200 && Json::parse(fresh.body).at("controller_incarnation")!=previous,"corrected configuration establishes a fresh working manager");
}
int main(){
    {
        std::uint64_t controller=100000,transport=1000;
        const auto origin=wtp_fleet_observation_origin([&]{return controller;},[&]{
            const auto sampled=transport;controller+=20000;transport+=20000;return sampled;
        },900);
        check(origin && controller-*origin>=20100,"preemption between clock samples cannot rejuvenate an observation");
        check(!wtp_fleet_observation_origin([]{return 1000ULL;},[]{return 500ULL;},600),"regressed transport clock has no valid age origin");
        check(!wtp_fleet_observation_origin([]{return 100ULL;},[]{return 1000ULL;},0),"publication-origin underflow is unknown");
        check(wtp_fleet_observation_origin([]{return 10ULL;},[]{return 20ULL;},10)==0,"zero is a valid mapped monotonic origin");
    }
    restarted_output_admission();
    char path[]="/tmp/wsprrypi-fleet-test.XXXXXX";check(mkdtemp(path)!=nullptr,"temporary directory");
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{path};
    const std::string file=std::string(path)+"/fleet.json";
    const std::string self(32,'f');
    concurrent_pause_publication(std::string(path)+"/publication.ini",self);
    unbound_takeover_observation(std::string(path)+"/unbound-host.ini",self);
    failed_manager_replacement(std::string(path)+"/replacement.ini",self);
    WtpFleetStore store(file,self);
    auto [empty,first]=store.snapshot();check(empty.at("assignments").empty(),"zero targets valid");
    check(kMaximumFleetOutputs==32,"supported capacity is independently specified as 32");
    {
        WtpFleetStore large(std::string(path)+"/large.json",self);
        Json expected;
        for(unsigned count=1;count<=32;++count) {
            auto row=assignment('a');
            const auto id=std::string(30,'0')+"0123456789abcdef"[count/16]+"0123456789abcdef"[count%16];
            row["device_id"]=id;row["settings"]["Device ID"]=id;
            row["name"]=std::string(80,'"');
            row["settings"]["Hostname"]=std::string(63,'a')+"."+std::string(63,'b')+"."+std::string(63,'c')+"."+std::string(61,'d');
            row["revocation_generation"]="18446744073709551615";
            for(const char* key:{"Endpoint","TLS CA File","TLS Client Certificate","TLS Client Key"})
                row["settings"][key]=std::string(512,'"');
            row["settings"]["USB Serial"]=std::string(128,'"');
            row["settings"]["TLS Server Identity"]=std::string(254,'"'); // inactive Plain LAN field
            row["schedule"]={{"mode","dfcw"},{"frequency_hz",14097100},{"message",std::string(32,'E')},{"dot_ms",60000},{"shift_hz",1000},{"period_seconds",86400},{"phase_seconds",86399}};
            row["last_start_ns"]="18446744073709551615";
            row["in_flight"]=true;row["enabled"]=(count%2==0);
            expected=large.update({},[&](Json& d){d["assignments"].push_back(row);}).first;
            WtpFleetStore reload(std::string(path)+"/large.json",self);
            check(reload.snapshot().first==expected,"all 1 through 32 assignments reload without normalization");
        }
        auto full=large.update({},[](Json& d){for(unsigned i=0;i<32;++i)
            d["removals"].push_back({{"device_id",std::string(32,'b')},{"revocation_generation","18446744073709551615"}});});
        check(std::filesystem::file_size(std::string(path)+"/large.json")>131072,"escaped maximum fields exceed former read ceiling");
        WtpFleetStore reload(std::string(path)+"/large.json",self);
        check(reload.snapshot().first==full.first,"full removal history and barriers reload");
        rejects([&]{large.update({},[&](Json& d){d["assignments"].push_back(assignment('c'));});},"33rd unique stored assignment refused");
        rejects([&]{large.update({},[](Json& d){d["assignments"][31]["device_id"]=d["assignments"][0]["device_id"];d["assignments"][31]["settings"]["Device ID"]=d["assignments"][0]["device_id"];});},"duplicate at capacity refused");
        rejects([&]{large.update({},[&](Json& d){d["assignments"][31]["device_id"]=self;d["assignments"][31]["settings"]["Device ID"]=self;});},"self at capacity refused");
        rejects([&]{large.update({},[](Json& d){d["assignments"][0]["settings"]["extension"]=std::string(kMaximumFleetFileBytes,'x');});},"oversize save refused before replacement");
        check(large.snapshot()==full,"all refused mutations preserve revision and memory");
        WtpFleetStore still_saved(std::string(path)+"/large.json",self);
        check(still_saved.snapshot()==full,"all refused mutations preserve disk");
        const auto full_ini=std::string(path)+"/full-runtime.ini";
        WtpFleetStore full_runtime(full_ini+".wtp-assignments.json",self);
        const auto preserved=full_runtime.update({},[&](Json& d){d=full.first;});
        start_wtp_fleet(full_ini,self);
        const auto running_full=wtp_fleet_api("GET","","");
        check(running_full.status==200 && Json::parse(running_full.body).at("outputs").size()==32,"32 crash-retained contexts start");
        stop_wtp_fleet();
        WtpFleetStore after_stop(full_ini+".wtp-assignments.json",self);
        check(after_stop.snapshot()==preserved,"32 crash-retained slots and barriers survive shutdown unchanged");
        const auto exact_file=std::string(path)+"/exact.json";
        WtpFleetStore exact(exact_file,self);
        auto boundary=full.first;
        boundary["assignments"][0]["settings"]["extension"]="";
        const auto overhead=boundary.dump(2).size()+1;
        boundary["assignments"][0]["settings"]["extension"]=std::string(kMaximumFleetFileBytes-overhead,'x');
        const auto exact_saved=exact.update({},[&](Json& d){d=boundary;});
        check(std::filesystem::file_size(exact_file)==kMaximumFleetFileBytes,"exact save limit accepted");
        WtpFleetStore exact_reload(exact_file,self);
        check(exact_reload.snapshot()==exact_saved,"exact byte limit reloads");
        rejects([&]{exact.update({},[](Json& d){d["assignments"][0]["settings"]["extension"].get_ref<std::string&>()+='x';});},"one byte over save limit refused");
        {std::ofstream append(exact_file,std::ios::app);append<<' ';}
        rejects([&]{WtpFleetStore oversized(exact_file,self);oversized.snapshot();},"one byte over read limit refused");
        std::cout<<"Maximum populated fixture bytes: "<<std::filesystem::file_size(std::string(path)+"/large.json")<<"\n";
    }
    auto a=assignment('a'),b=assignment('b');b["schedule"]["phase_seconds"]=30;
    auto [two,revision]=store.update(first,[&](Json& d){d["assignments"]=Json::array({a,b});});
    check(two.at("assignments").size()==2,"independent outputs saved");
    rejects([&]{store.update(first,[](Json& d){d["assignments"].clear();});},"stale revision rejected");
    rejects([&]{store.update(revision,[&](Json& d){d["assignments"].push_back(a);});},"duplicate identity rejected");
    rejects([&]{store.update(revision,[&](Json& d){d["assignments"].push_back(assignment('f'));});},"self target rejected");
    rejects([&]{store.update(revision,[&](Json& d){d["assignments"]=b;});},
            "a single assignment object is not an assignment array");
    check(store.snapshot().first==two && store.snapshot().second==revision,
          "rejected assignment shape preserves the document and revision");
    WtpFleetStore unchanged(file,self);
    check(unchanged.snapshot().first==two,"rejected assignment shape is never persisted");
    check(wtp_fleet_next_slot(a.at("schedule"),61000000000ULL,0)==120000000000ULL,"next future slot");
    check(wtp_fleet_next_slot(b.at("schedule"),61000000000ULL,0)==90000000000ULL,"independent phase");
    check(wtp_fleet_next_slot(a.at("schedule"),61000000000ULL,120000000000ULL)==180000000000ULL,"consumed slot never repeats after restart");
    auto bad=a.at("schedule");bad["duration_ms"]=60001;
    rejects([&]{validate_wtp_fleet_schedule(bad);},"job must fit repeat interval");
    bad=a.at("schedule");bad["duration_ms"]=-1;
    rejects([&]{validate_wtp_fleet_schedule(bad);},"negative duration rejected");
    bad=a.at("schedule");bad["unexpected"]=true;
    rejects([&]{validate_wtp_fleet_schedule(bad);},"unknown field rejected");
    const Json wspr={{"mode","wspr"},{"frequency_hz",14097100},{"callsign","K1ABC"},{"locator","FN42"},
        {"power_dbm",10},{"period_seconds",120},{"phase_seconds",0}};
    validate_wtp_fleet_schedule(wspr);
    const auto request = wtp_fleet_request(wspr);
    check(std::get<WsprPayload>(request.payload).base_frequency_hz == 14097100,
          "Fleet frequency remains the entered absolute RF center");
    const auto plan = ExecutionPlanCompiler{}.compile(request);
    const double expected[] = {14097097.802734375, 14097099.267578125, 14097100.732421875, 14097102.197265625};
    check(plan.events.size() == 162, "Fleet WSPR compiles a complete RF frame");
    bool seen[4] = {};
    for (const auto &event : plan.events) {
        bool matches = false;
        for (unsigned i = 0; i < 4; ++i) {
            const bool same = std::fabs(event.frequency_hz - expected[i]) < 1e-8;
            matches |= same;
            seen[i] |= same;
        }
        check(matches, "Fleet tones surround entered center without dial conversion");
    }

    check(seen[0] && seen[1] && seen[2] && seen[3], "Fleet emits all four expected tone targets");
    check(wtp_fleet_next_slot(wspr,10000000000ULL,0)==121000000000ULL,"WSPR uses second one of two-minute slot");
    for(const auto* mode:{"qrss","fskcw","dfcw"}){
        Json morse={{"mode",mode},{"frequency_hz",14097000},{"message","E"},{"dot_ms",3000},{"period_seconds",60},{"phase_seconds",0}};
        if(std::string(mode)!="qrss")morse["shift_hz"]=4;
        validate_wtp_fleet_schedule(morse);
    }
    // Frozen cleanup authority is a private, independently versioned store.
    {
        const auto journal_path=std::string(path)+"/contexts.json";
        WtpFleetJournal journal(journal_path,self);
        check(journal.snapshot().at("contexts").empty(),"missing journal starts empty without guessing targets");
        const auto row=assignment('a');
        Json entry={{"retirement_id",std::string(32,'1')},{"kind","operator_remove"},{"assignment",row},
            {"source_etag",first},{"last_boot",std::string(32,'2')},{"last_job",std::string(32,'3')},
            {"disposition","pending"},{"unresolved",true}};
        journal.put(entry);
        WtpFleetJournal restored(journal_path,self);
        check(restored.snapshot().at("contexts").at(0)==entry,"frozen cleanup target and job survive reload");
        auto wrong=entry;wrong["extra"]=true;
        rejects([&]{journal.put(wrong);},"closed journal entry rejects extra fields before write");
        wrong=entry;wrong["last_boot"]="wrong";
        rejects([&]{journal.put(wrong);},"journal rejects malformed boot binding");
        wrong=entry;wrong["assignment"]=assignment('b');
        rejects([&]{journal.put(wrong);},"retirement ID cannot replace a frozen target");
        WtpFleetJournal unchanged(journal_path,self);
        check(unchanged.snapshot().at("contexts").at(0)==entry,"refused journal writes preserve disk recovery authority");
        journal.erase(entry.at("retirement_id").get<std::string>());
        WtpFleetJournal empty_journal(journal_path,self);
        check(empty_journal.snapshot().at("contexts").empty(),"confirmed disposition can retire exactly its entry");
    }
    // Restart at each durable retirement boundary. No carrier factory may run
    // until explicit recovery; frozen authority survives both sides of deletion.
    for(unsigned scenario=0;scenario<4;++scenario) {
        const auto ini=std::string(path)+"/retirement-"+std::to_string(scenario);
        auto row=assignment('a');row["enabled"]=false;
        WtpFleetStore saved(ini+".wtp-assignments.json",self);
        const auto state=saved.update({},[&](Json& d){if(scenario!=1)d["assignments"].push_back(row);});
        WtpFleetJournal journal(ini+".wtp-fleet-contexts.json",self);
        const Json entry={{"retirement_id",std::string(32,'1')},{"kind","operator_remove"},{"assignment",row},
            {"source_etag",state.second},{"last_boot",std::string(32,'2')},{"last_job",std::string(32,'3')},
            {"disposition",scenario==1?"removed":"pending"},{"unresolved",true}};
        journal.put(entry);
        if(scenario==2)saved.update({},[](Json& d){d["assignments"][0]["settings"]["Hostname"]="substituted.local";});
        if(scenario==3){std::ofstream corrupt(ini+".wtp-fleet-contexts.json");corrupt<<"{broken";}
        std::atomic<unsigned> contacts{0};WtpFleetTestHooks hooks;
        hooks.create_target=[&](auto,auto,auto)->std::unique_ptr<WtpTargetRuntime>{++contacts;throw std::runtime_error("Unrequested carrier work");};
        hooks.revocation_status=[&](auto,auto)->Json{++contacts;throw std::runtime_error("Unrequested host observation");};
        start_wtp_fleet_for_test(ini,self,hooks);
        struct StopFleet {~StopFleet(){stop_wtp_fleet();}} stop;
        Json snapshot;
        for(unsigned attempt=0;attempt<100;++attempt) {
            snapshot=Json::parse(wtp_fleet_api("GET","","").body);
            if(snapshot.at("outputs").at(std::string(32,'a')).at("state")== (scenario==1?"removed":"recovery_required"))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto output=snapshot.at("outputs").at(std::string(32,'a'));
        check(output.at("state")== (scenario==1?"removed":"recovery_required") && !output.at("can_resume").get<bool>(),"restored retirement blocks automatic dispatch");
        check(contacts==0,"restored or corrupt authority never guesses a target or starts initial inspection");
        check(snapshot.at("assignments").size()==(scenario==1?0U:1U),"restart preserves assignment deletion boundary");
        if(scenario==1)check(output.at("revoked")==true,"deleted unresolved output remains visible and consumes capacity");
        if(scenario==2)check(output.at("can_reconcile")==false,"conflicting frozen endpoint refuses recovery before contact");
        if(scenario==3)check(!snapshot.at("storage_error").get<std::string>().empty() && output.at("can_pause")==false && output.at("can_reconcile")==false,"corrupt journal cannot authorize guessed cleanup");
        if(scenario!=3)check(journal.snapshot().at("contexts").at(0)==entry,"read-only restart cannot retire cleanup authority");
    }
    store.revoke(std::string(32,'a'),"8");
    WtpFleetStore restarted(file,self);
    auto [after,after_revision]=restarted.snapshot();(void)after_revision;
    check(after.at("assignments").size()==1 && after.at("assignments")[0].at("device_id")==std::string(32,'b'),"offline takeover durably removes only its output");
    check(after.at("removals")[0].at("revocation_generation")=="8","revocation reason retained");
    auto lease=wtp_reserve_output_identity(std::string(32,'a'));
    rejects([&]{(void)wtp_reserve_output_identity(std::string(32,'a'));},"legacy/fleet duplicate authority blocked");
    auto other=wtp_reserve_output_identity(std::string(32,'b'));(void)other;
    lease.reset();check(!wtp_output_identity_in_use(std::string(32,'a')),"identity released with context");
    other.reset();
    const auto fifo=std::string(path)+"/fifo";
    check(mkfifo(fifo.c_str(),0600)==0,"create nonregular state fixture");
    rejects([&]{WtpFleetStore invalid(fifo,self);invalid.snapshot();},"nonregular state rejected without blocking");
    // Exercise the actual worker shutdown against a saved crash marker. The
    // target cannot be contacted with the test target's hardware-free guard.
    const auto ini=std::string(path)+"/runtime.ini";
    WtpFleetStore crash(ini+".wtp-assignments.json",self);
    b["enabled"]=false;b["in_flight"]=true;
    // A bare {b} can select JSON's copy constructor on older Clang versions.
    // Request the array explicitly, including for this single-output fixture.
    const auto crash_saved=crash.update({},[&](Json& d){d["assignments"]=Json::array({b});});
    check(crash_saved.first.at("assignments").is_array() &&
          crash_saved.first.at("assignments").size()==1 &&
          crash_saved.first.at("assignments").at(0)==b,
          "crash fixture saves exactly one complete assignment in an array");
    start_wtp_fleet(ini,self);
    const auto running=wtp_fleet_api("GET","","");
    check(running.status==200,"real fleet manager started");
    const auto output=Json::parse(running.body).at("outputs").at(std::string(32,'b'));
    check(output.at("state")!="unavailable","real worker owns crash fixture");
    const Json ui_command={{"operation","assign"},{"name","UI contract"},
        {"settings",a.at("settings")},{"schedule",a.at("schedule")},{"enabled",false},
        {"consent_plain",true},{"management_port",31415}};
    const auto rejected_transport=wtp_fleet_api("POST",ui_command.dump(),running.etag);
    check(rejected_transport.status==409 &&
          Json::parse(rejected_transport.body).at("error").at("message")!="Invalid assignment fields",
          "documented seven-field UI assignment reaches guarded transport inspection");
    stop_wtp_fleet();
    WtpFleetStore stopped(ini+".wtp-assignments.json",self);
    check(stopped.snapshot().first.at("assignments")[0].at("in_flight")==true,
          "stopping an unconnected controller preserves prior-process dispatch uncertainty");
    std::cout<<"Fleet schedules, persistence, revocation and identity tests passed.\n";
}
