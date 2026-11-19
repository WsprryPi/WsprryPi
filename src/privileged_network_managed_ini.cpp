// SPDX-License-Identifier: MIT
#include "privileged_network_managed_ini.hpp"
#include "config_handler.hpp"
PrivilegedNetworkIniIO managed_network_ini_io(){
    return {
        [](const std::string &path){
            std::optional<std::string> bytes;
            if(read_owned_ini_snapshot(path,bytes))return bytes;
            return read_text_file(path);
        },
        [](const std::string &path,const std::string &before,const std::string &after){
            if(persist_owned_network_safety_ini(path,before,after))return true;
            return write_text_file_atomically(path,after);
        }};
}
