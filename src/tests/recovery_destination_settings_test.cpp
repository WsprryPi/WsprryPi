// SPDX-License-Identifier: MIT
#include "application_recovery/destination_settings.hpp"
#include "application_recovery/definition_adaptation.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace wsprrypi;
void check(bool value){if(!value)throw std::runtime_error("Recovery destination settings assertion failed");}
int main(){try{
    std::ifstream input("wtp_integration/contract/control-controller-vectors.json");nlohmann::json vectors;input>>vectors;
    nlohmann::json native;
    for(const auto &item:vectors.at("cases"))if(item.at("name")=="Pi closed structural save")native=item.at("value").at("candidate").at("configuration");
    check(native.is_object());PersistentConfiguration source{StateRole::Pi,native,{}},destination=source;
    source.configuration["GPIO"]["Transmit Pin"]=17;source.configuration["Calibration"]["PPM"]=3.25;
    source.configuration["Si5351"]["Adapter Identity"]="source-adapter";source.configuration["Operation"]["LED Pin"]=22;
    source.configuration["Operation"]["Web Port"]=32001;source.configuration["Operation"]["Socket Port"]=32002;
    source.configuration["WSPR"]["Call Sign"]="K1ABC";source.configuration["CW"]["Message"]="CQ CQ DE K1ABC";
    source.configuration["CW"]["Fade In Ms"]=7;source.configuration["CW"]["Fade Shape"]="linear";
    source.private_settings={true,SavedNetworkSafety::InsecureDisabled,true,true,true,17};
    const auto before=destination;
    const auto moved=recovery_destination_settings(source,destination,true);
    check(moved.configuration["WSPR"]==source.configuration["WSPR"]&&moved.configuration["CW"]==source.configuration["CW"]);
    for(const auto *section:{"GPIO","Calibration","Si5351","Band GPIO"})check(moved.configuration[section]==destination.configuration[section]);
    check(moved.configuration["Operation"]["LED Pin"]==destination.configuration["Operation"]["LED Pin"]&&
        moved.configuration["Operation"]["Web Port"]==32001&&moved.configuration["Operation"]["Socket Port"]==32002);
    check(moved.private_settings.network_safety==destination.private_settings.network_safety&&!moved.private_settings.allow_unqualified_frequency&&
        !moved.private_settings.allow_non_amateur_frequency&&moved.private_settings.debug_logging&&moved.private_settings.startup_loop_tx&&moved.private_settings.startup_tx_iterations==17);
    check(recovery_destination_settings(source,destination,false)==source&&destination==before);
    const auto changes=recovery_configuration_changes(destination,moved);check(!changes.empty());
    for(const auto &item:changes)check(item["setting"]!="GPIO.Transmit Pin"&&item["setting"]!="Calibration.PPM"&&item["setting"]!="Si5351.Adapter Identity");
    auto controller=PersistentConfiguration{StateRole::Controller,{{"Operation",{{"Web Port",32003},{"Socket Port",32004}}}}, {}};
    bool refused=false;try{recovery_destination_settings(source,controller,true);}catch(const StateStorageError &){refused=true;}check(refused);
    auto old=controller;controller.private_settings.network_safety=SavedNetworkSafety::InsecureDisabled;
    check(recovery_destination_settings(controller,old,true).private_settings.network_safety==SavedNetworkSafety::Enforced);
    nlohmann::json definition{{"id","a"},{"legacy_unverified",false},{"settings",{{"Device ID","b"},{"Transport","usb"}}}};
    auto destinations=nlohmann::json::array({definition});
    check(recovery_definition_resolution(definition,destinations,true)=="preserved_destination_connection");
    destinations[0]["settings"]["Device ID"]="c";
    check(recovery_definition_resolution(definition,destinations,true)=="destination_transport_required");
    check(recovery_definition_resolution(definition,destinations,false)=="source_usb_connection");
    definition["settings"]["Transport"]="network";
    check(recovery_definition_resolution(definition,destinations,true)=="destination_connection_required");
    definition["legacy_unverified"]=true;
    check(recovery_definition_resolution(definition,destinations,true)=="unverified_definition");
    char temporary[]="/tmp/recovery-history-paths-XXXXXX";check(::mkdtemp(temporary));
    const auto root=std::filesystem::canonical(temporary),application=root/"application";
    struct Remove {std::filesystem::path path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
    std::filesystem::create_directories(application/"generation-2");
    {std::ofstream output(application/"generation-2/key");output<<"synthetic dependency";}
    std::filesystem::create_symlink(application/"generation-2/key",root/"key-alias");
    auto profile=nlohmann::json{{"settings",{{"TLS CA File",(root/"external-ca").string()},
        {"TLS Client Certificate",(application/"generation-2/certificate").string()},
        {"TLS Client Key",(root/"key-alias").string()}}}};
    check(recovery_history_dependencies(nlohmann::json::array({profile}),application)==std::set<std::string>{"2"});
    profile["settings"]["TLS Client Certificate"]=(root/"application-other/generation-3/certificate").string();
    profile["settings"]["TLS Client Key"]=(application/"generation-99999999999999999999/key").string();
    check(recovery_history_dependencies(nlohmann::json::array({profile}),application).empty());
    std::cout<<"PASS complete recovery, explicit portable signal/advanced policy, destination hardware/calibration/security retention, concrete changes, role-conversion refusal and no input mutation\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
