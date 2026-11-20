// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
namespace wsprrypi {
std::optional<std::uint64_t> recovery_evidence_member_maximum(std::string_view logical_name);
// Immutable inactive evidence only. Verifies the closed index and streamed
// payload digests; returned paths are backend-private snapshot inputs.
std::map<std::string,std::string> capture_recovery_evidence(const std::string &generation);
}
