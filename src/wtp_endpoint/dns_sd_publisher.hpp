#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace wsprrypi {

// Publication follows a successfully bound Plain LAN listener. Avahi failure
// affects discovery only; the caller retains the direct WTP endpoint.
class WtpPiDnsSdPublisher {
public:
    WtpPiDnsSdPublisher() = default;
    ~WtpPiDnsSdPublisher();
    WtpPiDnsSdPublisher(const WtpPiDnsSdPublisher&) = delete;
    WtpPiDnsSdPublisher& operator=(const WtpPiDnsSdPublisher&) = delete;
    bool start(const std::string& interface_name,
               const std::string& instance_name, std::uint16_t port,
               const std::string& target, const std::string& address);
    void stop() noexcept;
    bool published() const noexcept { return published_.load(); }

private:
    void run() noexcept;
    [[maybe_unused]] std::string interface_name_, instance_name_, target_, address_;
    [[maybe_unused]] std::uint16_t port_ = 0;
    [[maybe_unused]] std::atomic<bool> stopping_{false};
    std::atomic<bool> published_{false};
    [[maybe_unused]] std::thread worker_;
};

} // namespace wsprrypi
