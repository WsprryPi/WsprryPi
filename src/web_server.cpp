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
#include "privileged_network_managed_ini.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"

#include "web_server_routes.hpp"
#include "wtp_integration/pi_pair_guarded_http.hpp"
#include "wtp_integration/pi_pair_runtime.hpp"
#include "wtp_integration/pi_pair_machine_backend.hpp"

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
void WebServer::bind_loopback_for_test() {
    std::lock_guard lock(mtx);
    if(running)throw std::logic_error("Cannot replace a running listener binding");
    loopback_fixture_=true;
}

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
            svr = std::make_unique<wsprrypi::PiPairGuardedHttpServer>(wsprrypi::pi_pair_admission());
        }
        if (controller_only_mode()) {
            // Match the shared browser adapter's complete-request body bound.
            svr->set_payload_max_length(32768);
            svr->set_read_timeout(5, 0);
            svr->set_write_timeout(5, 0);
        }
        if(wsprrypi::pi_managed_host_selected()){
            svr->set_read_timeout(5,0);svr->set_write_timeout(5,0);
        }
        if (!supportBundleJobManager_)
        {
            supportBundleJobManager_ = SupportBundleRuntime::create_production();
        }
        if (!privilegedNetworkAdmin_)
        {
            privilegedNetworkAdmin_ = std::make_unique<PrivilegedNetworkAdmin>(
                PrivilegedNetworkAdminPaths{config.ini_filename},run_privileged_network_command,managed_network_ini_io());
        }
        if (!rp1GpclkRouteService_)
        {
            rp1GpclkRouteService_ = &wsprrypi::productionRp1GpclkRouteService();
        }
    }

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
    // Exact pairing routes must precede the broad Control/1 dispatcher.
    web_server_routes::register_pairing(*svr,[this]{
        std::lock_guard lock(pairing_mutex_);
        return pairing_runtime_?pairing_runtime_->local_service():nullptr;
    },network_snapshot_provider_);
    web_server_routes::register_control(*svr, set_cors_headers,[this]{
        std::lock_guard lock(pairing_mutex_);return pairing_runtime_?pairing_runtime_->local_service():nullptr;
    },network_snapshot_provider_);
    try{start_pairing_supervisor();}catch(...){pairing_stopping_.store(true);/* Pairing remains unavailable. */}
    web_server_routes::register_privileged_admin(
        *svr, *privilegedNetworkAdmin_, *rp1GpclkRouteService_);
    web_server_routes::register_version(*svr, set_cors_headers);

    {
        std::lock_guard<std::mutex> lock(mtx);
        running = true;
        cvStarted.notify_one();  // Notify after all routes are ready.
    }

    svr->listen(loopback_fixture_?"127.0.0.1":"0.0.0.0", port_);

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
    // Cancel admission before joining HTTP handlers. The supervisor performs
    // network/maintenance joins and waits for accepted handler leases itself.
    pairing_stopping_.store(true);
    {std::lock_guard lock(pairing_mutex_);if(pairing_runtime_)pairing_runtime_->request_stop();}
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

    stop_pairing_supervisor();

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

void WebServer::start_pairing_supervisor(){
    if(pairing_thread_.joinable())throw std::logic_error("pairing_supervisor_already_started");
    pairing_stopping_.store(false);
    pairing_thread_=std::thread([this]{
        while(!pairing_stopping_.load()){
            try{
                const auto owner=wsprrypi::pi_pair_owner();
                const auto boot=controller_only_mode()?controller_only_process_boot():wsprrypi::pi_managed_host_process_boot();
                if(owner&&!boot.empty()&&isListening()){
                    wsprrypi::wtp_pi_publish_management_http(this,owner->self(),static_cast<unsigned>(port_),[this]{return !pairing_stopping_.load()&&isListening();});
                    const auto backend=web_server_routes::pairing_machine_backend();
                    const std::function<bool()> web_ready=[this]{return !pairing_stopping_.load()&&isListening()&&
                        (controller_only_mode()?controller_only_status().value("controller_ready",false):!wsprrypi::pi_managed_host_held());};
                    const wsprrypi::PiPairMachineService::Targets targets=[backend](const auto &principal,const auto &cancelled){return backend->targets(principal,cancelled);};
                    const wsprrypi::PiPairMachineService::Target target=[backend](const auto &principal,const auto &request,const auto &cancelled){return backend->request(principal,request,cancelled);};
                    // The existing explicit web-fixture seam also isolates its
                    // management listener: loopback/ephemeral, no native DNS-SD.
                    const wsprrypi::PiPairRuntime::RemoteFactory remote=[](auto history){return web_server_routes::pairing_remote_controller(std::move(history));};
                    const wsprrypi::PiPairRuntime::Handover handover=[](auto &history,const auto &cancelled){
                        if(const auto host=wsprrypi::pi_managed_host()){
                            wsprrypi::PiPairRetirementClient client(history,wsprrypi::pi_pair_admission());
                            host->advance_remote_handover(client,cancelled);
                            if(!cancelled())host->reclaim_pairing_retirements(history);
                        }
                    };
                    auto runtime=loopback_fixture_?
                        std::make_shared<wsprrypi::PiPairRuntime>(owner,boot,static_cast<unsigned>(port_),web_ready,targets,target,wsprrypi::PiPairRuntime::LoopbackTest{},remote,handover):
                        std::make_shared<wsprrypi::PiPairRuntime>(owner,boot,static_cast<unsigned>(port_),web_ready,targets,target,remote,handover);
                    {
                        std::lock_guard lock(pairing_mutex_);
                        if(pairing_stopping_.load())break;
                        pairing_runtime_=runtime;
                        if(!runtime->start()){pairing_runtime_.reset();throw std::runtime_error("pairing_supervisor_unavailable");}
                    }
                    while(!pairing_stopping_.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    runtime->stop();
                    break;
                }
            }catch(...){/* Pairing holds never select another role or output owner. */}
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}
void WebServer::stop_pairing_supervisor() noexcept{
    pairing_stopping_.store(true);
    {std::lock_guard lock(pairing_mutex_);if(pairing_runtime_)pairing_runtime_->request_stop();}
    if(pairing_thread_.joinable())pairing_thread_.join();
    wsprrypi::wtp_pi_withdraw_management_http(this);
    std::lock_guard lock(pairing_mutex_);pairing_runtime_.reset();
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
