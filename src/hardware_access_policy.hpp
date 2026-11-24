// SPDX-License-Identifier: MIT
#pragma once
#include <cstdlib>
#include <string_view>
namespace wsprrypi {
// Preserve the existing exact-value developer inhibition on every host.
inline bool hardware_access_disabled() {
#ifdef _MSC_VER
    std::size_t length{};char value[2]{};
    return ::getenv_s(&length,value,sizeof value,"WSPRRYPI_DISABLE_HARDWARE_ACCESS")==0&&length==2&&value[0]=='1';
#else
    const auto *value=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
    return value&&std::string_view(value)=="1";
#endif
}
}
