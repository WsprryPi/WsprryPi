// SPDX-License-Identifier: MIT
#pragma once
#include "configuration.hpp"
#include "consumer_radio.hpp"
#include "pico_usb_management_channel.hpp"

namespace wsprrypi {
// A settings transport owner only: no WtpApplication, output reservation,
// CLAIM, scheduling worker, or execution credential grants management access.
class PicoManagementTarget {
public:
    struct Fixtures {
        PicoManagementExchange engineering;
        ConsumerRadioClient::Transport consumer;
        std::shared_ptr<PicoUsbManagementChannel> usb;
        // C++ host-fixture admission only. TLS still loads/validates the actual
        // configured credentials, identity, ALPN, deadlines and HTTP parser.
        bool loopback_tls{};
    };
    explicit PicoManagementTarget(WtpSettings);
    PicoManagementTarget(WtpSettings, Fixtures);
    ~PicoManagementTarget();
    PicoHttpResponse connect(const std::string &carrier, std::string password = {});
    PicoHttpResponse disconnect();
    bool refresh();
    PicoHttpResponse recovery(const std::string &carrier,std::string password={},const nlohmann::json &scope=nullptr);
    PicoHttpResponse request(const std::string &resource, const std::string &method,
        const std::string &body = {}, const std::string &revision = {});
    nlohmann::json binding() const;
    // Invoked only at the final, validated native mutation boundary. A refusal
    // prevents transport I/O; no UI setting or environment variable installs it.
    using MutationGate = std::function<std::optional<PicoHttpResponse>()>;
    void mutation_gate(MutationGate);
    void cancel() noexcept;
private:
    static std::uint64_t now();
    PicoHttpResponse exchange(const std::string &,const std::string &,const std::string &,const std::string &);
    bool negotiate(bool initial);
    WtpSettings settings_;
    Fixtures fixtures_;
    mutable std::recursive_mutex mutex_;
    std::shared_ptr<std::atomic_bool> cancelled_{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<PicoUsbManagementChannel> usb_;
    TlsStream tls_;
    PlainTcpStream tcp_;
    std::shared_ptr<TlsCredentials> credentials_;
    std::unique_ptr<ConsumerRadioClient> consumer_;
    std::string carrier_, boot_, generation_, capabilities_,management_;
    std::uint64_t deadline_{};
    bool connected_{};
    MutationGate mutation_gate_;
};
} // namespace wsprrypi
