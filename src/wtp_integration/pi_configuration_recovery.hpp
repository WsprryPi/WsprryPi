// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "config_ini_draft.hpp"
#include <cstddef>
namespace wsprrypi {
// Pure projection of an unconfirmed owning-parser snapshot. Only current
// configuration fields survive; invalid values remain explicit, and private
// connection-file references are never returned. No file or runtime mutation.
nlohmann::json pi_configuration_recovery_draft(const nlohmann::json &internal,
    std::size_t maximum_bytes=28000);
nlohmann::json pi_configuration_recovery_draft(const ConfigIniDraftSnapshot &,
    std::size_t maximum_bytes=28000);
} // namespace wsprrypi
