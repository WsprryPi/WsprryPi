// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
// Adapted from WsprryPico tests/network_mock/tcp.cpp at
// 0fd8191c5218d3b5f2da9122a2ae55bf728ae3f2. Socket errors deliver
// lwIP's error callback; ignoring ECONNRESET strands a test TLS slot.
#include "lwip/tcp.h"

#include "pico/time.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
static tcp_pcb* listener;
static std::vector<tcp_pcb*> clients;
std::uint64_t time_us_64() {
    static const auto origin = std::chrono::steady_clock::now();
    static const auto scale =
        std::getenv("WSPRRY_NETWORK_TEST_TIME_SCALE")
            ? std::strtoul(std::getenv("WSPRRY_NETWORK_TEST_TIME_SCALE"), nullptr, 10)
            : 1;
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                 origin)
               .count() *
           scale;
}
extern "C" int mbedtls_hardware_poll(void*, unsigned char* output, std::size_t count,
                                     std::size_t* used) {
    std::random_device random;
    for (std::size_t i = 0; i < count; ++i)
        output[i] = static_cast<unsigned char>(random());
    *used = count;
    return 0;
}
tcp_pcb* tcp_new_ip_type(int) {
    auto* pcb = new tcp_pcb;
    pcb->fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(pcb->fd, F_SETFL, O_NONBLOCK);
    int yes = 1;
    setsockopt(pcb->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    return pcb;
}
err_t tcp_bind(tcp_pcb* pcb, const void*, unsigned port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    const auto *test_address = std::getenv("WSPRRY_TEST_LISTEN_ADDRESS");
    if (test_address && std::string(test_address) != "127.0.0.1" && std::string(test_address) != "127.0.0.2") return ERR_ABRT;
    if (inet_pton(AF_INET, test_address ? test_address : "127.0.0.1", &address.sin_addr) != 1) return ERR_ABRT;
    address.sin_port = htons(port);
    return bind(pcb->fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ? ERR_ABRT
                                                                                 : ERR_OK;
}
tcp_pcb* tcp_listen_with_backlog(tcp_pcb* pcb, int backlog) {
    if (listen(pcb->fd, backlog))
        return nullptr;
    listener = pcb;
    return pcb;
}
void tcp_arg(tcp_pcb* p, void* a) {
    p->arg = a;
}
void tcp_accept(tcp_pcb* p, err_t (*f)(void*, tcp_pcb*, err_t)) {
    p->accept = f;
}
void tcp_recv(tcp_pcb* p, err_t (*f)(void*, tcp_pcb*, pbuf*, err_t)) {
    p->receive = f;
}
void tcp_err(tcp_pcb* p, void (*f)(void*, err_t)) {
    p->error = f;
}
void tcp_sent(tcp_pcb* p, err_t (*f)(void*, tcp_pcb*, u16_t)) {
    p->sent = f;
}
void tcp_abort(tcp_pcb* p) {
    close(p->fd);
    clients.erase(std::remove(clients.begin(), clients.end(), p), clients.end());
    delete p;
}
err_t tcp_close(tcp_pcb* p) {
    tcp_abort(p);
    return ERR_OK;
}
unsigned tcp_sndbuf(tcp_pcb*) {
    return 1024;
}
err_t tcp_write(tcp_pcb* p, const void* b, u16_t n, int) {
    // Small writes on the local socket are atomic in this bounded test adapter.
    const auto sent = send(p->fd, b, n, 0);
    if (sent == n)
        p->pending += n;
    return sent == n                                                 ? ERR_OK
           : (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) ? ERR_MEM
                                                                     : ERR_ABRT;
}
err_t tcp_output(tcp_pcb*) {
    return ERR_OK;
}
void tcp_recved(tcp_pcb*, u16_t) {}
u16_t pbuf_copy_partial(const pbuf* p, void* dest, u16_t n, u16_t offset) {
    n = std::min<u16_t>(n, p->tot_len - offset);
    std::memcpy(dest, p->payload + offset, n);
    return n;
}
void pbuf_free(pbuf* p) {
    delete[] p->payload;
    delete p;
}
void mock_tcp_poll() {
    if (!listener)
        return;
    int fd = accept(listener->fd, nullptr, nullptr);
    if (fd >= 0) {
        fcntl(fd, F_SETFL, O_NONBLOCK);
        auto* pcb = new tcp_pcb;
        pcb->fd = fd;
        clients.push_back(pcb);
        listener->accept(listener->arg, pcb, ERR_OK);
    }
    const auto copy = clients;
    for (auto* pcb : copy) {
        if (pcb->pending && pcb->sent) {
            const auto n = pcb->pending;
            pcb->pending = 0;
            pcb->sent(pcb->arg, pcb, n);
        }
        if (!pcb->receive)
            continue;
        // Peek first so ERR_MEM faithfully leaves data available for retry.
        std::array<unsigned char, 2048> buffer{};
        const auto n = recv(pcb->fd, buffer.data(), buffer.size(), MSG_PEEK);
        if (n == 0) {
            pcb->receive(pcb->arg, pcb, nullptr, ERR_OK);
            continue;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            const auto callback = pcb->error;
            auto* argument = pcb->arg;
            tcp_abort(pcb);
            if (callback) callback(argument, ERR_ABRT);
            continue;
        }
        auto* packet = new pbuf{static_cast<u16_t>(n), new unsigned char[n]};
        std::copy_n(buffer.data(), n, packet->payload);
        const auto result = pcb->receive(pcb->arg, pcb, packet, ERR_OK);
        if (result == ERR_MEM)
            pbuf_free(packet);
        else if (result != ERR_ABRT)
            (void)recv(pcb->fd, buffer.data(), n, 0);
    }
}
