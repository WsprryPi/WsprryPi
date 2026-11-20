// SPDX-License-Identifier: MIT
#include "application_storage/controller_settings.hpp"
#include "application_control/advanced_settings.hpp"
#include <filesystem>
#include <iostream>
#include <vector>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected controller settings refusal");}
}
int main(){
    try {
        char pattern[]="/tmp/controller-settings-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        auto database=std::make_shared<SqliteStateStore>(root,std::string(32,'a'),StateRole::Controller,StateOpen::CreateNew);
        refuses([&]{ControllerSettings missing(database);});
        const nlohmann::json ports{{"Operation",{{"Web Port",32001},{"Socket Port",32002}}}};
        const PrivateConfiguration policy{true,SavedNetworkSafety::Enforced,false,false};
        database->commit("0",{{StateRecord::Configuration,encode_persistent_configuration(ports,policy,StateRole::Controller)}});
        ControllerSettings owner(database);const auto first=owner.snapshot();
        check(first.settings.configuration==ports&&first.settings.private_settings==policy,"Controller requires only saved listeners and private policy");
        auto before=database->snapshot();database->commit(before.revision,{{StateRecord::Catalog,"unrelated catalog record"}});
        const auto committed=owner.persist_network_safety(first.revision,SavedNetworkSafety::InsecureDisabled);
        const auto after=owner.snapshot();
        check(after.revision==committed&&after.settings.configuration==ports&&after.settings.private_settings.debug_logging&&
            after.settings.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled,
            "Private policy saves preserve listener/debug values without station fields or native owner");
        before=database->snapshot();
        check(owner.persist_network_safety(after.revision,SavedNetworkSafety::InsecureDisabled)==after.revision&&database->snapshot()==before,
            "Idempotent policy application performs no write");
        refuses([&]{owner.persist_network_safety(first.revision,SavedNetworkSafety::Enforced);});
        refuses([&]{owner.persist_network_safety(after.revision,static_cast<SavedNetworkSafety>(90));});
        check(database->snapshot()==before,"Stale and invalid policy requests preserve state");
        const nlohmann::json edit{{"Meta.debug_logging",false},{"Operation.Web Port",32002},{"Operation.Socket Port",32001}};
        const auto advanced=owner.persist_advanced(after.revision,edit);
        const auto edited=owner.snapshot();
        check(edited.revision==advanced&&advanced_values(edited.settings)==edit&&
            edited.settings.private_settings.network_safety==SavedNetworkSafety::InsecureDisabled,
            "Atomic advanced edit swaps ports and debug without native settings or changing network policy");
        before=database->snapshot();
        for(const auto &bad:std::vector<nlohmann::json>{
            {{"Operation.Web Port",32001}},{{"Operation.Web Port",true}},{{"Meta.debug_logging","false"}},
            {{"CW.Fade In Ms",20}},{{"Security.Privileged Network Safety","enforced"}},
            {{"Meta.Loop TX",true}},{{"Operation.Web Port",80}},nlohmann::json::array(),nlohmann::json::object()}){
            refuses([&]{owner.persist_advanced(advanced,bad);});
            check(database->snapshot()==before,"Invalid/unsupported advanced edits never partly commit");
        }
        refuses([&]{owner.persist_advanced(after.revision,{{"Meta.debug_logging",true}});});
        check(owner.persist_advanced(advanced,edit)==advanced&&database->snapshot()==before,"Advanced no-op preserves revision and bytes");
        auto forbidden=edited.settings;forbidden.private_settings.network_safety=SavedNetworkSafety::Enforced;
        check(!advanced_only_change(edited.settings,forbidden),"Advanced owner cannot smuggle private network policy");
        forbidden=edited.settings;forbidden.private_settings.startup_tx_iterations=19;
        check(!advanced_only_change(edited.settings,forbidden),"Advanced owner cannot alter saved startup counters");
        std::cout<<"PASS: controller-only saved settings, private policy CAS, no-op writes and no native configuration dependency\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
