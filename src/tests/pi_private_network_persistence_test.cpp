// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "privileged_network_admin.hpp"
#include "privileged_network_managed_ini.hpp"
#include "privileged_network_runtime.hpp"
#include "scheduling.hpp"
#include "runtime_config_bridge.hpp"
#include "wtp_integration/pi_managed_startup.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool denied=false;try{action();}catch(...){denied=true;}check(denied,"Expected private-setting refusal");}
void put(const std::string &path,const std::string &value){std::ofstream file(path);file<<value;file.close();check(bool(file)&&!chmod(path.c_str(),0600),"Private fixture file");}
std::string bytes(const std::string &path){std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};}
void fixture(const std::string &ini){
    testing::set_wtp_pi_private_state_path(ini+".ledger");
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
    config.wspr.callsign=config.callsign;config.wspr.grid_square=config.grid_square;config.wspr.power_dbm=config.power_dbm;config.wspr.frequencies=config.frequencies;
    config.wtp.transport="network_plain";config.wtp.hostname="127.0.0.1";config.wtp.device_id=std::string(32,'b');config.wtp.tcp_port=31417;
    resolve_backend_specific_config(config);config_to_json();jConfig["Operation"]["Transmit Backend"]="wtp";
    put(ini,"; exact private layout\n[Security]\nPrivileged Network Safety=enforced\n");iniFile.set_filename(ini);
    iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();initialize_privileged_network_runtime("enforced");
}
}
int main(){
 try{
    char pattern[]="/tmp/pi-private-network-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private test directory");const std::string root=temporary;
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    const std::string self(32,'a');
    for(const bool rollback:{false,true}){
        const auto ini=root+(rollback?"/rollback.ini":"/change.ini"),policy=ini+".apache.conf";fixture(ini);put(policy,"previous Apache policy\n");
        PiManagedStartup owner(ini,self);owner.prepare();std::string error;check(load_json(ini,&error,nullptr),"Valid managed configuration");
        bool permit=false;unsigned reservations=0;
        owner.activate([&](const auto &action){++reservations;if(!permit)throw std::runtime_error("Fixture RF reservation unavailable");action();});
        const auto original=bytes(ini);const auto old=owner.store().settings();const auto public_before=get_public_config_snapshot();
        unsigned reloads=0;PrivilegedNetworkAdmin admin({ini,policy,"/fixture/apache","/fixture/service"},[&](const auto &arguments){
            if(arguments.at(0)=="/fixture/apache")return true;
            check(arguments==std::vector<std::string>{"/fixture/service","reload","apache2"},"Only fixture service calls");
            ++reloads;return !rollback||reloads!=1;
        },managed_network_ini_io());
        const auto result=admin.apply("insecure-disabled");
        check(reservations==0&&get_public_config_snapshot()==public_before&&!config.transmit,
            "Private policy transition cannot reserve RF, change public settings, or publish transmitter runtime");
        if(rollback){
            check(result.status==PrivilegedNetworkTransactionStatus::reload_failed_rolled_back&&bytes(ini)==original&&
                bytes(policy)=="previous Apache policy\n"&&owner.store().settings()["configuration_generation"]=="3"&&
                owner.store().settings()["configuration"]==old["configuration"]&&owner.store().settings()["revision"]!=old["revision"],
                "Failed reload restores exact private bytes through a second complete transaction without reviving an old revision");
            PiManagedStore cold(ini,self,owner.catalog().pi_management_anchor(self,{}));cold.bind_recurrence_storage(ini+".ledger.recurrence-"+self+"/authority.json",true);cold.recover();
            check(cold.settings()==owner.store().settings()&&active_privileged_network_mode()==PrivilegedNetworkMode::enforced,
                "Cold recovery validates the complete rolled-back private policy generation");
        }else{
            check(result.applied()&&owner.store().settings()["configuration_generation"]=="2"&&
                owner.store().settings()["configuration"]==old["configuration"]&&
                iniFile.get_string_value("Security","Privileged Network Safety")=="insecure-disabled",
                "Private policy commit joins INI/settings/history and refreshes raw passthrough layout");
            const auto changed=bytes(ini);const auto saved=owner.store().settings();
            refuses([&]{patch_all_from_web({{"CW",{{"Message","DENIED"}}}});});
            check(bytes(ini)==changed&&owner.store().settings()==saved,"Ordinary RF-related edits still require native admission");
            permit=true;patch_all_from_web({{"CW",{{"Message","AFTER PRIVATE CHANGE"}}}});
            check(read_privileged_network_ini_value(bytes(ini))==std::optional<std::string>("insecure-disabled")&&
                owner.store().settings()["configuration_generation"]=="3","Subsequent ordinary saves preserve the new private setting");
            const auto current=bytes(ini);const auto record=owner.store().settings();auto forbidden=jConfig;forbidden["CW"]["Message"]="HIDDEN PUBLIC CHANGE";
            refuses([&]{persist_owned_network_safety_ini(ini,current,serialize_config_ini_projection_for_management(forbidden));});
            forbidden=jConfig;forbidden["Experimental"]["Allow Non-Amateur Frequency"]=true;
            refuses([&]{persist_owned_network_safety_ini(ini,current,serialize_config_ini_projection_for_management(forbidden));});
            check(bytes(ini)==current&&owner.store().settings()==record,"Private writer cannot smuggle public or other server-owned changes");
            permit=false;
            refuses([&]{persist_runtime_transmit_disabled();});
            check(bytes(ini)==current&&owner.store().settings()==record,
                "A managed Stop persistence refusal never falls through to a direct INI write");
            permit=true;
            const auto runtime_before=jConfig;
            jConfig["CW"]["Message"]="TRANSIENT UNSAVED RUNTIME";
            persist_runtime_transmit_disabled();
            jConfig=runtime_before;
            const auto stopped=owner.store().settings();
            check(stopped["configuration"]["Operation"]["Transmit"]==false&&
                stopped["configuration"]["CW"]["Message"]=="AFTER PRIVATE CHANGE"&&
                stopped["scheduling"]==record["scheduling"]&&
                read_privileged_network_ini_value(bytes(ini))==std::optional<std::string>("insecure-disabled"),
                "Existing Stop joins the complete transaction without saving transient values or changing schedule enablement");
            PiManagedStore cold(ini,self,owner.catalog().pi_management_anchor(self,{}));cold.bind_recurrence_storage(ini+".ledger.recurrence-"+self+"/authority.json",true);cold.recover();
            check(cold.settings()==stopped,"Cold recovery agrees with the existing Stop save");
        }
    }
    std::cout<<"PASS: production private-network and existing Stop INI adapters, full managed transactions, exact rollback, ABA revision protection, transient-value exclusion, raw passthrough refresh, native-admission separation, and no service or RF operations\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
