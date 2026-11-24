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
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include <sys/stat.h>
#endif
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw StateStorageError(StateStorageError::Code::Unavailable);}
std::filesystem::path disk_path(const std::string &path){
#ifdef _WIN32
    return windows_file::path_from_utf8(path);
#else
    return path;
#endif
}
bool directory_present(const std::string &directory){
#ifdef _WIN32
    const auto path=disk_path(directory);windows_file::ParentChain parents(path,windows_file::current_sid());
    if(::GetFileAttributesW(windows_file::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES){
        windows_file::Handle handle(::CreateFileW(windows_file::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        require(handle.get()!=INVALID_HANDLE_VALUE);(void)windows_file::inspect(handle.get(),true);
        windows_file::verify_exact_owner(handle.get(),windows_file::current_sid());windows_file::verify_security(handle.get(),windows_file::current_sid(),true,true);return true;
    }
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
#else
    struct stat info{};if(!::lstat(directory.c_str(),&info))return true;require(errno==ENOENT);return false;
#endif
}
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
        require(!directory_present(directory));
        return result;
    }
    PicoManagementJournal owner(directory,identity,identity,catalog->management_history_authority({}));
    // The production owner validates every enrolled/retired journal, identity,
    // checksum, receipt and bounded staging file, including lost-directory cases.
    (void)owner.snapshot();
    if(directory_present(directory)){
        std::vector<std::string> targets;
        for(const auto &entry:std::filesystem::directory_iterator(
#ifdef _WIN32
            windows_file::api_path(disk_path(directory))
#else
            disk_path(directory)
#endif
        )){
            const auto name=entry.path().filename().string();
            if(name.size()==37&&name.ends_with(".json"))targets.push_back(name.substr(0,32));
        }
        std::sort(targets.begin(),targets.end());require(targets.size()<=96);
        for(const auto &target:targets){
            const auto saved=inspect_companion_file(prefix,CompanionKind::TargetManagement,target);
            require(saved.has_value());result.push_back(*saved);
        }
    }
    (void)owner.snapshot();
    for(const auto &file:result)require(inspect_companion_file(prefix,file.kind,file.target)==file);
    (void)companion_manifest(result);return result;
}
} // namespace wsprrypi
