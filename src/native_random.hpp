// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// This service needs no GDI declarations; wingdi.h's ERROR macro conflicts
// with the existing logging and signal components' public enum members.
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#include <bcrypt.h>
#include <limits>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace wsprrypi {
// No seeded PRNG or fallback identity: failure of the OS CSPRNG closes the
// operation. This supplies fresh bytes, never persistent authority storage.
inline void native_random(std::span<std::uint8_t> bytes) {
    if(bytes.empty())return;
#ifdef _WIN32
    if(bytes.size()>std::numeric_limits<ULONG>::max()||
       BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
        throw std::runtime_error("Native cryptographic random source unavailable");
#else
    const int fd=::open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(fd<0)throw std::runtime_error("Native cryptographic random source unavailable");
    struct Guard {int value;~Guard(){::close(value);}} guard{fd};
    std::size_t offset=0;
    while(offset<bytes.size()) {
        const auto count=::read(fd,bytes.data()+offset,bytes.size()-offset);
        if(count<0&&errno==EINTR)continue;
        if(count<=0)throw std::runtime_error("Native cryptographic random read failed");
        offset+=static_cast<std::size_t>(count);
    }
#endif
}
} // namespace wsprrypi
