// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace wsprrypi {
struct ManagedOutputObservation {
    std::uint64_t generation{};
    std::optional<std::string> owner_id,job_id;
    bool operator==(const ManagedOutputObservation &) const = default;
};
}
