/**
 * @file arg_parser_internal.hpp
 * @brief Private cross-translation-unit support for argument parsing.
 */

#ifndef ARG_PARSER_INTERNAL_HPP
#define ARG_PARSER_INTERNAL_HPP

#include "config_types.hpp"
#include "lcblog.hpp"

#include <atomic>
#include <sstream>
#include <string>
#include <utility>

class BandLookup;

extern std::atomic<bool> startup_diagnostic_deferral_enabled;

void defer_startup_diagnostic(LogLevel level, std::string message);

template <typename... Args>
void log_startup_config_message(LogLevel level, Args &&...args)
{
    if (startup_diagnostic_deferral_enabled.load(std::memory_order_acquire))
    {
        std::ostringstream oss;
        (oss << ... << args);
        defer_startup_diagnostic(level, oss.str());
        return;
    }

    llog.logS(level, std::forward<Args>(args)...);
}

namespace arg_parser_internal
{
    bool persisted_qrss_config_available(const ArgParserConfig &cfg) noexcept;
    bool persisted_fskcw_config_available(const ArgParserConfig &cfg) noexcept;
    bool persisted_dfcw_config_available(const ArgParserConfig &cfg) noexcept;

    std::string get_wspr_gpio_suffix_for_entry(
        const WsprFrequencyEntry &entry,
        const ArgParserConfig &config,
        BandLookup &lookup);
}

#endif // ARG_PARSER_INTERNAL_HPP
