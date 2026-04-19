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

#include "config_handler.hpp"
#include "logging.hpp"
#include "sha1.hpp"
#include "scheduling.hpp"
#include "wspr_transmit.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
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
 * Creates an IPv6 socket configured to accept both IPv6 and IPv4 connections,
 * binds to the specified port, and begins listening for client connections.
 * Spawns the main server thread and, optionally, a keep-alive ping thread.
 *
 * @param port The TCP port to bind to (must be between 1024 and 49151).
 * @param keep_alive_secs Interval in seconds between keep-alive pings.
 *        Set to 0 to disable pinging.
 *
 * @return true if the server started successfully.
 * @return false if socket creation, binding, or listening fails.
 *
 * @note Uses IPv6 dual-stack by setting IPV6_V6ONLY to 0, allowing both IPv4
 *       and IPv6 clients to connect.
 */
bool WebSocketServer::start(uint16_t port, uint32_t keep_alive_secs)
{
    if (port < 1024 || port > 49151)
    {
        llog.logE(ERROR, "Port must be between 1024 and 49151: ", port);
        return false;
    }
    keep_alive_secs_ = keep_alive_secs;

    // Create an IPv6 socket.
    listen_fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        std::perror("socket");
        return false;
    }

    // Allow reuse of the address.
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::perror("setsockopt");
        return false;
    }

    // Disable IPV6_V6ONLY to accept both IPv6 and IPv4 connections.
    int off = 0;
    if (setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) < 0)
    {
        std::perror("setsockopt IPV6_V6ONLY");
        // Not a fatal error on some systems, so you may continue.
    }

    // Bind to the unspecified IPv6 address (this accepts all addresses)
    struct sockaddr_in6 addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any; // Accept connections from any IPv6 (and IPv4 via mapped addresses)
    addr.sin6_port = htons(port);
    if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::perror("bind");
        return false;
    }

    if (listen(listen_fd_, 10) < 0)
    {
        std::perror("listen");
        return false;
    }

    running_ = true;
    server_thread_ = std::thread(&WebSocketServer::serverLoop, this);
    if (keep_alive_secs_ > 0)
    {
        keep_alive_thread_ = std::thread(&WebSocketServer::keepAliveLoop, this, keep_alive_secs_);
    }

    llog.logS(INFO, "Socket server started on port: ", port);
    return true;
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
    if (!running_)
        return;

    // Disable the server loop
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

    // Shut down and close every client socket
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int fd : client_sockets_)
        {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    }

    // Join all client handler threads
    for (auto &t : client_threads_)
    {
        if (t.joinable())
            t.join();
    }
    client_threads_.clear();
    client_sockets_.clear();

    // Join the main server thread
    if (server_thread_.joinable())
        server_thread_.join();

    // Join the keep-alive ping thread
    if (keep_alive_thread_.joinable())
        keep_alive_thread_.join();

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
 * @brief Handles and dispatches incoming JSON client messages.
 *
 * This function parses the raw input text as JSON, extracts the
 * "command" field, and dispatches the request to the appropriate
 * stubbed action (shutdown, reboot, get_tx_state, or echo). It
 * trims and lowercases the incoming message to support case- and
 * whitespace-insensitive command parsing, recognizes supported
 * command keywords, and sends a JSON response back to the client
 * via sendJSON().
 *
 * - "tx_status" → Responds with transmission status acknowledgment.
 * - "shutdown"  → Responds with shutdown acknowledgment.
 * - "reboot"    → Responds with reboot acknowledgment.
 * - "stop"      → Stops active transmission and disables transmit persistently.
 *
 * All other messages are echoed back with a generic reply.
 *
 * @param raw_message The raw text message received from the client.
 */
void WebSocketServer::handleMessage(const std::string &raw_message)
{
    using json = nlohmann::json;
    json reply;

    try
    {
        // Parse incoming text as JSON
        auto j = json::parse(raw_message);

        // Extract "command" (defaults to empty string if missing), lowercase it
        std::string cmd = j.value("command", "");
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "shutdown")
        {
            llog.logS(INFO, "Received websocket shutdown command.");
            reply["command"] = "shutdown";
            shutdown_system();
        }
        else if (cmd == "reboot")
        {
            llog.logS(INFO, "Received websocket reboot command.");
            reply["command"] = "reboot";
            reboot_system();
        }
        else if (cmd == "stop")
        {
            llog.logS(INFO, "Received websocket stop command.");
            const StopTransmissionResult stop_result =
                stop_transmission_by_user_request();
            const bool stop_request_succeeded = stop_result.transmit_disabled;
            reply["command"] = "stop";
            reply["status"] = stop_request_succeeded ? "ok" : "error";
            reply["transmission_active"] = stop_result.transmission_active;
            reply["stop_performed"] = stop_result.stop_performed;
            reply["transmit_disabled"] = stop_result.transmit_disabled;
            reply["persisted"] = stop_result.persisted;
            reply["message"] = stop_result.message;
        }
        else if (cmd == "get_tx_state")
        {
            llog.logS(DEBUG, "Received JSON get_tx_state command.");
            const WsprRuntimeStatusSnapshot snapshot =
                current_tx_runtime_status_snapshot();
            reply["tx_state"] = snapshot.tx_state;
            reply["runtime_mode"] = snapshot.runtime_mode;
            reply["plan_type"] = snapshot.plan_type;
            reply["frame_count"] = snapshot.frame_count;
            reply["current_frame"] = snapshot.current_frame;
            reply["callsign_raw"] = snapshot.callsign_raw;
            reply["callsign_normalized"] = snapshot.callsign_normalized;
            reply["locator_raw"] = snapshot.locator_raw;
            reply["locator_normalized"] = snapshot.locator_normalized;
            reply["frame_callsign"] = snapshot.frame_callsign;
            reply["frame_locator"] = snapshot.frame_locator;
            reply["cw_message"] = snapshot.cw_message;
            reply["cw_active_char_index"] = snapshot.cw_active_char_index;
            auto now = std::chrono::system_clock::now();
            auto now_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_utc{};
            gmtime_r(&now_t, &tm_utc);
            std::ostringstream oss;
            oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
            reply["timestamp"] = oss.str();
        }
        else if (cmd == "tone_start")
        {
            llog.logS(DEBUG, "Received JSON test_tone_start command.");
            start_test_tone();
            reply["tone_start"] = "ok";
        }
        else if (cmd == "tone_end")
        {
            llog.logS(DEBUG, "Received JSON test_tone_stop command.");
            end_test_tone();
            reply["tone_end"] = "ok";
        }
        else if (cmd == "echo")
        {
            llog.logS(INFO, "Received JSON echo command.");
            if (j.contains("payload"))
            {
                // Echo back the provided payload verbatim
                reply["payload"] = j["payload"];
            }
            else
            {
                reply["message"] = "missing payload";
            }
        }
        else
        {
            llog.logS(WARN, "Unknown command received: " + cmd);
            reply["status"] = "error";
            reply["message"] = "unknown command";
            reply["command"] = cmd;
        }
    }
    catch (const json::parse_error &e)
    {
        // JSON was invalid
        llog.logE(ERROR, "JSON parse error in handleMessage: " + std::string(e.what()));
        reply["status"] = "error";
        reply["error"] = "invalid JSON";
    }

    // Send the JSON‐formatted reply
    sendJSON(reply);
}

/**
 * @brief Serialize a given object to JSON and send it to the connected client.
 *
 * This function converts the provided object to a JSON string using
 * nlohmann::json and transmits it over the active WebSocket connection.
 * The object type T must be compatible with nlohmann::json.
 *
 * @tparam T Type that can be converted to nlohmann::json.
 * @param obj The object to serialize and send to the client.
 *
 * @throws nlohmann::json::type_error If the object cannot be serialized.
 * @throws std::runtime_error If sending the serialized data fails.
 */
template <typename T>
void WebSocketServer::sendJSON(const T &obj)
{
    // T could be nlohmann::json or any structure you convert to it
    sendAllClients(obj.dump());
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
        ssize_t sent = ::send(fd, frame.data(), frame.size(), 0);
        if (sent < 0)
        {
            std::perror("send");
        }
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
        if (::send(fd, frame.data(), frame.size(), 0) < 0)
        {
            std::perror("sendAllClients");
        }
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

        // Perform WebSocket handshake
        if (!performHandshake(client))
        {
            llog.logE(WARN, "Handshake failed for new client");
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
            client_sockets_.push_back(client);
            client_threads_.emplace_back(
                &WebSocketServer::clientLoop,
                this,
                client);
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
void WebSocketServer::clientLoop(int client_fd)
{
    char buf[1024];
    bool connection_open = true;

    while (running_ && connection_open)
    {
        ssize_t bytes = recv(client_fd, buf, sizeof(buf), 0);
        if (bytes <= 0)
        {
            // TCP FIN or error
            break;
        }

        size_t pos = 0;
        while (pos < static_cast<size_t>(bytes))
        {
            size_t frame_size = 0;
            uint8_t opcode = 0;
            std::string message = decodeWebSocketFrame(
                buf + pos, bytes - pos, frame_size, opcode);
            if (frame_size == 0)
            {
                // Incomplete frame
                break;
            }
            pos += frame_size;

            switch (opcode)
            {
            case 0x1: // Text frame
                handleMessage(message);
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

    // Clean up this client
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        client_sockets_.erase(
            std::remove(client_sockets_.begin(),
                        client_sockets_.end(),
                        client_fd),
            client_sockets_.end());
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
                unsigned char ping[2] = {0x89, 0x00};
                if (::send(fd, ping, sizeof(ping), 0) < 0)
                {
                    std::perror("send ping");
                }
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
bool WebSocketServer::performHandshake(int client)
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
            break;
    }

    std::istringstream stream(request);
    std::string line;
    std::string key;

    while (std::getline(stream, line))
    {
        if (line.find("Sec-WebSocket-Key:") != std::string::npos)
        {
            size_t pos = line.find(":");
            if (pos != std::string::npos)
            {
                key = line.substr(pos + 1);
                key.erase(std::remove(key.begin(), key.end(), '\r'), key.end());
                key.erase(0, key.find_first_not_of(" \t"));
            }
            break;
        }
    }

    if (key.empty())
    {
        llog.logE(WARN, "Socket key not found in request.");
        return false;
    }

    std::string accept_key = computeWebSocketAccept(key);
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

    return true;
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
