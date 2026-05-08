/**
 * @file logging.hpp
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

#ifndef _LOGGING_HPP
#define _LOGGING_HPP

#include "lcblog.hpp"

/**
 * @brief Initializes the logger from startup logging configuration.
 *
 * This function applies startup logging transport preferences and selects the
 * application log level from persisted configuration or CLI overrides.
 */
extern void initialize_logger(bool use_journald = false,
                              bool enable_timestamps = false,
                              bool enable_debug_logging = false);

/**
 * @brief Re-applies the configured DEBUG vs INFO log level.
 *
 * This updates only the logger threshold. It does not change journald or
 * timestamp routing, which remain startup-scoped runtime choices.
 */
extern void refresh_logger_level_from_config();

#endif // _LOGGING_HPP
