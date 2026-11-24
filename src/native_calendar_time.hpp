// SPDX-License-Identifier: MIT
#pragma once
#include <ctime>
namespace wsprrypi::native_calendar {
inline std::tm *utc(const std::time_t *value,std::tm *output) noexcept {
#ifdef _WIN32
    return ::gmtime_s(output,value)==0?output:nullptr;
#else
    return ::gmtime_r(value,output);
#endif
}
inline std::tm *local(const std::time_t *value,std::tm *output) noexcept {
#ifdef _WIN32
    return ::localtime_s(output,value)==0?output:nullptr;
#else
    return ::localtime_r(value,output);
#endif
}
}
