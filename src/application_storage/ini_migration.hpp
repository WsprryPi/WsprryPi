// SPDX-License-Identifier: MIT
#pragma once
#include "persistent_configuration.hpp"
namespace wsprrypi {
struct IniMigrationConfiguration {
    nlohmann::json internal;
    PrivateConfiguration private_settings;
    std::vector<std::string> warnings;
};
// One-time source conversion only. Uses the existing parser/normalizer without
// publishing runtime state, opening devices, saving a file or granting output.
IniMigrationConfiguration decode_ini_migration_configuration(const std::string &bytes,StateRole);
} // namespace wsprrypi
