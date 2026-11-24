// SPDX-License-Identifier: MIT
#pragma once
#include <optional>

// Admission for a dedicated zero-output artifact, before any runtime owner.
// Independent web, maintenance, and local-command roles retain their validators.
std::optional<int> controller_profile_entry(int argc, char *argv[]);
