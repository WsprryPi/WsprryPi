// SPDX-License-Identifier: MIT
#include "pi_managed_host.hpp"
#include "arg_parser.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "controller_runtime.hpp"
#include "scheduling.hpp"
#include "wtp_pi_control.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
using namespace wsprrypico::wtp;
using Json=nlohmann::json;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool denied=false;try{action();}catch(...){denied=true;}check(denied,"Expected lifecycle refusal");}
void put(const std::string &path,const std::string &value){std::ofstream file(path);file<<value;file.close();check(bool(file)&&!chmod(path.c_str(),0600),"Private fixture file");}
std::string bytes(const std::string &path){std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};}
Json fixture(const std::string &ini){
    testing::set_wtp_pi_private_state_path(ini+".ledger");
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
    config.wspr.callsign=config.callsign;config.wspr.grid_square=config.grid_square;config.wspr.power_dbm=config.power_dbm;config.wspr.frequencies=config.frequencies;
    config.wtp.transport="network_plain";config.wtp.hostname="127.0.0.1";config.wtp.device_id=std::string(32,'b');config.wtp.tcp_port=31417;
    resolve_backend_specific_config(config);config_to_json();jConfig["Operation"]["Transmit Backend"]="gpio";
    put(ini,"; host fixture\n[Security]\nPrivileged Network Safety = enforced\n");iniFile.set_filename(ini);
    iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();return jConfig;
}
class TestClock final:public Clock {public:ClockSnapshot snapshot() const override{return {ClockState::Synchronized,1000000000000ULL,10000000000ULL,1000,0,LeapState::Normal,std::nullopt};}};
class TestIdentity final:public IdentitySource {public:std::string new_boot_id() override{return std::string(32,'1');}};
Json view(PiManagedHost &host){const auto result=host.recovery({"GET","/api/control/v1/host/configuration-recovery"},true);check(result.status==200,"Closed recovery view");return Json::parse(result.body);}
BrowserRequest save(const Json &current,Json configuration){
    for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})configuration["WTP"][key]="";
    Json command{{"contract","Wsprry-Control/1"},{"schema","configuration-recovery-save"},{"binding",current["binding"]},
        {"capability_revision",current["capability_revision"]},{"operation_id",std::string(32,'e')},
        {"expected_revision",current["revision"]},{"configuration",configuration}};
    return {"PUT","/api/control/v1/host/configuration-recovery",command.dump(),"\""+current["revision"].get<std::string>()+"\"",true};
}
}
int main(){
 try{
    char pattern[]="/tmp/pi-managed-host-XXXXXX";const auto directory=mkdtemp(pattern);check(directory,"Private fixture directory");const std::string root=directory;
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    const std::string self(32,'a'),boot(32,'c');const auto ini=root+"/host.ini";const auto valid=fixture(ini);
    TestClock clock;TestIdentity identity;InhibitedRfEngine engine;JobService jobs(clock,engine,identity);WtpPiAuthority authority(jobs,false);
    const auto observe=[&]()->std::optional<PiManagementObservation>{const auto current=authority.management_snapshot(self);return PiManagementObservation{
        self,current.first,current.second,[&]{return authority.begin_configuration();},[&]{authority.end_configuration();}};};
    auto invalid=valid;invalid["CW"]["Fade Shape"]="invalid";
    invalid["Operation"]["Web Port"]=32123;invalid["Operation"]["Socket Port"]=32124;
    put(ini,serialize_config_ini_projection_for_management(invalid));
    {
        PiManagedHost host(ini,self,boot,observe);host.prepare();
        refuses([&]{host.prepare();});refuses([&]{host.hold_output();});
        std::string error;check(!load_json(ini,&error,nullptr),"Invalid input does not publish configuration");
        config.web_port=31415;config.socket_port=31416;host.restore_recovery_listener_ports();
        check(config.web_port==32123&&config.socket_port==32124&&!config.transmit,"Only valid saved listener ports survive rejected transmitter configuration");
        host.parsed();
        check(host.state()==PiHostReadiness::ConfigurationRecovery&&!wtp_pi_management_ready()&&!controller_only_mode(),"Recovery remains the transmitter role with closed admission");
        check(!host.retry_saved_configuration()&&host.management({"GET","/api/control/v1/targets/"+self+"/context"},true).status==503,
            "Unchanged failed input is not retried and an idle fixture authority cannot bypass startup");
        check(!host.consume_configuration_signal(),"Recovery signal begins clear");
        const auto before=get_public_config_snapshot();const auto request=save(view(host),config_handler_serialization::saved_configuration_from_internal_json(valid));
        const auto response=host.recovery(request,true);check(response.status==200&&get_public_config_snapshot()==before&&!wtp_pi_management_ready(),
            "Explicit recovery save persists without activating runtime or output");
        check(host.consume_configuration_signal()&&!host.consume_configuration_signal(),"Accepted recovery signals one saved readback without polling");
        check(host.retry_saved_configuration()&&host.state()==PiHostReadiness::Starting&&!wtp_pi_management_ready(),
            "One new saved revision automatically passes owning-parser readback while native admission stays closed");
        check(!host.retry_saved_configuration(),"Saved readback is not a poll-write or repeated startup loop");
        host.activate([&](const auto &action){check(authority.begin_configuration(),"Native fixture configuration reservation");struct End{WtpPiAuthority &authority;~End(){authority.end_configuration();}} end{authority};action();});
        check(host.state()==PiHostReadiness::Ready&&wtp_pi_management_ready()&&host.recovery(request,true).body==response.body,
            "Explicit native activation opens readiness and retains the exact earlier save receipt");
        check(host.management({"GET","/api/control/v1/targets/"+self+"/context"},true).status==200,
            "Native management becomes available only after readiness");
        const auto rejected=host.recovery(save(view(host),config_handler_serialization::saved_configuration_from_internal_json(valid)),false);
        check(rejected.status==403&&!host.consume_configuration_signal(),"Rejected recovery cannot signal a parser retry");
        check(host.management({"GET","/api/control/v1/targets/"+self+"/operations/"+std::string(32,'e')},true).body==response.body,
            "A fresh management context can retrieve the earlier recovery receipt");
        put(ini+".replacement",bytes(ini));check(!rename((ini+".replacement").c_str(),ini.c_str()),"External equal-byte replacement");
        check(host.state()==PiHostReadiness::StorageRecovery&&!wtp_pi_management_ready()&&!host.retry_saved_configuration(),
            "Unverifiable initialized input is a storage hold, never another automatic adoption");
    }
    const auto blocked=root+"/output.ini";const auto blocked_config=fixture(blocked);
    {
        PiManagedHost host(blocked,self,boot,observe);host.prepare();std::string error;check(load_json(blocked,&error,nullptr),"Valid output-hold fixture");host.parsed();host.hold_output();
        check(host.state()==PiHostReadiness::OutputRecovery&&!wtp_pi_management_ready()&&!controller_only_mode(),"Output failure cannot select controller-only or open admission");
        refuses([&]{host.activate([](const auto &action){action();});});
        check(host.recovery(save(view(host),config_handler_serialization::saved_configuration_from_internal_json(blocked_config)),true).status==503,
            "A configuration save cannot clear native output uncertainty");
    }
    put(blocked+".managed-settings.json","{corrupt initialized history");
    {
        PiManagedHost host(blocked,self,boot,observe);host.prepare();host.parsed();
        check(host.state()==PiHostReadiness::StorageRecovery&&!wtp_pi_management_ready()&&!view(host)["writable"].get<bool>(),"Corrupt history keeps protected read-only recovery available");
        refuses([&]{validate_config_ini_input(blocked);});
    }
    check(!config.transmit&&!authority.snapshot().remote.owner_id&&!authority.snapshot().remote.output_active,"Fixture never enabled or acquired RF");
    std::cout<<"PASS: process-owned protected recovery, staged save, automatic saved readback, explicit native readiness, durable receipt continuity, unchanged-input retry bound, output/storage holds, and no controller-role fallback\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
