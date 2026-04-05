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

#include "lcblog.hpp"
#include "version.hpp"

/**
 * @brief Initializes the logger with the appropriate log level.
 *
 * This function sets the log level based on the current debug state. If the
 * build is compiled with the DEBUG_BUILD macro, the log level is set to DEBUG.
 * Otherwise, it defaults to INFO.
 *
 * @note Ensure that the `get_debug_state()` function correctly reflects the
 *       build configuration for accurate log level assignment.
 *
 * @example
 * initialize_logger();
 * // Sets llog to DEBUG or INFO depending on the build mode.
 */
void initialize_logger(bool use_journald)
{
    // Determine the log level based on the build mode.
    const std::string debug_state = get_debug_state();

    // Set the appropriate log level.
    if (debug_state == "DEBUG")
    {
        llog.setLogLevel(DEBUG); // Enable detailed debug logging.
    }
    else
    {
        llog.setLogLevel(INFO); // Default to informational logging.
    }

    // Journald is an explicit opt-in backend. Default CLI runs stay on
    // stdout/stderr unless the caller requests journald explicitly.
    llog.enableJournald(use_journald);
    if (use_journald)
    {
        llog.setJournaldIdentifier(get_exe_name());
    }
}
