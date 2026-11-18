// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// P62-05 private outgoing-key index. Never a browser document, discovery cache,
// enrollment assertion or TLS authority. The owning coordinator checks those
// boundaries and publishes the complete index with its operation/selector.
nlohmann::json empty_pi_pair_peers(const std::string &self);
bool valid_pi_pair_peers(const nlohmann::json &,const std::string &self) noexcept;
nlohmann::json parse_pi_pair_peers(const std::string &,const std::string &self);
// Pure P62-09 projection, not authority or publication. Its owner must first
// verify explicit protected intent and mutual PAKE, and atomically reserve the
// replacement operation with this image. It preserves the old row exactly.
nlohmann::json supersede_pi_pair_pending(const nlohmann::json &,const std::string &self,
    const nlohmann::json &replacement);
} // namespace wsprrypi
