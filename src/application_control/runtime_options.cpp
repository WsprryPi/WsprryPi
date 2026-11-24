// SPDX-License-Identifier: MIT
#include "runtime_options.hpp"
#include "backend_capabilities.hpp"
#ifndef _WIN32
#include "native_controller_owners.hpp"
#include "native_controller_paths.hpp"
#include <filesystem>
#include <mutex>
#include <pwd.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
std::mutex mutex;
std::optional<std::string> fixture;
bool selected=false;
std::optional<NativeControllerOwners> native_owners(){
#if WSPRRYPI_CONTROLLER_ONLY
    {std::lock_guard lock(mutex);if(fixture)return {};}
    return native_controller_owners();
#else
    return {};
#endif
}
LocalTransportOptions native_options(const std::string &root,const NativeControllerOwners &owners){
    return {root,owners.backend.uid,owners.web.uid,owners.web.gid,LocalChannel::Control,uid_t{0}};
}
std::string directory(const std::optional<std::string> &provided){
    std::lock_guard lock(mutex);selected=true;
    const auto value=provided.value_or(fixture.value_or(
#if WSPRRYPI_CONTROLLER_ONLY
        posix_controller_paths().runtime
#else
        "/run/wsprrypi"
#endif
    ));
    if(!std::filesystem::path(value).is_absolute()||value.find('\0')!=std::string::npos)
        throw LocalControlError(LocalControlError::Code::Unavailable);
    return value;
}
}
LocalTransportOptions backend_control_options(const std::optional<std::string> &provided){
    const auto root=directory(provided);const auto uid=::geteuid();
    if(const auto owners=native_owners()){
        if(uid!=owners->backend.uid)throw LocalControlError(LocalControlError::Code::Unauthorized);
        return native_options(root,*owners);
    }
    const auto web=::getpwnam("www-data");
    return {root,uid,web?web->pw_uid:static_cast<uid_t>(-1),web&&uid==0 ? web->pw_gid : ::getegid()};
}
LocalTransportOptions administrator_control_options(const std::optional<std::string> &provided){
    const auto root=directory(provided);
    if(const auto owners=native_owners()){
        if(::geteuid()!=0&&::geteuid()!=owners->backend.uid)throw LocalControlError(LocalControlError::Code::Unauthorized);
        return native_options(root,*owners);
    }
    return {root,::geteuid(),static_cast<uid_t>(-1),::getegid()};
}
LocalTransportOptions web_control_options(const std::optional<std::string> &provided){
    const auto root=directory(provided);
    if(const auto owners=native_owners()){
        if(::geteuid()!=owners->web.uid)throw LocalControlError(LocalControlError::Code::Unauthorized);
        return native_options(root,*owners);
    }
    const auto web=::getpwnam("www-data");const auto uid=::geteuid();
    return {root,web&&web->pw_uid==uid?uid_t{0}:uid,uid,::getegid()};
}
void testing::set_application_control_runtime_directory(const std::string &value){
    std::lock_guard lock(mutex);
    if(selected||fixture||!std::filesystem::path(value).is_absolute()||value.find('\0')!=std::string::npos)
        throw std::logic_error("Application control fixture path must be selected once before startup");
    fixture=value;
}
bool testing::application_control_runtime_fixture_selected(){
    std::lock_guard lock(mutex);return fixture.has_value();
}
}

#endif
