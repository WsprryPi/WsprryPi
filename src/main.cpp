#include "udp_worker_image.hpp"
/**
 * @file main.cpp
 * @brief Entry point for the Wsprry Pi application.
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

// Primary header for this source file
#include "main.hpp"
#ifdef _WIN32
#include "native_windows_arguments.hpp"
#include "application_recovery/windows_service_entry.hpp"
#endif
#include "si5351_inventory_process.hpp"
#include "controller_runtime.hpp"
#include "controller_instance_lock.hpp"
#include "application_storage/controller_identity.hpp"
#include "controller_profile_entry.hpp"
#include "pi_managed_host_runtime.hpp"
#include "application_storage/state_entry.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/route_maintenance.hpp"
#include "application_control/local_entry.hpp"
#include "application_control/diagnostic_export.hpp"
#include "application_control/advanced_runtime.hpp"
#include "application_control/runtime_options.hpp"
#include "application_control/backend_services.hpp"
#include "application_control/browser_sessions.hpp"
#include "application_control/http_sessions.hpp"
#include "application_control/http_domain.hpp"
#include "application_control/web_presence.hpp"
#include "application_control/web_service.hpp"
#include "application_recovery/helper_image.hpp"
#include "application_recovery/backend_entry.hpp"
#include "application_recovery/maintenance_cli.hpp"
#include "application_recovery/maintenance_service.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"
#include "wtp_endpoint/identity.hpp"
#include "web_server_routes.hpp"

// Project headers
#include "arg_parser.hpp"
#include "gpio_output.hpp"
#ifdef DEBUG_WSPR
#include "qualification_gpio_test_mode.hpp"
#endif
#include "scheduling.hpp"
#include "signal_handler.hpp"
#include "version.hpp"
#include "logging.hpp"
#include "machine_power_control.hpp"
#include "singleton.hpp"

// Standard headers
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <thread>

// System headers
#ifndef _WIN32
#include <fcntl.h>  // fcntl()
#include <poll.h>
#include <unistd.h> // pipe(), read(), write()
#endif

/**
 * @brief TCP port used for singleton instance checking.
 *
 * This constant defines the port number used for checking if a singleton
 * instance of the application is already running. It allows the program to
 * prevent multiple instances from running concurrently.
 *
 * @note This feature may become redundant with `tcp_server` running.
 * @see tcp_server
 */
constexpr int SINGLETON_PORT = 1234;

// Global unique instance of SignalHandler.
SignalHandler signalHandler;

namespace
{
std::atomic<int> g_shutdown_exit_signal{0};
#ifndef _WIN32
std::atomic<int> g_async_shutdown_signal{0};
int g_async_shutdown_pipe[2] = {-1, -1};

void notify_async_shutdown_signal(int signum) noexcept
{
    if (signum != 0 && signum != SIGUSR1)
    {
        int expected = 0;
        (void)g_shutdown_exit_signal.compare_exchange_strong(
            expected,
            signum,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    g_async_shutdown_signal.store(signum, std::memory_order_release);

    const std::uint8_t wake = 1;
    if (g_async_shutdown_pipe[1] >= 0)
    {
        const ssize_t wake_result =
            ::write(g_async_shutdown_pipe[1], &wake, sizeof(wake));
        (void)wake_result;
    }
}

void async_shutdown_signal_handler(int signum) noexcept
{
    notify_async_shutdown_signal(signum);
}

bool install_async_shutdown_handlers()
{
    if (::pipe(g_async_shutdown_pipe) != 0)
    {
        return false;
    }

    for (int fd : g_async_shutdown_pipe)
    {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
        {
            (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = async_shutdown_signal_handler;
    sa.sa_flags = 0;

    constexpr int kSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    for (int signum : kSignals)
    {
        if (::sigaction(signum, &sa, nullptr) != 0)
        {
            return false;
        }
    }

    return true;
}

void close_async_shutdown_pipe() noexcept
{
    if (g_async_shutdown_pipe[0] >= 0)
    {
        ::close(g_async_shutdown_pipe[0]);
        g_async_shutdown_pipe[0] = -1;
    }

    if (g_async_shutdown_pipe[1] >= 0)
    {
        ::close(g_async_shutdown_pipe[1]);
        g_async_shutdown_pipe[1] = -1;
    }
}

void drain_async_shutdown_pipe() noexcept
{
    if (g_async_shutdown_pipe[0] < 0)
    {
        return;
    }

    std::uint8_t discard[32];
    while (::read(g_async_shutdown_pipe[0], discard, sizeof(discard)) > 0)
    {
    }
}

#endif
std::string shutdown_reason_for_signal(int signum)
{
    if (signum == 0)
    {
        return "external signal requested shutdown";
    }

    return std::string("received ") +
           std::string(SignalHandler::signalToString(signum)) +
           " signal";
}

} // namespace

/**
 * @brief Custom signal handling function.
 *
 * This function is called when a signal is received. It logs the signal and,
 * if critical, terminates immediately. Otherwise, it initiates a graceful shutdown.
 *
 * @param signum The signal number received.
 * @param is_critical Indicates whether the signal is critical.
 */
void callback_signal_handler(int signum, bool is_critical)
{
    if (!is_critical)
    {
#ifdef _WIN32
        // The native SignalHandler invokes this on its dedicated worker, not
        // an asynchronous CRT signal handler. No POSIX self-pipe is needed.
        if(signum){int expected=0;(void)g_shutdown_exit_signal.compare_exchange_strong(expected,signum);}
        request_wspr_shutdown(shutdown_reason_for_signal(signum));
#else
        notify_async_shutdown_signal(signum);
#endif
    }
    else
    {
        std::string_view signal_name = SignalHandler::signalToString(signum);
        std::cerr << "[FATAL] Critical signal received: " << signal_name << ". Performing immediate shutdown." << std::endl;
#ifdef _WIN32
        ::TerminateProcess(::GetCurrentProcess(),static_cast<UINT>(signum));
        std::abort();
#else
        std::quick_exit(signum);
#endif
    }
}

/**
 * @brief Entry point for the WsprryPi application.
 *
 * This function initializes the application, parses command-line arguments,
 * loads the INI configuration, verifies NTP synchronization, and starts the
 * main WSPR transmission loop. It also sets system performance modes and
 * handles signal management for graceful shutdown.
 *
 * @param argc The number of command-line arguments.
 * @param argv Array of C-style strings representing the arguments.
 * @return int Exit status: 0 on success, non-zero on failure.
 *
 * @note Ensure that NTP synchronization is stable before proceeding.
 *       If NTP verification fails, the program exits immediately.
 *       The log level is set to INFO by default, but can be changed
 *       via a macro or configuration option.
 */
static int application_main(int argc, char *argv[])
{
    if(const auto result=controller_profile_entry(argc,argv))return *result;
    if(const auto result=wsprrypi::recovery_helper_image_main(argc,argv))return *result;
    if(const auto result=wsprrypi::maintenance_service_main(argc,argv))return *result;
    if(const auto result=wsprrypi::web_service_main(argc,argv))return *result;
    if(const auto result=wsprrypi::local_command_main(argc,argv))return *result;
    if(const auto result=wsprrypi::diagnostic_export_main(argc,argv))return *result;
    if(const auto result=wsprrypi::recovery_backend_entry(argc,argv))return *result;
    if(const auto result=wsprrypi::maintenance_cli_main(argc,argv))return *result;
    if(const auto result=wsprrypi::state_command_main(argc,argv,SINGLETON_PORT))return *result;
    wsprrypi::BackendStateArguments state_arguments;
    try{state_arguments=wsprrypi::backend_state_arguments(argc,argv);}
    catch(const std::exception &error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
    argc=static_cast<int>(state_arguments.arguments.size())-1;argv=state_arguments.arguments.data();
    if(state_arguments.runtime_directory&&!state_arguments.directory){
        std::cerr<<"A backend runtime directory requires --state-directory.\n";return EXIT_FAILURE;
    }
    for(int index=1;index<argc;++index){
        const std::string_view option=argv[index];
        if(option=="-i"||option=="--ini-file"||option.starts_with("--ini-file=")||
            (option.starts_with("-i")&&!option.starts_with("--"))){
            std::cerr<<"INI input is available only through --migrate-state --ini-source; use --state-directory for saved settings.\n";
            return EXIT_FAILURE;
        }
    }
    const bool controller_only = controller_only_requested(argc, argv);
    // Role parsing must precede even diagnostic helpers: mixed invocations
    // cannot use an early hardware-inventory exit to evade role validation.
    // Export only the bundled diagnostic worker; no singleton or transmitter initialization.
    if (!controller_only) {
        if (const auto worker_exit = udp_worker_image_main(argc,argv)) return *worker_exit;
        // Inventory workers never acquire the application singleton or initialize output.
        if (const auto inventory_exit = si5351_inventory_process::helper_main(argc, argv))
            return *inventory_exit;
    }
    // Maintain retval for main()
    int retval = EXIT_SUCCESS;

#ifdef DEBUG_WSPR
    const char *qualification_gpio_test_mode =
        std::getenv("WSPRRYPI_QUALIFICATION_GPIO_TEST_MODE");
    if (qualification_gpio_test_mode_requested(qualification_gpio_test_mode))
    {
        GPIOOutput::setTestMode(true);
    }
#endif

#ifndef _WIN32
    if (!install_async_shutdown_handlers())
    {
        std::perror("sigaction/pipe");
        return EXIT_FAILURE;
    }

    std::thread async_shutdown_monitor(
        []
        {
            while (true)
            {
                struct pollfd pfd{};
                pfd.fd = g_async_shutdown_pipe[0];
                pfd.events = POLLIN;

                const int rc = ::poll(&pfd, 1, -1);
                if (rc < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    return;
                }

                if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                {
                    return;
                }

                if ((pfd.revents & POLLIN) == 0)
                {
                    continue;
                }

                drain_async_shutdown_pipe();

                const int signum =
                    g_async_shutdown_signal.exchange(0, std::memory_order_acq_rel);
                if (signum == SIGUSR1)
                {
                    return;
                }

                request_wspr_shutdown(shutdown_reason_for_signal(signum));
            }
        });

#endif

    // Register signal handlers for safe shutdown and terminal management.
    block_signals();
    signalHandler.setCallback(callback_signal_handler);
    signalHandler.start();
#ifdef _WIN32
    if(!signalHandler.running()){
        std::cerr<<"Native shutdown handling could not start.\n";return EXIT_FAILURE;
    }
#else
    if (!controller_only) signalHandler.setPriority(SCHED_RR, 40);
#endif

    bool simulated_backend = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string option(argv[i]);
        if ((option == "--backend" && i + 1 < argc &&
             std::string(argv[i + 1]) == "simulated") ||
            option == "--backend=simulated")
        {
            simulated_backend = true;
        }
    }

    if (simulated_backend || controller_only)
        set_hardware_platform_detection_enabled(false);

    // Sets up logger based on DEBUG flag: INFO or DEBUG
    initialize_logger();

    // Parse command line first allowing calls for -h or -v
    if (!controller_only) handle_early_cli_options(argc, argv);
    else {
        for (int i = 1; i < argc; ++i)
            if (std::string_view(argv[i]) == "--help" || std::string_view(argv[i]) == "-h")
                print_usage("Controller-only mode has no local transmitter; use --state-directory <directory> for controller state.", EXIT_SUCCESS);
    }

    // Preserve the pre-parse privilege boundary for every executable capable
    // of physical GPIO. Explicit simulation and the no-output controller role
    // bypass this legacy gate; neither can be selected after a startup failure.
#ifndef _WIN32
    if (getuid() != 0 && !simulated_backend && !controller_only &&
        build_has_physical_gpio_capability())
    {
        print_usage("This program must be run as root or with sudo.", EXIT_FAILURE);
    }

#endif
    // Enforce Singleton
    SingletonProcess singleton(SINGLETON_PORT);
    std::unique_ptr<InstallationLock> controller_instance;
    if (controller_only) {
        try {controller_instance=wsprrypi::acquire_controller_installation_lock();wsprrypi::configure_native_controller_identity();}
        catch(const std::exception &error){llog.logE(FATAL,error.what());std::exit(EXIT_FAILURE);}
    } else if (!singleton()) {
        llog.logE(FATAL, "Another instance is running on port: ", SINGLETON_PORT);
        std::exit(EXIT_FAILURE);
    }

    // Keep the managed owner alive until the scheduling loop has stopped all
    // workers. Recover durable input before the component first opens the INI.
    std::unique_ptr<wsprrypi::ApplicationStateSelection> selected_state;
    std::shared_ptr<wsprrypi::PiManagedHost> managed_host;
    struct BackendControlLifetime {
        ~BackendControlLifetime(){web_server_routes::shutdown_backend_control_owners();}
    } backend_control_lifetime;
    std::unique_ptr<wsprrypi::LocalControlServer> control_server;
    std::unique_ptr<wsprrypi::LocalControlServer> event_server;
    std::unique_ptr<wsprrypi::LocalControlServer> browser_server;
    std::unique_ptr<wsprrypi::LocalControlServer> http_server;
    std::unique_ptr<wsprrypi::LocalControlServer> web_presence_server;
    wsprrypi::BackendRequestDrain request_drain([&]{
        if(web_presence_server)web_presence_server->stop();
        if(http_server)http_server->stop();
        wsprrypi::stop_backend_http_domain();
        if(browser_server)browser_server->stop();
        if(event_server)event_server->stop();
        if(control_server)control_server->stop();
    });
    // Now do the full arguments check
    try
    {
        if(state_arguments.directory){
            auto database=wsprrypi::open_application_state(*state_arguments.directory,wsprrypi::wtp_pi_device_id(),
                controller_only?wsprrypi::StateRole::Controller:wsprrypi::StateRole::Pi);
            selected_state=std::make_unique<wsprrypi::ApplicationStateSelection>(database);
            if(wsprrypi::pi_managed_ini_argument(argc,argv))throw std::invalid_argument("Use --ini-source only with the explicit one-time migration command");
        }
        if(controller_only&&!selected_state)
            throw std::invalid_argument("Controller startup requires --state-directory");
        if (controller_only)
        {
            configure_controller_only(argc, argv);
        }
        else
        {
            if(wsprrypi::application_database_selected())
                managed_host=wsprrypi::prepare_pi_managed_host(wsprrypi::application_database());
            if(!parse_command_line(argc, argv))
                print_usage("Failure parsing command line.", EXIT_FAILURE);
            if(managed_host)managed_host->parsed();
        }
        if(selected_state){
            const auto database=wsprrypi::application_database();
            const auto boot=controller_only?prepare_controller_process_boot():wsprrypi::pi_managed_host_process_boot();
            wsprrypi::prepare_backend_web_bootstrap(database,boot);
            const auto handler=[database,boot](wsprrypi::LocalPeer peer,const wsprrypi::LocalRequest &request){
                return wsprrypi::backend_local_request(database,peer,request,boot);
            };
            auto options=wsprrypi::backend_control_options(state_arguments.runtime_directory);
            control_server=std::make_unique<wsprrypi::LocalControlServer>(options,boot,handler);
            options.channel=wsprrypi::LocalChannel::Events;
            event_server=std::make_unique<wsprrypi::LocalControlServer>(options,boot,handler);
            options.channel=wsprrypi::LocalChannel::Browser;
            browser_server=std::make_unique<wsprrypi::LocalControlServer>(options,boot,handler,
                [](wsprrypi::LocalPeer,const std::string &connection){wsprrypi::backend_browser_disconnected(connection);});
            options.channel=wsprrypi::LocalChannel::Http;
            http_server=std::make_unique<wsprrypi::LocalControlServer>(options,boot,handler,
                [](wsprrypi::LocalPeer,const std::string &connection){wsprrypi::backend_http_disconnected(connection);});
            options.channel=wsprrypi::LocalChannel::WebService;
            web_presence_server=std::make_unique<wsprrypi::LocalControlServer>(options,boot,handler,
                [](wsprrypi::LocalPeer,const std::string &connection){wsprrypi::backend_web_disconnected(connection);});
        }
    }
    catch (const std::exception &e)
    {
        // Handle any exceptions thrown during command-line parsing.
        std::string error_message = "Exception caught processing arguments: " + std::string(e.what());
        if (controller_only) {
            std::cerr << "Controller startup refused: " << e.what() << '\n';
            std::exit(EXIT_FAILURE);
        }
        print_usage(error_message, EXIT_FAILURE);
    }

    // GPIO-capable executables retain the legacy privilege boundary. A
    // GPIO-free Si5351 executable relies on the selected I2C device's ordinary
    // kernel permissions, and simulation requires no hardware privilege.
#ifndef _WIN32
    if (getuid() != 0 && !controller_only && transmit_backend_requires_root(config.transmit_backend))
    {
        print_usage("This program must be run as root or with sudo.", EXIT_FAILURE);
    }
#endif
    initialize_logger(
        config.use_journald,
        config.date_time_log,
        config.debug_logging);

#ifdef DEBUG_WSPR
    if (qualification_gpio_test_mode_requested(qualification_gpio_test_mode))
    {
        llog.logS(
            WARN,
            "Qualification GPIO test mode enabled: physical GPIO output requests, writes, and releases are suppressed. Hardware is not qualified.");
    }
#endif

    // Display version, Raspberry Pi model, and process ID after CLI parsing so
    // the first backend banner matches the requested logging mode.
    llog.logS(INFO, get_version_string());

    if (!config.use_journald)
    {
        llog.logS(DEBUG,
                  "Log timestamps:",
                  config.date_time_log ? "enabled" : "disabled");
    }

    if (controller_only)
        llog.logS(INFO, "Role: controller-only; no local transmitter output.");
    else
        llog.logS(INFO, "Platform: ", get_pi_model(), ".");

    llog.logS(
        INFO,
        "OS: ",
        get_os_version_name(),
        (sizeof(void *) == 8 ? " 64-bit" : " 32-bit"),
        ".");

#ifdef _WIN32
    llog.logS(DEBUG, "Process PID:", ::GetCurrentProcessId());
#else
    llog.logS(DEBUG, "Process PID:", getpid());
#endif

    // Re-assert the handled signal mask on the main runtime thread before
    // entering the long-lived scheduling loop.
    block_signals();

    // Startup WSPR loop
    try
    {
        if(control_server){
            wsprrypi::start_backend_services(config.web_port,webServer.loopback_fixture());
            socketServer.start_command_runtime(config.socket_loopback_only);
            control_server->start();
            event_server->start();
            browser_server->start();
            http_server->start();
            web_presence_server->start();
        }
        if (!wspr_loop())
        {
            retval = EXIT_FAILURE;
        }
    }
    catch (const std::exception &e)
    {
        llog.logE(ERROR, "Unhandled exception in main(): ", e.what());
        retval = EXIT_FAILURE;
    }
    catch (...)
    {
        llog.logE(ERROR, "Unknown fatal error in main().");
        retval = EXIT_FAILURE;
    }

    if(web_presence_server)web_presence_server->stop();
    if(http_server)http_server->stop();
    wsprrypi::stop_backend_http_domain();
    if(browser_server)browser_server->stop();
    socketServer.stop_command_runtime();
    if(event_server)event_server->stop();
    if(control_server)control_server->stop();
    wsprrypi::stop_backend_services();
    web_server_routes::retire_control_management();
    // Stop the SignalHandler.
    signalHandler.stop();

#ifndef _WIN32
    notify_async_shutdown_signal(SIGUSR1);
    if (async_shutdown_monitor.joinable())
    {
        async_shutdown_monitor.join();
    }
    close_async_shutdown_pipe();
#endif

    if (reboot_flag.load(std::memory_order_acquire))
    {
        if (machine_power_control_supported())
        {
            llog.logS(INFO, "Rebooting.");
            std::cerr << "[INFO ] Rebooting." << std::endl;
        }
        reboot_machine();
    }
    if (shutdown_flag.load(std::memory_order_acquire))
    {
        if (machine_power_control_supported())
        {
            llog.logS(INFO, "Shutting down.");
            std::cerr << "[INFO ] Shutting down." << std::endl;
        }
        shutdown_machine();
    }

    return retval;
}

int main(int argc,char *argv[]){
#ifdef _WIN32
    std::vector<std::string> native_arguments;std::vector<char *> native_argv;
    try{
        native_arguments=wsprrypi::native_windows_arguments();native_argv.reserve(native_arguments.size()+1);
        for(auto &argument:native_arguments)native_argv.push_back(argument.data());
        native_argv.push_back(nullptr);
        argc=static_cast<int>(native_arguments.size());argv=native_argv.data();
    }catch(...){std::cerr<<"Native command line could not be read.\n";return EXIT_FAILURE;}
#endif
#ifdef _WIN32
    if(const auto result=wsprrypi::windows_controller_service_entry(argc,argv,[](const std::vector<std::string> &arguments){
        auto values=arguments;std::vector<char *> pointers;for(auto &value:values)pointers.push_back(value.data());pointers.push_back(nullptr);
        return application_main(static_cast<int>(values.size()),pointers.data());
    },[]{request_wspr_shutdown("Windows service requested shutdown");}))return *result;
#endif
    return application_main(argc,argv);
}
