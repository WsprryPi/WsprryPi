// SPDX-License-Identifier: MIT
#include "runtime_options.hpp"
#include <filesystem>
#include <mutex>
#include <pwd.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
std::mutex mutex;
std::optional<std::string> fixture;
bool selected=false;
std::string directory(const std::optional<std::string> &provided){
    std::lock_guard lock(mutex);selected=true;
    const auto value=provided.value_or(fixture.value_or("/run/wsprrypi"));
    if(!std::filesystem::path(value).is_absolute()||value.find('\0')!=std::string::npos)
        throw LocalControlError(LocalControlError::Code::Unavailable);
    return value;
}
}
LocalTransportOptions backend_control_options(const std::optional<std::string> &provided){
    const auto root=directory(provided);const auto uid=::geteuid();
    const auto web=::getpwnam("www-data");
    return {root,uid,web?web->pw_uid:static_cast<uid_t>(-1),web&&uid==0 ? web->pw_gid : ::getegid()};
}
LocalTransportOptions administrator_control_options(const std::optional<std::string> &provided){
    return {directory(provided),::geteuid(),static_cast<uid_t>(-1),::getegid()};
}
LocalTransportOptions web_control_options(const std::optional<std::string> &provided){
    const auto web=::getpwnam("www-data");const auto uid=::geteuid();
    return {directory(provided),web&&web->pw_uid==uid?uid_t{0}:uid,uid,::getegid()};
}
void testing::set_application_control_runtime_directory(const std::string &value){
    std::lock_guard lock(mutex);
    if(selected||fixture||!std::filesystem::path(value).is_absolute()||value.find('\0')!=std::string::npos)
        throw std::logic_error("Application control fixture path must be selected once before startup");
    fixture=value;
}
}
