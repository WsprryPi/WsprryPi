// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
namespace wsprrypi {
// Process-wide exclusion across legacy single-output and fleet runtime owners.
std::shared_ptr<void> wtp_reserve_output_identity(const std::string& device_id);
bool wtp_output_identity_in_use(const std::string& device_id);
}
