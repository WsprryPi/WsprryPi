// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
namespace wsprrypi {
bool valid_web_bootstrap(const nlohmann::json &);
nlohmann::json read_web_bootstrap_cache(const std::string &);
void write_web_bootstrap_cache(const std::string &,const nlohmann::json &);
}
