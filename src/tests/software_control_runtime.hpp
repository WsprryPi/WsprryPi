// SPDX-License-Identifier: MIT
#pragma once
#include "application_control/runtime_options.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <filesystem>
#include <string_view>
namespace wsprrypi::testing {
// Unix socket path limits are shorter than macOS's normal temporary paths.
// Keep a deterministic private fixture endpoint across backend/client children,
// and remove only its empty directory after the actual main has stopped.
class SoftwareControlRuntime {
public:
    SoftwareControlRuntime(const std::string &ledger,int argc,char *argv[])
        :directory_(std::filesystem::canonical("/tmp").string()+"/wsprrypi-ctl-"+PiManagementJournal::digest(ledger).substr(0,24)){
        set_application_control_runtime_directory(directory_);
        for(int index=1;index<argc;++index){
            const std::string_view option=argv[index];
            if(option=="--state-directory"||option.starts_with("--state-directory="))cleanup_=true;
            if(option=="--settings"||option=="--route-observation"||option=="--initialize-state"||option=="--migrate-state"||
                option=="--check-state"||option=="--route-maintenance"||option=="--storage-format")client_=true;
        }
    }
    ~SoftwareControlRuntime(){if(cleanup_&&!client_){std::error_code ignored;std::filesystem::remove(directory_,ignored);}}
private:std::string directory_;bool cleanup_{},client_{};
};
}
