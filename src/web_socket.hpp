/**
 * @file web_socket.hpp
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

#ifndef WEBSOCKET_HPP
#define WEBSOCKET_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "websocket_listener_config.hpp"
#include "support_request_guard.hpp"

/**
 * @brief Default keep-alive interval for WebSocket ping frames (in seconds).
 *
 * This constant defines how often the server will send a ping frame to the client
 * to maintain the connection. It can be overridden by passing a custom value
 * to `WebSocketServer::start()`.
 */
extern const uint32_t SOCKET_KEEPALIVE;

/**
 * @class WebSocketServer
 * @brief A minimal standalone WebSocket server supporting one client at a time.
 *
 * This class implements a threaded WebSocket server that listens for incoming
 * connections on a specified port. It handles the WebSocket handshake, message
 * decoding, keep-alive pinging, and sending text messages to the connected client.
 *
 * Only a single client is supported at any given time. The server is designed to be
 * used in standalone environments with no external dependencies.
 */
class WebSocketServer
{
    friend class WebSocketLifecycleTest;
    friend class TestToneLifecycleTest;
public:
    /**
     * @brief Constructs a new WebSocketServer instance.
     */
    WebSocketServer();

    /**
     * @brief Destroys the server and cleans up resources.
     */
    ~WebSocketServer();

    /**
     * @brief Starts the WebSocket server on the given port.
     *
     * The server will listen on the specified port and spawn a thread
     * to handle incoming connections. Optionally enables a periodic
     * keep-alive ping.
     *
     * @param port Port number to bind (must be between 1024 and 49151).
     * @param keep_alive_secs Keep-alive ping interval in seconds (0 disables ping).
     * @param loopback_only Restrict the listener to a literal loopback address.
     * @param loopback_family Family policy used only for a loopback listener.
     * @return true if the server started successfully.
     * @return false if startup failed (e.g. bind or listen error).
     */
    bool start(
        uint16_t port,
        uint32_t keep_alive_secs = 30,
        bool loopback_only = false,
        WebSocketLoopbackFamily loopback_family = WebSocketLoopbackFamily::Auto);

    /** Returns the exact bound literal while the listener is running. */
    const std::string &listening_address() const noexcept;

    /**
     * @brief Stops the server and closes any open connections.
     */
    void stop();

    /**
     * @brief Set scheduling policy and priority for internal threads.
     *
     * This applies the given scheduling policy and priority to both
     * the server and keep-alive threads (if active and joinable).
     *
     * @param schedPolicy Scheduling policy (e.g., SCHED_FIFO, SCHED_RR).
     * @param priority Thread priority (valid for the chosen policy).
     * @return true if all applicable threads were updated successfully.
     */
    bool setThreadPriority(int schedPolicy, int priority);

    /**
     * @brief Sends a text message to the connected client.
     *
     * The message is encoded as a WebSocket text frame.
     *
     * @param message The text string to send.
     */
    void sendToClient(const std::string &message);

    /**
     * @brief Broadcasts a text message to all connected WebSocket clients.
     *
     * Locks the clients mutex, constructs a WebSocket text frame for the
     * given message, and sends it to each socket in client_sockets_. Logs
     * any errors encountered during send().
     *
     * @param message The UTF-8 encoded text payload to broadcast.
     */
    void sendAllClients(const std::string &message);

private:
    struct ClientAuthorizationContext
    {
        std::string peer_address;
        std::string upgrade_request;
    };

    int listen_fd_;             ///< Socket file descriptor for listening.
    std::atomic<bool> running_; ///< Indicates whether the server is running.
    uint32_t keep_alive_secs_;  ///< Interval for keep-alive pings.

    // For each connection:
    std::mutex clients_mutex_;
    std::vector<int> client_sockets_;
    std::map<int, ClientAuthorizationContext> client_authorizations_;
    std::vector<int> handshaking_sockets_;
    std::size_t active_client_handlers_{0};
    bool client_registration_closed_{false};
    std::condition_variable client_handlers_cv_;
    std::recursive_mutex stop_mutex_;
    std::mutex test_tone_command_mutex_;
    std::mutex bounded_tone_mutex_;
    std::condition_variable bounded_tone_cv_;
    bool bounded_tone_stop_requested_{false};
    std::string bounded_tone_request_id_;
    std::thread bounded_tone_watchdog_;
    std::uint64_t bounded_tone_generation_{0};
    std::chrono::steady_clock::time_point bounded_tone_deadline_;
    // Typed, transient test seams; no production request/configuration surface.
    std::function<void()> bounded_expiry_ready_for_test_;
    bool fail_bounded_thread_creation_for_test_{false};
    std::function<void(const std::string &)> reply_ready_for_test_;
    std::function<void(const std::string &)> inbound_message_for_test_;
    void boundedToneLoop();
    void cancelBoundedTone();
    bool loopback_only_{false};
    std::string listening_address_;
    std::function<SupportRequestGuardSnapshot()> network_snapshot_provider_{
        [] { return SupportRequestGuard::discover_local_networks(); }};

    enum class StartupFailureStage
    {
        None,
        Socket,
        ReuseAddress,
        IPv6Only,
        Bind,
        Listen
    };

    struct StartupAttempt
    {
        bool started{false};
        StartupFailureStage stage{StartupFailureStage::None};
        int error_number{0};
    };

    std::function<std::optional<StartupAttempt>(WebSocketLoopbackFamily)>
        startup_attempt_override_;

    StartupAttempt startLoopbackCandidate(
        uint16_t port,
        WebSocketLoopbackFamily family);
    StartupAttempt startWildcard(uint16_t port);
    bool finishStart(uint32_t keep_alive_secs);
    void closeFailedListener() noexcept;
    static bool ipv6Unavailable(const StartupAttempt &attempt) noexcept;

    std::condition_variable keep_alive_cv_; ///< Conditional to break from loop
    std::mutex keep_alive_mutex_;           ///< Mutex to control loop break
    std::thread server_thread_;             ///< Thread running the main server loop.
    std::thread keep_alive_thread_;         ///< Thread for sending periodic pings.
    // std::mutex client_mutex_;       ///< Mutex to guard access to client socket.

    // Helper for per‐client work:
    void clientLoop(int client_fd, ClientAuthorizationContext authorization);

    /**
     * @brief Main server loop that accepts and manages the client connection.
     */
    void serverLoop();

    /**
     * @brief Loop for periodically sending WebSocket ping frames.
     *
     * Runs in a background thread while the server is active.
     *
     * @param interval Ping interval in seconds.
     */
    void keepAliveLoop(uint32_t interval);

    /**
     * @brief Performs the WebSocket handshake with the connected client.
     *
     * Validates the client's HTTP Upgrade request and sends the server response.
     *
     * @param client Socket descriptor for the client.
     * @return true if handshake succeeded, false otherwise.
     */
    bool performHandshake(
        int client,
        const std::string &peer_address,
        std::string &upgrade_request);
    [[nodiscard]] bool clientAuthorizationCurrent(
        const ClientAuthorizationContext &authorization) const;

    /**
     * @brief Trims whitespace from both ends of a string.
     *
     * Whitespace includes spaces, tabs, line feeds, and carriage returns.
     *
     * @param s The string to trim.
     * @return A new string with leading and trailing whitespace removed.
     */
    std::string trim(const std::string &s);

    /**
     * @brief Converts a string to lowercase (ASCII only).
     *
     * @param s Input string.
     * @return Lowercase version of the input string.
     */
    std::string toLower(const std::string &s);

    /**
     * @brief Handles incoming messages from the client.
     *
     * Processes text messages and dispatches actions based on content.
     *
     * @param message The decoded and unmasked client message.
     */
    void handleMessage(const std::string &message);

    /** Stops and joins any bounded-tone command owned by this server. */
    void stopBoundedTone();

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
    void sendJSON(const T &obj);

    /**
     * @brief Encodes binary data as a Base64 string.
     *
     * Used for computing the Sec-WebSocket-Accept header.
     *
     * @param data Pointer to binary input.
     * @param len Number of bytes to encode.
     * @return Base64-encoded string.
     */
    std::string base64Encode(const unsigned char *data, size_t len);

    /**
     * @brief Computes the WebSocket accept key from the client-provided key.
     *
     * Concatenates the GUID and applies SHA1 + Base64 encoding.
     *
     * @param client_key The value from Sec-WebSocket-Key header.
     * @return The Sec-WebSocket-Accept value to send back to the client.
     */
    std::string computeWebSocketAccept(const std::string &client_key);

    /**
     * @brief Decodes a WebSocket frame from raw socket data.
     *
     * Handles payload extraction, unmasking, and opcode parsing.
     *
     * @param data Pointer to input buffer (raw socket bytes).
     * @param data_len Length of input data in bytes.
     * @param frame_size Outputs the total size of the complete frame.
     * @param opcode Outputs the opcode of the WebSocket frame (e.g. 0x1 for text).
     * @return The unmasked payload string (text data if opcode is 0x1).
     */
    std::string decodeWebSocketFrame(const char *data, size_t data_len, size_t &frame_size, uint8_t &opcode);
};

/**
 * @brief Global instance of the WebSocket server.
 *
 * This external declaration allows other translation units to access
 * and control the WebSocketServer instance. It is typically defined in
 * a `.cpp` file to coordinate startup, shutdown, and message routing.
 */
extern WebSocketServer socketServer;

#endif // WEBSOCKET_HPP
