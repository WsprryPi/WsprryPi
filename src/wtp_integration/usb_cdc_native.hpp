// SPDX-License-Identifier: MIT
#pragma once
#include "usb_cdc.hpp"
#include <vector>
namespace wsprrypi {
struct NativeCdcAlias {std::string path;std::uint64_t device_number{};};
struct NativeCdcEndpoint {
    CdcIdentity identity;
    std::string device_instance,product,interface_name;
    std::vector<NativeCdcAlias> aliases;
};
// Cached OS metadata only; this never opens a serial port or configures USB.
std::vector<NativeCdcEndpoint> macos_cdc_inventory();
std::vector<NativeCdcEndpoint> windows_cdc_inventory();
std::vector<NativeCdcEndpoint> native_cdc_inventory();
// Pure binding over one bounded OS snapshot, shared with adapter tests.
CdcIdentity select_native_cdc_identity(const std::vector<NativeCdcEndpoint> &,const std::string &canonical_path);
PicoCdcSelection select_native_cdc_role(const std::vector<NativeCdcEndpoint> &,const PicoCdcSelection &,PicoCdcRole);
CdcIdentity inspect_native_cdc(const std::string &);
PicoCdcSelection management_native_cdc(const PicoCdcSelection &);
PicoCdcSelection logging_native_cdc(const PicoCdcSelection &);
}
