// SPDX-License-Identifier: MIT
#include "pps_input.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
namespace wsprrypi {
PpsInputBinding resolve_pps_input(unsigned gpio, const std::string& root) {
    if (gpio > 27) throw std::runtime_error("PPS input GPIO must be an integer from 0 through 27.");
    std::ostringstream expected;
    expected << "pps@" << std::hex << gpio << ".-1";
    PpsInputBinding result;
    unsigned matches = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().string();
        if (name.size() <= 3 || name.substr(0,3) != "pps" ||
            !std::all_of(name.begin()+3, name.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
        std::ifstream input(entry.path() / "name");
        std::string source; std::getline(input, source);
        if (source != expected.str()) continue;
        struct stat state{};
        if (::stat(entry.path().c_str(), &state) != 0)
            throw std::runtime_error("Cannot verify selected PPS input identity.");
        result.device_name = name;
        result.identity = std::filesystem::canonical(entry.path()).string() + ":" +
            std::to_string(static_cast<unsigned long long>(state.st_dev)) + ":" +
            std::to_string(static_cast<unsigned long long>(state.st_ino));
        ++matches;
    }
    if (matches != 1) throw std::runtime_error(
        "Configure exactly one existing Linux PPS input on GPIO" + std::to_string(gpio) +
        "; no GPIO configuration is changed by calibration.");
    return result;
}
}
