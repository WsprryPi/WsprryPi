// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <realtimeapiset.h>
#else
#include <time.h>
#endif
namespace wsprrypi {
enum class NativeClock {Utc,Monotonic,Elapsed};
inline std::optional<std::uint64_t> native_clock_scaled(std::uint64_t value,std::uint64_t scale) noexcept {
    if(!scale||value>std::numeric_limits<std::uint64_t>::max()/scale)return {};
    return value*scale;
}
inline std::optional<std::uint64_t> native_filetime_ns(std::uint64_t value) noexcept {
    constexpr std::uint64_t epoch=116444736000000000ULL;
    return value<epoch?std::nullopt:native_clock_scaled(value-epoch,100);
}
inline std::optional<std::uint64_t> read_native_clock(NativeClock source) noexcept {
#ifdef _WIN32
    if(source==NativeClock::Utc){
        FILETIME value{};::GetSystemTimePreciseAsFileTime(&value);
        return native_filetime_ns((std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime);
    }
    if(source==NativeClock::Monotonic){ULONGLONG value{};::QueryUnbiasedInterruptTimePrecise(&value);return native_clock_scaled(value,100);}
    // Unlike unbiased interrupt time, GetTickCount64 explicitly includes sleep
    // and hibernation. Its tick granularity is below the existing 100 ms fence.
    if(source==NativeClock::Elapsed)return native_clock_scaled(::GetTickCount64(),1000000);
    return {};
#else
    clockid_t id{};
    switch(source){case NativeClock::Utc:id=CLOCK_REALTIME;break;case NativeClock::Monotonic:id=CLOCK_MONOTONIC;break;
        case NativeClock::Elapsed:
#ifdef __linux__
            id=CLOCK_BOOTTIME;
#else
            id=CLOCK_MONOTONIC;
#endif
            break;
        default:return {};
    }
    timespec value{};constexpr std::uint64_t second=1000000000;
    if(::clock_gettime(id,&value)||value.tv_sec<0||value.tv_nsec<0||value.tv_nsec>=static_cast<long>(second)||
        static_cast<std::uint64_t>(value.tv_sec)>(UINT64_MAX-static_cast<std::uint64_t>(value.tv_nsec))/second)return {};
    return static_cast<std::uint64_t>(value.tv_sec)*second+static_cast<std::uint64_t>(value.tv_nsec);
#endif
}
}
