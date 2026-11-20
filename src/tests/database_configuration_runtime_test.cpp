// SPDX-License-Identifier: MIT
#include "application_storage/application_state.hpp"
#include "application_storage/ini_migration.hpp"
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>

using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected refusal");}
}
int main(int argc,char **argv){
    try {
        if(argc!=2)return 2;
        char pattern[]="/tmp/database-configuration-runtime-XXXXXX";
        const auto temporary=mkdtemp(pattern);check(temporary,"Temporary private root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        init_default_config();
        set_scheduler_execution_suppressed_for_test(true);
        set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
        config.simulated_backend_override=true;
        config.transmit_backend=TransmitBackendKind::SIMULATED;
        config.transmit=false;
        config.callsign="AA0NT";config.grid_square="EM18";
        config.power_dbm=20;config.frequencies="20m";
        resolve_backend_specific_config(config);config_to_json();
        {
            iniFile.set_filename(argv[1]);
            PreparedConfigCandidate original;
            prepare_ini_config_candidate(argv[1],original);
            check(original.valid,"Original installed template validates with output disabled");
            check(decode_persistent_configuration(encode_persistent_configuration(original.normalized_json,{},StateRole::Pi),StateRole::Pi)==
                decode_persistent_configuration(encode_persistent_configuration(installed_configuration_draft(),{},StateRole::Pi),StateRole::Pi),
                "Database defaults preserve every supported installed-template setting");
        }
        const PrivateConfiguration policy{true,SavedNetworkSafety::Enforced,false,false,true,17};
        auto database=std::make_shared<SqliteStateStore>(root,std::string(32,'a'),StateRole::Pi,StateOpen::CreateNew);
        database->commit("0",{{StateRecord::Configuration,encode_persistent_configuration(jConfig,policy,StateRole::Pi)}});
        {
            ApplicationStateSelection selected(database);
            refuses([&]{ApplicationStateSelection duplicate(database);});
            const auto name=application_configuration_namespace();
            check(!std::filesystem::exists(name),"Configuration namespace is not a file");
            PreparedConfigCandidate candidate;
            prepare_ini_config_candidate(name,candidate);
            check(candidate.valid&&!candidate.transmit_enabled,"Stored settings use real runtime validation");
            check(candidate.normalized_config.simulated_backend_override,"Explicit process simulation stays explicit");
            check(candidate.normalized_config.loop_tx&&candidate.normalized_config.tx_iterations.load()==17,
                "Saved startup policy initializes the runtime counter");
            commit_config_candidate(candidate);
            check(jConfig["Meta"]["debug_logging"]==true,"Saved private debug setting loaded");
            refuses([&]{ini_to_json(name);});
            refuses([&]{validate_config_ini_input(root+"/other.ini");});
            refuses([&]{json_to_ini();});
            auto stock=[](const nlohmann::json &,const std::string &){throw std::logic_error("Unused test owner");};
            set_config_persistence_owner([](const auto &,const auto &persist){persist();},[](const auto &){},stock);
            jConfig["CW"]["Message"]="UNCOMMITTED";
            refuses([&]{json_to_ini();});
            set_config_persistence_owner([&](const auto &source,const auto &persist){
                const auto snapshot=database->snapshot();
                database->commit(snapshot.revision,{{StateRecord::Configuration,database_configuration_after_image(source)}});
                persist();
            },[](const auto &){},stock);
            jConfig["CW"]["Message"]="SAVED IN DATABASE";
            jConfig["Meta"]["debug_logging"]=false; // A process override must not persist.
            jConfig["Meta"]["TX Iterations"]=4;
            json_to_ini();
            check(database_configuration_input().settings.configuration["CW"]["Message"]=="SAVED IN DATABASE"&&
                database_configuration_input().settings.private_settings==policy,"Owner saves to SQLite and preserves private policy");
            const auto before=get_public_config_snapshot().second;
            auto snapshot=database->snapshot();
            database->commit(snapshot.revision,{{StateRecord::Catalog,"unrelated catalog record"}});
            check(before==get_public_config_snapshot().second,"Unrelated Fleet changes do not invalidate local settings revision");
            snapshot=database->snapshot();
            database->commit(snapshot.revision,{{StateRecord::Configuration,snapshot.records.at(StateRecord::Configuration)}});
            check(before!=get_public_config_snapshot().second,"Equal-value settings decisions still revoke stale revisions");
            set_config_persistence_owner({},{},{});
            check(!std::filesystem::exists(name),"No INI file or compatibility projection was created");
        }
        check(application_database_selected(),"Teardown retains selected storage mode");
        refuses([&]{application_database();});
        refuses([&]{json_to_ini();});
        check(saved_ini_startup_policy("[Meta]\nLoop TX = TRUE\nTX Iterations = 27 # saved\n")==std::pair{true,27},
            "Migration uses saved startup values and original comment rules");
        check(saved_ini_startup_policy("[Meta]\nLoop TX=true\n[Other]\nLoop TX=false\n[Meta]\nTX Iterations=9\nTX Iterations=5\n")==std::pair{true,5},
            "Migration follows owning parser sections and duplicate-value semantics");
        check(saved_ini_startup_policy("[Meta]\nLoop TX=\nTX Iterations=\n")==std::pair{false,0},
            "Missing or empty startup policy uses the existing defaults");
        refuses([&]{saved_ini_startup_policy("[Meta]\nLoop TX=1\n");});
        refuses([&]{saved_ini_startup_policy("[Meta]\nTX Iterations=9223372036854775807\n");});
        const auto migrated=saved_ini_private_configuration("[Meta]\nDebug Logging=TRUE\nLoop TX=true\nTX Iterations=27\n"
            "[Experimental]\nAllow Unqualified Frequency=true\nAllow Non-Amateur Frequency=true\n"
            "[Security]\nPrivileged Network Safety=insecure-disabled\n");
        check(migrated==PrivateConfiguration{true,SavedNetworkSafety::InsecureDisabled,true,true,true,27},
            "Migration captures private flags and startup policy from saved source rather than runtime overrides");
        refuses([&]{saved_ini_private_configuration("[Meta]\ndebug_logging=false\nDebug Logging=true\n");});
        refuses([&]{saved_ini_private_configuration("[Security]\nPrivileged Network Safety=enforced\nPrivileged Network Safety=insecure-disabled\n");});
        refuses([&]{decode_ini_migration_configuration("[Operation]\nMode=WSPR\n[WSPR]\ncall sign=AA0NT\n",StateRole::Pi);});
        refuses([&]{decode_ini_migration_configuration("[Operation]\nMode=WSPR\n[CW]\nfade in ms=37\n",StateRole::Pi);});
        refuses([&]{decode_ini_migration_configuration("[operation]\nWeb Port=35001\n",StateRole::Controller);});
        const auto canonical=decode_ini_migration_configuration("[Operation]\nMode=WSPR\n[WSPR]\nCall Sign=AA0NT\ncall sign=IGNORED\n",StateRole::Pi);
        check(canonical.internal["WSPR"]["Call Sign"]=="AA0NT"&&!canonical.warnings.empty(),
            "An existing valid canonical value keeps precedence over an unsupported differently cased extra field");
        std::cout<<"PASS: database runtime parsing, owner-only persistence, private settings, revision isolation and no file fallback\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
