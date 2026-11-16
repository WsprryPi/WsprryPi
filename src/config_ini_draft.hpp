// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <functional>
#include <string>
#include <vector>
// Trusted parent-only parser observation, not a public or validated settings
// document. File stamps and private references must never be sent to a browser;
// use the closed, redacting recovery projection before serialization.
struct ConfigIniDraftSnapshot {
    std::string filename,file_stamp;
    nlohmann::json source;
    std::vector<std::string> present_fields;
    // Original fixed-field input for bounded diagnostics when native parsing
    // yields a wrong type (for example a numeric-looking string). Private too.
    nlohmann::json raw_source = nlohmann::json::object();
};
using ConfigIniDraftObserver=std::function<void(const ConfigIniDraftSnapshot &)>;
