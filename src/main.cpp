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
#include "si5351_inventory_process.hpp"

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
#include "wspr_transmit.hpp"

// Standard headers
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <thread>

// System headers
#include <fcntl.h>  // fcntl()
#include <poll.h>
#include <unistd.h> // pipe(), read(), write()

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
std::atomic<int> g_async_shutdown_signal{0};
std::atomic<int> g_shutdown_exit_signal{0};
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
        notify_async_shutdown_signal(signum);
    }
    else
    {
        std::string_view signal_name = SignalHandler::signalToString(signum);
        std::cerr << "[FATAL] Critical signal received: " << signal_name << ". Performing immediate shutdown." << std::endl;
        std::quick_exit(signum);
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
int main(int argc, char *argv[])
{
    // Inventory workers never acquire the application singleton or initialize output.
    if (const auto inventory_exit = si5351_inventory_process::helper_main(argc, argv))
        return *inventory_exit;
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

    // Register signal handlers for safe shutdown and terminal management.
    block_signals();
    signalHandler.setCallback(callback_signal_handler);
    signalHandler.start();
    signalHandler.setPriority(SCHED_RR, 40);

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

    if (simulated_backend)
        set_hardware_platform_detection_enabled(false);

    // Sets up logger based on DEBUG flag: INFO or DEBUG
    initialize_logger();

    // Parse command line first allowing calls for -h or -v
    handle_early_cli_options(argc, argv);

    // Preserve the pre-parse privilege boundary for every executable capable
    // of physical GPIO. Only an explicit simulation bypasses this legacy gate.
    if (getuid() != 0 && !simulated_backend &&
        build_has_physical_gpio_capability())
    {
        print_usage("This program must be run as root or with sudo.", EXIT_FAILURE);
    }

    // Enforce Singleton
    SingletonProcess singleton(SINGLETON_PORT);
    if (!singleton())
    {
        llog.logE(FATAL, "Another instance is running on port: ", SINGLETON_PORT);
        std::exit(EXIT_FAILURE);
    }

    // Now do the full arguments check
    try
    {
        if (!parse_command_line(argc, argv))
        {
            print_usage("Failure parsing command line.", EXIT_FAILURE);
        }
    }
    catch (const std::exception &e)
    {
        // Handle any exceptions thrown during command-line parsing.
        std::string error_message = "Exception caught processing arguments: " + std::string(e.what());
        print_usage(error_message, EXIT_FAILURE);
    }

    // GPIO-capable executables retain the legacy privilege boundary. A
    // GPIO-free Si5351 executable relies on the selected I2C device's ordinary
    // kernel permissions, and simulation requires no hardware privilege.
    if (getuid() != 0 && transmit_backend_requires_root(config.transmit_backend))
    {
        print_usage("This program must be run as root or with sudo.", EXIT_FAILURE);
    }

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

    llog.logS(
        INFO,
        "Platform: ",
        get_pi_model(),
        ".");

    llog.logS(
        INFO,
        "OS: ",
        get_os_version_name(),
        (sizeof(void *) == 8 ? " 64-bit" : " 32-bit"),
        ".");

    llog.logS(DEBUG, "Process PID:", getpid());

    // Re-assert the handled signal mask on the main runtime thread before
    // entering the long-lived scheduling loop.
    block_signals();

    // Startup WSPR loop
    try
    {
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

    // Stop the SignalHandler.
    signalHandler.stop();

    notify_async_shutdown_signal(SIGUSR1);
    if (async_shutdown_monitor.joinable())
    {
        async_shutdown_monitor.join();
    }
    close_async_shutdown_pipe();

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
