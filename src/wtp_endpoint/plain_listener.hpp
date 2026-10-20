#pragma once

#include "WTP-Server/include/wtp_server/endpoint.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wsprrypi {

// Numeric-address TCP carrier for WTP/1 Plain LAN. The caller selects a
// station interface/address pair and controls publication through DNS-SD.
// An empty interface is permitted only for portable loopback tests.
class WtpPlainListener {
public:
    WtpPlainListener(wsprrypico::wtp::IJobService& service,
                     std::string device_id, std::string version);
    ~WtpPlainListener();
    WtpPlainListener(const WtpPlainListener&) = delete;
    WtpPlainListener& operator=(const WtpPlainListener&) = delete;

    bool start(const std::string& numeric_address, std::uint16_t port,
               const std::string& interface_name = {});
    void stop() noexcept;
    bool running() const noexcept { return running_.load(); }
    std::uint16_t bound_port() const noexcept { return bound_port_.load(); }
    std::string error() const;

private:
    struct Client;
    void run() noexcept;
    void accept_ready();
    void service_client(Client& client, short events);
    void drop_closed();
    std::optional<std::uint64_t> next_event_id(std::string_view boot_id);

    wsprrypico::wtp::IJobService& service_;
    std::string device_id_, version_, error_;
    mutable std::mutex error_mutex_;
    int fd_ = -1;
    std::atomic<std::uint16_t> bound_port_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::vector<std::unique_ptr<Client>> clients_;
    std::string event_boot_id_;
    std::uint64_t event_id_ = 0;
};
} // namespace wsprrypi
