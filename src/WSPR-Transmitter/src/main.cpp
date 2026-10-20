/**
 * @file main.cpp
 * @brief A test rig for the WSPR-Transmitter class
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
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

// C++ standard library headers
#include <array>              // std::array for signal list
#include <chrono>             // std::chrono
#include <condition_variable> // g_end_cv
#include <csignal>            // sigaction, std::signal
#include <cstring>            // strsignal()
#include <cstdlib>            // setenv()
#include <cstdio>             // getchar()
#include <iomanip>            // std::ostringstream
#include <iostream>           // std::cout, std::getline
#include <syncstream>         // std::osyncstream
#include <mutex>              // g_end_mtx
#include <optional>           // std::optional
#include <string>             // std::string

// POSIX and system headers
#include <errno.h>      // errno
#include <sys/select.h> // select(), fd_set, FD_* macros
#include <sys/time.h>   // struct timeval
#include <termios.h>    // tcgetattr(), tcsetattr()
#include <unistd.h>     // STDIN_FILENO

// Project headers
#include "config_handler.hpp"
#include "utils.hpp"
#include "wspr_reference_adapter.hpp"
#include "wspr_transmit.hpp"

#define SELFTEST

static constexpr auto log_tag_chars = make_log_tag_chars("Test-Transmit");
static constexpr std::string_view log_tag = as_string_view(log_tag_chars);

static constexpr std::string_view CALLSIGN = "AA0NT";
static constexpr std::string_view GRID = "EM18";
static constexpr uint8_t POWER_DBM = 20;

// Frequency choices - Leave alone (see below)
constexpr double WSPR_CENTER_FREQ_2200M = 137500.0;
constexpr double WSPR_CENTER_FREQ_2200M_15 = 137612.5;
constexpr double WSPR_CENTER_FREQ_630M = 475700.0;
constexpr double WSPR_CENTER_FREQ_630M_15 = 475812.5;
constexpr double WSPR_CENTER_FREQ_160M = 1838100.0;
constexpr double WSPR_CENTER_FREQ_160M_15 = 1838212.5;
constexpr double WSPR_CENTER_FREQ_80M = 3570100.0;
constexpr double WSPR_CENTER_FREQ_60M = 5288700.0;
constexpr double WSPR_CENTER_FREQ_40M = 7040100.0;
constexpr double WSPR_CENTER_FREQ_30M = 10140200.0;
constexpr double WSPR_CENTER_FREQ_20M = 14097100.0;
constexpr double WSPR_CENTER_FREQ_17M = 18106100.0;
constexpr double WSPR_CENTER_FREQ_15M = 21096100.0;
constexpr double WSPR_CENTER_FREQ_12M = 24926100.0;
constexpr double WSPR_CENTER_FREQ_10M = 28126100.0;
constexpr double WSPR_CENTER_FREQ_6M = 50294500.0;
constexpr double WSPR_CENTER_FREQ_4M = 70092500.0;
constexpr double WSPR_CENTER_FREQ_2M = 144490500.0;

// Select your frequency constant
static constexpr double WSPR_FREQ = WSPR_CENTER_FREQ_80M;
static constexpr uint8_t POWER = 7; // Power index, 0-7, where 0 is 2mA (-3.4dBm) and 7 is 16mA (+10.6dBm)

/**
 * @brief Mutex used to guard shared shutdown and transmission state
 *
 * This mutex protects access to `g_transmission_done` and is paired
 * with the condition variable `g_end_cv` to coordinate between threads.
 */
static std::mutex g_end_mtx;

/**
 * @brief Condition variable to notify transmission completion or termination
 *
 * This condition variable is used to block or wake threads waiting
 * for the end of a transmission or shutdown signal. It should always
 * be used with `g_end_mtx`.
 */
static std::condition_variable g_end_cv;

/**
 * @brief Flag indicating whether the transmission has completed
 *
 * This boolean tracks the state of the transmitter. It must be accessed
 * only while holding `g_end_mtx`.
 *
 * @note Not thread-safe on its own; synchronize access with `g_end_mtx`.
 */
static bool g_transmission_done = false;

/**
 * @brief Global termination flag shared between signal handler and main loop
 *
 * This atomic flag is set to true when a termination condition (e.g., SIGINT)
 * occurs. It is used by worker threads and the main loop to safely shut down.
 *
 * @note This flag is safe to access from a signal handler.
 */
static std::atomic<bool> g_terminate{false};

/**
 * @brief Pipe file descriptors for self-pipe wake-up mechanism
 *
 * This array holds the read and write ends of a self-pipe used to wake the
 * main thread from blocking system calls like select(), poll(), or condition
 * variable waits. When a signal is received or shutdown is requested,
 * writing a byte to `sig_pipe_fds[1]` will cause the blocking call on
 * `sig_pipe_fds[0]` to return, allowing responsive shutdown.
 *
 * @details The self-pipe mechanism is a common pattern for making signal
 *          handling and inter-thread notifications safe and non-blocking.
 *
 * - `sig_pipe_fds[0]`: Read end (used in select()/poll()).
 * - `sig_pipe_fds[1]`: Write end (used in signal handlers or control logic).
 *
 * @note Must be initialized with `pipe(sig_pipe_fds)` before use.
 */
static int sig_pipe_fds[2] = {-1, -1};

struct AppArgs
{
    bool transmit_now = false;
    bool one_shot = false;
    std::optional<std::string> inject_wd_stall; // Value for WSPR_TX_INJECT_WD_STALL
};

AppArgs parse_args(int argc, char **argv)
{
    AppArgs args;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        if (a == "--now" || a == "-n")
        {
            args.transmit_now = true;
        }
        else if (a == "--oneshot" || a == "--one-shot" || a == "-1")
        {
            args.one_shot = true;
        }
        else if (a.rfind("--inject-wd-stall", 0) == 0)
        {
            std::string value;

            // Support: --inject-wd-stall=5  or  --inject-wd-stall 5
            const auto eq = a.find('=');
            if (eq != std::string_view::npos)
            {
                value = std::string(a.substr(eq + 1));
            }
            else
            {
                if (i + 1 < argc)
                {
                    value = argv[++i];
                }
                else
                {
                    value = "1";
                }
            }

            args.inject_wd_stall = value;

            // Also push into the environment so the transmitter can see it.
            ::setenv("WSPR_TX_INJECT_WD_STALL", value.c_str(), 1);
        }
        else if (a == "--help" || a == "-h")
        {
            std::cout << "Options:\n"
                      << "  -n, --now        Start WSPR immediately (no window wait).\n"
                      << "  -1, --oneshot    Run exactly one WSPR transmission and exit.\n"
                         "  --inject-wd-stall [N|Nms]  Inject a watchdog stall"
                         " after N seconds or Nms.\n";
            std::exit(0);
        }
    }
    return args;
}

/**
 * @brief RAII class to temporarily modify terminal input settings
 *
 * This guard disables canonical mode and input echo on `STDIN_FILENO`
 * for the duration of its lifetime. When the object is destroyed,
 * it automatically restores the previous terminal settings.
 *
 * @details Canonical mode is disabled using `ICANON`, which allows input
 *          to be read one character at a time. `ECHO` is disabled so that
 *          typed characters are not displayed. The `VMIN` and `VTIME`
 *          fields are also configured to ensure immediate character reads.
 *
 * Example use case:
 * - Reading user input interactively without waiting for newline.
 * - Temporarily hiding user keystrokes (like password prompts).
 *
 * @note This struct modifies `STDIN_FILENO` and uses `tcgetattr()` and
 *       `tcsetattr()` internally. No error checking is performed.
 */
struct TermiosGuard
{
    termios old_; ///< Original terminal settings (restored on destruction)
    termios cur_; ///< Modified settings with canonical mode disabled

    /**
     * @brief Constructor disables canonical mode and echo
     *
     * Captures the current terminal settings, modifies them
     * to disable canonical input and echoing, and applies them.
     */
    TermiosGuard()
    {
        // Get current terminal settings
        tcgetattr(STDIN_FILENO, &old_);
        cur_ = old_;

        // Disable canonical mode and echo
        cur_.c_lflag &= ~(ICANON | ECHO);

        // Require at least one character, no timeout
        cur_.c_cc[VMIN] = 1;
        cur_.c_cc[VTIME] = 0;

        // Apply new settings immediately
        tcsetattr(STDIN_FILENO, TCSANOW, &cur_);
    }

    /**
     * @brief Destructor restores the original terminal settings
     */
    ~TermiosGuard()
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_);
    }
};

/**
 * @brief Reads a single character from standard input without waiting for
 *        Enter.
 *
 * This function configures the terminal for noncanonical mode to read a single
 * character immediately. It then restores the terminal settings.
 *
 * @return The character read from standard input.
 */
int getch()
{
    struct termios oldAttr, newAttr;
    tcgetattr(STDIN_FILENO, &oldAttr); // Get terminal attributes
    newAttr = oldAttr;
    newAttr.c_lflag &= ~(ICANON | ECHO);        // Disable canonical mode and echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newAttr); // Set new terminal attributes
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldAttr); // Restore old terminal attributes
    return ch;
}

/**
 * @brief Pauses execution until the user presses the spacebar or a shutdown
 *        signal is received
 *
 * This function blocks until either:
 * - The user presses the spacebar (detected using raw terminal input), or
 * - A signal triggers a write to the self-pipe, indicating a termination
 *   request.
 *
 * It uses the RAII-based TermiosGuard to temporarily set the terminal to
 * non-canonical, non-echoing mode so input can be read one character at a time.
 * A `select()` call monitors both `STDIN_FILENO` and `sig_pipe_fds[0]`.
 *
 * @details
 * - The function polls with `select()` to wait for input or a signal.
 * - It cleanly handles `EINTR` and uses an atomic flag `g_terminate`
 *   to check for termination requests.
 *
 * @note This function blocks indefinitely unless interrupted or input is
 *       received.
 *       It is typically used to pause before transmitting or resuming an
 *       operation.
 */
void wait_for_space_or_signal()
{
    TermiosGuard tguard; // Switch to non-canonical mode for the duration

    char c;
    while (!g_terminate.load(std::memory_order_acquire))
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);    // Watch for keyboard input
        FD_SET(sig_pipe_fds[0], &rfds); // Watch for signal-triggered wakeup

        int nf = std::max(STDIN_FILENO, sig_pipe_fds[0]) + 1;

        // Block until input or signal
        if (::select(nf, &rfds, nullptr, nullptr, nullptr) < 0)
        {
            if (errno == EINTR)
                continue; // Interrupted by signal, retry
            break;        // Other error: exit
        }

        if (FD_ISSET(sig_pipe_fds[0], &rfds))
        {
            // Signal arrived via self-pipe
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            // Read one character
            if (::read(STDIN_FILENO, &c, 1) == 1 && c == ' ')
                break;
        }
    }
}

/**
 * @brief Prompts the user to choose between WSPR and TONE modes
 *
 * This function uses raw terminal input to prompt the user to select
 * one of two transmission modes:
 * - 1) WSPR
 * - 2) TONE
 *
 * It waits for the user to press either '1' or '2', or exits early if a
 * termination signal is received via the self-pipe mechanism.
 *
 * @return true if WSPR mode is selected or implied (default),
 *         false if TONE mode is selected or termination was signaled.
 *
 * @details
 * - Terminal is placed in non-canonical mode using TermiosGuard.
 * - `select()` monitors `STDIN_FILENO` and `sig_pipe_fds[0]`.
 * - If the signal pipe is triggered, the function exits with `false`.
 * - Invalid input defaults to WSPR (true).
 *
 * @note Safe for use in main loops that are responsive to termination.
 */
bool select_wspr()
{
    TermiosGuard tg; // Enable raw input for single-character response

    // Prompt user for input
    std::cout << "Select mode:\n"
              << "  1) WSPR\n"
              << "  2) TONE\n"
              << "Enter [1/2]: " << std::flush;

    fd_set rfds;
    while (!g_terminate.load())
    {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);    // Monitor keyboard
        FD_SET(sig_pipe_fds[0], &rfds); // Monitor signal pipe

        int nf = std::max(STDIN_FILENO, sig_pipe_fds[0]) + 1;

        // Block until input or signal
        if (::select(nf, &rfds, nullptr, nullptr, nullptr) < 0)
        {
            std::cout << std::endl;
            if (errno == EINTR)
                continue; // Retry after interrupt
            throw std::runtime_error("select failed");
        }

        if (FD_ISSET(sig_pipe_fds[0], &rfds))
        {
            // Received signal — exit early
            std::cout << std::endl;
            return false;
        }

        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            char c;
            if (::read(STDIN_FILENO, &c, 1) == 1)
            {
                std::cout << std::endl;
                if (c == '2')
                    return false; // User selected TONE mode
                else
                    return true; // Default or user selected WSPR
            }
        }
    }

    // Exited due to global terminate flag
    std::cout << std::endl;
    return false;
}

/**
 * @brief Signal handler for graceful shutdown
 *
 * This function is invoked when the process receives a termination signal.
 * It writes a message to `stderr`, stops the transmitter (if safe), signals
 * termination, and wakes the main thread if it is blocked in `select()` or
 * `poll()` using a self-pipe trick.
 *
 * @param signal The signal number that triggered the handler (unused).
 *
 * @note Only async-signal-safe functions should be called from within
 *       this handler. Ensure `wsprTransmitter.stopAndJoin()` is safe, or
 *       move its logic to a safe location based on `g_terminate`.
 */
void sig_handler(int)
{
    // Keep the signal handler async-signal-safe.
    // Do NOT call into the transmitter here (may deadlock on 32-bit).
    const char msg[] = "\nCaught signal\nShutting down transmissions.\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);

    g_terminate.store(true, std::memory_order_release);

    // Wake up the main thread if it's blocked on select()/poll().
    const char wake = 1;
    (void)write(sig_pipe_fds[1], &wake, 1);
}

/**
 * @brief Print a transmission start message.
 *
 * This callback prints to stdout a notice that transmission has begun.
 * If both a descriptive message and frequency are provided, it prints
 * both. Otherwise it prints whichever is available, or a default notice
 * if neither is provided.
 *
 * @param msg          Transmission descriptor string; may be empty.
 * @param frequency    Frequency in Hz; zero indicates no frequency.
 */
void transmitter_cb(
    WsprTransmissionCallbackEvent event,
    WsprTransmitLogLevel level,
    const std::string &msg,
    double value)
{

    switch (event)
    {
    case WsprTransmissionCallbackEvent::STARTING:
    {
        const double frequency = value;

        if (!msg.empty() && frequency != 0.0)
        {
            std::cout << log_tag
                      << "Started transmission ("
                      << msg
                      << ") "
                      << wsprTransmitter.formatFrequencyMHz(frequency)
                      << " MHz."
                      << std::endl;
        }
        else if (frequency != 0.0)
        {
            std::cout << log_tag
                      << "Started transmission: "
                      << wsprTransmitter.formatFrequencyMHz(frequency)
                      << " MHz."
                      << std::endl;
        }
        else if (!msg.empty())
        {
            std::cout << log_tag
                      << "Started transmission ("
                      << msg
                      << ")."
                      << std::endl;
        }
        else
        {
            std::cout << log_tag
                      << "Started transmission."
                      << std::endl;
        }
        break;
    }
    case WsprTransmissionCallbackEvent::COMPLETE:
    {
        const double elapsed = value;

        if (!msg.empty() && elapsed != 0.0)
        {
            std::cout << log_tag
                      << "Completed transmission ("
                      << msg
                      << ") "
                      << std::fixed
                      << std::setprecision(6)
                      << elapsed
                      << " seconds."
                      << std::endl;
        }
        else if (elapsed != 0.0)
        {
            std::cout << log_tag
                      << "Completed transmission: "
                      << std::fixed
                      << std::setprecision(6)
                      << elapsed
                      << " seconds."
                      << std::endl;
        }
        else if (!msg.empty())
        {
            std::cout << log_tag
                      << "Completed transmission ("
                      << msg
                      << ")."
                      << std::endl;
        }
        else
        {
            std::cout << log_tag
                      << "Completed transmission."
                      << std::endl;
        }

        {
            std::lock_guard<std::mutex> lk(g_end_mtx);
            g_transmission_done = true;
        }
        g_end_cv.notify_one();
        break;
    }
    case WsprTransmissionCallbackEvent::CANCELLED:
    {
        const double elapsed = value;

        std::cout << log_tag
                  << "Transmission canceled after "
                  << std::fixed
                  << std::setprecision(6)
                  << elapsed
                  << " seconds."
                  << std::endl;

        {
            std::lock_guard<std::mutex> lk(g_end_mtx);
            g_transmission_done = true;
        }
        g_end_cv.notify_one();
        break;
    }
    case WsprTransmissionCallbackEvent::LOGGING:
    {
        std::ostream &base =
            (level == WsprTransmitLogLevel::ERROR)
                ? static_cast<std::ostream &>(std::cerr)
                : static_cast<std::ostream &>(std::cout);

        std::osyncstream os(base);

        if (msg.empty())
        {
            os << '\n';
            break;
        }

        os << log_tag << msg;

        if (msg.back() != '\n')
            os << '\n';

        break;
    }

    default:
        break;
    }
}

/**
 * @brief Callback invoked when a transmission finishes.
 *
 * Prints a completion message to stdout that includes an optional
 * descriptor and the elapsed time in seconds (to three decimal places).
 * Then it sets the global flag `g_transmission_done` to true under lock
 * and notifies the condition variable `g_end_cv`.
 *
 * @param msg     Descriptor string for the transmission; may be empty.
 * @param elapsed Duration of the transmission in seconds; zero indicates
 *                no timing information.
 */

/**
 * @brief Sets up signal handlers and the self-pipe mechanism
 *
 * Installs signal handlers for termination and user-defined signals.
 * These handlers write to a self-pipe to safely interrupt blocking calls
 * like `select()` or `poll()`. Also ignores `SIGCHLD` to prevent zombies.
 *
 * @throws std::runtime_error if pipe creation fails
 */
void setup_signal_handlers()
{
    if (::pipe(sig_pipe_fds) < 0)
    {
        throw std::runtime_error("Failed to create signal pipe");
    }

    std::array<int, 6> signals = {SIGINT, SIGTERM, SIGHUP, SIGUSR1, SIGUSR2, SIGQUIT};
    for (int s : signals)
    {
        struct sigaction sa{};
        sa.sa_handler = sig_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(s, &sa, nullptr);
    }

    std::signal(SIGCHLD, SIG_IGN);
}

/**
 * @brief Configures the transmitter based on the selected mode
 *
 * Sets real-time scheduling, applies PPM correction, and sets up
 * callbacks and transmission parameters depending on the selected mode.
 *
 * @param isWspr True if WSPR mode is selected, false for TONE mode
 */
void configure_transmitter(bool isWspr)
{
    config.ppm = get_ppm_from_chronyc();

    wsprTransmitter.setThreadScheduling(SCHED_FIFO, 50);

    wsprTransmitter.setTransmissionCallbacks(
        [](WsprTransmissionCallbackEvent event,
           WsprTransmitLogLevel level,
           const std::string &msg,
           double value)
        {
            transmitter_cb(event, level, msg, value);
        });

    if (isWspr)
    {
        const PreparedWsprTransmission plan =
            build_prepared_wspr_transmission(
                std::string(CALLSIGN),
                std::string(GRID),
                POWER_DBM);

        TransmissionRequest request;
        request.mode = TransmissionMode::WSPR;
        request.payload = plan;
        request.dial_frequency_hz = WSPR_FREQ;
        request.actual_rf_frequency_hz = WSPR_FREQ;
        request.ppm = config.ppm;
        request.power_level = POWER;
        request.tx_gpio = config.tx_pin;
        request.use_offset = true;
        wsprTransmitter.configureExecution(request);
    }
    else
    {
        TransmissionRequest request;
        request.mode = TransmissionMode::TONE;
        request.dial_frequency_hz = WSPR_FREQ;
        request.actual_rf_frequency_hz = WSPR_FREQ;
        request.ppm = config.ppm;
        request.tx_gpio = config.tx_pin;
        wsprTransmitter.configureExecution(request);
    }

#ifdef DEBUG_WSPR_TRANSMIT
    wsprTransmitter.dumpParameters();
#endif
}

/**
 * @brief Waits for transmission to complete or user signal
 *
 * In WSPR mode, waits for either transmission completion or shutdown.
 * In TONE mode, waits for spacebar press to end the tone.
 *
 * @param isWspr True if WSPR mode is selected, false for TONE mode
 */
static void wait_for_completion(bool isWspr)
{
    using namespace std::chrono_literals;

    bool stop_requested = false;

    // Allow spacebar to request stop while transmission is running.
    // We place the terminal into non-canonical, non-echoing mode so we can
    // read single characters without requiring Enter.
    std::optional<TermiosGuard> tg;
    tg.emplace();

    while (!g_terminate.load(std::memory_order_acquire))
    {
        const auto state = wsprTransmitter.getState();

        if (state == WsprTransmitState::COMPLETE ||
            state == WsprTransmitState::CANCELLED)
        {
            std::cout
                << log_tag
                << "Transmission in "
                << wsprTransmitter.stateToString(state)
                << " state."
                << std::endl;
            return;
        }

        if (state == WsprTransmitState::HUNG)
        {
            std::cerr
                << log_tag
                << "Transmission entered "
                << wsprTransmitter.stateToString(state)
                << " state."
                << std::endl;
            return;
        }

        if (state == WsprTransmitState::RECOVERING)
        {
            std::cerr
                << log_tag
                << "Transmission entered "
                << wsprTransmitter.stateToString(state)
                << " state."
                << std::endl;
            return;
        }

        if (state == WsprTransmitState::DISABLED)
        {
            std::cerr
                << log_tag
                << "Transmission entered "
                << wsprTransmitter.stateToString(state)
                << " state."
                << std::endl;
            return;
        }

        if (!stop_requested)
        {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            FD_SET(sig_pipe_fds[0], &rfds);

            const int nf = std::max(STDIN_FILENO, sig_pipe_fds[0]) + 1;

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100ms

            const int sel_ret = ::select(nf, &rfds, nullptr, nullptr, &tv);
            if (sel_ret < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }

            if (sel_ret == 0)
                continue;

            if (FD_ISSET(sig_pipe_fds[0], &rfds))
                break;

            if (FD_ISSET(STDIN_FILENO, &rfds))
            {
                char c = '\0';
                if (::read(STDIN_FILENO, &c, 1) == 1 && c == ' ')
                {
                    stop_requested = true;
                    wsprTransmitter.requestStopTx();
                }
            }

            continue;
        }

        std::this_thread::sleep_for(100ms);
    }
}

/**
 * @brief Main entry point for the transmitter application
 *
 * Coordinates signal handling, user interaction, and WSPR or TONE
 * transmission setup and control. Uses cooperative shutdown and
 * precise timing management to support clean signal generation
 * and safe cancellation.
 *
 * @details
 * - Sets up a self-pipe mechanism and signal handlers for safe
 *   interruption of blocking system calls like `select()`.
 * - Prompts the user to select between WSPR and TONE transmission.
 * - Applies PPM correction obtained from chronyd.
 * - Configures the transmitter based on the selected mode.
 * - Waits for the appropriate trigger to begin transmission.
 * - Launches the transmission thread and waits for completion
 *   or user cancellation.
 * - Cleans up and stops the transmitter on exit.
 *
 * @return 0 on normal termination, 1 on unrecoverable failure.
 */
int main(int argc, char **argv)
{
    try
    {
        const AppArgs args = parse_args(argc, argv);

        // Set up signal handling and the self-pipe mechanism
        setup_signal_handlers();

        // Prompt user to choose transmission mode (WSPR or TONE)
        bool isWspr = select_wspr();

        // Exit early if signal (e.g., Ctrl-C) was received during input
        if (g_terminate.load(std::memory_order_acquire))
            return 0;

        std::cout << log_tag
                  << "Mode selected: "
                  << (isWspr ? "WSPR" : "TONE")
                  << std::endl;

        // Configure transmitter settings based on user selection
        configure_transmitter(isWspr);

        wsprTransmitter.setTransmitNow(args.transmit_now);
        wsprTransmitter.setOneShot(args.one_shot);

        if (isWspr)
        {
            // WSPR mode waits for the next time slot unless --now was supplied
            if (!args.transmit_now)
                std::cout << log_tag
                          << "Waiting for next transmission window."
                          << std::endl;
            else
                std::cout << log_tag
                          << "Transmit-now enabled. Starting immediately."
                          << std::endl;
        }
        else
        {
            // Tone mode requires spacebar press to begin
            std::cout << "Press <spacebar> to begin test tone." << std::endl;
            wait_for_space_or_signal();
        }
        // Begin scheduled or immediate transmission
        wsprTransmitter.startAsync();

        // Wait for transmission to finish or be aborted
        wait_for_completion(isWspr);

        // Clean up and stop transmission
        wsprTransmitter.stopAndJoin();
        return 0;
    }
    catch (const std::exception &e)
    {
        // Catch and report any fatal errors
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
