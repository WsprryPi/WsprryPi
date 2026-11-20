// SPDX-License-Identifier: MIT
#include "companion_capture.hpp"
#include "sqlite_state_store.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/pico_management_journal.hpp"
#include "wtp_integration/usb_logging.hpp"
#include "udp_logging/contract.hpp"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <sys/stat.h>
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw StateStorageError(StateStorageError::Code::Unavailable);}
}
std::vector<CompanionFile> capture_application_companions(const std::string &prefix,const std::string &identity,WtpCatalog *catalog){
    require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos);
    std::vector<CompanionFile> result;
    for(const auto kind:{CompanionKind::FleetAssignments,CompanionKind::FleetContexts,CompanionKind::UdpLogging,CompanionKind::UsbLogging}){
        const auto before=inspect_companion_file(prefix,kind);if(!before)continue;
        switch(kind){
            case CompanionKind::FleetAssignments:{WtpFleetStore owner(companion_source_path(prefix,*before),identity);(void)owner.snapshot();break;}
            case CompanionKind::FleetContexts:{WtpFleetJournal owner(companion_source_path(prefix,*before),identity);(void)owner.snapshot();break;}
            case CompanionKind::UdpLogging:(void)udp::saved_host_policy(read_logging_companion(prefix,*before),identity);break;
            case CompanionKind::UsbLogging:validate_saved_usb_logging_policy(read_logging_companion(prefix,*before),identity);break;
            case CompanionKind::TargetManagement:throw StateStorageError(StateStorageError::Code::InvalidInput);
        }
        require(inspect_companion_file(prefix,kind)==before);result.push_back(*before);
    }
    const auto directory=prefix+".management";
    if(!catalog){
        struct stat info{};require(::lstat(directory.c_str(),&info)&&errno==ENOENT);
        return result;
    }
    PicoManagementJournal owner(directory,identity,identity,catalog->management_history_authority({}));
    // The production owner validates every enrolled/retired journal, identity,
    // checksum, receipt and bounded staging file, including lost-directory cases.
    (void)owner.snapshot();
    struct stat info{};
    if(!::lstat(directory.c_str(),&info)){
        std::vector<std::string> targets;
        for(const auto &entry:std::filesystem::directory_iterator(directory)){
            const auto name=entry.path().filename().string();
            if(name.size()==37&&name.ends_with(".json"))targets.push_back(name.substr(0,32));
        }
        std::sort(targets.begin(),targets.end());require(targets.size()<=96);
        for(const auto &target:targets){
            const auto saved=inspect_companion_file(prefix,CompanionKind::TargetManagement,target);
            require(saved.has_value());result.push_back(*saved);
        }
    }else require(errno==ENOENT);
    (void)owner.snapshot();
    for(const auto &file:result)require(inspect_companion_file(prefix,file.kind,file.target)==file);
    (void)companion_manifest(result);return result;
}
} // namespace wsprrypi
