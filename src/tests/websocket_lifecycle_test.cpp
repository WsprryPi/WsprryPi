#include "../web_socket.hpp"
#include "../privileged_network_runtime.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

class WebSocketLifecycleTest {
    static std::string text_frame(const std::string &text) {
        assert(text.size() <= 65535U);
        std::string frame(1, static_cast<char>(0x81));
        if (text.size() < 126) frame.push_back(static_cast<char>(0x80U | text.size()));
        else {
            frame.push_back(static_cast<char>(0xFE));
            frame.push_back(static_cast<char>(text.size() >> 8));
            frame.push_back(static_cast<char>(text.size()));
        }
        const unsigned char mask[4]={1,2,3,4};
        frame.append(reinterpret_cast<const char *>(mask),4);
        for(std::size_t i=0;i<text.size();++i) frame.push_back(static_cast<char>(text[i]^mask[i%4]));
        return frame;
    }
    static void send_bytes(int fd, const std::string &frame) {
        std::size_t offset = 0;
        while (offset < frame.size()) {
            auto sent = send(fd, frame.data()+offset, frame.size()-offset, 0);
            assert(sent > 0); offset += static_cast<std::size_t>(sent);
        }
    }
    static void send_text(int fd, const std::string &text) {
        send_bytes(fd, text_frame(text));
    }
    static int connect_client(
        unsigned short port,
        const std::string &represented_client = {}) {
        int fd=socket(AF_INET,SOCK_STREAM,0); assert(fd>=0);
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
        assert(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);
        const std::string host = represented_client.empty()
            ? "127.0.0.1:" + std::to_string(port)
            : "wsprrypi";
        std::string h="GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
        if (!represented_client.empty())
            h += "X-WsprryPi-Client-Address: " + represented_client + "\r\n";
        h += "\r\n";
        assert(send(fd,h.data(),h.size(),0)>0); char b[512]{}; auto n=recv(fd,b,sizeof(b),0); assert(n>0); assert(std::string(b,n).find("HTTP/1.1 101") == 0); return fd;
    }
    static void expect_rejected_upgrade(
        unsigned short port,
        const std::string &represented_client) {
        int fd=socket(AF_INET,SOCK_STREAM,0); assert(fd>=0);
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
        assert(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);
        const std::string request =
            "GET / HTTP/1.1\r\nHost: wsprrypi\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "X-WsprryPi-Client-Address: " + represented_client + "\r\n\r\n";
        assert(send(fd,request.data(),request.size(),0)>0);
        char response[512]{};
        const auto size=recv(fd,response,sizeof(response),0);
        assert(size>0);
        assert(std::string(response,size).find("HTTP/1.1 403") == 0);
        close(fd);
    }
    static bool wait_disconnected(
        int fd,
        const std::string &forbidden_payload = {}) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        std::string received;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
            pollfd descriptor{fd, POLLIN | POLLHUP | POLLERR, 0};
            const int ready = poll(
                &descriptor, 1,
                static_cast<int>(std::max<std::int64_t>(1, remaining.count())));
            if (ready <= 0)
                return false;
            char buffer[512]{};
            const auto size = recv(fd, buffer, sizeof(buffer), 0);
            if (size <= 0)
                return forbidden_payload.empty() ||
                       received.find(forbidden_payload) == std::string::npos;
            received.append(buffer, static_cast<std::size_t>(size));
            if (!forbidden_payload.empty() &&
                received.find(forbidden_payload) != std::string::npos)
                return false;
        }
        return false;
    }
    static bool wait_zero(WebSocketServer &s) {
        std::unique_lock<std::mutex> l(s.clients_mutex_);
        return s.client_handlers_cv_.wait_for(l,std::chrono::seconds(2),[&]{return s.active_client_handlers_==0;});
    }
    static bool wait_client_count(WebSocketServer &s, std::size_t expected) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(s.clients_mutex_);
                if (s.client_sockets_.size() == expected &&
                    s.active_client_handlers_ == expected)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    static bool wait_handshake(WebSocketServer &s) {
        std::unique_lock<std::mutex> l(s.clients_mutex_);
        return s.client_handlers_cv_.wait_for(l,std::chrono::seconds(2),[&]{return !s.handshaking_sockets_.empty();});
    }
    static void verify_test_tone_transaction_lock(WebSocketServer &s) {
        std::mutex state_mutex;
        std::condition_variable state_cv;
        bool holder_acquired=false;
        bool release_holder=false;
        std::thread holder([&]{
            std::unique_lock<std::mutex> transaction(s.test_tone_command_mutex_);
            std::unique_lock<std::mutex> state(state_mutex);
            holder_acquired=true;
            state_cv.notify_all();
            state_cv.wait(state,[&]{return release_holder;});
        });
        {
            std::unique_lock<std::mutex> state(state_mutex);
            assert(state_cv.wait_for(state,std::chrono::seconds(2),[&]{return holder_acquired;}));
        }
        assert(!s.test_tone_command_mutex_.try_lock());
        {
            std::lock_guard<std::mutex> state(state_mutex);
            release_holder=true;
        }
        state_cv.notify_all();
        holder.join();
        assert(s.test_tone_command_mutex_.try_lock());
        s.test_tone_command_mutex_.unlock();
    }
    static void verify_loopback_binding(unsigned short port) {
        WebSocketServer local;
        assert(local.start(port, 0, true, WebSocketLoopbackFamily::IPv6));
        sockaddr_in6 address{};
        socklen_t size = sizeof(address);
        assert(getsockname(
            local.listen_fd_, reinterpret_cast<sockaddr *>(&address), &size) == 0);
        assert(address.sin6_family == AF_INET6);
        assert(IN6_IS_ADDR_LOOPBACK(&address.sin6_addr));
        assert(local.listening_address() == "::1");
        local.stop();
        assert(local.listening_address().empty());
    }
    static void verify_ipv4_loopback_binding(unsigned short port) {
        WebSocketServer local;
        assert(local.start(port, 0, true, WebSocketLoopbackFamily::IPv4));
        sockaddr_in address{};
        socklen_t size = sizeof(address);
        assert(getsockname(
            local.listen_fd_, reinterpret_cast<sockaddr *>(&address), &size) == 0);
        assert(address.sin_family == AF_INET);
        assert(ntohl(address.sin_addr.s_addr) == INADDR_LOOPBACK);
        assert(local.listening_address() == "127.0.0.1");
        local.stop();
    }
    static void verify_auto_prefers_ipv6(unsigned short port) {
        WebSocketServer local;
        assert(local.start(port, 0, true, WebSocketLoopbackFamily::Auto));
        assert(local.listening_address() == "::1");
        local.stop();
    }
    static void verify_bounded_fallback(unsigned short port) {
        WebSocketServer local;
        local.startup_attempt_override_ = [](WebSocketLoopbackFamily family)
            -> std::optional<WebSocketServer::StartupAttempt> {
            if (family == WebSocketLoopbackFamily::IPv6)
                return WebSocketServer::StartupAttempt{
                    false,
                    WebSocketServer::StartupFailureStage::Socket,
                    EAFNOSUPPORT};
            return std::nullopt;
        };
        assert(local.start(port, 0, true, WebSocketLoopbackFamily::Auto));
        assert(local.listening_address() == "127.0.0.1");
        local.stop();

        local.startup_attempt_override_ = [](WebSocketLoopbackFamily family)
            -> std::optional<WebSocketServer::StartupAttempt> {
            if (family == WebSocketLoopbackFamily::IPv6)
                return WebSocketServer::StartupAttempt{
                    false,
                    WebSocketServer::StartupFailureStage::Bind,
                    EADDRNOTAVAIL};
            return std::nullopt;
        };
        assert(local.start(port, 0, true, WebSocketLoopbackFamily::Auto));
        assert(local.listening_address() == "127.0.0.1");
        local.stop();
    }
    static void verify_prohibited_fallback(unsigned short port) {
        WebSocketServer local;
        local.startup_attempt_override_ = [](WebSocketLoopbackFamily family)
            -> std::optional<WebSocketServer::StartupAttempt> {
            if (family == WebSocketLoopbackFamily::IPv6)
                return WebSocketServer::StartupAttempt{
                    false,
                    WebSocketServer::StartupFailureStage::Bind,
                    EADDRINUSE};
            return std::nullopt;
        };
        assert(!local.start(port, 0, true, WebSocketLoopbackFamily::Auto));
        assert(local.listen_fd_ == -1);
        assert(local.listening_address().empty());
        assert(!local.start(port, 0, true, WebSocketLoopbackFamily::IPv6));
        assert(local.listen_fd_ == -1);
        assert(!local.start(port, 0, false, WebSocketLoopbackFamily::IPv4));

        local.startup_attempt_override_ = [](WebSocketLoopbackFamily family)
            -> std::optional<WebSocketServer::StartupAttempt> {
            if (family == WebSocketLoopbackFamily::IPv6)
                return WebSocketServer::StartupAttempt{
                    false,
                    WebSocketServer::StartupFailureStage::Socket,
                    EAFNOSUPPORT};
            return std::nullopt;
        };
        assert(!local.start(port, 0, true, WebSocketLoopbackFamily::IPv6));
        assert(local.listen_fd_ == -1);
        assert(local.listening_address().empty());
    }
    static void verify_failed_bind_cleanup(unsigned short port) {
        WebSocketServer owner;
        WebSocketServer blocked;
        assert(owner.start(port, 0, true, WebSocketLoopbackFamily::IPv6));
        assert(!blocked.start(port, 0, true, WebSocketLoopbackFamily::Auto));
        assert(blocked.listen_fd_ == -1);
        assert(blocked.listening_address().empty());
        owner.stop();
        assert(blocked.start(port, 0, true, WebSocketLoopbackFamily::IPv6));
        blocked.stop();
    }
    static void verify_current_network_reauthorization(unsigned short port) {
        WebSocketServer local;
        std::atomic<int> phase{1};
        local.network_snapshot_provider_ = [&] {
            SupportRequestGuardSnapshot snapshot{true, "wsprrypi", {}, {}};
            if (phase.load(std::memory_order_acquire) == 1)
                snapshot.networks = {{"192.168.50.10", "255.255.255.0"}};
            else if (phase.load(std::memory_order_acquire) == 2)
                snapshot.networks = {{"10.20.30.2", "255.255.255.0"}};
            else if (phase.load(std::memory_order_acquire) == 3)
                snapshot.discovery_succeeded = false;
            return snapshot;
        };
        const WebSocketServer::ClientAuthorizationContext authorization{
            "127.0.0.1",
            "GET /socket HTTP/1.1\r\n"
            "Host: wsprrypi\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "X-WsprryPi-Client-Address: 192.168.50.42\r\n\r\n"};
        assert(local.clientAuthorizationCurrent(authorization));
        phase.store(0, std::memory_order_release);
        assert(!local.clientAuthorizationCurrent(authorization));
        phase.store(2, std::memory_order_release);
        assert(!local.clientAuthorizationCurrent(authorization));
        phase.store(3, std::memory_order_release);
        assert(!local.clientAuthorizationCurrent(authorization));
        phase.store(1, std::memory_order_release);
        assert(local.clientAuthorizationCurrent(authorization));

        // Exercise the same transition through a real upgraded socket.  A
        // message received after the eligible LAN disappears must close the
        // connection without dispatching a command response.
        initialize_privileged_network_runtime("enforced");
        phase.store(0, std::memory_order_release);
        assert(local.start(port, 0));
        expect_rejected_upgrade(port, "192.168.50.42");
        phase.store(1, std::memory_order_release);
        int inbound = connect_client(port, "192.168.50.42");
        send_text(inbound, "{\"command\":42}");
        char response[512]{};
        assert(recv(inbound, response, sizeof(response), 0) > 0);
        phase.store(0, std::memory_order_release);
        send_text(inbound, "{\"command\":42}");
        assert(wait_disconnected(inbound));
        close(inbound);
        assert(wait_zero(local));

        // The outbound path must also reauthorize before broadcasting.
        phase.store(1, std::memory_order_release);
        int outbound = connect_client(port, "192.168.50.42");
        assert(wait_client_count(local, 1));
        phase.store(2, std::memory_order_release);
        local.sendAllClients("{\"status\":\"must-not-send\"}");
        assert(wait_disconnected(outbound, "must-not-send"));
        close(outbound);
        assert(wait_zero(local));
        local.stop();
    }
    static void verify_stream_and_control_queue(unsigned short port) {
        WebSocketServer local;
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::string> messages;
        bool release = false;
        local.inbound_message_for_test_ = [&](const std::string &message) {
            std::unique_lock<std::mutex> lock(mutex);
            messages.push_back(message); cv.notify_all();
            if (message == "barrier") cv.wait(lock, [&]{ return release; });
        };
        assert(local.start(port, 0, true, WebSocketLoopbackFamily::IPv4));
        int fd = connect_client(port);
        const auto split = text_frame(std::string(1800, 'x'));
        // Separate reads in the middle of both an extended header and payload.
        send_bytes(fd, split.substr(0, 3));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        send_bytes(fd, split.substr(3, 401));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        { std::lock_guard<std::mutex> lock(mutex); assert(messages.empty()); }
        send_bytes(fd, split.substr(404) + text_frame("barrier"));
        {
            std::unique_lock<std::mutex> lock(mutex);
            assert(cv.wait_for(lock, std::chrono::seconds(2), [&]{return messages.size()==2;}));
            assert(messages.front() == std::string(1800, 'x'));
        }
        std::string backlog;
        for (int i=0;i<80;++i) backlog += text_frame(i % 2 == 0
            ? "{\"command\":\"GET_TX_STATE\"}" : "{\"command\":\"get_tx_state\"}");
        backlog += text_frame("{\"command\":\"tone_end\"}");
        backlog += text_frame("{\"command\":\"bounded_tone\"}");
        send_bytes(fd, backlog);
        shutdown(fd, SHUT_WR); // Drain complete queued commands after TCP FIN.
        {
            std::lock_guard<std::mutex> lock(mutex); release = true;
        }
        cv.notify_all();
        {
            std::unique_lock<std::mutex> lock(mutex);
            assert(cv.wait_for(lock, std::chrono::seconds(2), [&]{return messages.size()>=5;}));
            assert(messages.size()==5);
            assert(messages[2]=="{\"command\":\"get_tx_state\"}");
            assert(messages[3]=="{\"command\":\"tone_end\"}");
            assert(messages[4]=="{\"command\":\"bounded_tone\"}");
        }
        assert(wait_disconnected(fd)); close(fd); assert(wait_zero(local));
        // Reject a hostile 64-bit length before allocation or overflow.
        fd = connect_client(port);
        send_bytes(fd, std::string("\x81\xff\x7f\xff\xff\xff\xff\xff\xff\xff",10));
        assert(wait_disconnected(fd)); close(fd); assert(wait_zero(local));
        local.stop();
    }
public:
    static int run() {
        // The Test Tone lifecycle fixture uses 39526 and runs in parallel.
        verify_stream_and_control_queue(39527);
        verify_loopback_binding(39518);
        verify_ipv4_loopback_binding(39520);
        verify_auto_prefers_ipv6(39524);
        verify_bounded_fallback(39521);
        verify_prohibited_fallback(39522);
        verify_failed_bind_cleanup(39523);
        verify_current_network_reauthorization(39525);
        WebSocketServer s; verify_test_tone_transaction_lock(s); const unsigned short p=39519; assert(s.start(p,0));
        int raw=socket(AF_INET,SOCK_STREAM,0); assert(raw>=0); sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(p); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr); assert(connect(raw,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);
        assert(wait_handshake(s)); std::thread incomplete_stopper([&]{s.stop();}); incomplete_stopper.join(); close(raw);
        assert(s.active_client_handlers_==0 && s.client_sockets_.empty());
        assert(s.start(p,0));
        int invalid=connect_client(p); send_text(invalid,"{\"command\":42}"); char response[512]{}; auto response_size=recv(invalid,response,sizeof(response),0); assert(response_size>0); assert(std::string(response,response_size).find("command failure")!=std::string::npos); close(invalid); assert(wait_zero(s));
        int survivor=connect_client(p); close(survivor); assert(wait_zero(s));
        for(int i=0;i<8;++i) { int fd=connect_client(p); const unsigned char close_frame[]={0x88,0x80,0,0,0,0}; send(fd,close_frame,sizeof(close_frame),0); close(fd); assert(wait_zero(s)); }
        for(int i=0;i<8;++i) { int fd=connect_client(p); close(fd); assert(wait_zero(s)); }
        int blocked=connect_client(p); std::thread stopper([&]{s.stop();}); stopper.join(); close(blocked);
        assert(s.active_client_handlers_==0); assert(s.client_sockets_.empty()); s.stop();
        std::cout << "websocket lifecycle regression passed\n"; return 0;
    }
};
int main(){return WebSocketLifecycleTest::run();}
