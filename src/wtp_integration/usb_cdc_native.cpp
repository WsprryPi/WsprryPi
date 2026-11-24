// SPDX-License-Identifier: MIT
#include "usb_cdc_native.hpp"
#include <algorithm>
#include <stdexcept>

namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::runtime_error("USB identity or interface changed; refresh and select the Pico again");}
const NativeCdcEndpoint &find(const std::vector<NativeCdcEndpoint> &rows,const std::string &path) {
    require(rows.size()<=4096);const NativeCdcEndpoint *result=nullptr;
    for(const auto &row:rows)if(row.identity.path==path||std::any_of(row.aliases.begin(),row.aliases.end(),[&](const auto &alias){return alias.path==path;})) {
        require(!result&&row.product=="WsprryPico"&&!row.device_instance.empty()&&
            !row.identity.usb_serial.empty()&&row.identity.usb_serial.size()<=128&&row.identity.vendor_id&&row.identity.product_id);
        result=&row;
    }
    require(result);return *result;
}
bool matches(const CdcIdentity &identity,const PicoCdcSelection &selected) {
    return identity.usb_serial==selected.usb_serial&&identity.vendor_id==selected.vendor_id&&identity.product_id==selected.product_id;
}
[[maybe_unused]] std::string canonical_endpoint(const std::string &path) {
    require(!path.empty()&&path.size()<=4096&&std::filesystem::path(path).is_absolute()&&
        std::none_of(path.begin(),path.end(),[](unsigned char c){return c<32||c==127;}));
    return std::filesystem::canonical(path).string();
}
}
std::vector<NativeCdcEndpoint> native_cdc_inventory(){
#ifdef __APPLE__
    return macos_cdc_inventory();
#elif defined(_WIN32)
    return windows_cdc_inventory();
#else
    throw std::runtime_error("Native USB metadata inventory is unavailable on this host");
#endif
}
CdcIdentity select_native_cdc_identity(const std::vector<NativeCdcEndpoint> &rows,const std::string &path) {
    const auto &row=find(rows,path);auto result=row.identity;
    if(path!=result.path) {
        bool found=false;
        for(const auto &alias:row.aliases)if(alias.path==path){require(!found);found=true;result.device_number=alias.device_number;}
        require(found);
    }
    result.path=path;return result;
}
PicoCdcSelection select_native_cdc_role(const std::vector<NativeCdcEndpoint> &rows,const PicoCdcSelection &selected,PicoCdcRole role) {
    require(role==PicoCdcRole::Management||role==PicoCdcRole::Logging);
    const auto &source=find(rows,selected.path);require(matches(source.identity,selected));
    require(source.identity.control_interface==static_cast<unsigned>(PicoCdcRole::Management)||
        source.identity.control_interface==static_cast<unsigned>(PicoCdcRole::Wtp));
    const NativeCdcEndpoint *result=nullptr;
    for(const auto &row:rows) {
        // Duplicate serials on another physical device cannot be selected safely.
        if(matches(row.identity,selected))require(row.device_instance==source.device_instance);
        if(row.device_instance!=source.device_instance||row.identity.control_interface!=static_cast<unsigned>(role))continue;
        require(!result&&matches(row.identity,selected)&&row.product=="WsprryPico");result=&row;
    }
    require(result&&result->interface_name==(role==PicoCdcRole::Management?"WsprryPico Management":"WsprryPico Logging"));
    return {result->identity.path,selected.usb_serial,selected.vendor_id,selected.product_id,role};
}
CdcIdentity inspect_native_cdc(const std::string &path) {
#ifdef __linux__
    return inspect_linux_cdc(path);
#elif defined(__APPLE__)
    const auto canonical=canonical_endpoint(path);
    return select_native_cdc_identity(native_cdc_inventory(),canonical);
#elif defined(_WIN32)
    return select_native_cdc_identity(native_cdc_inventory(),path);
#else
    (void)path;throw std::runtime_error("Native USB CDC is unavailable on this host");
#endif
}
PicoCdcSelection management_native_cdc(const PicoCdcSelection &selected) {
#ifdef __linux__
    return management_linux_cdc(selected);
#elif defined(__APPLE__)
    auto canonical=selected;canonical.path=canonical_endpoint(selected.path);
    return select_native_cdc_role(native_cdc_inventory(),canonical,PicoCdcRole::Management);
#elif defined(_WIN32)
    return select_native_cdc_role(native_cdc_inventory(),selected,PicoCdcRole::Management);
#else
    (void)selected;throw std::runtime_error("Native USB management is unavailable on this host");
#endif
}
PicoCdcSelection logging_native_cdc(const PicoCdcSelection &selected) {
#ifdef __linux__
    return logging_linux_cdc(selected);
#elif defined(__APPLE__)
    auto canonical=selected;canonical.path=canonical_endpoint(selected.path);
    return select_native_cdc_role(native_cdc_inventory(),canonical,PicoCdcRole::Logging);
#elif defined(_WIN32)
    return select_native_cdc_role(native_cdc_inventory(),selected,PicoCdcRole::Logging);
#else
    (void)selected;throw std::runtime_error("Native USB logging is unavailable on this host");
#endif
}
}
