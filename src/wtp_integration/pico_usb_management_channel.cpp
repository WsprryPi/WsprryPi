#include "hardware_access_policy.hpp"
// SPDX-License-Identifier: MIT
#include "pico_usb_management_channel.hpp"
#include "usb_cdc_native.hpp"
#include "../pico_usb_runtime_ownership.hpp"
#include <chrono>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <thread>

namespace wsprrypi {
std::uint64_t PicoUsbManagementChannel::now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
PicoUsbManagementChannel::~PicoUsbManagementChannel() {
    stopping_=true;
    time_wait_.notify_all();
    if(time_worker_.joinable())time_worker_.join();
    if (client) client->close();
}
void PicoUsbManagementChannel::start_time_refresh() {
    std::lock_guard lock(mutex);
    if(binding_.transport!="usb"||time_worker_.joinable())return;
    time_worker_=std::thread([this]{
        while(!stopping_){
            try {
                // A busy settings transaction wins. Time never replays a
                // mutation, opens an unselected tty, or negotiates WTP.
                std::unique_lock channel(mutex,std::try_to_lock);
                if(channel.owns_lock()&&!stopping_&&client&&client->connected()&&
                    (!cancellation||!cancellation->load())&&
                    !pico_usb_update_blocked(binding_.usb_serial))
                    (void)client->synchronize(utc_observer);
            }catch(...){/* A failed sample cannot strengthen clock authority. */}
            std::unique_lock wait(time_wait_mutex_);
            time_wait_.wait_for(wait,std::chrono::seconds(5),[this]{return stopping_.load();});
        }
    });
}
bool PicoUsbManagementChannel::matches(const WtpSettings &other) const {
    return binding_.transport == other.transport && binding_.path == other.path &&
        binding_.usb_serial == other.usb_serial && binding_.vendor_id == other.vendor_id &&
        binding_.product_id == other.product_id;
}
bool PicoUsbManagementChannel::ensure(const std::string &expected, bool verified_update_reconnect,
    std::uint64_t operation_deadline,const std::function<bool()> &operation_cancelled) {
    std::lock_guard lock(mutex);
    if (now() >= operation_deadline || (cancellation && cancellation->load())||(operation_cancelled&&operation_cancelled())) return false;
    if (binding_.transport != "usb" || (pico_usb_update_blocked(binding_.usb_serial) && !verified_update_reconnect)) return false;
    if (!factory_for_test) {
        if (hardware_access_disabled()) return false;
    }
    if (client && client->connected()) return expected.empty() || client->device() == expected;
    try {
        if (factory_for_test) client = factory_for_test();
        else {
            updater_lease = pico_usb_runtime_lease(binding_.usb_serial);
            stream.close();
            const auto paired = management_native_cdc({binding_.path, binding_.usb_serial,
                static_cast<std::uint16_t>(binding_.vendor_id), static_cast<std::uint16_t>(binding_.product_id),
                PicoCdcRole::Management});
            auto current = now();
            const auto deadline = std::min(current + 10000, operation_deadline);
            if (!stream.begin_open(paired, current)) return false;
            while (stream.state() == CdcState::Resetting && current < deadline) {
                if ((cancellation&&cancellation->load())||(operation_cancelled&&operation_cancelled())) {stream.close();return false;}
                stream.poll_open(current);
                if (stream.state() == CdcState::Resetting) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                current = now();
            }
            if (stream.state() != CdcState::Ready) {stream.close();return false;}
            client = std::make_unique<UsbManagementClient>(stream, now,
                [](unsigned ms) {std::this_thread::sleep_for(std::chrono::milliseconds(ms));},
                [this] {return cancellation && cancellation->load();});
        }
        const auto current = now();
        return client && current < operation_deadline && client->hello(expected,
            static_cast<unsigned>(std::min<std::uint64_t>(2000, operation_deadline-current)),operation_cancelled);
    } catch (...) {stream.close();return false;}
}
std::shared_ptr<PicoUsbManagementChannel> pico_usb_management_channel(const WtpSettings &settings) {
    return pico_usb_management_channel(settings,true);
}
std::shared_ptr<PicoUsbManagementChannel> pico_usb_management_channel(const WtpSettings &settings, bool refresh_time) {
    if (settings.transport != "usb") return std::make_shared<PicoUsbManagementChannel>(settings);
    static std::mutex mutex;
    static std::map<std::string,std::weak_ptr<PicoUsbManagementChannel>> channels;
    std::unique_lock lock(mutex);
    for (auto it=channels.begin();it!=channels.end();) it=it->second.expired()?channels.erase(it):std::next(it);
    const auto found = channels.find(settings.usb_serial);
    if (auto existing = found == channels.end() ? nullptr : found->second.lock()) {
        if (!existing->matches(settings)) throw std::runtime_error("USB management profile binding changed while in use");
        lock.unlock();
        if (refresh_time) existing->start_time_refresh();
        return existing;
    }
    if (channels.size() >= 64) throw std::runtime_error("USB management context capacity exceeded");
    auto channel = std::make_shared<PicoUsbManagementChannel>(settings);
    channels[settings.usb_serial] = channel;
    lock.unlock();
    if (refresh_time) channel->start_time_refresh();
    return channel;
}
} // namespace wsprrypi
