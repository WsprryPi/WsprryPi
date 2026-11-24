// SPDX-License-Identifier: MIT
#include "application_storage/controller_identity.hpp"
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "native_windows_owners.hpp"
#include "application_recovery/windows_owner_pipe.hpp"
#endif
#include "wtp_endpoint/identity.hpp"
#include "wtp_endpoint/runtime.hpp"
#include <cerrno>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
namespace wsprrypi {
void configure_native_controller_identity(){
#ifndef __linux__
    if(testing::wtp_pi_device_identity_selected())return;
    static std::once_flag configured;
    std::call_once(configured,[]{
        const auto owner=controller_installation_owner();
#ifdef _WIN32
        if(windows_file::current_sid()!=owner.sid){
            const auto owners=native_windows_controller_owners();
            if(windows_file::current_sid()!=owners.maintenance.sid)throw std::runtime_error("Controller identity owner is unavailable");
            const auto identity=recovery_windows::public_identity(true);
            select_controller_identity_source([identity]{return identity;});return;
        }
#endif
        const ControllerIdentityStore store(owner.home,
#ifdef _WIN32
            owner.sid
#else
            owner.uid
#endif
        );
        select_controller_identity_source([store,selected=std::optional<std::string>{},had_store=false]() mutable {
            const auto stored=store.read();
            const auto current=stored?*stored:wtp_legacy_device_id();
            if(selected&&((had_store!=stored.has_value())||current!=*selected))
                throw std::runtime_error("Controller identity is unavailable or changed");
            selected=current;had_store=stored.has_value();return current;
        });
    });
#endif
}
void initialize_native_controller_identity(const std::string &root){
#ifndef __linux__
    if(testing::wtp_pi_device_identity_selected())return;
#ifdef _WIN32
    const auto path=windows_file::path_from_utf8(root);
    if(::GetFileAttributesW(windows_file::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return;
    const auto error=::GetLastError();
    if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Controller state inspection failed");
#else
    struct stat info{};
    if(::lstat(root.c_str(),&info)==0)return;
    if(errno!=ENOENT)throw std::runtime_error("Controller state inspection failed");
#endif
    const auto owner=controller_installation_owner();
    ControllerIdentityStore store(owner.home,
#ifdef _WIN32
        owner.sid
#else
        owner.uid
#endif
    );
    (void)store.initialize([]{return WtpPiBootIdentity{}.new_boot_id();});
#else
    (void)root;
#endif
}
bool controller_identity_matches(const std::string &expected) noexcept{
    try{return wtp_pi_device_id()==expected;}catch(...){return false;}
}
}
