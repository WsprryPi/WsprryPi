// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// Private authoritative selector, independent of PairRecord and wire formats.
// Digests bind the complete catalog/operation after-images on cold startup.
nlohmann::json pi_pair_history_record(const std::string &self,const std::string &namespace_id,
    const nlohmann::json &initialization,const nlohmann::json &catalog,
    const nlohmann::json &operations,const nlohmann::json &peers,bool clean_shutdown);
bool valid_pi_pair_history_record(const nlohmann::json &,const std::string &self) noexcept;
nlohmann::json parse_pi_pair_history_record(const std::string &,const std::string &self);
} // namespace wsprrypi
