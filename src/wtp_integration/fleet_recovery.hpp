// SPDX-License-Identifier: MIT
#pragma once
#include "scheduler.hpp"
namespace wsprrypi {
// A changed boot ends the old session, not the old slot's uncertainty. Only an
// explicit Fleet recovery may replace it after fresh read-only negotiation.
inline bool wtp_fleet_restarted_output_safe(const WtpRuntimeStatus& previous,
    const WtpRuntimeStatus& fresh, const std::string& device, const std::string& product) {
    if(previous.session_phase!=wtp::SessionPhase::IdentityChanged || !previous.identity ||
       previous.identity->device_id!=device || previous.identity->product!=product ||
       fresh.phase!=WtpSchedulePhase::Idle || fresh.session_phase!=wtp::SessionPhase::Ready ||
       !fresh.identity || !fresh.capabilities || !fresh.remote || !fresh.status_observed_ms ||
       fresh.identity->device_id!=device || fresh.identity->product!=product ||
       fresh.identity->boot_id==previous.identity->boot_id ||
       fresh.remote->boot_id!=fresh.identity->boot_id || fresh.uncertain || fresh.safety_fault ||
       fresh.owns || fresh.remote->owner_id || fresh.remote->output_active) return false;
    return fresh.remote->state==wtp::State::Empty || fresh.remote->state==wtp::State::Complete ||
           fresh.remote->state==wtp::State::Aborted || fresh.remote->state==wtp::State::Missed;
}
} // namespace wsprrypi
