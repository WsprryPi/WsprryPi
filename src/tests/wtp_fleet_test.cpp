#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/fleet_schedule.hpp"
#include "wtp_integration/output_lease.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_settings_json.hpp"
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
int main(){
    char path[]="/tmp/wsprrypi-fleet-test.XXXXXX";check(mkdtemp(path)!=nullptr,"temporary directory");
    struct Cleanup{std::string path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{path};
    const std::string file=std::string(path)+"/fleet.json";
    const std::string self(32,'f');
    WtpFleetStore store(file,self);
    auto [empty,first]=store.snapshot();check(empty.at("assignments").empty(),"zero targets valid");
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
