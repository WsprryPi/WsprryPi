// SPDX-License-Identifier: MIT
#pragma once

#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wsprrypi
{
inline constexpr char bcm2835_qualification_lease[] = "/run/lock/wsprrypi-gpclk0.lock";
inline constexpr char bcm2835_qualification_directory[] = "/var/lib/wsprrypi/gpio-qualification";
inline constexpr char bcm2835_qualification_hold[] = "/var/lib/wsprrypi/gpio-qualification/recovery.json";

inline bool bcm2835QualificationModel(std::string_view model)
{
    return model == "Raspberry Pi Model A+" ||
        model == "Raspberry Pi Model A+ Rev 1.1" ||
        model == "Raspberry Pi Model A Plus Rev 1.1";
}

inline bool bcm2835QualificationBoard()
{
    std::ifstream model("/proc/device-tree/model", std::ios::binary);
    std::string name;
    if (!std::getline(model, name, '\0') || !bcm2835QualificationModel(name))
        return false;
    std::ifstream compatible("/proc/device-tree/compatible", std::ios::binary);
    std::string entry;
    while (std::getline(compatible, entry, '\0'))
        if (entry == "brcm,bcm2835") return true;
    return false;
}

class Bcm2835QualificationLease
{
public:
    Bcm2835QualificationLease() = default;
    Bcm2835QualificationLease(const Bcm2835QualificationLease&) = delete;
    Bcm2835QualificationLease& operator=(const Bcm2835QualificationLease&) = delete;
    ~Bcm2835QualificationLease() { release(); }

    void acquire()
    {
        if (fd_ >= 0) return;
        const int fd = open(bcm2835_qualification_lease,
            O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        struct stat metadata{};
        if (fd < 0 || fstat(fd, &metadata) || !S_ISREG(metadata.st_mode) ||
            metadata.st_uid != 0 || metadata.st_nlink != 1 || (metadata.st_mode & 077) ||
            flock(fd, LOCK_EX | LOCK_NB))
        {
            if (fd >= 0) close(fd);
            throw std::runtime_error("BCM2835 GPCLK0 and pad-bank lease unavailable.");
        }
        if (!lstat(bcm2835_qualification_hold, &metadata) || errno != ENOENT)
        {
            close(fd);
            throw std::runtime_error("BCM2835 qualification recovery must be reconciled.");
        }
        fd_ = fd;
    }

    void release() noexcept
    {
        // A worker inheriting this open description keeps the lease until exit.
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
    }

private:
    int fd_{-1};
};
}
