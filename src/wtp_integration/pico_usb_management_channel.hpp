// SPDX-License-Identifier: MIT
#pragma once
#include "usb_cdc.hpp"
#include "usb_management.hpp"
#include "../wtp_settings.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace wsprrypi {
// One physical management CDC owner, independent of WTP sessions and output
// leases. Callers retain mutex through the complete request/response/ACK chain.
// Clocks/cancellation storage outlive any particular execution/editor owner.
struct PicoUsbManagementChannel {
    explicit PicoUsbManagementChannel(WtpSettings binding) : binding_(std::move(binding)) {}
    ~PicoUsbManagementChannel();
    std::recursive_mutex mutex;
    PosixCdcSystem system;
    UsbCdcStream stream{system};
    std::unique_ptr<UsbManagementClient> client;
    std::function<std::unique_ptr<UsbManagementClient>()> factory_for_test;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::shared_ptr<void> updater_lease;
    // Typed clock seam only; production uses the existing read-only host probe.
    std::function<HostUtcObservation()> utc_observer{qualified_host_utc};
    // The shared physical owner supplies time independently of editors/jobs.
    void start_time_refresh();
    bool matches(const WtpSettings &) const;
    bool ensure(const std::string &expected_device, bool verified_update_reconnect = false,
        std::uint64_t operation_deadline = UINT64_MAX,const std::function<bool()> &operation_cancelled = {});
private:
    static std::uint64_t now();
    WtpSettings binding_;
    std::thread time_worker_;
    std::atomic_bool stopping_{};
    std::mutex time_wait_mutex_;
    std::condition_variable time_wait_;
};
std::shared_ptr<PicoUsbManagementChannel> pico_usb_management_channel(const WtpSettings &);
std::shared_ptr<PicoUsbManagementChannel> pico_usb_management_channel(const WtpSettings &, bool refresh_time);
} // namespace wsprrypi
