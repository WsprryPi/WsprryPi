#pragma once

#include "httplib.hpp"
#include "support_bundle_intake_production.hpp"
#include "support_bundle_job_manager.hpp"
#include "support_request_guard.hpp"

#include <functional>

using SupportRequestGuardSnapshotProvider =
    std::function<SupportRequestGuardSnapshot()>;
using SupportBundleIntakeProductionProvider =
    std::function<SupportBundleIntakeProductionResult()>;

// Registers the guarded support-bundle lifecycle API and the on-demand intake
// capability endpoint. Providers are typed in-process dependencies, not public
// runtime configuration.
void register_support_bundle_http_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    SupportRequestGuardSnapshotProvider snapshot_provider,
    SupportBundleIntakeProductionProvider intake_provider);
