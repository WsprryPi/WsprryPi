#pragma once

#include "support_bundle_intake_runtime.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

enum class SupportBundleIntakeProductionStatus {
    active,
    disabled,
    upgrade_required,
    unavailable,
};

struct SupportBundleIntakeProductionResult {
    SupportBundleIntakeProductionStatus status =
        SupportBundleIntakeProductionStatus::unavailable;
    std::uint64_t generation = 0;
    std::string expires_at;
    std::string minimum_upload_version;
    std::string signing_key_id;
    std::string bundle_key_id;
    std::optional<std::string> request_url;
    std::string release_url;
    std::optional<std::string> user_message;
};

SupportBundleIntakeProductionResult resolve_support_bundle_intake_production();

using SupportBundleIntakeRuntimeProvider =
    std::function<SupportBundleIntakeRuntimeResult()>;

// Typed in-process provider seam; not runtime configuration or a public API.
SupportBundleIntakeProductionResult resolve_support_bundle_intake_production_for_test(
    const SupportBundleIntakeRuntimeProvider &provider);
