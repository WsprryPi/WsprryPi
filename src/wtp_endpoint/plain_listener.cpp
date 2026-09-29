#include "wtp_endpoint/plain_listener.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <limits>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace wsprrypi {
namespace {
constexpr std::size_t kMaximumClients = 8;
constexpr std::uint64_t kIdleClientMs = 30'000;

std::uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int send_flags() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}
} // namespace

struct WtpPlainListener::Client {
    int fd;
    wsprrypico::wtp::Endpoint endpoint;
    std::array<std::uint8_t, 64> pending{};
    std::size_t pending_size = 0;
    bool closed = false;
    std::uint64_t last_progress_ms = now_ms();
    bool session_admitted = false;

    Client(int socket, wsprrypico::wtp::IJobService& service,
           const std::string& device_id, const std::string& version,
           wsprrypico::wtp::Endpoint::EventIdSource event_ids)
        : fd(socket), endpoint(service, device_id, version, "WsprryPi",
                                std::move(event_ids)) {
        endpoint.connect("local-network");
    }
    ~Client() { if (fd >= 0) ::close(fd); }
};

WtpPlainListener::WtpPlainListener(wsprrypico::wtp::IJobService& service,
                                   std::string device_id, std::string version)
    : service_(service), device_id_(std::move(device_id)),
      version_(std::move(version)) {}

WtpPlainListener::~WtpPlainListener() { stop(); }

std::string WtpPlainListener::error() const {
    std::lock_guard lock(error_mutex_);
    return error_;
}

bool WtpPlainListener::start(const std::string& address, std::uint16_t port) {
    if (running_ || thread_.joinable() || fd_ >= 0 || address.empty() ||
        address == "0.0.0.0" || address == "::") {
        { std::lock_guard lock(error_mutex_); error_ = "Listener already active or address absent"; }
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* addresses = nullptr;
    const int resolved = getaddrinfo(address.c_str(), std::to_string(port).c_str(),
                                     &hints, &addresses);
    if (resolved != 0 || !addresses) {
        { std::lock_guard lock(error_mutex_); error_ = "Invalid numeric listener address"; }
        return false;
    }
    for (auto* item = addresses; item; item = item->ai_next) {
        const int candidate = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate < 0) continue;
        int reuse = 1;
        (void)setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_NOSIGPIPE
        (void)setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &reuse, sizeof(reuse));
#endif
        if (nonblocking(candidate) && bind(candidate, item->ai_addr, item->ai_addrlen) == 0 &&
            listen(candidate, static_cast<int>(kMaximumClients)) == 0) {
            fd_ = candidate;
            break;
        }
        ::close(candidate);
    }
    freeaddrinfo(addresses);
    if (fd_ < 0) {
        { std::lock_guard lock(error_mutex_); error_ = "Unable to bind WTP listener"; }
        return false;
    }
    sockaddr_storage local{};
    socklen_t length = sizeof(local);
    if (getsockname(fd_, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        { std::lock_guard lock(error_mutex_); error_ = "Unable to inspect WTP listener port"; }
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    bound_port_ = local.ss_family == AF_INET
                      ? ntohs(reinterpret_cast<sockaddr_in*>(&local)->sin_port)
                      : ntohs(reinterpret_cast<sockaddr_in6*>(&local)->sin6_port);
    { std::lock_guard lock(error_mutex_); error_.clear(); }
    running_ = true;
    try {
        thread_ = std::thread(&WtpPlainListener::run, this);
    } catch (...) {
        running_ = false;
        ::close(fd_);
        fd_ = -1;
        bound_port_ = 0;
        { std::lock_guard lock(error_mutex_); error_ = "Unable to start WTP listener worker"; }
        return false;
    }
    return true;
}

void WtpPlainListener::stop() noexcept {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    clients_.clear();
    bound_port_ = 0;
}

void WtpPlainListener::accept_ready() {
    for (;;) {
        const int accepted = accept(fd_, nullptr, nullptr);
        if (accepted < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (clients_.size() >= kMaximumClients || !nonblocking(accepted)) {
            ::close(accepted);
            continue;
        }
#ifdef SO_NOSIGPIPE
        int one = 1;
        (void)setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        clients_.push_back(std::make_unique<Client>(accepted, service_,
            device_id_, version_, [this](std::string_view boot) {
                return next_event_id(boot);
            }));
    }
}

void WtpPlainListener::service_client(Client& client, short events) {
    const auto now = now_ms();
    if (now >= client.last_progress_ms &&
        now - client.last_progress_ms >= kIdleClientMs) {
        client.closed = true;
        return;
    }
    client.endpoint.poll(now);
    if (client.endpoint.closed() || (events & (POLLERR | POLLHUP | POLLNVAL))) {
        client.closed = true;
        return;
    }
    if (client.pending_size && client.endpoint.can_receive()) {
        const auto consumed = client.endpoint.receive(
            std::span<const std::uint8_t>(client.pending.data(), client.pending_size), now);
        if (consumed > client.pending_size) { client.closed = true; return; }
        client.pending_size -= consumed;
        std::move(client.pending.begin() + static_cast<std::ptrdiff_t>(consumed),
                  client.pending.begin() + static_cast<std::ptrdiff_t>(consumed + client.pending_size),
                  client.pending.begin());
    }
    if ((events & POLLIN) && !client.pending_size && client.endpoint.can_receive()) {
        const auto count = recv(client.fd, client.pending.data(), client.pending.size(), 0);
        if (count == 0) { client.closed = true; return; }
        if (count > 0) {
            client.last_progress_ms = now;
            client.pending_size = static_cast<std::size_t>(count);
            const auto consumed = client.endpoint.receive(
                std::span<const std::uint8_t>(client.pending.data(), client.pending_size), now);
            if (consumed > client.pending_size) { client.closed = true; return; }
            client.pending_size -= consumed;
            std::move(client.pending.begin() + static_cast<std::ptrdiff_t>(consumed),
                      client.pending.begin() + static_cast<std::ptrdiff_t>(consumed + client.pending_size),
                      client.pending.begin());
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            client.closed = true;
            return;
        }
    }
    const auto output = client.endpoint.output();
    if (!output.empty() && (events & POLLOUT)) {
        const auto sent = send(client.fd, output.data(), output.size(), send_flags());
        if (sent > 0) {
            client.last_progress_ms = now;
            client.endpoint.consume_output(static_cast<std::size_t>(sent), now);
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            client.closed = true;
    }
}

void WtpPlainListener::drop_closed() {
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                  [](const auto& item) { return item->closed; }),
                   clients_.end());
}

std::optional<std::uint64_t> WtpPlainListener::next_event_id(std::string_view boot) {
    if (boot != event_boot_id_) {
        event_boot_id_ = boot;
        event_id_ = 0;
    }
    if (event_id_ == std::numeric_limits<std::uint64_t>::max()) return {};
    return event_id_++;
}

void WtpPlainListener::run() noexcept {
    try {
    while (running_) {
        std::array<pollfd, kMaximumClients + 1> fds{};
        fds[0] = {fd_, POLLIN, 0};
        for (std::size_t i = 0; i < clients_.size(); ++i) {
            auto& client = *clients_[i];
            fds[i + 1] = {client.fd,
                          static_cast<short>((client.endpoint.can_receive() &&
                                              !client.pending_size ? POLLIN : 0) |
                                             (!client.endpoint.output().empty() ? POLLOUT : 0)),
                          0};
        }
        const auto count = poll(fds.data(), static_cast<nfds_t>(clients_.size() + 1), 20);
        if (count < 0 && errno != EINTR) {
            { std::lock_guard lock(error_mutex_); error_ = "WTP listener poll failed"; }
            break;
        }
        if (fds[0].revents & POLLIN) accept_ready();
        const auto existing = std::min(clients_.size(),
                                       static_cast<std::size_t>(fds.size() - 1));
        for (std::size_t i = 0; i < existing; ++i) {
            auto& client = *clients_[i];
            service_client(client, fds[i + 1].revents);
            if (!client.closed && !client.session_admitted &&
                !client.endpoint.session_id().empty()) {
                client.session_admitted = true;
                for (auto& previous : clients_)
                    if (previous.get() != &client && previous->session_admitted &&
                        previous->endpoint.session_id() == client.endpoint.session_id())
                        previous->endpoint.replace_session(now_ms());
            }
        }
        drop_closed();
        service_.poll();
    }
    } catch (...) {
        { std::lock_guard lock(error_mutex_); error_ = "WTP listener worker failed"; }
    }
    running_ = false;
}
} // namespace wsprrypi
