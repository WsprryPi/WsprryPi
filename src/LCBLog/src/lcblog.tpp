/**
 * @file lcblog.tpp
 * @brief A non-blocking logging class for handling log levels, formatting,
 * and time stamping within a C++ project.
 *
 * This logging class provides a flexible and thread-safe logging mechanism
 * with support for multiple log levels, timestamped logs, and customizable
 * output streams. include the header (`lcblog.hpp`), implementation
 * (`lcblog.cpp`), and template definitions (`lcblog.tpp`)when using in
 * a project.
 *
 * This software is distributed under the MIT License. See the repository root
 * LICENSE.md for details.
 *
 * Copyright © 2023 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
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

#ifndef _LCBLOG_TPP
#define _LCBLOG_TPP
#pragma once

#include "lcblog.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cctype>

/**
 * @brief Emit a one-time banner describing the active logging backend.
 *
 * This banner is emitted once per process lifetime and is intended
 * to help confirm whether logging is going to streams or journald.
 */
inline void LCBLog::emitBootstrapDiagnosticToStdout_(
    LogLevel level,
    const std::string &message)
{
    std::ostringstream line;
    std::string levelStr = ::logLevelToString(level);
    constexpr int LOG_LEVEL_WIDTH = 5;

    if (levelStr.size() < LOG_LEVEL_WIDTH)
    {
        levelStr.append(LOG_LEVEL_WIDTH - levelStr.size(), ' ');
    }

    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (printTimestamps)
        {
            line << getStamp() << "\t";
        }
    }

    line << "[" << levelStr << "] " << message << std::endl;
    std::cout << line.str();
}

/**
 * @brief Emit a one-time banner describing the active logging backend.
 *
 * This banner is emitted once per process lifetime and is intended
 * to help confirm whether logging is going to streams or journald.
 */
inline void LCBLog::emitBackendBannerIfNeeded_()
{
    if (!shouldLog(LogLevel::DEBUG))
    {
        return;
    }

    bool expected = false;
    if (!backendBannerLogged_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return;
    }

    const bool journaldEnabled = useJournald_.load(std::memory_order_acquire);
    const std::string backend = journaldEnabled ? "journald" : "streams";
    emitBootstrapDiagnosticToStdout_(
        LogLevel::DEBUG,
        std::string("Logging backend: ") + backend);
}

/**
 * @brief Enqueue a formatted log message for asynchronous processing.
 *
 * This template formats the provided arguments into a single string,
 * selects the appropriate output queue (standard or error), and
 * notifies a background worker without blocking the calling thread.
 *
 * @tparam Args Types of the message components.
 * @param level Severity level of the message.
 * @param args Components of the message to log.
 */
template<typename... Args>
void LCBLog::log(LogLevel level, Args&&... args)
{
    emitBackendBannerIfNeeded_();

    if (!shouldLog(level)) {
        return;
    }

    std::ostringstream oss;
    logToStream(oss, level, std::forward<Args>(args)...);

    auto entry = std::make_unique<LogEntry>();
    entry->level = level;
    entry->msg   = oss.str();
    entry->dest = (level >= LogLevel::ERROR
                       ? ::LogEntry::Err
                       : ::LogEntry::Out);

    auto& queue = (entry->dest == ::LogEntry::Err ? errQueue_ : outQueue_);
    auto& mtx   = (entry->dest == ::LogEntry::Err ? errMtx_   : outMtx_);
    auto& cv    = (entry->dest == ::LogEntry::Err ? errCv_    : outCv_);

    {
        std::lock_guard<std::mutex> lock(mtx);
        // drop oldest if we’re at capacity
        if (queue.size() >= this->maxQueueSize_) {
            queue.pop_front();
        }
        queue.push_back(std::move(entry));
    }
    cv.notify_one();
}

/**
 * @brief Format log message components and write to an output stream.
 *
 * This template converts each argument to a string, applies spacing logic,
 * splits on line breaks, applies optional timestamps and log level tags,
 * and writes each line to the provided stream.
 *
 * @tparam T   Type of the first message component.
 * @tparam Args Types of any additional components.
 * @param stream Output stream for formatted log text.
 * @param level Severity level of the message.
 * @param t     First component of the message.
 * @param args  Remaining components of the message.
 */
template<typename T, typename... Args>
void LCBLog::logToStream(std::ostream& stream,
                         LogLevel       level,
                         T&&            t,
                         Args&&...      args)
{
    const bool journaldEnabled =
        useJournald_.load(std::memory_order_acquire);
    std::string levelStr;
    if (!journaldEnabled)
    {
        levelStr = ::logLevelToString(level);
        constexpr int LOG_LEVEL_WIDTH = 5;
        if (levelStr.size() < LOG_LEVEL_WIDTH)
        {
            levelStr.append(LOG_LEVEL_WIDTH - levelStr.size(), ' ');
        }
    }

    // Prepare conversion lambda
    auto toString = [&](auto&& val) {
        std::ostringstream tmp;
        if constexpr (std::is_floating_point_v<std::decay_t<decltype(val)>>) {
            if (val == static_cast<long long>(val)) {
                tmp << std::showpoint << std::fixed << std::setprecision(1);
            }
        }
        tmp << val;
        return tmp.str();
    };

    // Collect all parts as strings
    std::vector<std::string> parts;
    parts.reserve(1 + sizeof...(args));
    parts.push_back(toString(std::forward<T>(t)));
    (parts.push_back(toString(std::forward<Args>(args))), ...);

    // Recombine with spacing logic
    std::ostringstream combined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0 && !::shouldSkipSpace(parts[i - 1], parts[i])) {
            combined << ' ';
        }
        combined << parts[i];
    }

    // Split lines, apply cleanup, timestamp, and tag
    std::istringstream msgStream(combined.str());
    std::string        line;
    bool               firstLine  = true;
    bool               printedAny = false;

    while (std::getline(msgStream, line)) {
        if (!firstLine) {
            stream << std::endl;
        }
        if (printTimestamps)
        {
            stream << getStamp() << "\t";
        }
        crush(line);
        if (journaldEnabled)
        {
            stream << line;
        }
        else
        {
            stream << "[" << levelStr << "] " << line;
        }
        firstLine  = false;
        printedAny = true;
    }

    // Emit an empty entry if nothing was printed
    if (!printedAny)
    {
        if (printTimestamps)
        {
            stream << getStamp() << "\t";
        }

        if (!journaldEnabled)
        {
            stream << "[" << levelStr << "] ";
        }
    }

    // Write final newline
    stream << std::endl;
}

/**
 * @brief Determine if a space should be omitted between two tokens.
 *
 * This free function returns true when the next token is empty or
 * starts with punctuation, or when the previous token ends with
 * whitespace. It returns false if a space is required before a word.
 *
 * @tparam PrevT Type of the previous token.
 * @tparam T     Type of the next token.
 * @param prevArg Previous token string.
 * @param arg     Next token string.
 * @return True if no space should be added, false otherwise.
 */
template<typename PrevT, typename T>
bool shouldSkipSpace(const PrevT& prevArg, const T& arg)
{
    std::ostringstream ossPrev, ossCurr;
    ossPrev << prevArg;
    ossCurr << arg;
    std::string prev = ossPrev.str();
    std::string curr = ossCurr.str();

    // Skip if next token is empty or begins with punctuation
    if (curr.empty() || std::ispunct(static_cast<unsigned char>(curr.front()))) {
        return true;
    }

    // Require space if this is the first token
    if (prev.empty()) {
        return false;
    }

    // Skip if previous ends in whitespace
    if (std::isspace(static_cast<unsigned char>(prev.back()))) {
        return true;
    }

    // Skip after opening delimiters so adjacent quoted/grouped fragments
    // stay contiguous when callers split messages across log arguments.
    switch (prev.back()) {
        case '"':
        case '\'':
        case '(':
        case '[':
        case '{':
            return true;
        default:
            break;
    }

    // Otherwise, add a space
    return false;
}

/**
 * @brief Convenience wrapper to log to standard‐output queue.
 *
 * Formats the arguments and enqueues the message with INFO/WARN/DEBUG
 * semantics on the non‐error queue.
 *
 * @tparam T    Type of the first message component.
 * @tparam Args Types of any additional components.
 * @param level Severity level of the message.
 * @param t     First component of the message.
 * @param args  Remaining components of the message.
 */
template<typename T, typename... Args>
void LCBLog::logS(LogLevel level, T&& t, Args&&... args)
{
    log(level, std::forward<T>(t), std::forward<Args>(args)...);
}

/**
 * @brief Convenience wrapper to log to error‐output queue.
 *
 * Formats the arguments and enqueues the message with ERROR/FATAL
 * semantics on the error queue.
 *
 * @tparam T    Type of the first message component.
 * @tparam Args Types of any additional components.
 * @param level Severity level of the message.
 * @param t     First component of the message.
 * @param args  Remaining components of the message.
 */
template<typename T, typename... Args>
void LCBLog::logE(LogLevel level, T&& t, Args&&... args)
{
    log(level, std::forward<T>(t), std::forward<Args>(args)...);
}

#endif
