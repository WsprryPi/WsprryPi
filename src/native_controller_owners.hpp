// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <optional>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
namespace wsprrypi {
struct NativeControllerAccount {std::string home;uid_t uid;gid_t gid;};
struct NativeControllerOwners {NativeControllerAccount backend,web,http;};
inline std::optional<NativeControllerAccount> native_controller_account(const char *name){
    for(std::size_t size=8192;size<=1048576;size*=2){
        std::vector<char> buffer(size);passwd record{},*result=nullptr;
        const auto error=name?::getpwnam_r(name,&record,buffer.data(),buffer.size(),&result):
            ::getpwuid_r(::geteuid(),&record,buffer.data(),buffer.size(),&result);
        if(error==ERANGE)continue;
        if(error)throw std::runtime_error("Native controller account lookup failed");
        if(!result)return {};
        if(!record.pw_dir||!*record.pw_dir)throw std::runtime_error("Native controller account home is unavailable");
        return NativeControllerAccount{record.pw_dir,record.pw_uid,record.pw_gid};
    }
    throw std::runtime_error("Native controller account lookup exceeded its bound");
}
inline std::optional<NativeControllerOwners> select_native_controller_owners(
    const std::optional<NativeControllerAccount> &backend,const std::optional<NativeControllerAccount> &web,
    const std::optional<NativeControllerAccount> &http){
#ifdef __APPLE__
    constexpr auto backend_home="/Library/Application Support/WsprryPi/Controller";
    constexpr auto web_home="/Library/Application Support/WsprryPi/Web";
#else
    constexpr auto backend_home="/var/lib/wsprrypi/controller";
    constexpr auto web_home="/var/lib/wsprrypi-web";
#endif
    if(!backend&&!web)return {};
    if(!backend||!web||!http||!backend->uid||!web->uid||!http->uid||!backend->gid||!web->gid||!http->gid||
        backend->uid==web->uid||backend->uid==http->uid||web->uid==http->uid||
        backend->gid==web->gid||backend->gid==http->gid||web->gid==http->gid||
        backend->home!=backend_home||web->home!=web_home)
        throw std::runtime_error("Native controller installation accounts are inconsistent");
    return NativeControllerOwners{*backend,*web,*http};
}
inline std::optional<NativeControllerOwners> native_controller_owners(){
#ifdef __APPLE__
    const auto backend=native_controller_account("_wsprrypi"),web=native_controller_account("_wsprrypiweb");
    constexpr auto http_name="_wsprrypihttp";
#else
    const auto backend=native_controller_account("wsprrypi-controller"),web=native_controller_account("wsprrypi-web");
    constexpr auto http_name="www-data";
#endif
    // No installation accounts means the existing foreground runtime. A partial
    // or conflicting native installation never falls back to that ownership.
    if(!backend&&!web)return {};
    return select_native_controller_owners(backend,web,native_controller_account(http_name));
}
}
