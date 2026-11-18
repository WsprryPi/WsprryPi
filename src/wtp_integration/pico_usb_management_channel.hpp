// SPDX-License-Identifier: MIT
#pragma once
#include "usb_cdc.hpp"
#include "usb_management.hpp"
#include "../wtp_settings.hpp"
#include <atomic>
#include <memory>
#include <mutex>

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
    bool matches(const WtpSettings &) const;
    bool ensure(const std::string &expected_device, bool verified_update_reconnect = false,
        std::uint64_t operation_deadline = UINT64_MAX,const std::function<bool()> &operation_cancelled = {});
private:
    static std::uint64_t now();
    WtpSettings binding_;
};
std::shared_ptr<PicoUsbManagementChannel> pico_usb_management_channel(const WtpSettings &);
} // namespace wsprrypi
