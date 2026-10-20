/**
 * @file lcblog.cpp
 * @brief A non-blocking logging class for handling log levels, formatting,
 * and time stamping within a C++ project.
 *
 * This logging class provides a flexible and thread-safe logging mechanism
 * with support for multiple log levels, timestamped logs, and customizable
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

#include "lcblog.hpp"

#include <algorithm>
#include <iostream>
#include <regex>

#if defined(__has_include)
#  if __has_include(<systemd/sd-journal.h>)
#    include <systemd/sd-journal.h>
#    define LCBLOG_HAS_JOURNALD 1
#  else
#    define LCBLOG_HAS_JOURNALD 0
#  endif
#else
#  define LCBLOG_HAS_JOURNALD 0
#endif

#include <thread>

/**
 * @brief Converts a log level to its string representation.
 *
 * @param level The log level to convert.
 * @return A string representing the log level.
 */
std::string logLevelToString(LogLevel level)
{
    switch (level)
    {
    case DEBUG:
        return "DEBUG";
    case INFO:
        return "INFO";
    case WARN:
        return "WARN";
    case ERROR:
        return "ERROR";
    case FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

namespace {

int logLevelToJournaldPriority(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return 7; // Debug
    case LogLevel::INFO:
        return 6; // Informational
    case LogLevel::WARN:
        return 4; // Warning
    case LogLevel::ERROR:
        return 3; // Error
    case LogLevel::FATAL:
        return 2; // Critical
    default:
        return 5; // Notice
    }
}

[[maybe_unused]]
std::string sanitizeJournaldLine(const std::string &line)
{
    std::string cleaned = line;

    static const std::regex timestampRe(
        R"(^\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}\s+UTC\t)");
    cleaned = std::regex_replace(cleaned, timestampRe, "");

    static const std::regex levelPrefixRe(R"(^\[[A-Z]{4,7}\s*\]\s*)");
    cleaned = std::regex_replace(cleaned, levelPrefixRe, "");

    return cleaned;
}

#if LCBLOG_HAS_JOURNALD
void sendToJournald(int priority, const std::string &ident, const std::string &line)
{
    if (ident.empty())
    {
        (void)sd_journal_print(priority, "%s", line.c_str());
        return;
    }

    (void)sd_journal_send(
        "PRIORITY=%d", priority,
        "SYSLOG_IDENTIFIER=%s", ident.c_str(),
        "MESSAGE=%s", line.c_str(),
        nullptr);
}
#endif

void sendMessageLinesToJournald(int priority,
                               const std::string &ident,
                               const std::string &msg)
{
#if !LCBLOG_HAS_JOURNALD
    (void)priority;
    (void)ident;
    (void)msg;
#else
    size_t start = 0;
    while (start < msg.size())
    {
        size_t end = msg.find('\n', start);
        if (end == std::string::npos)
        {
            end = msg.size();
        }

        if (end > start)
        {
            std::string line = msg.substr(start, end - start);
            sendToJournald(priority, ident, sanitizeJournaldLine(line));
        }

        if (end == msg.size())
        {
            break;
        }
        start = end + 1;
    }
#endif
}

} // namespace


/**
 * @brief Constructs the logger and starts asynchronous worker threads.
 *
 * Initializes the log level and binds the output streams for immediate flushing.
 * Then spawns two worker threads—one for standard output and one for error output—
 * each processing its own message queue.
 *
 * @param outStream Reference to the std::ostream for INFO/WARN/DEBUG logs.
 * @param errStream Reference to the std::ostream for ERROR/FATAL logs.
 */
LCBLog::LCBLog(std::ostream &outStream, std::ostream &errStream)
    : logLevel(INFO) // Default threshold to INFO level
      ,
      out(outStream) // Stream for non-error messages
      ,
      err(errStream) // Stream for error messages
{
    // Ensure both streams flush on every insertion
    out << std::unitbuf;
    err << std::unitbuf;

    // Launch worker thread to drain the stdout queue
    outWorker_ = std::thread(
        &LCBLog::workerLoop, this,
        std::ref(outQueue_), std::ref(outMtx_),
        std::ref(outCv_), std::ref(std::cout));

    // Launch worker thread to drain the stderr queue
    errWorker_ = std::thread(
        &LCBLog::workerLoop, this,
        std::ref(errQueue_), std::ref(errMtx_),
        std::ref(errCv_), std::ref(std::cerr));
}

/**
 * @brief Cleans up the logger, ensuring remaining entries are processed.
 *
 * Signals worker threads to stop, wakes them if they are waiting,
 * and joins them so that all queued log messages are flushed before
 * destruction completes.
 */
LCBLog::~LCBLog()
{
    // Tell workers to exit their processing loops
    done_.store(true, std::memory_order_release);

    // Wake up any workers that are waiting on the condition variables
    outCv_.notify_all();
    errCv_.notify_all();

    // Wait for the stdout worker to finish draining its queue
    if (outWorker_.joinable())
    {
        outWorker_.join();
    }

    // Wait for the stderr worker to finish draining its queue
    if (errWorker_.joinable())
    {
        errWorker_.join();
    }
}

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
LCBLog &getLogger(std::ostream &out, std::ostream &err)
{
    // This static is constructed exactly once, on the first call.
    static LCBLog instance{out, err};
    return instance;
}

/**
 * @brief Returns the singleton logger instance.
 *
 * Constructs the LCBLog instance on first invocation using default
 * output and error streams. Subsequent calls ignore their parameters
 * and return the same instance.
 *
 * @return Reference to the global LCBLog singleton instance
 */
LCBLog& getLogger() {
    return getLogger(std::cout, std::cerr);
}

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
void LCBLog::workerLoop(std::deque<std::unique_ptr<LogEntry>> &queue,
                        std::mutex &mtx,
                        std::condition_variable &cv,
                        std::ostream &stream)
{
    struct BatchItem
    {
        LogLevel level;
        std::string msg;
    };

    std::vector<BatchItem> batch;
    batch.reserve(batchSize_);

    auto lastFlush = std::chrono::steady_clock::now();

    while (!done_.load(std::memory_order_acquire) || !queue.empty())
    {
        bool journaldEnabled = useJournald_.load(std::memory_order_acquire);

        {
            std::unique_lock<std::mutex> lk(mtx);
            if (journaldEnabled)
            {
                cv.wait(lk, [&] {
                    return done_.load(std::memory_order_acquire) || !queue.empty();
                });
            }
            else
            {
                cv.wait_for(lk, flushInterval_, [&] {
                    return done_.load(std::memory_order_acquire) || !queue.empty();
                });
            }

            while (!queue.empty() && batch.size() < batchSize_)
            {
                auto entry = std::move(queue.front());
                queue.pop_front();

                BatchItem item;
                item.level = entry->level;
                item.msg   = std::move(entry->msg);
                batch.push_back(std::move(item));
            }
        }

        if (journaldEnabled)
        {
            std::string ident;
            {
                std::lock_guard<std::mutex> lock(logMutex);
                ident = journaldIdent_;
            }

            for (auto &item : batch)
            {
                const int prio = logLevelToJournaldPriority(item.level);
                sendMessageLinesToJournald(prio, ident, item.msg);
            }
        }
        else
        {
            for (auto &item : batch)
            {
                stream << item.msg;
            }

            auto now = std::chrono::steady_clock::now();
            if (batch.size() >= batchSize_ || now - lastFlush >= flushInterval_)
            {
                stream << std::flush;
                lastFlush = now;
            }
        }

        batch.clear();
    }

    {
        std::lock_guard<std::mutex> lk(mtx);
        while (!queue.empty())
        {
            if (useJournald_.load(std::memory_order_acquire))
            {
                std::string ident;
                {
                    std::lock_guard<std::mutex> lock(logMutex);
                    ident = journaldIdent_;
                }

                const int prio = logLevelToJournaldPriority(queue.front()->level);
                sendMessageLinesToJournald(prio, ident, queue.front()->msg);
            }
            else
            {
                stream << queue.front()->msg;
            }
            queue.pop_front();
        }

        if (!useJournald_.load(std::memory_order_acquire))
        {
            stream << std::flush;
        }
    }
}

/**
 * @brief Set the minimum log level for filtering messages.
 *
 * This method updates the internal log level threshold in a thread-safe manner,
 * ensuring that subsequent log calls respect the new level.
 *
 * @param level New log level threshold.
 */
void LCBLog::setLogLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(logMutex);
    logLevel = level;
}

/**
 * @brief Enable or disable timestamping for log messages.
 *
 * This method updates the setting that determines whether each log
 * line is prefixed with a timestamp. It acquires a lock to ensure
 * thread safety when modifying the flag.
 *
 * @param enable True to include timestamps in logs, false to omit them.
 */
void LCBLog::enableTimestamps(bool enable)
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (useJournald_.load(std::memory_order_acquire))
    {
        printTimestamps = false;
        return;
    }

    printTimestamps = enable;
}

void LCBLog::enableJournald(bool enable)
{
    if (enable)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        printTimestamps = false;
    }

    useJournald_.store(enable && (LCBLOG_HAS_JOURNALD != 0),
                      std::memory_order_release);
    if (useJournald_.load(std::memory_order_acquire))
    {
        outCv_.notify_all();
        errCv_.notify_all();
    }
}

void LCBLog::setJournaldIdentifier(const std::string &ident)
{
    std::lock_guard<std::mutex> lock(logMutex);
    journaldIdent_ = ident;
}


/**
 * @brief Determine if a message meets the current log level threshold.
 *
 * This method compares the provided level against the internally stored
 * threshold to decide whether the message should be emitted.
 *
 * @param level Log level of the message to evaluate.
 * @return True if the message level is equal to or higher than the threshold.
 */
bool LCBLog::shouldLog(LogLevel level) const
{
    return level >= logLevel;
}

/**
 * @brief Generate a UTC timestamp string with millisecond precision.
 *
 * This function retrieves the current system time in UTC, formats it as
 * YYYY-MM-DD HH:MM:SS, appends a three-digit millisecond component, and
 * tags the result with "UTC".
 *
 * @return A formatted timestamp string in UTC with millisecond precision.
 */
std::string LCBLog::getStamp()
{
    using namespace std::chrono;

    // Retrieve current time point and convert to time_t
    auto now = system_clock::now();
    auto now_time_t = system_clock::to_time_t(now);

    // Compute milliseconds past the current second
    auto now_ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    // Convert to UTC broken-down time
    std::tm tm{};
    gmtime_r(&now_time_t, &tm);

    // Format date and time, then append milliseconds and UTC label
    std::ostringstream oss;
    oss << std::put_time(&tm, "%F %T");
    oss << '.' << std::setfill('0') << std::setw(3) << now_ms.count() << " UTC";

    return oss.str();
}

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
void LCBLog::crush(std::string &s)
{
    // Trim whitespace from both ends
    static const std::regex trimRe(R"(^\s+|\s+$)");
    s = std::regex_replace(s, trimRe, "");

    // Collapse multiple whitespace characters into one space
    static const std::regex wsRe(R"(\s+)");
    s = std::regex_replace(s, wsRe, " ");

    // Remove spaces before punctuation characters
    static const std::regex prePunctRe(R"(\s+([,\.!?:;]))");
    s = std::regex_replace(s, prePunctRe, "$1");

    // Remove spaces immediately after an opening parenthesis
    static const std::regex parenOpenRe(R"(\(\s+)");
    s = std::regex_replace(s, parenOpenRe, "(");

    // Remove spaces immediately before a closing parenthesis
    static const std::regex parenCloseRe(R"(\s+\))");
    s = std::regex_replace(s, parenCloseRe, ")");
}

/**
 * @brief Prevent unused function warnings for key `LCBLog` methods.
 *
 * This anonymous namespace references selected `LCBLog` member functions
 * so that static analysis tools and the linker recognize them as used,
 * avoiding unintended “unused” warnings.
 */
namespace
{
    /**
     * @brief Reference `LCBLog` methods to suppress warnings.
     *
     * This function takes the addresses of critical `LCBLog` methods
     * to ensure they are retained by the compiler and static analyzers.
     */
    void suppress_unused_warnings()
    {
        (void)&LCBLog::setLogLevel;
        (void)&LCBLog::enableTimestamps;
    }

    /**
     * @brief Invoke the suppression function at initialization.
     *
     * This static variable is initialized by calling
     * `suppress_unused_warnings()`, guaranteeing the function is
     * emitted without altering runtime behavior.
     */
    static const auto _suppress = (suppress_unused_warnings(), 0);
} // namespace (anonymous)
