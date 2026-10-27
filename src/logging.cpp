/**
 * @file logging.cpp
 * @brief Handles management of the LCBLog logging utility.
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

#include "logging.hpp"

#include "config_handler.hpp"
#include "lcblog.hpp"
#include "version.hpp"
#include "wtp_endpoint/identity.hpp"
#include <atomic>
#include <chrono>
#include <unistd.h>
#if defined(__linux__) && LIB_JOURNALD
#include <systemd/sd-journal.h>
#include <sys/timex.h>
#endif

namespace
{
    bool native_journal_record(int priority, const std::string& identifier, const std::string& message)
    {
#if defined(__linux__) && LIB_JOURNALD
        try {
            static const auto device = wsprrypi::wtp_pi_device_id();
            static const auto boot = wsprrypi::WtpPiBootIdentity{}.new_boot_id();
            static std::atomic<std::uint64_t> sequence{0};
            auto previous = sequence.load();
            do { if (previous == UINT64_MAX) return true; }
            while (!sequence.compare_exchange_weak(previous, previous + 1));
            char hostname[256]{};
            if (gethostname(hostname, sizeof(hostname) - 1)) hostname[0] = 0;
            const auto mono = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto utc = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            timex quality{};
            const bool valid_utc = adjtimex(&quality) == TIME_OK && !(quality.status & (STA_UNSYNC | STA_CLOCKERR));
            const auto utc_text = valid_utc ? std::to_string(utc) : std::string{};
            return sd_journal_send("MESSAGE=%s", message.c_str(), "PRIORITY=%d", priority,
                "SYSLOG_IDENTIFIER=%s", identifier.c_str(), "SYSLOG_FACILITY=23",
                "WSPRRY_LOG_SCHEMA=wsprry-log/1", "WSPRRY_ORIGIN_KIND=pi",
                "WSPRRY_ORIGIN_ID=%s", device.c_str(), "WSPRRY_ORIGIN_NAME=%s", hostname,
                "WSPRRY_ORIGIN_BOOT=%s", boot.c_str(),
                "WSPRRY_ORIGIN_SEQUENCE=%s", std::to_string(previous + 1).c_str(),
                "WSPRRY_ORIGIN_MONOTONIC_NS=%s", std::to_string(mono).c_str(),
                "WSPRRY_ORIGIN_UTC_NS=%s", utc_text.c_str(),
                "WSPRRY_EVENT=PI_LOG", "WSPRRY_INGRESS=native", "WSPRRY_COLLECTOR_ID=%s", device.c_str(),
                "WSPRRY_FORWARDERS=[]", nullptr) >= 0;
        } catch (...) { return false; }
#else
        (void)priority; (void)identifier; (void)message;
        return false;
#endif
    }
    void apply_log_level(bool enable_debug_logging)
    {
        llog.setLogLevel(enable_debug_logging ? DEBUG : INFO);
        config.debug_logging = enable_debug_logging;
    }
}

/**
 * @brief Initializes the logger from startup logging configuration.
 *
 * This function applies startup logging transport preferences and selects the
 * application log level from persisted configuration or CLI overrides.
 */
void initialize_logger(
    bool use_journald,
    bool enable_timestamps,
    bool enable_debug_logging)
{
    LCBLog::setJournaldSink(native_journal_record);
    apply_log_level(enable_debug_logging);

    if (use_journald)
    {
        enable_timestamps = false;
    }

    config.use_journald = use_journald;
    config.date_time_log = enable_timestamps;

    // Defensive guard against future regressions that try to combine
    // journald with timestamp prefixes.
    if (config.use_journald && config.date_time_log)
    {
        config.date_time_log = false;
    }

    llog.enableJournald(use_journald);
    llog.enableTimestamps(config.date_time_log);

    if (use_journald)
    {
        llog.setJournaldIdentifier(get_exe_name());
    }
}

void refresh_logger_level_from_config()
{
    apply_log_level(config.debug_logging);
}
