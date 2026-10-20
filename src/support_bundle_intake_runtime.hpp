#pragma once

#include "support_bundle_intake_controller.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kSupportBundleIntakeProductionStateRoot =
    "/var/lib/wsprrypi/support-bundles";
inline constexpr std::uint64_t kSupportBundleIntakeClientProtocol = 1;

struct SupportBundleIntakeRuntimeTrust {
    std::vector<SupportBundleIntakeSigningKey> signing_keys;
    std::vector<std::string> recognized_bundle_key_ids;
};

enum class SupportBundleIntakeRuntimeStatus {
    completed,
    invalid_trust_metadata,
    invalid_runtime_environment,
    resolution_failed,
};

struct SupportBundleIntakeRuntimeResult {
    SupportBundleIntakeRuntimeStatus status =
        SupportBundleIntakeRuntimeStatus::invalid_runtime_environment;
    SupportBundleIntakeControllerResult controller;
    [[nodiscard]] bool completed() const noexcept {
        return status == SupportBundleIntakeRuntimeStatus::completed;
    }
};

SupportBundleIntakeRuntimeResult resolve_support_bundle_intake_runtime(
    const SupportBundleIntakeRuntimeTrust &trust);

struct SupportBundleIntakeRuntimeTestDependencies {
    std::filesystem::path state_root;
    std::function<std::string()> installed_version;
    std::function<std::optional<std::int64_t>()> now_utc_seconds;
    std::function<SupportBundleIntakeControllerResult(
        const SupportBundleIntakeControllerRequest &)> resolve;
};

// Typed in-process dependency seam; production paths, clock, version, and
// controller are fixed and cannot be supplied through runtime configuration.
SupportBundleIntakeRuntimeResult resolve_support_bundle_intake_runtime_for_test(
    const SupportBundleIntakeRuntimeTrust &trust,
    const SupportBundleIntakeRuntimeTestDependencies &dependencies);
