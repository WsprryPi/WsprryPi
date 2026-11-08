#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/fleet_limits.hpp"
#include "wtp_integration/fleet_schedule.hpp"
#include "wtp_integration/output_lease.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/fleet_recovery.hpp"
#include "wtp_settings_json.hpp"
#include "WSPR-Transmitter/src/execution_plan_compiler.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>
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
int main(){
    restarted_output_admission();
    char path[]="/tmp/wsprrypi-fleet-test.XXXXXX";check(mkdtemp(path)!=nullptr,"temporary directory");
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{path};
    const std::string file=std::string(path)+"/fleet.json";
    const std::string self(32,'f');
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
