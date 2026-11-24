// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "runtime_options.hpp"
#include "native_windows_owners.hpp"
#include <mutex>
namespace wsprrypi {
namespace {
std::mutex mutex;std::optional<std::string> fixture;bool selected=false;
struct Selection {std::string root;bool testing;};
Selection directory(const std::optional<std::string> &provided){
    std::lock_guard lock(mutex);selected=true;
    const auto value=provided.value_or(fixture.value_or(""));
    if(!value.empty()){
        (void)windows_file::path_from_utf8(value);return {value,fixture.has_value()};
    }
    const auto path=(windows_controller_root()/L"Controller"/L"Run").u8string();
    return {std::string(path.begin(),path.end()),false};
}
LocalTransportOptions options(const Selection &selection){
    if(selection.testing)return {selection.root,windows_file::current_sid(),L"",LocalChannel::Control,{},false};
    const auto owners=native_windows_controller_owners();
    return {selection.root,owners.backend.sid,owners.web.sid,LocalChannel::Control,owners.maintenance.sid,true};
}
bool elevated_administrator(){
    SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;PSID administrators{};
    if(!::AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&administrators))
        throw LocalControlError(LocalControlError::Code::Unauthorized);
    struct Free {PSID value;~Free(){::FreeSid(value);}} free{administrators};
    BOOL enabled{};
    if(!::CheckTokenMembership(nullptr,administrators,&enabled))throw LocalControlError(LocalControlError::Code::Unauthorized);
    return enabled!=FALSE;
}
void require(bool value){if(!value)throw LocalControlError(LocalControlError::Code::Unauthorized);}
}
LocalTransportOptions backend_control_options(const std::optional<std::string> &provided){
    const auto selection=directory(provided);auto result=options(selection);
    require(windows_file::current_sid()==result.backend_sid&&(selection.testing||!elevated_administrator()));return result;
}
LocalTransportOptions administrator_control_options(const std::optional<std::string> &provided){
    auto result=options(directory(provided));const auto own=windows_file::current_sid();
    require(own==result.backend_sid||(result.administrator_sid&&own==*result.administrator_sid)||
        (result.allow_elevated_administrators&&elevated_administrator()));return result;
}
LocalTransportOptions web_control_options(const std::optional<std::string> &provided){
    const auto selection=directory(provided);auto result=options(selection);
    require(selection.testing||(windows_file::current_sid()==result.web_sid&&!elevated_administrator()));return result;
}
void testing::set_application_control_runtime_directory(const std::string &value){
    (void)windows_file::path_from_utf8(value);std::lock_guard lock(mutex);
    if(selected||fixture)throw std::logic_error("Application control fixture path must be selected once before startup");
    fixture=value;
}
bool testing::application_control_runtime_fixture_selected(){std::lock_guard lock(mutex);return fixture.has_value();}
}
#endif
