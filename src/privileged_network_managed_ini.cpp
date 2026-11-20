// SPDX-License-Identifier: MIT
#include "privileged_network_managed_ini.hpp"
#include "config_handler.hpp"
#include "application_storage/application_state.hpp"
#include "controller_runtime.hpp"
PrivilegedNetworkIniIO managed_network_ini_io(){
    return {
        [](const std::string &path){
            if(wsprrypi::application_database_selected())throw std::logic_error("Database settings require the typed policy owner");
            std::optional<std::string> bytes;
            if(read_owned_ini_snapshot(path,bytes))return bytes;
            return read_text_file(path);
        },
        [](const std::string &path,const std::string &before,const std::string &after){
            if(wsprrypi::application_database_selected())throw std::logic_error("Database settings require the typed policy owner");
            if(persist_owned_network_safety_ini(path,before,after))return true;
            return write_text_file_atomically(path,after);
        }};
}
PrivilegedNetworkSettingsIO managed_network_settings_io(){
    return {
        []()->std::optional<PrivilegedNetworkSavedSetting>{
            try {
                const auto saved=wsprrypi::database_configuration_input();
                return PrivilegedNetworkSavedSetting{
                    saved.settings.private_settings.network_safety==wsprrypi::SavedNetworkSafety::InsecureDisabled
                        ?PrivilegedNetworkMode::insecure_disabled:PrivilegedNetworkMode::enforced,saved.record_revision};
            }catch(...){return {};}
        },
        [](const std::string &revision,PrivilegedNetworkMode mode)->std::optional<std::string>{
            if(mode!=PrivilegedNetworkMode::enforced&&mode!=PrivilegedNetworkMode::insecure_disabled)return {};
            return controller_only_mode()?persist_controller_network_safety_setting(revision,mode==PrivilegedNetworkMode::insecure_disabled):
                persist_owned_network_safety_setting(revision,mode==PrivilegedNetworkMode::insecure_disabled);
        }};
}
