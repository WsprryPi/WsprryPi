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

#include "web_server.hpp"

#include "web_server_routes.hpp"

#include "config_handler.hpp"
#include "httplib.hpp"
#include "logging.hpp"
#include "privileged_network_admin.hpp"
#include "rp1_gpclk_route_service.hpp"
#include "support_bundle_http.hpp"
#include "support_bundle_intake_production.hpp"
#include "support_bundle_runtime.hpp"

/**
 * @brief Global instance of the WebServer class.
 * @details This `declaration enables access to a shared instance of the
 *          WebServer throughout the application. Ensure that the instance is
 *          defined exactly once in a source file (e.g., `WebServer
 * webServer;`).
 */
WebServer webServer;

/**
 * @brief Default constructor for the WebServer class.
 *
 * @details
 * Initializes the WebServer object by setting the listening port to 0 and the
 * running flag to false.
 */
WebServer::WebServer() : port_(0), running(false) {}

/**
 * @brief Destructor for the WebServer class.
 *
 * @details
 * Ensures that the web server is stopped and any associated resources are
 * released before the object is destroyed.
 */
WebServer::~WebServer() { stop(); }

bool WebServer::isListening()
{
    std::lock_guard<std::mutex> lock(mtx);
    return svr && svr->is_running();
}

/**
 * @brief Sets the Cross-Origin Resource Sharing (CORS) headers on the HTTP
 * response.
 *
 * @details
 * Advertises method and content headers without granting a cross-origin
 * origin. Browser operation uses the same-origin Apache proxy; protected
 * responses never receive a wildcard origin policy.
 *
 * @param res The HTTP response object on which to set the CORS headers.
 */
void WebServer::setCORSHeaders(httplib::Response &res)
{
    res.headers.erase("Access-Control-Allow-Origin");
    res.set_header("Access-Control-Allow-Methods",
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Accept");
}

/**
 * @brief Starts the web server on the specified port.
 *
 * @details
 * Validates the port number to ensure it falls within the allowed non-root
 * range (1024–49151). If the server is not already running, it launches the
 * server on a new thread and sets up HTTP handlers for GET, PUT, PATCH, and
 * OPTIONS requests. Uses condition variables and mutex locking to ensure safe
 * startup and avoid race conditions.
 *
 * The GET handler returns a simple JSON response.
 * The PUT and PATCH handlers parse incoming JSON and print it to stdout.
 * CORS headers are applied to all relevant responses.
 *
 * @param port The TCP port to bind the server to.
 * @throws std::invalid_argument If the port is outside the allowed range.
 */
void WebServer::start(int port)
{
    // Validate port range for non-root processes.
    if (port < 1024 || port > 49151)
    {
        throw std::invalid_argument("Port must be between 1024 and 49151.");
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        if (running)
        {
            // Server is already running; skip reinitialization.
            return;
        }
        port_ = port;
        if (!svr)
        {
            svr = std::make_unique<httplib::Server>();
        }
        if (!supportBundleJobManager_)
        {
            supportBundleJobManager_ = SupportBundleRuntime::create_production();
        }
        if (!privilegedNetworkAdmin_)
        {
            privilegedNetworkAdmin_ = std::make_unique<PrivilegedNetworkAdmin>(
                PrivilegedNetworkAdminPaths{config.ini_filename});
        }
        if (!rp1GpclkRouteService_)
        {
            rp1GpclkRouteService_ = &wsprrypi::productionRp1GpclkRouteService();
        }
    }

    llog.logS(INFO, "Web server started on port: ", config.web_port);

    // Launch the server in a separate thread.
    serverThread = std::thread([this]()
                               {

    if (!supportBundleRoutesRegistered_)
    {
        register_support_bundle_http_routes(
            *svr,
            *supportBundleJobManager_,
            [this] { return network_snapshot_provider_(); },
            resolve_support_bundle_intake_production);
        supportBundleRoutesRegistered_ = true;
    }

    web_server_routes::register_request_guard(
        *svr, network_snapshot_provider_);
    const web_server_routes::CorsHeaderSetter set_cors_headers =
        [this](httplib::Response &response)
        {
            setCORSHeaders(response);
        };
    web_server_routes::register_config(*svr, set_cors_headers);
    web_server_routes::register_control(*svr, set_cors_headers);
    web_server_routes::register_privileged_admin(
        *svr, *privilegedNetworkAdmin_, *rp1GpclkRouteService_);
    web_server_routes::register_version(*svr, set_cors_headers);

    {
        std::lock_guard<std::mutex> lock(mtx);
        running = true;
        cvStarted.notify_one();  // Notify after all routes are ready.
    }

    // Accept connections from any network interface.
    svr->listen("0.0.0.0", port_);

    // Reset running flag once the server stops.
    {
      std::lock_guard<std::mutex> lock(mtx);
      running = false;
    } });

    // Wait until the server signals it has successfully started.
    {
        std::unique_lock<std::mutex> lock(mtx);
        cvStarted.wait(lock, [this]
                       { return running; });
    }
}

/**
 * @brief Stops the web server and joins the server thread.
 *
 * @details
 * If the server is currently running, this method signals the HTTP server
 * to shut down cleanly using `svr.stop()`. It then joins the background
 * server thread to ensure resources are released properly and the thread
 * exits cleanly.
 *
 * Uses a mutex to safely check the running state. If the server is already
 * stopped, the method returns immediately without action.
 */
void WebServer::stop()
{
    bool was_running = false;
    {
        std::lock_guard<std::mutex> lock(mtx);
        was_running = running;
    }

    if (was_running)
    {
        // Stop accepting requests before cancelling any support-bundle work.
        svr->stop();
    }

    // Wait for route handling to finish before the manager can be shut down.
    if (serverThread.joinable())
    {
        serverThread.join();
    }

    if (supportBundleJobManager_)
    {
        supportBundleJobManager_->shutdown();
    }

    // A restart must not retain handlers that captured a shut-down manager.
    // Destroy the server (and its routes) before releasing the manager.
    svr.reset();
    supportBundleJobManager_.reset();
    supportBundleRoutesRegistered_ = false;
}

/**
 * @brief Sets the priority for the server thread.
 *
 * @param schedPolicy The scheduling policy (e.g., SCHED_RR, SCHED_FIFO).
 * @param priority The priority level.
 * @return true if the priority was set successfully, false otherwise.
 */
bool WebServer::setThreadPriority(int schedPolicy, int priority)
{
    bool success = true;
    sched_param sch_params;
    sch_params.sched_priority = priority;

    if (serverThread.joinable())
    {
        int ret = pthread_setschedparam(serverThread.native_handle(), schedPolicy,
                                        &sch_params);
        if (ret != 0)
        {
            std::perror("pthread_setschedparam (serverThread)");
            success = false;
        }
    }
    else
    {
        llog.logE(ERROR, "Server thread is not joinable. Cannot set priority.");
        success = false;
    }
    return success;
}
