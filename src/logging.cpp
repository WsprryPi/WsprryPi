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

namespace
{
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
