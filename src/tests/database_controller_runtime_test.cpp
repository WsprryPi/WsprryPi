// SPDX-License-Identifier: MIT
#include "application_storage/application_state.hpp"
#include "application_storage/controller_settings.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "config_handler.hpp"
#include "controller_runtime.hpp"
#include "privileged_network_managed_ini.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected controller runtime refusal");}
}
int main(){
    try {
        char pattern[]="/tmp/database-controller-runtime-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const std::string self(32,'a');
        const auto seed=[](const auto &,const auto &){
            return std::vector<StateChange>{{StateRecord::Configuration,encode_persistent_configuration(
                nlohmann::json{{"Operation",{{"Web Port",32001},{"Socket Port",32002}}}},
                {true,SavedNetworkSafety::Enforced,false,false},StateRole::Controller)},
                {StateRecord::Catalog,WtpCatalog::initial_storage_image()}};
        };
        const auto validate=[](std::shared_ptr<SqliteStateStore> database){
            ControllerSettings owner(database);(void)owner.snapshot();
            WtpCatalog catalog(sqlite_catalog_storage(database));(void)catalog.snapshot({});
        };
        auto database=initialize_application_state(root+"/state",self,StateRole::Controller,seed,validate);
        ApplicationStateSelection selected(database);
        init_default_config();
        char executable[]="fixture",role[]="--controller-only",debug[]="--no-debug-logging";
        char *arguments[]{executable,role,debug};configure_controller_only(3,arguments);
        check(controller_only_mode()&&!config.transmit&&!config.loop_tx&&config.web_port==32001&&config.socket_port==32002&&
            !config.debug_logging,"Actual controller parser loads SQL listeners and preserves transient flag precedence without a transmitter config");
        check(application_origin_namespace()==application_configuration_namespace()&&
            &application_catalog()==&controller_storage_catalog(),"Controller uses one SQL catalog owner and a stable storage namespace");
        check(!controller_http_allowed("POST","/config"),"Controller role retains native-configuration route refusal");
        const auto policy=managed_network_settings_io();const auto before=policy.read();check(before.has_value(),"Typed controller policy read");
        const auto committed=policy.write(before->revision,PrivilegedNetworkMode::insecure_disabled);
        check(committed&&policy.read()->revision==*committed&&policy.read()->mode==PrivilegedNetworkMode::insecure_disabled,
            "Actual private network adapter dispatches to the controller owner");
        const auto saved=database_configuration_input().settings;
        check(saved.configuration.size()==1&&saved.configuration["Operation"].size()==2&&saved.private_settings.debug_logging&&
            saved.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled&&!config.debug_logging,
            "Controller policy save preserves saved debug/listeners and does not persist CLI overrides or station fields");
        refuses([&]{iniFile.set_filename(application_configuration_namespace());});
        refuses([&]{policy.write(before->revision,PrivilegedNetworkMode::enforced);});
        check(!std::filesystem::exists(application_configuration_namespace()),"Controller runtime creates no INI or compatibility projection");
        std::cout<<"PASS: actual controller SQL configuration, shared catalog, transient CLI precedence, typed policy persistence and no INI/native dependency\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
