/**
 * @file web_socket.cpp
 * @brief A simple websocket server for Wsprry Pi server support.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "web_socket.hpp"
#include "support_request_guard.hpp"
#include "privileged_network_runtime.hpp"
#include "websocket_upgrade_guard.hpp"

#include "logging.hpp"
#include "sha1.hpp"
#include "json.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>


// Connect with wscat -c ws://localhost:31416

/**
 * @brief Global instance of the WebSocket server.
 *
 * This declaration allows other translation units to access and control
 * the WebSocketServer instance. It is typically defined in a `.cpp` file
 * to coordinate startup, shutdown, and message routing.
 */
WebSocketServer socketServer;

/**
 * @brief Default keep-alive interval for WebSocket ping frames (in seconds).
 *
 * This constant defines how often the server will send a ping frame to the client
 * to maintain the connection. It can be overridden by passing a custom value
 * to `WebSocketServer::start()`.
 */
constexpr const uint32_t SOCKET_KEEPALIVE = 30;

/**
 * @brief Constructs a new WebSocketServer instance.
 *
 * Initializes all internal members:
 * - Sets the listening socket and client socket to -1 (invalid).
 * - Sets the running flag to false.
 * - Sets the default keep-alive interval to 30 seconds.
 */
WebSocketServer::WebSocketServer()
    : listen_fd_(-1), running_(false), keep_alive_secs_(SOCKET_KEEPALIVE)
{
}

/**
 * @brief Destroys the WebSocketServer instance.
 *
 * Calls stop() to ensure that the server is properly shut down and
 * all resources (threads, sockets) are cleaned up.
 */
WebSocketServer::~WebSocketServer()
{
    stop();
}

/**
 * @brief Starts the WebSocket server on a specified port.
 *
 * Creates either the existing IPv6 wildcard listener or a literal loopback
 * listener under the requested family policy, then begins accepting clients.
 * Spawns the main server thread and, optionally, a keep-alive ping thread.
 *
 * @param port The TCP port to bind to (must be between 1024 and 49151).
 * @param keep_alive_secs Interval in seconds between keep-alive pings.
 *        Set to 0 to disable pinging.
 *
 * @return true if the server started successfully.
 * @return false if socket creation, binding, or listening fails.
 *
 * @note The wildcard listener retains its IPv6 dual-stack behavior. A loopback
 *       listener binds exactly `::1` or `127.0.0.1`.
 */
void WebSocketServer::closeFailedListener() noexcept
{
    if (listen_fd_ >= 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    listening_address_.clear();
}

WebSocketServer::StartupAttempt WebSocketServer::startLoopbackCandidate(
    uint16_t port,
    WebSocketLoopbackFamily family)
{
    if (startup_attempt_override_)
    {
        const auto overridden = startup_attempt_override_(family);
        if (overridden)
            return *overridden;
    }

    const bool ipv6 = family == WebSocketLoopbackFamily::IPv6;
    const int address_family = ipv6 ? AF_INET6 : AF_INET;
    listen_fd_ = socket(address_family, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        return {false, StartupFailureStage::Socket, errno};

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        const int error_number = errno;
        closeFailedListener();
        return {false, StartupFailureStage::ReuseAddress, error_number};
    }

    if (ipv6)
    {
        if (setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
        {
            const int error_number = errno;
            closeFailedListener();
            return {false, StartupFailureStage::IPv6Only, error_number};
        }
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_loopback;
        address.sin6_port = htons(port);
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            const int error_number = errno;
            closeFailedListener();
            return {false, StartupFailureStage::Bind, error_number};
        }
        listening_address_ = "::1";
    }
    else
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            const int error_number = errno;
            closeFailedListener();
            return {false, StartupFailureStage::Bind, error_number};
        }
        listening_address_ = "127.0.0.1";
    }

    if (listen(listen_fd_, 10) < 0)
    {
        const int error_number = errno;
        closeFailedListener();
        return {false, StartupFailureStage::Listen, error_number};
    }
    return {true, StartupFailureStage::None, 0};
}

WebSocketServer::StartupAttempt WebSocketServer::startWildcard(uint16_t port)
{
    listen_fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        return {false, StartupFailureStage::Socket, errno};

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        const int error_number = errno;
        closeFailedListener();
        return {false, StartupFailureStage::ReuseAddress, error_number};
    }

    int off = 0;
    if (setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) < 0)
        std::perror("setsockopt IPV6_V6ONLY");

    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    address.sin6_port = htons(port);
    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        const int error_number = errno;
        closeFailedListener();
        return {false, StartupFailureStage::Bind, error_number};
    }
    listening_address_ = "::";

    if (listen(listen_fd_, 10) < 0)
    {
        const int error_number = errno;
        closeFailedListener();
        return {false, StartupFailureStage::Listen, error_number};
    }
    return {true, StartupFailureStage::None, 0};
}

bool WebSocketServer::ipv6Unavailable(const StartupAttempt &attempt) noexcept
{
    if (attempt.stage == StartupFailureStage::Socket)
    {
        return attempt.error_number == EAFNOSUPPORT ||
#ifdef EPROTONOSUPPORT
               attempt.error_number == EPROTONOSUPPORT ||
#endif
#ifdef EPFNOSUPPORT
               attempt.error_number == EPFNOSUPPORT ||
#endif
               false;
    }
    return attempt.stage == StartupFailureStage::Bind &&
           attempt.error_number == EADDRNOTAVAIL;
}

bool WebSocketServer::finishStart(uint32_t keep_alive_secs)
{
    keep_alive_secs_ = keep_alive_secs;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_registration_closed_ = false;
    }
    running_ = true;
    try
    {
        server_thread_ = std::thread(&WebSocketServer::serverLoop, this);
        if (keep_alive_secs_ > 0)
            keep_alive_thread_ = std::thread(&WebSocketServer::keepAliveLoop, this, keep_alive_secs_);
    }
    catch (const std::system_error &error)
    {
        llog.logE(ERROR, "Unable to start WebSocket listener threads: ", error.what());
        stop();
        return false;
    }
    return true;
}

bool WebSocketServer::start(
    uint16_t port,
    uint32_t keep_alive_secs,
    bool loopback_only,
    WebSocketLoopbackFamily loopback_family)
{
    std::lock_guard<std::recursive_mutex> start_lock(stop_mutex_);
    if (port < 1024 || port > 49151)
    {
        llog.logE(ERROR, "Port must be between 1024 and 49151: ", port);
        return false;
    }
    if (running_ || server_thread_.joinable() || keep_alive_thread_.joinable() || listen_fd_ >= 0)
    {
        llog.logE(ERROR, "Socket server is already started.");
        return false;
    }
    loopback_only_ = loopback_only;

    StartupAttempt attempt;
    if (!loopback_only)
    {
        if (loopback_family != WebSocketLoopbackFamily::Auto)
        {
            llog.logE(ERROR, "A WebSocket loopback family requires loopback-only mode.");
            return false;
        }
        attempt = startWildcard(port);
    }
    else if (loopback_family == WebSocketLoopbackFamily::IPv4)
    {
        attempt = startLoopbackCandidate(port, WebSocketLoopbackFamily::IPv4);
    }
    else
    {
        attempt = startLoopbackCandidate(port, WebSocketLoopbackFamily::IPv6);
        if (!attempt.started && loopback_family == WebSocketLoopbackFamily::Auto &&
            ipv6Unavailable(attempt))
        {
            llog.logS(WARN, "IPv6 loopback unavailable; falling back to 127.0.0.1.");
            attempt = startLoopbackCandidate(port, WebSocketLoopbackFamily::IPv4);
        }
    }

    if (!attempt.started)
    {
        errno = attempt.error_number;
        std::perror("WebSocket listener startup");
        return false;
    }

    if (!finishStart(keep_alive_secs))
        return false;

    const std::string endpoint_address =
        listening_address_.find(':') != std::string::npos
            ? "[" + listening_address_ + "]"
            : listening_address_;
    const std::string endpoint = endpoint_address + ":" + std::to_string(port);
    llog.logS(
        INFO,
        "WebSocket listener started on ", endpoint,
        loopback_only ? " (loopback only)." : ".");
    return true;
}

const std::string &WebSocketServer::listening_address() const noexcept
{
    return listening_address_;
}

/**
 * @brief Stops the WebSocket server and releases all resources.
 *
 * Gracefully shuts down the server by:
 * - marking the server as not running
 * - notifying the keep-alive thread to wake up
 * - closing the listening socket to unblock accept()
 * - shutting down and closing all active client sockets
 * - joining all client handler threads
 * - joining the main server and keep-alive threads
 *
 * After this call, the server may be restarted via start().
 */
void WebSocketServer::stop()
{
    std::lock_guard<std::recursive_mutex> stop_lock(stop_mutex_);
    if (!running_ && !server_thread_.joinable() && !keep_alive_thread_.joinable())
        return;

    running_ = false;

    // Wake keep-alive thread if sleeping
    keep_alive_cv_.notify_all();

    // Close listening socket to break out of accept()
    if (listen_fd_ != -1)
    {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }

    // Close the registration gate and unblock all sockets owned by the accept
    // loop or handlers.  Socket owners perform the final close exactly once.
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_registration_closed_ = true;
        for (int fd : client_sockets_)
            shutdown(fd, SHUT_RDWR);
        for (int fd : handshaking_sockets_)
            shutdown(fd, SHUT_RDWR);
    }

    stopBoundedTone();

    // Detached handlers release their own thread resources on exit.  Do not
    // destroy server state until every handler has observed shutdown and left.
    {
        std::unique_lock<std::mutex> lock(clients_mutex_);
        client_handlers_cv_.wait(lock, [this] { return active_client_handlers_ == 0; });
        client_sockets_.clear();
        client_authorizations_.clear();
        handshaking_sockets_.clear();
    }

    // Join the main server thread
    if (server_thread_.joinable())
        server_thread_.join();

    // Join the keep-alive ping thread
    if (keep_alive_thread_.joinable())
        keep_alive_thread_.join();

    listening_address_.clear();

}

/**
 * @brief Trims whitespace from both ends of a string.
 *
 * This method removes all leading and trailing whitespace characters
 * (spaces, tabs, newlines, carriage returns) from the input string.
 *
 * @param s The string to trim.
 * @return A new string with whitespace removed from the beginning and end.
 */
std::string WebSocketServer::trim(const std::string &s)
{
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start)))
    {
        ++start;
    }

    auto end = s.end();
    do
    {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(static_cast<unsigned char>(*end)));

    return std::string(start, end + 1);
}

/**
 * @brief Converts all characters in a string to lowercase.
 *
 * This utility performs ASCII-only lowercase conversion by applying
 * std::tolower to each character in the input string.
 *
 * @param s The string to convert.
 * @return A lowercase version of the input string.
 */
std::string WebSocketServer::toLower(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
    return result;
}

/**
 * @brief Computes the Sec-WebSocket-Accept response key for a WebSocket handshake.
 *
 * This function implements the server-side handshake response logic defined
 * by the WebSocket protocol (RFC 6455). It appends the fixed magic GUID to the
 * client's Sec-WebSocket-Key, computes the SHA1 hash of the result, and then
 * encodes that hash in Base64.
 *
 * @param client_key The value received in the client's Sec-WebSocket-Key header.
 * @return A Base64-encoded SHA1 hash string suitable for the Sec-WebSocket-Accept header.
 */
std::string WebSocketServer::computeWebSocketAccept(const std::string &client_key)
{
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concatenated = client_key + magic;
    SHA1 sha;
    sha.update(concatenated);
    std::string sha1_result = sha.final(); // 20-byte binary hash
    return base64Encode(reinterpret_cast<const unsigned char *>(sha1_result.data()), sha1_result.size());
}


/**
 * @brief Decodes a WebSocket frame from raw socket data.
 *
 * This function parses a WebSocket frame according to RFC 6455.
 * It extracts the payload length, masking key (if present),
 * opcode, and unmasked payload data. It handles both standard,
 * extended 16-bit, and 64-bit payload lengths.
 *
 * @param data Pointer to the buffer containing raw frame bytes.
 * @param data_len Number of bytes available in the buffer.
 * @param frame_size Output parameter set to the total number of bytes consumed.
 * @param opcode Output parameter set to the frame's opcode (e.g., 0x1 = text).
 *
 * @return A decoded and unmasked message string, or an empty string on failure/incomplete frame.
 *
 * @note This function assumes that frames are received in full. Partial frames
 *       should be accumulated before calling this function.
 */
std::string WebSocketServer::decodeWebSocketFrame(const char *data, size_t data_len, size_t &frame_size, uint8_t &opcode)
{
    if (data_len < 2)
    {
        frame_size = 0;
        return "";
    }

    constexpr std::uint64_t maximum_payload = 64U * 1024U;
    uint8_t byte1 = static_cast<uint8_t>(data[0]);
    opcode = byte1 & 0x0F;
    // bool fin = byte1 & 0x80; // Currently unused
    uint8_t byte2 = static_cast<uint8_t>(data[1]);
    bool mask = byte2 & 0x80;
    uint64_t payload_length = byte2 & 0x7F;

    size_t pos = 2;

    if (payload_length == 126)
    {
        if (data_len < pos + 2)
        {
            frame_size = 0;
            return "";
        }
        payload_length = (static_cast<uint8_t>(data[pos]) << 8) |
                         static_cast<uint8_t>(data[pos + 1]);
        pos += 2;
    }
    else if (payload_length == 127)
    {
        if (data_len < pos + 8)
        {
            frame_size = 0;
            return "";
        }
        payload_length = 0;
        for (int i = 0; i < 8; i++)
        {
            payload_length = (payload_length << 8) |
                             static_cast<uint8_t>(data[pos + i]);
        }
        pos += 8;
    }

    // Bound length arithmetic before allocating or awaiting a payload.
    if (payload_length > maximum_payload)
        throw std::runtime_error("WebSocket frame exceeds the 64 KiB limit.");

    if (mask)
    {
        if (data_len < pos + 4)
        {
            frame_size = 0;
            return "";
        }

        uint8_t masking_key[4] = {
            static_cast<uint8_t>(data[pos]),
            static_cast<uint8_t>(data[pos + 1]),
            static_cast<uint8_t>(data[pos + 2]),
            static_cast<uint8_t>(data[pos + 3])};
        pos += 4;

        if (data_len < pos + payload_length)
        {
            frame_size = 0;
            return "";
        }

        std::string message;
        message.resize(payload_length);
        for (uint64_t i = 0; i < payload_length; i++)
        {
            message[i] = data[pos + i] ^ masking_key[i % 4];
        }

        frame_size = pos + payload_length;
        return message;
    }
    else
    {
        // Unmasked frame (should not happen from clients per RFC)
        if (data_len < pos + payload_length)
        {
            frame_size = 0;
            return "";
        }

        std::string message(data + pos, payload_length);
        frame_size = pos + payload_length;
        return message;
    }
}

/**
 * @brief Encodes binary data into a Base64-encoded string.
 *
 * This minimal Base64 encoder processes input bytes in 3-byte chunks,
 * converting each to 4 ASCII characters from the standard Base64 alphabet.
 * Padding (`=`) is added as necessary when the input is not a multiple of 3 bytes.
 *
 * @param data Pointer to the input byte buffer.
 * @param len  Number of bytes to encode.
 *
 * @return A Base64-encoded string representation of the input data.
 *
 * @note This implementation does not insert line breaks or headers.
 *       It is sufficient for WebSocket key encoding and similar purposes.
 */
std::string WebSocketServer::base64Encode(const unsigned char *data, size_t len)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        int val = data[i] << 16;

        if (i + 1 < len)
        {
            val |= data[i + 1] << 8;
        }
        if (i + 2 < len)
        {
            val |= data[i + 2];
        }

        encoded.push_back(table[(val >> 18) & 0x3F]);
        encoded.push_back(table[(val >> 12) & 0x3F]);
        encoded.push_back(i + 1 < len ? table[(val >> 6) & 0x3F] : '=');
        encoded.push_back(i + 2 < len ? table[val & 0x3F] : '=');
    }

    return encoded;
}

/**
 * @brief Sends a text message to all connected WebSocket clients.
 *
 * Constructs a WebSocket text frame (RFC 6455) using the given message,
 * then iterates over the active client sockets and transmits the frame.
 * If no clients are connected, this is a no-op. Errors on individual
 * sends are logged via perror().
 *
 * @param message The UTF-8 encoded text message to send.
 */
void WebSocketServer::sendToClient(const std::string &message)
{
    // Lock the clients list while building and sending the frame
    std::lock_guard<std::mutex> lock(clients_mutex_);

    // Build the WebSocket text frame header
    std::string frame;
    frame.reserve(2 + message.size());
    frame.push_back(static_cast<char>(0x81)); // FIN=1, opcode=0x1 (text)

    size_t len = message.size();
    if (len < 126)
    {
        frame.push_back(static_cast<char>(len));
    }
    else if (len < 65536)
    {
        frame.push_back(126);
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
        }
    }

    // Append payload
    frame.append(message);

    // Broadcast to every connected client
    for (int fd : client_sockets_)
    {
        const auto authorization = client_authorizations_.find(fd);
        if (authorization == client_authorizations_.end() ||
            !clientAuthorizationCurrent(authorization->second))
        {
            shutdown(fd, SHUT_RDWR);
            continue;
        }
        const auto sent = ::send(fd, frame.data(), frame.size(), MSG_DONTWAIT
#ifdef MSG_NOSIGNAL
                                 | MSG_NOSIGNAL
#endif
        );
        if (sent != static_cast<ssize_t>(frame.size())) shutdown(fd, SHUT_RDWR);
    }
}

/**
 * @brief Broadcasts a text message to all connected WebSocket clients.
 *
 * Locks the clients mutex, constructs a WebSocket text frame for the
 * given message, and sends it to each socket in client_sockets_. Logs
 * any errors encountered during send().
 *
 * @param message The UTF-8 encoded text payload to broadcast.
 */
void WebSocketServer::sendAllClients(const std::string &message)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    const std::size_t client_count = client_sockets_.size();

    if (client_count == 0U)
    {
        llog.logS(DEBUG, "Dropping websocket broadcast; no connected clients.");
        return;
    }

    llog.logS(
        DEBUG,
        "Broadcasting websocket payload to ",
        static_cast<int>(client_count),
        " client(s).");

    // Build WebSocket text frame header
    std::string frame;
    frame.reserve(2 + message.size());
    frame.push_back(static_cast<char>(0x81)); // FIN=1, opcode=0x1 (text)

    size_t len = message.size();
    if (len < 126)
    {
        frame.push_back(static_cast<char>(len));
    }
    else if (len < 65536)
    {
        frame.push_back(126);
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
        }
    }

    // Append the payload
    frame.append(message);

    // Send to every connected client
    for (int fd : client_sockets_)
    {
        const auto authorization = client_authorizations_.find(fd);
        if (authorization == client_authorizations_.end() ||
            !clientAuthorizationCurrent(authorization->second))
        {
            shutdown(fd, SHUT_RDWR);
            continue;
        }
        // A slow/disconnected observer must not block lifecycle cleanup or a
        // transmitter join. A partial frame cannot be resumed by another send;
        // close that connection and let its owner retire it.
        const auto sent = ::send(fd, frame.data(), frame.size(), MSG_DONTWAIT
#ifdef MSG_NOSIGNAL
                                 | MSG_NOSIGNAL
#endif
        );
        if (sent != static_cast<ssize_t>(frame.size())) shutdown(fd, SHUT_RDWR);
    }
}

/**
 * @brief Main server loop that accepts and manages client connections.
 *
 * This function runs in its own thread once the server is started.
 * It continuously listens for new TCP client connections using `accept()`.
 * For each accepted client it:
 *  1. Performs the WebSocket handshake.
 *  2. Logs the peer’s IP address.
 *  3. Spawns a dedicated thread (`clientLoop`) to service that client.
 */
void WebSocketServer::serverLoop()
{
    while (running_)
    {
        sockaddr_storage peer_addr;
        socklen_t addrlen = sizeof(peer_addr);
        int client = accept(
            listen_fd_,
            reinterpret_cast<sockaddr *>(&peer_addr),
            &addrlen);
        if (client < 0)
        {
            if (!running_)
                break; // stop() was called
            std::perror("accept");
            continue;
        }

#ifdef LOCAL_ONLY
        // Optionally reject non-localhost connections
        if (peer_addr.ss_family == AF_INET)
        {
            auto *s4 = reinterpret_cast<sockaddr_in *>(&peer_addr);
            if (std::string(inet_ntoa(s4->sin_addr)) != "127.0.0.1")
            {
                llog.logE(WARN, "Rejected non-localhost connection");
                close(client);
                continue;
            }
        }
#endif

        // The accept loop owns this descriptor until it becomes a registered
        // handler.  Publish it before the blocking handshake so stop() can
        // unblock it without closing concurrently.
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (client_registration_closed_)
            {
                close(client);
                continue;
            }
            handshaking_sockets_.push_back(client);
            client_handlers_cv_.notify_all();
        }

        char peer_ip[INET6_ADDRSTRLEN]{};
        if (peer_addr.ss_family == AF_INET)
        {
            const auto *s4 = reinterpret_cast<sockaddr_in *>(&peer_addr);
            inet_ntop(AF_INET, &s4->sin_addr, peer_ip, sizeof(peer_ip));
        }
        else if (peer_addr.ss_family == AF_INET6)
        {
            const auto *s6 = reinterpret_cast<sockaddr_in6 *>(&peer_addr);
            inet_ntop(AF_INET6, &s6->sin6_addr, peer_ip, sizeof(peer_ip));
        }

        std::string upgrade_request;
        if (!performHandshake(client, peer_ip, upgrade_request))
        {
            llog.logE(WARN, "Handshake failed for new client");
            std::lock_guard<std::mutex> lock(clients_mutex_);
            handshaking_sockets_.erase(std::remove(handshaking_sockets_.begin(), handshaking_sockets_.end(), client), handshaking_sockets_.end());
            close(client);
            continue;
        }

        // Log the peer’s real IP
        char ipstr[INET6_ADDRSTRLEN];
        if (peer_addr.ss_family == AF_INET)
        {
            auto *s4 = reinterpret_cast<sockaddr_in *>(&peer_addr);
            inet_ntop(AF_INET, &s4->sin_addr, ipstr, sizeof(ipstr));
        }
        else
        {
            auto *s6 = reinterpret_cast<sockaddr_in6 *>(&peer_addr);
            inet_ntop(AF_INET6, &s6->sin6_addr, ipstr, sizeof(ipstr));
        }
        llog.logS(DEBUG, "Client connected from: ", ipstr);

        // Store and spawn handler thread
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            handshaking_sockets_.erase(std::remove(handshaking_sockets_.begin(), handshaking_sockets_.end(), client), handshaking_sockets_.end());
            if (client_registration_closed_)
            {
                close(client);
                continue;
            }
            client_sockets_.push_back(client);
            const ClientAuthorizationContext authorization{
                peer_ip, std::move(upgrade_request)};
            client_authorizations_.emplace(client, authorization);
            ++active_client_handlers_;
            try
            {
                std::thread(
                    &WebSocketServer::clientLoop, this, client,
                    authorization).detach();
            }
            catch (const std::system_error &e)
            {
                llog.logE(ERROR, "Could not create WebSocket client handler: ", e.what());
                --active_client_handlers_;
                client_sockets_.erase(std::remove(client_sockets_.begin(), client_sockets_.end(), client), client_sockets_.end());
                client_authorizations_.erase(client);
                shutdown(client, SHUT_RDWR);
                close(client);
                client_handlers_cv_.notify_all();
            }
        }
    }
}

/**
 * @brief Per‐client service loop.
 *
 * Continuously reads WebSocket frames from a single client socket,
 * decodes them, dispatches text frames to handleMessage(), and
 * handles control frames (close, ping/pong). Cleans up the socket
 * and removes it from the active clients list on exit.
 *
 * @param client_fd File descriptor of the accepted client socket.
 */
void WebSocketServer::clientLoop(
    int client_fd,
    ClientAuthorizationContext authorization)
{
    struct HandlerExit
    {
        WebSocketServer *server;
        int fd;
        ~HandlerExit()
        {
            std::lock_guard<std::mutex> lock(server->clients_mutex_);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            server->client_sockets_.erase(std::remove(server->client_sockets_.begin(), server->client_sockets_.end(), fd), server->client_sockets_.end());
            server->client_authorizations_.erase(fd);
            --server->active_client_handlers_;
            server->client_handlers_cv_.notify_all();
        }
    } handler_exit{this, client_fd};
    char buf[4096];
    std::string buffered;
    struct Frame { uint8_t opcode; std::string message; bool status_query; };
    std::deque<Frame> pending;
    std::size_t pending_bytes = 0;
    constexpr std::size_t maximum_buffered = 128U * 1024U;
    bool connection_open = true;
    bool input_closed = false;
    const auto is_status_query = [this](uint8_t opcode, const std::string &message) {
        if (opcode != 0x1) return false;
        const auto value = nlohmann::json::parse(message, nullptr, false);
        return value.is_object() && value.contains("command") &&
               value["command"].is_string() &&
               toLower(value["command"].get<std::string>()) == "get_tx_state";
    };

    try
    {
    while (running_ && connection_open)
    {
        // Read available input before dispatching another queued command. Status
        // observations can be slower than polling clients; retain only the latest
        // consecutive observation so End/STOP cannot sit behind stale queries.
        ssize_t bytes = -1;
        if (!input_closed)
        {
            bytes = recv(client_fd, buf, sizeof(buf),
                         pending.empty() ? 0 : MSG_DONTWAIT);
            if (bytes == 0) input_closed = true;
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
        }
        if (input_closed && pending.empty()) break;
        if (!clientAuthorizationCurrent(authorization))
        {
            llog.logS(INFO,
                "Closing WebSocket client because its current network "
                "authorization is no longer valid.");
            break;
        }
        if (bytes > 0) buffered.append(buf, static_cast<std::size_t>(bytes));
        if (buffered.size() + pending_bytes > maximum_buffered)
            throw std::runtime_error("WebSocket command buffer exceeded its limit.");
        std::size_t consumed = 0;
        while (consumed < buffered.size())
        {
            size_t frame_size = 0;
            uint8_t opcode = 0;
            std::string message = decodeWebSocketFrame(
                buffered.data() + consumed, buffered.size() - consumed,
                frame_size, opcode);
            if (frame_size == 0) break; // Keep incomplete bytes for the next recv.
            consumed += frame_size;
            const bool status_query = is_status_query(opcode, message);
            if (status_query && !pending.empty() && pending.back().status_query)
            {
                pending_bytes -= pending.back().message.size();
                pending.pop_back();
            }
            pending_bytes += message.size();
            pending.push_back({opcode, std::move(message), status_query});
            if (pending.size() > 1024)
                throw std::runtime_error("WebSocket command count exceeded its limit.");
        }
        buffered.erase(0, consumed);
        if (pending.empty()) continue;
        Frame frame = std::move(pending.front());
        pending.pop_front();
        pending_bytes -= frame.message.size();
        const auto opcode = frame.opcode;
        const auto &message = frame.message;

        switch (opcode)
        {
        case 0x1: // Text frame
            if (inbound_message_for_test_) inbound_message_for_test_(message);
            else handleMessage(message);
            break;

        case 0x8: // Close frame
        {
            llog.logS(DEBUG, "Received Close frame from fd: ",
                      client_fd);
            const char close_resp[] = {static_cast<char>(0x88), 0x00};
            send(client_fd, close_resp, sizeof(close_resp), 0);
            connection_open = false;
        }
        break;

        case 0x9: // Ping frame
        {
            // llog.logS(DEBUG, "Received Ping; sending Pong to fd: ", client_fd);
            const unsigned char pong[2] = {0x8A, 0x00};
            send(client_fd, pong, sizeof(pong), 0);
        }
        break;

        case 0xA: // Pong frame
            // llog.logS(DEBUG, "Received Pong from fd: ", client_fd);
            break;

        default:
            llog.logS(WARN, "Unhandled opcode", static_cast<int>(opcode),
                      "from fd: ", client_fd);
        }
    }
    }
    catch (const std::exception &error)
    {
        llog.logE(
            ERROR,
            "WebSocket client handler failed: " +
                std::string(error.what()));
    }
    catch (...)
    {
        llog.logE(ERROR, "Unknown WebSocket client handler failure.");
    }

    llog.logS(DEBUG, "Client handler thread exiting for fd: ", client_fd);
}

/**
 * @brief Background loop for sending periodic WebSocket ping frames.
 *
 * Runs in a dedicated thread when keep-alive is enabled.
 * Waits on a condition variable for the specified interval
 * or until stop() notifies. On timeout, sends a WebSocket
 * ping (opcode 0x9) to each connected client.
 *
 * @param interval Number of seconds between ping frames.
 */
void WebSocketServer::keepAliveLoop(uint32_t interval)
{
    std::unique_lock<std::mutex> lock(keep_alive_mutex_);
    while (running_)
    {
        auto status = keep_alive_cv_.wait_for(
            lock, std::chrono::seconds(interval));
        if (status == std::cv_status::timeout)
        {
            std::lock_guard<std::mutex> clients_lock(
                clients_mutex_);
            for (int fd : client_sockets_)
            {
                const auto authorization = client_authorizations_.find(fd);
                if (authorization == client_authorizations_.end() ||
                    !clientAuthorizationCurrent(authorization->second))
                {
                    shutdown(fd, SHUT_RDWR);
                    continue;
                }
                unsigned char ping[2] = {0x89, 0x00};
                const auto sent = ::send(fd, ping, sizeof(ping), MSG_DONTWAIT
#ifdef MSG_NOSIGNAL
                                         | MSG_NOSIGNAL
#endif
                );
                if (sent != static_cast<ssize_t>(sizeof(ping))) shutdown(fd, SHUT_RDWR);
            }
        }
    }
}

/**
 * @brief Performs the WebSocket handshake with a newly connected client.
 *
 * This function reads the client's HTTP Upgrade request, extracts the
 * `Sec-WebSocket-Key` header, and sends a proper HTTP 101 Switching Protocols
 * response with the `Sec-WebSocket-Accept` field.
 *
 * The handshake follows the WebSocket protocol as defined in RFC 6455.
 *
 * @param client The socket file descriptor for the accepted client connection.
 * @return true if the handshake succeeds and the client is upgraded to a WebSocket connection.
 * @return false if the handshake fails (invalid request, no key, send failure).
 *
 * @note If the handshake fails, this method does not close the socket.
 *       The caller is responsible for cleanup.
 */
bool WebSocketServer::performHandshake(
    int client,
    const std::string &peer_address,
    std::string &upgrade_request)
{
    const int buf_size = 4096;
    char buf[buf_size];
    std::string request;

    // Read until the end of the HTTP headers.
    while (request.find("\r\n\r\n") == std::string::npos)
    {
        ssize_t bytes = recv(client, buf, buf_size - 1, 0);
        if (bytes <= 0)
            return false;
        buf[bytes] = '\0';
        request.append(buf);
        if (request.size() > 4096)
            return false;
    }

    const auto guard = evaluate_websocket_upgrade(
        request, peer_address, network_snapshot_provider_(),
        active_privileged_network_mode());
    if (guard.decision != WebSocketUpgradeGuardDecision::allowed)
    {
        if (guard.decision == WebSocketUpgradeGuardDecision::malformed)
            llog.logS(DEBUG, "WebSocket upgrade rejected: malformed request.");
        else if (guard.rejection_reason ==
                 SupportRequestGuardDecision::invalid_trusted_proxy_identity)
            llog.logS(WARN, "WebSocket upgrade rejected: invalid trusted-proxy identity.");
        else if (guard.rejection_reason == SupportRequestGuardDecision::rejected_host)
            llog.logS(DEBUG, "WebSocket upgrade rejected: invalid Host.");
        else if (guard.rejection_reason == SupportRequestGuardDecision::rejected_origin)
            llog.logS(DEBUG, "WebSocket upgrade rejected: invalid Origin.");
        else if (guard.rejection_reason == SupportRequestGuardDecision::no_eligible_network)
            llog.logS(DEBUG, "WebSocket upgrade rejected: enforced safety has no eligible LAN.");
        else if (guard.rejection_reason ==
                 SupportRequestGuardDecision::interface_discovery_unavailable)
            llog.logS(DEBUG, "WebSocket upgrade rejected: current network discovery failed.");
        else
            llog.logS(DEBUG, "WebSocket upgrade rejected: client network is not eligible.");
        const char *status = guard.decision == WebSocketUpgradeGuardDecision::rejected
            ? "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"
            : "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        (void)send(client, status, std::strlen(status), 0);
        return false;
    }

    std::string accept_key = computeWebSocketAccept(guard.key);
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_key << "\r\n\r\n";
    std::string response_str = response.str();

    if (send(client, response_str.c_str(), response_str.size(), 0) < 0)
    {
        std::perror("send handshake");
        return false;
    }

    upgrade_request = std::move(request);
    return true;
}

bool WebSocketServer::clientAuthorizationCurrent(
    const ClientAuthorizationContext &authorization) const
{
    return evaluate_websocket_upgrade(
               authorization.upgrade_request,
               authorization.peer_address,
               network_snapshot_provider_(),
               active_privileged_network_mode()).decision ==
           WebSocketUpgradeGuardDecision::allowed;
}

/**
 * @brief Set scheduling policy and priority for internal threads.
 *
 * This applies the given scheduling policy and priority to both
 * the server and keep-alive threads (if active and joinable).
 *
 * @param schedPolicy Scheduling policy (e.g., SCHED_RR, SCHED_FIFO).
 * @param priority Thread priority (valid for the chosen policy).
 * @return true if all applicable threads were updated successfully.
 */
bool WebSocketServer::setThreadPriority(int schedPolicy, int priority)
{
    bool success = true;
    sched_param sch_params;
    sch_params.sched_priority = priority;

    if (server_thread_.joinable())
    {
        int ret = pthread_setschedparam(server_thread_.native_handle(), schedPolicy, &sch_params);
        if (ret != 0)
        {
            std::perror("pthread_setschedparam (server_thread_)");
            success = false;
        }
    }

    if (keep_alive_thread_.joinable())
    {
        int ret = pthread_setschedparam(keep_alive_thread_.native_handle(), schedPolicy, &sch_params);
        if (ret != 0)
        {
            std::perror("pthread_setschedparam (keep_alive_thread_)");
            success = false;
        }
    }

    return success;
}
