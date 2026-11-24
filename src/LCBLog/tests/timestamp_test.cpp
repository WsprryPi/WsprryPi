// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "lcblog.hpp"
#include <ctime>
#include <regex>
#include <stdexcept>

static std::string utc_now() {
    const auto now=std::time(nullptr);
    const auto *utc=std::gmtime(&now);
    if(!utc)throw std::runtime_error("Test UTC clock unavailable");
    std::ostringstream output;
    output<<std::put_time(utc,"%Y-%m-%d %H:%M:%S");
    return output.str();
}
int main() {
    std::ostringstream captured;
    auto *original=std::cout.rdbuf(captured.rdbuf());
    struct Restore {std::streambuf *value;~Restore(){std::cout.rdbuf(value);}} restore{original};
    const auto before=utc_now();
    {
        LCBLog logger;
        logger.enableTimestamps(true);
        logger.logS(INFO,"native-timestamp-check");
    } // Drain the actual asynchronous logger before inspecting its output.
    const auto after=utc_now();
    const auto output=captured.str();
    const std::regex timestamp(R"((\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\.\d{3} UTC\t[^\n]*native-timestamp-check)");
    std::smatch match;
    if(!std::regex_search(output,match,timestamp)||match[1].str()<before||match[1].str()>after)
        throw std::runtime_error("Native log timestamp must retain UTC format and current UTC time");
    std::cerr<<"PASS native UTC timestamp format, clock, and asynchronous drain\n";
}
