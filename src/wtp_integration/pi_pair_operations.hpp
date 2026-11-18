// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// Private pairing operation history. No entry grants authority. Principals and
// current generations must still be checked before receipt access or mutation.
// Rotation continuations may contain a temporary private key; never serialize
// this book through a browser API, diagnostics, logs, or a support bundle.
bool valid_pi_pair_operations(const nlohmann::json &,const std::string &self) noexcept;
nlohmann::json parse_pi_pair_operations(const std::string &,const std::string &self);
struct PiPairOperationsUpdate {nlohmann::json document;bool evicted{};};
// Pure bounded projection. The caller atomically publishes it with the catalog.
// If evicted is true it must retire affected local edit contexts before further
// admission; no unresolved operation is ever evicted. Input stays unchanged.
PiPairOperationsUpdate pi_pair_operations(const std::string &self,const nlohmann::json &entries);
} // namespace wsprrypi
