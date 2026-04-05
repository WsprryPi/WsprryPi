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

// Project headers
#include "arg_parser.hpp"
#include "scheduling.hpp"
#include "signal_handler.hpp"
#include "version.hpp"
#include "logging.hpp"
#include "singleton.hpp"
#include "wspr_transmit.hpp"

// Standard headers
#include <atomic>
#include <string_view>

// System headers
#include <fcntl.h>  // fcntl()
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
        request_wspr_shutdown({});
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
    // Maintain retval for main()
    int retval = EXIT_SUCCESS;

    // Register signal handlers for safe shutdown and terminal management.
    block_signals();
    signalHandler.setCallback(callback_signal_handler);
    signalHandler.start();
    signalHandler.setPriority(SCHED_RR, 40);

    // Sets up logger based on DEBUG flag: INFO or DEBUG
    initialize_logger();

    // Parse command line first allowing calls for -h or -v
    handle_early_cli_options(argc, argv);

    // Make sure we are running as root
    if (getuid() != 0)
    {
        print_usage("This program must be run as root or with sudo.", EXIT_FAILURE);
    }

    // Enforce Singleton
    SingletonProcess singleton(SINGLETON_PORT);
    if (!singleton())
    {
        llog.logE(FATAL, "Another instance is running on port:", SINGLETON_PORT);
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

    initialize_logger(config.use_journald, config.date_time_log);

    // Display version, Raspberry Pi model, and process ID after CLI parsing so
    // the first backend banner matches the requested logging mode.
    llog.logS(INFO, get_version_string());

    if (!config.use_journald)
    {
        llog.logS(INFO,
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

    llog.logS(INFO, "Process PID:", getpid());

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

    return retval;
}
