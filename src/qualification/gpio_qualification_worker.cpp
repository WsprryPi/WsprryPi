// SPDX-License-Identifier: MIT
#include "gpio_qualification_owner.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gpio_qualification
{
namespace
{
class Descriptor
{
public:
    explicit Descriptor(int fd) : fd_(fd) { require(fd >= 0, "Could not open private worker channel."); }
    ~Descriptor() { if (fd_ >= 0) close(fd_); }
    int get() const { return fd_; }
    int release() { const int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};

void sendJson(int fd, const Json& value)
{
    const auto bytes = value.dump() + "\n";
    require(send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) == ssize_t(bytes.size()),
        "Worker channel write was not completed.");
}

Json recvJson(int fd)
{
    std::string bytes;
    char c;
    while (bytes.size() < max_message)
    {
        if (recv(fd, &c, 1, 0) != 1) throw std::runtime_error("Worker channel closed or timed out.");
        if (c == '\n') return parse(bytes);
        bytes += c;
    }
    throw std::runtime_error("Worker message exceeds bound.");
}

void groupWorker(int fd, QualificationHardware& hardware, QualificationBank bank,
    int pin, int drive, std::int64_t group_deadline)
{
    std::optional<Bcm2835QualificationPlan> running;
    std::int64_t cutoff{};
    unsigned bus{};
    try
    {
        bus = hardware.prepare(bank);
        sendJson(fd, {{"ready", true}, {"bus", bus}});
        while (nowMs() < group_deadline)
        {
            const auto remaining = (running ? cutoff : group_deadline) - nowMs();
            if (remaining <= 0) break;
            pollfd poller{fd, POLLIN, 0};
            const int readable = poll(&poller, 1, static_cast<int>(std::min(remaining, std::int64_t{100})));
            if (running && nowMs() >= cutoff) break;
            if (readable < 0 && errno == EINTR) continue;
            if (readable < 0 || (poller.revents & (POLLERR | POLLHUP))) break;
            if (!(poller.revents & POLLIN)) continue;
            const auto request = recvJson(fd);
            const auto op = request.at("operation");
            if (op == "exit") break;
            if (op == "start")
            {
                const auto& source = request.at("source");
                const auto& exact_point = request.at("point");
                const auto computed = admission(source, exact_point);
                cutoff = request.at("cutoff_at_ms").get<std::int64_t>();
                require(!running && source.at("gpio") == pin &&
                    computed == request.at("admission") && computed.at("refusal").is_null() &&
                    computed.at("drive_code") == drive && cutoff <= group_deadline &&
                    cutoff > nowMs() + 1500 && cutoff <= nowMs() + cutoff_ms - 1500,
                    "Worker enable did not match its exact armed plan and deadline.");
                running = wsprrypi::planBcm2835Qualification(pin, drive,
                    computed.at("carrier_hz").get<int>(), computed.at("ppm_millionths").get<int>() / 1e6);
                hardware.enable(*running, bank, [&]
                {
                    pollfd authorization{fd, POLLIN, 0};
                    return nowMs() < cutoff && poll(&authorization, 1, 0) == 0;
                });
                const auto readback_end = nowMs() + 40;
                while (!hardware.active(*running, bank) && nowMs() < readback_end)
                    usleep(1000);
                require(hardware.active(*running, bank), "Native active readback was not verified.");
            }
            else if (op == "stop" && running)
            {
                require(hardware.stop(*running, bank, bus), "Native shutdown or bank restoration failed.");
                running.reset();
            }
            else
            {
                require(op == "stop" || op == "status", "Unexpected worker operation.");
            }
            const bool active = running && hardware.active(*running, bank);
            require(!running || active, "Native active readback changed.");
            sendJson(fd, {{"active", active}, {"native", nativeJson(hardware.observe(), pin, active)}});
        }
    }
    catch (...) {}
    try
    {
        const bool stopped = !running || hardware.stop(*running, bank, bus);
        const auto observed = hardware.observe();
        _exit(stopped && observed.idle() && observed.bank == bank && hardware.finish() ? 0 : 2);
    }
    catch (...) { _exit(2); }
}
}

Worker::Worker(QualificationHardware& hardware, QualificationBank bank, int pin, int drive,
    std::int64_t group_deadline, int supervisor_socket)
{
    // The anonymous connected pair is a capability accessible only through
    // inherited descriptors. No listener remains open during the group.
    Descriptor listener(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener.get(), reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == 0 &&
        listen(listener.get(), 1) == 0, "Could not bind private IPv4 worker channel.");
    socklen_t size = sizeof(endpoint);
    require(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&endpoint), &size) == 0,
        "Could not identify private worker endpoint.");
    port_ = ntohs(endpoint.sin_port);
    Descriptor client(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    require(connect(client.get(), reinterpret_cast<sockaddr*>(&endpoint), size) == 0,
        "Could not connect private worker channel.");
    sockaddr_in client_endpoint{}, connected_endpoint{};
    size = sizeof(client_endpoint);
    require(getsockname(client.get(), reinterpret_cast<sockaddr*>(&client_endpoint), &size) == 0,
        "Could not bind private worker credential.");
    size = sizeof(connected_endpoint);
    Descriptor server(accept4(listener.get(), reinterpret_cast<sockaddr*>(&connected_endpoint),
        &size, SOCK_CLOEXEC));
    require(connected_endpoint.sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
        connected_endpoint.sin_port == client_endpoint.sin_port,
        "Unexpected connection to private worker channel.");
    close(listener.release());
    const timeval timeout{0, 180'000};
    for (auto fd : {client.get(), server.get()})
    {
        require(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
            "Could not bound worker traffic.");
    }
    pid_ = fork();
    require(pid_ >= 0, "Could not create isolated drive worker.");
    if (!pid_)
    {
        close(client.release());
        close(supervisor_socket);
        groupWorker(server.get(), hardware, bank, pin, drive, group_deadline);
    }
    channel_ = client.release();
}

bool Worker::ready()
{
    if (bus_) return true;
    pollfd reader{channel_, POLLIN, 0};
    const int result = poll(&reader, 1, 0);
    if (result == 0 || (result < 0 && errno == EINTR)) return false;
    require(result > 0 && (reader.revents & POLLIN), "Drive worker closed before readiness.");
    const auto response = recvJson(channel_);
    require(response.at("ready") == true && response.at("bus").is_number_unsigned(),
        "Drive worker preparation failed.");
    bus_ = response.at("bus").get<unsigned>();
    require(bus_ != 0, "Worker bus identity is unavailable.");
    return true;
}

Worker::~Worker()
{
    if (pid_ > 0) retire();
    if (channel_ >= 0) close(channel_);
}

Json Worker::call(const Json& request)
{
    sendJson(channel_, request);
    if (request.at("operation") == "exit") return nullptr;
    return recvJson(channel_);
}

bool Worker::exited() const
{
    if (pid_ <= 0) return true;
    int status{};
    const auto result = waitpid(pid_, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return false;
    if (result != pid_) return false;
    clean_ = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    pid_ = -1;
    return true;
}

bool Worker::retire()
{
    if (exited()) return true;
    shutdown(channel_, SHUT_RDWR);
    const auto deadline = nowMs() + 150;
    while (!exited() && nowMs() < deadline)
        usleep(1000);
    if (exited()) return true;
    // waitpid identifies this unreaped child. The PID cannot be recycled
    // between WNOHANG and kill while the child belongs to this supervisor.
    kill(pid_, SIGKILL);
    const auto kill_deadline = nowMs() + 150;
    while (!exited() && nowMs() < kill_deadline)
        usleep(1000);
    return exited();
}
}
