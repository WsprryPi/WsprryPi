// SPDX-License-Identifier: MIT
#include "application_storage/application_state.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include "privileged_network_managed_ini.hpp"
#include "privileged_network_runtime.hpp"
#include "wtp_integration/pi_managed_startup.hpp"
#include "software_child_process.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected startup refusal");}
void put(const std::string &path,const std::string &bytes){std::ofstream out(path);out<<bytes;out.close();check(bool(out)&&!chmod(path.c_str(),0600),"Private source fixture");}
}
int main(int argc,char **argv){
    try {
        if(argc==5&&std::string(argv[1])=="--crash-native-migration") {
            const std::string source=argv[2],destination=argv[3],self(32,'a');
            nlohmann::json internal;std::ifstream input(argv[4]);input>>internal;
            init_default_config();config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
            config.use_ini=true;config.ini_filename=source;config.transmit=false;
            const auto capture=[&]{
                WtpCatalog catalog(source+".wtp-devices.json");PiManagedStore owner(source,self,catalog.pi_management_anchor(self,{}));
                owner.recover();return owner.database_import_image(internal,{});
            };
            const auto validate=[&](std::shared_ptr<SqliteStateStore> candidate){
                WtpCatalog catalog(sqlite_catalog_storage(candidate));PiManagedStore owner(candidate,catalog.pi_management_anchor(self,{}));
                owner.bind_recurrence_storage({},true);owner.recover();
                check(owner.settings()["configuration"]==config_handler_serialization::saved_configuration_from_internal_json(internal),"Native child candidate validates");
            };
            migrate_application_state(destination,self,StateRole::Pi,source,capture,validate,
                [](auto point){if(point==StateMigrationPoint::SourceRetired)_exit(73);});
            return 75;
        }
        if(argc!=2&&!(argc==3&&std::string(argv[2])=="--native"))return 2;
        const bool native=argc==3;
        char pattern[]="/tmp/database-managed-startup-XXXXXX";
        const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const auto source=root+"/source.ini";const std::string self(32,'a');
        init_default_config();set_scheduler_execution_suppressed_for_test(true);
        set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
        config.use_ini=true;config.ini_filename=source;config.transmit=false;
        config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
        config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
        resolve_backend_specific_config(config);config_to_json();
        const auto internal=jConfig;
        const auto bytes=serialize_config_ini_projection_for_management(internal);
        put(source,bytes);put(source+".stock",bytes);
        PiManagedStore::DatabaseImportImage image;
        {
            WtpCatalog catalog(source+".wtp-devices.json");
            PiManagedStore owner(source,self,catalog.pi_management_anchor(self,{}));
            owner.recover();owner.prepare_installation(false);
            nlohmann::json vectors;std::ifstream input(argv[1]);input>>vectors;
            nlohmann::json request;
            for(const auto &entry:vectors.at("cases"))
                if(entry.at("name")=="P209 complete recovery save")request=entry.at("value");
            check(request.is_object(),"Current complete recovery fixture");
            request["binding"]["device_id"]=self;
            request["configuration"]=config_handler_serialization::saved_configuration_from_internal_json(internal);
            request["expected_revision"]=owner.recovery_revision();
            PiManagedStore::Operation setup{self,"1","/api/control/v1/host/configuration-recovery",request};
            owner.persist_recovery(internal,[]{},setup);
            if(native) {
                nlohmann::json enable;
                for(const auto &entry:vectors.at("cases"))
                    if(entry.value("family",std::string{})=="target-enable"&&entry.value("valid",false)) {enable=entry.at("value");break;}
                check(enable.is_object(),"Current enable fixture");
                enable["binding"]["device_id"]=self;enable["operation_id"]=std::string(32,'8');
                enable["expected_revision"]=owner.settings()["revision"];
                enable["candidate_digest"]=owner.settings()["candidate_digest"];
                owner.persist_scheduling({self,"1","/api/control/v1/targets/"+self+"/enable",enable},true);
                check(owner.recurrence_authority()["state"]=="native"&&owner.recurrence_progress()&&!config.transmit,
                    "Source native progress initialized through real owner without output permission");
            }
            image=owner.database_import_image(internal,{});
        }
        const auto destination=root+"/database";
        const auto capture=[&]{
            WtpCatalog catalog(source+".wtp-devices.json");
            PiManagedStore owner(source,self,catalog.pi_management_anchor(self,{}));
            owner.recover();return owner.database_import_image(internal,{});
        };
        const auto validate=[&](std::shared_ptr<SqliteStateStore> candidate){
            WtpCatalog catalog(sqlite_catalog_storage(candidate));
            PiManagedStore owner(candidate,catalog.pi_management_anchor(self,{}));
            owner.bind_recurrence_storage({},true);owner.recover();
            check(owner.settings()["configuration"]==config_handler_serialization::saved_configuration_from_internal_json(internal),
                "Actual native owner validates the migrated complete settings/receipt/authority image");
        };
        if(native) {
            const auto input_file=root+"/native-input.json";put(input_file,internal.dump());
            const auto child=testing::launch_software_child(argv[0],{"--crash-native-migration",source,destination,input_file});
            check(testing::wait_software_child(child)==73,
                "Native migration interrupted after retiring INI and copying real execution progress");
            refuses([&]{open_application_state(destination,self,StateRole::Pi);});
        }
        auto database=migrate_application_state(destination,self,StateRole::Pi,source,capture,validate);
        check(bool(image.progress_bytes)==native&&
            std::filesystem::exists(database->directory()+"/configuration.recurrence-progress.json")==native,
            "Independent native execution progress relocates only when present in the admitted source");
        check(!std::filesystem::exists(source)&&std::filesystem::exists(source+".sqlite-migrated")&&
            std::filesystem::exists(destination+"/source-configuration"),"Real native migration retires active INI and retains its backup");
        ApplicationStateSelection selected(database);
        const auto name=application_configuration_namespace();
        {
            PiManagedStartup owner(database);owner.prepare();
            check(!owner.admission_ready()&&!owner.configuration_validated(),"Recovery alone does not admit local output");
            refuses([&]{owner.activate([](const auto &action){action();});});
            std::string error;
            check(load_json(name,&error,nullptr),error.c_str());
            check(owner.configuration_validated()&&!owner.admission_ready(),"Validation remains separate from activation");
            check(owner.draft().has_value(),"Saved database draft is observed without file parsing");
            unsigned gates=0;
            owner.activate([&](const auto &action){++gates;action();});
            check(owner.admission_ready(),"Explicit startup activation opens only owner readiness");
            patch_all_from_web({{"CW",{{"Message","HYBRID SQL SETTINGS"}}}});
            check(gates>0&&owner.store().settings()["configuration"]["CW"]["Message"]=="HYBRID SQL SETTINGS",
                "Production local save uses the native lifecycle gate and SQL owner");
            check(database_configuration_input().settings.configuration["CW"]["Message"]=="HYBRID SQL SETTINGS",
                "Runtime, saved configuration and managed settings agree");
            check(!owner.draft(),"Successful save retires earlier draft observation");
            const auto policy_file=root+"/apache-policy.conf";put(policy_file,"previous policy\n");
            initialize_privileged_network_runtime("enforced");
            const auto gates_before=gates;
            unsigned reloads=0;bool fail_next_reload=false;
            PrivilegedNetworkAdmin admin({name,policy_file,"/fixture/apache","/fixture/systemctl"},
                [&](const auto &arguments){
                    if(arguments.at(0)=="/fixture/apache")return true;
                    check(arguments==std::vector<std::string>{"/fixture/systemctl","reload","apache2"},"Only mocked service operations");
                    ++reloads;if(fail_next_reload){fail_next_reload=false;return false;}return true;
                },{},managed_network_settings_io());
            check(admin.apply("insecure-disabled").applied()&&
                database_configuration_input().settings.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled&&
                gates==gates_before,"Typed private policy save bypasses RF reservation and creates no INI");
            const auto stable_revision=database_configuration_input().record_revision;
            check(admin.apply("insecure-disabled").applied()&&database_configuration_input().record_revision==stable_revision,
                "Reapplying the same policy needs no database write");
            fail_next_reload=true;
            check(admin.apply("enforced").status==PrivilegedNetworkTransactionStatus::reload_failed_rolled_back&&
                database_configuration_input().settings.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled&&
                gates==gates_before&&reloads==4,"Reload failure restores the prior SQL policy through its owner");
            refuses([&]{persist_owned_network_safety_setting(stable_revision,false);});
            const auto runtime=jConfig;jConfig["CW"]["Message"]="UNSAVED RUNTIME";
            check(persist_owned_transmit_disabled(),"Existing Stop persists through database owner");
            jConfig=runtime;
            check(database_configuration_input().settings.configuration["CW"]["Message"]=="HYBRID SQL SETTINGS"&&
                database_configuration_input().settings.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled,
                "Stop excludes transient runtime edits and preserves private policy");
            const auto repaired=database_configuration_input().settings;
            persist_stock_configuration(true);
            check(database_configuration_input().settings==repaired,"Repair preserves every current supported saved field");
            persist_stock_configuration(false);
            const auto reset=database_configuration_input().settings;
            check(reset.configuration["CW"]["Message"]=="HELLO"&&reset.configuration["CW"]["Fade Shape"]=="raised_cosine"&&
                reset.configuration["CW"]["Fade In Ms"]==20&&reset.configuration["CW"]["Fade Out Ms"]==20&&
                reset.configuration["CW"]["Fade Slice Ms"]==2&&reset.configuration["Operation"]["Transmit"]==false&&
                reset.private_settings==PrivateConfiguration{},"Reset uses installed stock defaults and private policy without a stock INI");
        }
        check(!std::filesystem::exists(name)&&!config.transmit,"No INI projection or output was created");
        {
            PiManagedStartup restarted(database);restarted.prepare();std::string error;
            check(load_json(name,&error,nullptr),error.c_str());
            check(restarted.store().settings()["configuration"]["CW"]["Message"]=="HELLO"&&
                !restarted.admission_ready(),"Restart restores exact saved state without activating output");
        }
        std::cout<<"PASS: real native startup recovery, SQL parsing, activation gate, hybrid local save and restart without INI\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
