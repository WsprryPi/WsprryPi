// SPDX-License-Identifier: MIT
#include "application_storage/application_state.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "recurrence_authority.hpp"
#include "scheduling.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_integration/pi_managed_startup.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected first-install refusal");}
}
int main(int argc,char **argv){
    try {
        if(argc!=2)return 2;
        char pattern[]="/tmp/database-fresh-startup-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const std::string self(32,'a');
        testing::set_wtp_pi_private_state_path(root+"/never-used-output");
        init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
        const auto seed=[&](const auto &name,const auto &installation){return initial_application_records(name,self,StateRole::Pi,installation);};
        const auto validate=[&](std::shared_ptr<SqliteStateStore> candidate){
            WtpCatalog catalog(sqlite_catalog_storage(candidate));
            PiManagedStore owner(candidate,catalog.pi_management_anchor(self,{}));owner.bind_recurrence_storage({},true);owner.recover();
            check(!owner.initialized()&&!owner.installation_pending(),"Initial seed grants neither settings nor first-use authority");
        };
        auto database=initialize_application_state(root+"/state",self,StateRole::Pi,seed,validate);
        ApplicationStateSelection selected(database);
        const auto name=application_configuration_namespace();
        config.use_ini=true;config.ini_filename=name;config.simulated_backend_override=true;
        config.transmit_backend=TransmitBackendKind::SIMULATED;config_to_json();
        unsigned events=0;
        const auto provision=[&](PiPairInitializationProof proof){++events;check(proof.valid()&&proof.device_id()==self,
            "Explicit first-install authority issues the existing device-bound move-only proof");};
        {
            PiManagedStartup owner(database,provision);owner.prepare();
            check(events==1&&owner.store().installation_pending()&&!owner.admission_ready(),"Fresh startup remains held pending operator setup");
            bool loaded=false;try{loaded=load_json(name);}catch(...){}
            check(!loaded&&!owner.configuration_validated()&&!owner.store().initialized(),"Parser defaults cannot approve first setup");
            const auto draft=owner.draft();check(draft.has_value(),"Setup draft comes from persisted SQL seed");
            refuses([&]{owner.activate([](const auto &action){action();});});
            nlohmann::json vectors;std::ifstream input(argv[1]);input>>vectors;nlohmann::json request;
            for(const auto &entry:vectors.at("cases"))if(entry.at("name")=="P209 complete recovery save")request=entry.at("value");
            request["binding"]["device_id"]=self;request["expected_revision"]=owner.store().recovery_revision();
            auto desired=config_handler_serialization::saved_configuration_from_internal_json(installed_configuration_draft());
            desired["WSPR"]["Call Sign"]="AA0NT";desired["WSPR"]["Grid Square"]="EM18";request["configuration"]=desired;
            PiManagedStore::Operation setup{self,"1","/api/control/v1/host/configuration-recovery",request};
            const auto receipt=owner.execute_recovery(setup,[&]{persist_recovery_configuration(desired,draft->source);});
            check(receipt["state"]=="committed"&&owner.store().recurrence_authority()["state"]=="staged"&&
                owner.store().settings()["scheduling"]["enabled"]==false&&!owner.admission_ready(),"Protected operator setup stages disabled native SQL settings and exact receipt");
            unsigned repeated=0;
            check(owner.execute_recovery(setup,[&]{++repeated;})==receipt&&repeated==0,"Setup replay never repeats persistence");
            std::string error;check(load_json(name,&error,nullptr),error.c_str());
            owner.activate([](const auto &action){action();});
            check(owner.admission_ready()&&!config.transmit,"Validated setup and explicit activation remain distinct from Transmit");
        }
        {
            PiManagedStartup owner(database,provision);owner.prepare();
            std::string error;check(load_json(name,&error,nullptr),error.c_str());
            check(events==1&&!owner.admission_ready(),"Restart neither reissues pairing initialization nor activates output");
        }
        check(!std::filesystem::exists(name),"Fresh installation never creates an INI file or projection");
        std::cout<<"PASS: explicit fresh SQL initialization, real startup/setup gate, first-use proof, exact replay and restart without INI\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
