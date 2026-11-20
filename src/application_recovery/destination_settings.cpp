// SPDX-License-Identifier: MIT
#include "destination_settings.hpp"
namespace wsprrypi {
PersistentConfiguration recovery_destination_settings(const PersistentConfiguration &source,const PersistentConfiguration &destination,bool move){
    if(source.role!=destination.role)throw StateStorageError(StateStorageError::Code::Unsupported);
    // Decode both complete images before addressing their fields. The codec's
    // current closed schema also ensures advanced settings are represented.
    const auto from=decode_persistent_configuration(encode_persistent_configuration(source.configuration,source.private_settings,source.role),source.role);
    const auto current=decode_persistent_configuration(encode_persistent_configuration(destination.configuration,destination.private_settings,destination.role),destination.role);
    if(!move)return from;
    auto result=current;
    result.configuration["Operation"]["Web Port"]=from.configuration["Operation"]["Web Port"];
    result.configuration["Operation"]["Socket Port"]=from.configuration["Operation"]["Socket Port"];
    result.private_settings.debug_logging=from.private_settings.debug_logging;
    if(source.role==StateRole::Pi){
        result.configuration["WSPR"]=from.configuration["WSPR"];
        result.configuration["CW"]=from.configuration["CW"];
        for(const auto *field:{"Mode","Transmit","Enable on Boot"})result.configuration["Operation"][field]=from.configuration["Operation"][field];
        result.private_settings.startup_loop_tx=from.private_settings.startup_loop_tx;
        result.private_settings.startup_tx_iterations=from.private_settings.startup_tx_iterations;
    }
    (void)encode_persistent_configuration(result.configuration,result.private_settings,result.role);
    return result;
}
nlohmann::json recovery_configuration_changes(const PersistentConfiguration &before,const PersistentConfiguration &after){
    if(before.role!=after.role)throw StateStorageError(StateStorageError::Code::Unsupported);
    const auto old=nlohmann::json::parse(encode_persistent_configuration(before.configuration,before.private_settings,before.role));
    const auto proposed=nlohmann::json::parse(encode_persistent_configuration(after.configuration,after.private_settings,after.role));
    auto changes=nlohmann::json::array();
    for(const auto &[section,fields]:old.at("configuration").items())for(const auto &[name,value]:fields.items()){
        const auto next=proposed.at("configuration").at(section).at(name);
        if(next!=value)changes.push_back({{"setting",section+"."+name},{"before",value},{"after",next}});
    }
    for(const auto &[name,value]:old.at("private").items()){
        const auto next=proposed.at("private").at(name);
        if(next!=value)changes.push_back({{"setting","private."+name},{"before",value},{"after",next}});
    }
    return changes;
}
}
