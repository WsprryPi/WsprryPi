// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
namespace wsprrypi {
inline constexpr std::size_t kMaximumFleetOutputs = 32;
// Allow escaped maximum-length connection settings, schedules, indentation,
// and the separately bounded removal history. Enforce the same bound on save.
inline constexpr std::size_t kMaximumFleetFileBytes = kMaximumFleetOutputs * 16384 + 16384;
}
