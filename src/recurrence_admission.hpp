// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
namespace wsprrypi {
// Parent-owned legacy recurrence fence. It neither grants manual/one-shot
// permission nor changes independently claimed WTP authorization.
enum class LegacyRecurrenceAdmission {Open,DrainAcceptedPair,Held};
void set_legacy_recurrence_admission(std::function<LegacyRecurrenceAdmission()>);
LegacyRecurrenceAdmission legacy_recurrence_admission() noexcept;
bool legacy_recurrence_admitted() noexcept;
}
