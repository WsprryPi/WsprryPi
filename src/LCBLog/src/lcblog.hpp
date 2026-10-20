/**
 * @file lcblog.hpp
 * @brief A non-blocking logging class for handling log levels, formatting,
 * and time stamping within a C++ project.
 *
 * This logging class provides a flexible and thread-safe logging mechanism
 * with support for multiple log levels, time stamped logs, and customizable
 * output streams. include the header (`lcblog.hpp`), implementation
 * (`lcblog.cpp`), and template definitions (`lcblog.tpp`) when using in
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

#ifndef LCBLOG_HPP
#define LCBLOG_HPP
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>


/**
 * @enum LogLevel
 * @brief Define severity levels for logging.
 *
 * This enumeration determines the threshold for message importance
 * and controls which messages are emitted based on their level.
 */
enum LogLevel
{
    DEBUG = 0, /**< Debug-level messages for detailed troubleshooting. */
    INFO,      /**< Informational messages describing normal operation. */
    WARN,      /**< Warning messages indicating potential issues. */
    ERROR,     /**< Error messages requiring attention but allowing continued execution. */
    FATAL      /**< Fatal messages indicating critical errors that terminate the program. */
};

/**
 * @struct LogEntry
 * @brief Represent a log message and its target output stream.
 *
 * This struct associates a formatted message string with a destination
 * (standard output or error output) so that the asynchronous worker
 * threads know where to write each log entry.
 */
struct LogEntry
{
    /**
     * @enum Destination
     * @brief Define the output stream for the log entry.
     */
    enum Destination
    {
        Out, /**< Write message to standard output. */
        Err  /**< Write message to error output. */
    } dest; /**< Selected destination for this log entry. */

    LogLevel level; /**< Severity level associated with this entry. */

    std::string msg; /**< Formatted text content of the log entry. */
};


/**
 * @brief Converts a log level to its string representation.
 *
 * @param level The log level to convert.
 * @return A string representing the log level.
 */
std::string logLevelToString(LogLevel level);

/**
 * @class LCBLog
 * @brief Provide asynchronous, thread-safe logging with severity filtering.
 *
 * LCBLog manages separate queues and worker threads for standard and error
 * output streams. It supports configurable log levels, optional timestamps,
 * and non-blocking message emission in batch mode.
 */
class LCBLog
{
public:
    /**
     * @brief Construct a logger with specified output streams.
     *
     * @param outStream Stream for informational and debug messages.
     * @param errStream Stream for error and fatal messages.
     */
    explicit LCBLog(std::ostream &outStream = std::cout,
                    std::ostream &errStream = std::cerr);

    /**
     * @brief Destroy the logger, flushing all pending messages.
     *
     * Signals worker threads to stop, drains their queues, and joins them
     * before object destruction.
     */
    ~LCBLog();

    /**
     * @brief Returns the singleton logger instance.
     *
     * Constructs the LCBLog instance on first invocation using the given
     * output and error streams. Subsequent calls ignore their parameters
     * and return the same instance.
     *
     * @param out The output stream for standard logs (default: std::cout)
     * @param err The output stream for error logs (default: std::cerr)
     * @return Reference to the global LCBLog singleton instance
     */
    LCBLog &getLogger(std::ostream &out = std::cout,
                      std::ostream &err = std::cerr);

    /**
     * @brief Returns the singleton logger instance.
     *
     * Constructs the LCBLog instance on first invocation using default
     * output and error streams. Subsequent calls ignore their parameters
     * and return the same instance.
     *
     * @param out The output stream for standard logs (default: std::cout)
     * @param err The output stream for error logs (default: std::cerr)
     * @return Reference to the global LCBLog singleton instance
     */
    LCBLog &getLogger();

    /**
     * @brief Set the minimum log level for message output.
     *
     * Messages below this level are discarded.
     *
     * @param level New threshold for log message severity.
     */
    void setLogLevel(LogLevel level);

    /**
     * @brief Determine if a message meets the current log level threshold.
     *
     * @param level Severity level of the message to evaluate.
     * @return true if the message level is equal to or higher than the threshold.
     */
    bool shouldLog(LogLevel level) const;

    /**
     * @brief Enable or disable timestamping for each log line.
     *
     * When enabled, each line is prefixed with a UTC timestamp.
     *
     * If journald logging is enabled, timestamps are not prepended and
     * journald provides its own timestamp metadata.
     *
     * @param enable True to include timestamps, false to omit them.
     */
    void enableTimestamps(bool enable);

    /**
     * @brief Enable or disable logging to journald.
     *
     * When enabled, log entries are sent via libsystemd instead of
     * writing to the configured output streams.
     *
     * When journald is active, LCBLog does not prepend its own timestamp
     * or textual log level tag; journald provides those fields.
     *
     * @param enable True to send logs to journald, false to use streams.
     */
    void enableJournald(bool enable);

    /**
     * @brief Set the SYSLOG_IDENTIFIER value for journald logging.
     *
     * If empty, libsystemd will use its default identifier.
     *
     * @param ident Identifier string (for example "wsprrypi").
     */
    void setJournaldIdentifier(const std::string &ident);

    /**
     * @brief Enqueue a formatted log message.
     *
     * This template formats the provided arguments into a single message
     * and dispatches it to the appropriate worker queue without blocking.
     *
     * @tparam Args Types of the components to format.
     * @param level Severity level of the message.
     * @param args Components of the message to log.
     */
    template <typename... Args>
    void log(LogLevel level, Args &&...args);

    /**
     * @brief Log a message to the info/output queue.
     *
     * Convenience wrapper for log() targeting standard output.
     *
     * @tparam T Type of the first argument.
     * @tparam Args Types of any additional arguments.
     * @param level Severity level of the message.
     * @param t First part of the message.
     * @param args Remaining parts of the message.
     */
    template <typename T, typename... Args>
    void logS(LogLevel level, T &&t, Args &&...args);

    /**
     * @brief Log a message to the error queue.
     *
     * Convenience wrapper for log() targeting error output.
     *
     * @tparam T Type of the first argument.
     * @tparam Args Types of any additional arguments.
     * @param level Severity level of the message.
     * @param t First part of the message.
     * @param args Remaining parts of the message.
     */
    template <typename T, typename... Args>
    void logE(LogLevel level, T &&t, Args &&...args);

private:
    LogLevel logLevel;            /**< Threshold for message filtering. */
    std::ostream &out;            /**< Stream for non-error messages. */
    std::ostream &err;            /**< Stream for error messages. */
    bool printTimestamps = false; /**< Flag to include timestamps. */
    std::atomic<bool> useJournald_{false}; /**< Send logs to journald. */
    std::string journaldIdent_;           /**< SYSLOG_IDENTIFIER override. */
    std::atomic<bool> backendBannerLogged_{false}; /**< Backend banner logged. */
    std::mutex logMutex;          /**< Protects configuration changes. */

    /**
     * @brief Emit a one-time banner describing the active logging backend.
     *
     * This banner is emitted once per process lifetime and is intended
     * to help confirm whether logging is going to streams or journald.
     */
    void emitBackendBannerIfNeeded_();

    /**
     * @brief Emit a bootstrap diagnostic directly to standard output.
     *
     * This helper bypasses the configured logging backend so that
     * internal self-diagnostic messages can always be observed on
     * stdout without affecting routing for normal log traffic.
     *
     * @param level Severity level to render in the message prefix.
     * @param message Diagnostic text to emit.
     */
    void emitBootstrapDiagnosticToStdout_(LogLevel level,
                                          const std::string &message);

    /**
     * @brief Sanitize a string by normalizing whitespace and punctuation spacing.
     *
     * This method trims leading and trailing whitespace, collapses consecutive
     * whitespace runs into a single space, removes spaces before common
     * punctuation characters (comma, period, exclamation, question, semicolon,
     * colon), and eliminates spaces immediately after '(' or immediately before ')'.
     *
     * @param s Reference to the string to be cleaned.
     */
    static void crush(std::string &s); /**< Clean up whitespace and punctuation. */

    /**
     * @brief Generate a UTC timestamp string with millisecond precision.
     *
     * This function retrieves the current system time in UTC, formats it as
     * YYYY-MM-DD HH:MM:SS, appends a three-digit millisecond component, and
     * tags the result with "UTC".
     *
     * @return A formatted timestamp string in UTC with millisecond precision.
     */
    std::string getStamp(); /**< Generate a UTC timestamp string. */

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
    template <typename T, typename... Args>
    void logToStream(std::ostream &stream,
                     LogLevel level,
                     T &&t,
                     Args &&...args);

    std::deque<std::unique_ptr<LogEntry>> outQueue_; /**< Queue for standard output. */
    std::deque<std::unique_ptr<LogEntry>> errQueue_; /**< Queue for error output. */
    std::mutex outMtx_;                              /**< Protects outQueue_. */
    std::mutex errMtx_;                              /**< Protects errQueue_. */
    std::condition_variable outCv_;                  /**< Notifies outWorker_. */
    std::condition_variable errCv_;                  /**< Notifies errWorker_. */

    std::thread outWorker_;         /**< Worker for standard output. */
    std::thread errWorker_;         /**< Worker for error output. */
    std::atomic<bool> done_{false}; /**< Signal to stop worker loops. */

    const size_t maxQueueSize_ = 1024;                   /**< Max messages in queue (discards oldest). */
    const size_t batchSize_ = 16;                        /**< Messages per flush. */
    const std::chrono::milliseconds flushInterval_{200}; /**< Flush interval. */

    /**
     * @brief Processes queued log entries in batches on a background thread.
     *
     * This loop waits for new entries or a timeout, moves up to batchSize_
     * messages into a local buffer, writes them to the output stream, and
     * flushes when the batch is full or the flush interval has elapsed.
     *
     * @param queue Reference to the deque holding pending log entries.
     * @param mtx Reference to the mutex protecting the queue.
     * @param cv Reference to the condition variable for queue notifications.
     * @param stream Reference to the output stream where log messages are written.
     */
    void workerLoop(std::deque<std::unique_ptr<LogEntry>> &queue,
                    std::mutex &mtx,
                    std::condition_variable &cv,
                    std::ostream &stream);
};

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
template <typename PrevT, typename T>
bool shouldSkipSpace(const PrevT &prev, const T &curr);

#include "lcblog.tpp"

LCBLog &getLogger();

#define llog getLogger()

#endif // LCBLOG_HPP
