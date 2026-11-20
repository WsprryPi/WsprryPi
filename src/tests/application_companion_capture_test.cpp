// SPDX-License-Identifier: MIT
#include "application_storage/companion_capture.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/persistent_configuration.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "application_storage/migration_source.hpp"
#include "application_storage/ini_migration.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/pico_management_journal.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_settings_json.hpp"
#include "udp_logging/contract.hpp"
#include "wtp_endpoint/runtime.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected owner validation refusal");}
void put(const std::string &path,const std::string &bytes){std::ofstream out(path);out<<bytes;out.close();check(bool(out)&&!chmod(path.c_str(),0600),"Private fixture record");}
}
int main(){
    try{
        char pattern[]="/tmp/application-companions-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const auto source=root+"/settings.ini";const std::string self(32,'a'),device(32,'b'),operation(32,'c');
        testing::set_wtp_pi_private_state_path(root+"/private-fixture");
        const std::string original="[Operation]\nWeb Port=32001\nSocket Port=32002\n";put(source,original);
        WtpCatalog catalog(source+".wtp-devices.json");
        WtpSettings settings;settings.transport="usb";settings.path="/dev/software-fixture";settings.device_id=device;
        settings.usb_serial=device;settings.vendor_id=0xcafe;settings.product_id=0x4012;
        const auto created=catalog.mutate({{"operation","add"},{"name","Fixture"},{"method","manual"},{"discovery_id",""},
            {"settings",management_profile_settings_json(settings)},{"consent_plain",false}},catalog.snapshot({}).second,{});
        const auto profile=created.first["profiles"].back()["id"].get<std::string>();
        const auto generation=catalog.profile(profile,created.second,{}).generation;
        PicoManagementJournal history(source+".management",self,device,catalog.management_history_authority({}));
        const Json binding{{"device_id",device},{"boot_id",std::string(32,'d')},{"profile_generation",generation},{"connection_generation","1"}};
        check(!history.reserve(self,"/api/control/v1/targets/"+device+"/pico/station","PUT",binding,"1",operation,PicoManagementJournal::digest("fixture")),"Actual journal reserves unresolved work");
        const auto pending=history.snapshot();
        put(source+".wtp-assignments.json",Json{{"version",1},{"assignments",Json::array()},{"removals",Json::array()}}.dump());
        put(source+".wtp-fleet-contexts.json",Json{{"version",1},{"contexts",Json::array()}}.dump());
        auto policy=udp::defaults();policy["sender"]["rate"]=27;policy["schema"]="host-udp-logging/1";policy["collector_id"]=self;policy["revision"]="fixture-policy-1";
        put(source+".udp-logs.json",policy.dump());
        put(source+".usb-logs.json",Json{{"schema","host-usb-logging/1"},{"collector_id",self},{"sources",Json::array()}}.dump());
        StateMigrationSource source_owner(source,self,StateRole::Controller);
        const auto capture=[&]{return source_owner.capture();};
        const auto before=capture();check(before.companions.size()==5,"All existing independently owned settings and journal captured");
        const auto validate=[&](std::shared_ptr<SqliteStateStore> database){
            WtpCatalog selected(sqlite_catalog_storage(database));
            const auto observed=capture_application_companions(database->directory()+"/configuration",self,selected);
            check(observed.size()==5&&selected.management_history_authority({}).read().epochs.contains(device),"Actual SQL authority sees enrolled journal epochs");
            PicoManagementJournal journal(database->directory()+"/configuration.management",self,device,selected.management_history_authority({}));
            check(journal.snapshot()==pending,"Exact unresolved operation survives without execution or replay");
        };
        auto database=migrate_application_state(root+"/state",self,StateRole::Controller,source,capture,validate);
        validate(database);check(!std::filesystem::exists(source),"Complete admitted source retired");
        put(database->directory()+"/configuration.usb-logs.json",Json{{"schema","host-usb-logging/1"},{"collector_id",std::string(32,'f')},{"sources",Json::array()}}.dump());
        WtpCatalog selected(sqlite_catalog_storage(database));
        refuses([&]{capture_application_companions(database->directory()+"/configuration",self,selected);});
        const auto minimal=root+"/minimal.ini";
        put(minimal,"[Operation]\nWeb Port=32101\nSocket Port=32102\n[Meta]\ndebug_logging=true\n"
            "[Experimental]\nAllow Non-Amateur Frequency=not-a-native-setting\n[WSPR]\nCall Sign=\n"
            "[Security]\nPrivileged Network Safety=insecure-disabled\n");
        StateMigrationSource minimal_source(minimal,self,StateRole::Controller);
        auto minimal_database=migrate_application_state(root+"/minimal-state",self,StateRole::Controller,minimal,
            [&]{return minimal_source.capture();},validate_application_storage);
        const auto migrated=configuration_from_snapshot(minimal_database->snapshot(),StateRole::Controller);
        check(migrated.configuration["Operation"]["Web Port"]==32101&&migrated.private_settings.debug_logging&&
            migrated.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled&&
            !std::filesystem::exists(minimal+".wtp-devices.json"),"Minimal controller input migrates without station requirements or initializing source catalogs");
        std::cout<<"PASS: production Fleet/logging/journal validators, SQL management authority and complete companion migration preserving unresolved operations\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
