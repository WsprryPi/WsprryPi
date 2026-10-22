/**
 * @file web_server.cpp
 * @brief Creates a threaded instance of httplib web server.
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

#ifndef WEB_SERVER_HPP
#define WEB_SERVER_HPP

#include "httplib.hpp"
#include "json.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <string>

/**
 * @class WebServer
 * @brief A simple web server that runs in a separate thread with CORS support.
 *
 * @details
 * The WebServer class creates an HTTP server that listens on a user-specified port.
 * It will not start until the start() method is called with a valid port number (1024 to 65535).
 * The server handles:
 *  - GET requests by sending a JSON response.
 *  - PUT and PATCH requests by parsing the request body as JSON, printing it for debugging,
 *    and returning a text response.
 *  - OPTIONS requests to support CORS preflight, setting the proper headers.
 *
 * The server runs in its own thread and can be stopped cleanly by calling stop(), which uses
 * condition variables for synchronization.
 */
class WebServer
{
public:
    /**
     * @brief Constructs a WebServer instance.
     *
     * The server does not start automatically. Call start(port) to launch the server.
     */
    WebServer();

    /**
     * @brief Destructor.
     *
     * Ensures that the server is stopped and the thread is joined.
     */
    ~WebServer();

    /**
     * @brief Starts the web server on the specified port.
     *
     * @details
     * Validates the port, launches the HTTP server in a separate thread, and waits until
     * the server is running.
     *
     * @param port The port number to listen on (must be between 1024 and 65535).
     * @throws std::invalid_argument if the port is outside the allowed range.
     */
    void start(int port);

    /**
     * @brief Stops the web server.
     *
     * @details
     * Signals the server to stop and waits (using condition variables) for the server thread
     * to exit cleanly.
     */
    void stop();

    /**
     * @brief Sets the priority for the server thread.
     *
     * @param schedPolicy The scheduling policy (e.g., SCHED_FIFO, SCHED_RR).
     * @param priority The priority level.
     * @return true if the priority was set successfully, false otherwise.
     */
    bool setThreadPriority(int schedPolicy, int priority);

private:
    int port_;                         ///< Port on which the server listens.
    httplib::Server svr;               ///< Underlying HTTP server from cpp-httplib.
    std::thread serverThread;          ///< Thread running the HTTP server.
    std::mutex mtx;                    ///< Mutex for synchronizing start/stop operations.
    std::condition_variable cvStarted; ///< Condition variable to signal when the server has started.
    bool running;                      ///< Flag indicating whether the server is running.

    /**
     * @brief Sets the CORS headers on the given HTTP response.
     *
     * @param res The HTTP response to modify.
     */
    void setCORSHeaders(httplib::Response &res);
};

/**
 * @brief Global instance of the WebServer class.
 * @details This `extern` declaration enables access to a shared instance of the
 *          WebServer throughout the application. Ensure that the instance is
 *          defined exactly once in a source file (e.g., `WebServer webServer;`).
 */
extern WebServer webServer;

#endif // WEB_SERVER_HPP
