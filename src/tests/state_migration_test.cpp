// SPDX-License-Identifier: MIT
#include "application_storage/state_migration.hpp"
#include "application_storage/persistent_configuration.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_management_journal.hpp"
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
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected initialization refusal");}
void put(const std::string &path,const std::string &value){std::ofstream out(path);out<<value;out.close();check(bool(out)&&!chmod(path.c_str(),0600),"Private fixture source");}
std::string read(const std::string &path){std::ifstream in(path);return {std::istreambuf_iterator<char>(in),{}};}
StateMigrationCapture fixture(const std::string &source,bool create=true){
    const std::string original="[Operation]\nWeb Port=32001\nSocket Port=32002\n";
    if(create)put(source,original);else check(read(source)==original,"Existing source fixture required");
    const nlohmann::json ports{{"Operation",{{"Web Port",32001},{"Socket Port",32002}}}};
    StateMigrationCapture image;
    image.source_namespace=source;image.source_revision=PiManagementJournal::digest(original);
    image.records={{StateRecord::Configuration,encode_persistent_configuration(ports,{},StateRole::Controller)},
        {StateRecord::Catalog,WtpCatalog::initial_storage_image()}};
    image.original_records={{StateRecord::Configuration,original}};
    const auto management=source+".management",target=std::string(32,'c');
    if(create){prepare_companion_directory(management);put(management+"/"+target+".json",std::string(2*1048576+31,'r'));}
    image.companions.push_back(inspect_companion_file(source,CompanionKind::TargetManagement,target).value());
    return image;
}
void validate(std::shared_ptr<SqliteStateStore> database){
    check(configuration_from_snapshot(database->snapshot(),StateRole::Controller).configuration["Operation"]["Web Port"]==32001,
        "Actual persistent settings decoder validates the candidate");
    WtpCatalog catalog(sqlite_catalog_storage(database));
    check(catalog.snapshot({}).first["profiles"].empty(),"Actual catalog parser validates the candidate");
}
std::vector<StateChange> controller_seed(const std::string &,const std::string &){
    return {{StateRecord::Configuration,encode_persistent_configuration(
        nlohmann::json{{"Operation",{{"Web Port",32001},{"Socket Port",32002}}}},{},StateRole::Controller)},
        {StateRecord::Catalog,WtpCatalog::initial_storage_image()}};
}
}
int main(int argc,char **argv){
    try {
        const std::string self(32,'a');
        if((argc==5&&std::string(argv[1])=="--crash-migration")||
            (argc==4&&std::string(argv[1])=="--crash-initialize")) {
            const auto number=std::stoi(argv[argc-1]);
            if(number<0||number>4||std::to_string(number)!=argv[argc-1])return 2;
            const auto point=static_cast<StateMigrationPoint>(number);
            const auto checkpoint=[&](auto current){if(current==point)_exit(73);};
            if(argc==5) {
                const auto image=fixture(argv[2],false);
                migrate_application_state(argv[3],self,StateRole::Controller,argv[2],[&]{return image;},validate,checkpoint);
            } else initialize_application_state(argv[2],self,StateRole::Controller,controller_seed,validate,checkpoint);
            return 75;
        }
        if(argc!=1)return 2;
        char pattern[]="/tmp/state-migration-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const auto missing=root+"/missing";
        refuses([&]{open_application_state(missing,self,StateRole::Controller);});
        check(!std::filesystem::exists(missing),"Normal startup cannot initialize a missing namespace");
        for(const auto point:{StateMigrationPoint::SourceBackedUp,StateMigrationPoint::DatabaseCommitted,
                StateMigrationPoint::DatabasePublished,StateMigrationPoint::SourceRetired,StateMigrationPoint::Initialized}) {
            const auto label=std::to_string(static_cast<int>(point));
            const auto source=root+"/source-"+label+".ini",destination=root+"/state-"+label;
            const auto image=fixture(source);
            const auto capture=[&]{check(read(source)==image.original_records.at(StateRecord::Configuration),"Capture must use the original active source");return image;};
            const auto child=testing::launch_software_child(argv[0],{"--crash-migration",source,destination,label});
            check(testing::wait_software_child(child)==73,
                "Crash occurred at the requested durable boundary");
            if(point!=StateMigrationPoint::Initialized)
                refuses([&]{open_application_state(destination,self,StateRole::Controller);});
            {
                auto database=migrate_application_state(destination,self,StateRole::Controller,source,capture,validate);
                check(!std::filesystem::exists(source)&&read(source+".sqlite-migrated")==image.original_records.at(StateRecord::Configuration)&&
                    read(destination+"/source-configuration")==image.original_records.at(StateRecord::Configuration),
                    "Migration retires INI once and preserves the exact original backup");
                for(const auto &file:image.companions){
                    verify_companion_file(database->directory()+"/"+companion_relative_path(file),file);
                    verify_companion_file(destination+"/source-companions/"+companion_backup_name(file),file);
                }
                refuses([&]{open_application_state(destination,self,StateRole::Controller);});
                const auto before=database->snapshot();
                refuses([&]{database->commit(before.revision,{{StateRecord::Initialization,"replacement"}});});
                check(database->snapshot()==before,"Initialization provenance cannot be replaced by an ordinary write");
            }
            {
                auto database=open_application_state(destination,self,StateRole::Controller);validate(database);
                const auto before=database->snapshot();
                database->commit(before.revision,{{StateRecord::Operations,"bounded unrelated saved record"}});
            }
            unsigned captures=0;
            {
                auto database=migrate_application_state(destination,self,StateRole::Controller,source,[&]{++captures;throw std::runtime_error("Reimport forbidden");return image;},validate);
                check(captures==0&&database->snapshot().records.contains(StateRecord::Operations),"Ready initialization opens current state without reimport or initial-image comparison");
            }
            refuses([&]{open_application_state(destination,std::string(32,'b'),StateRole::Controller);});
            refuses([&]{open_application_state(destination,self,StateRole::Pi);});
            std::filesystem::remove_all(destination+"/generation-1");
            refuses([&]{open_application_state(destination,self,StateRole::Controller);});
            refuses([&]{migrate_application_state(destination,self,StateRole::Controller,source,[&]{++captures;return image;},validate);});
            check(captures==0&&!std::filesystem::exists(destination+"/generation-1"),"Missing established database never falls back to source or backups");
        }
        for(const auto point:{StateMigrationPoint::DatabaseCommitted,StateMigrationPoint::DatabasePublished,StateMigrationPoint::Initialized}) {
            const auto destination=root+"/fresh-"+std::to_string(static_cast<int>(point));
            const auto seed=controller_seed;
            const auto child=testing::launch_software_child(argv[0],{"--crash-initialize",destination,std::to_string(static_cast<int>(point))});
            check(testing::wait_software_child(child)==73,
                "Fresh initialization interrupted at its durable publication boundary");
            if(point!=StateMigrationPoint::Initialized)
                refuses([&]{open_application_state(destination,self,StateRole::Controller);});
            const auto marker_before=nlohmann::json::parse(read(destination+"/authority.json"));
            {
                auto database=initialize_application_state(destination,self,StateRole::Controller,seed,validate);
                validate(database);
                const auto provenance=nlohmann::json::parse(*database->record(StateRecord::Initialization).bytes);
                check(provenance["origin"]=="fresh"&&provenance["source_revision"]==marker_before["source_revision"],
                    "Resume retains the original initialization event rather than creating another identity");
            }
            unsigned calls=0;
            {
                auto database=initialize_application_state(destination,self,StateRole::Controller,
                    [&](const auto &,const auto &){++calls;throw std::runtime_error("Reinitialization forbidden");return std::vector<StateChange>{};},validate);
                check(calls==0,"Repeated explicit init cannot reseed an established database");
            }
            std::filesystem::remove_all(destination+"/generation-1");
            refuses([&]{initialize_application_state(destination,self,StateRole::Controller,seed,validate);});
            check(!std::filesystem::exists(destination+"/generation-1"),"Explicit init cannot replace missing established state");
        }
        {
            const auto source=root+"/changed.ini",destination=root+"/changed-state";
            const auto image=fixture(source);
            auto capture=[&]{auto current=image;current.original_records[StateRecord::Configuration]=read(source);
                current.source_revision=PiManagementJournal::digest(read(source));return current;};
            refuses([&]{migrate_application_state(destination,self,StateRole::Controller,source,capture,validate,[&](auto point){
                if(point==StateMigrationPoint::DatabaseCommitted)put(source,"operator changed source\n");
            });});
            check(read(source)=="operator changed source\n"&&!std::filesystem::exists(source+".sqlite-migrated"),
                "Source mutation prevents retirement and preserves operator edits");
            refuses([&]{migrate_application_state(destination,self,StateRole::Controller,source,capture,validate);});
        }
        std::cout<<"PASS: real SQLite migration, durable-boundary process crashes, source backup/retirement, immutable provenance, singleton admission and missing-state refusal\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
