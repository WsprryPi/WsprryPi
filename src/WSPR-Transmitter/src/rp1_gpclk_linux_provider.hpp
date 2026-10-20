#pragma once

#include "rp1_gpclk_backend.hpp"

#include <string>

namespace wsprrypi
{
class Rp1GpclkIo
{
public:
    virtual ~Rp1GpclkIo() = default;
    virtual int openDevice(const char* path, int flags) noexcept = 0;
    virtual int control(int fd, unsigned long request, void* argument) noexcept = 0;
    virtual int closeDevice(int fd) noexcept = 0;
    virtual int lastError() const noexcept = 0;
};

class Rp1GpclkPosixIo final : public Rp1GpclkIo
{
public:
    int openDevice(const char* path, int flags) noexcept override;
    int control(int fd, unsigned long request, void* argument) noexcept override;
    int closeDevice(int fd) noexcept override;
    int lastError() const noexcept override;
};

class Rp1GpclkLinuxProvider final : public Rp1GpclkProvider
{
public:
    explicit Rp1GpclkLinuxProvider(
        Rp1GpclkIo& io,
        std::string device = "/dev/rp1-gpclk") noexcept;
    ~Rp1GpclkLinuxProvider() override;

    bool query(
        std::uint32_t expected_route,
        std::uint64_t required_capabilities,
        bool require_operational_ready,
        Rp1GpclkProviderIdentity& identity,
        std::string& error) override;
    bool acquire(
        std::uint32_t expected_route,
        std::uint64_t required_capabilities,
        std::string& error) override;
    bool submitEvents(Rp1GpclkProviderEventProgram&, std::string& error) override;
    bool requestFiniteStop(std::uint64_t generation, std::string& error) override;
    Rp1GpclkCompletionState state(std::uint64_t generation) const noexcept override;
    Rp1GpclkProviderEventState eventState(std::uint64_t generation) const noexcept override;
    bool getState(
        std::uint64_t generation,
        Rp1GpclkProviderEventState& state,
        std::string& error) const;
    bool passiveSnapshot(Rp1GpclkPassiveSnapshot& snapshot, std::string& error) const;
    bool release(std::string& error) noexcept override;
    std::uint64_t leaseId() const noexcept override { return lease_id_; }
    std::string endpoint() const override { return device_; }
    bool endpointClosed() const noexcept override { return fd_ < 0; }

private:
    bool queryOpen(
        std::uint32_t expected_route,
        std::uint64_t required_capabilities,
        Rp1GpclkProviderIdentity& identity,
        std::string& error);
    bool failed(const char* operation, std::string& error) const;
    Rp1GpclkIo& io_;
    std::string device_;
    int fd_{-1};
    std::uint64_t lease_id_{0};
    std::uint64_t active_generation_{0};
    std::uint32_t supported_drive_ma_mask_{0};
};
} // namespace wsprrypi
