// SPDX-License-Identifier: MIT
#pragma once
#include "config_handler.hpp"
#include "application_recovery/restore_hold.hpp"
#include "privileged_network_runtime.hpp"
namespace wsprrypi {
// Internal final-publication/handoff scope. No network, PAKE, ACK wait or
// worker join belongs inside it. It grants no principal or pair authority.
struct PiPairPolicyAdmission {
    std::unique_lock<std::recursive_mutex> configuration{config_update_mutex(),std::try_to_lock};
    std::unique_lock<std::mutex> admission{privileged_network_admission_mutex(),std::defer_lock};
    PiPairPolicyAdmission(){if(!configuration.owns_lock()||!admission.try_lock())return;}
    bool allowed() const {
        if(!configuration.owns_lock()||!admission.owns_lock()||restore_maintenance_active())return false;
        const auto state=privileged_network_runtime_state();return state.active_known&&state.active==PrivilegedNetworkMode::enforced&&state.configured==PrivilegedNetworkMode::enforced;
    }
};
}
