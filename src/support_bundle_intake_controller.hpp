#pragma once

#include "support_bundle_intake_retrieval.hpp"
#include "support_bundle_intake_state.hpp"
#include "support_bundle_intake_validation.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

enum class SupportBundleIntakeControllerFailure {
    none,
    state_load_failed,
    retrieval_failed,
    validation_failed,
    state_commit_failed,
    state_durability_uncertain,
};

struct SupportBundleIntakeControllerRequest {
    std::filesystem::path state_root;
    SupportBundleIntakeRetrievalRequest retrieval;
    std::vector<SupportBundleIntakeSigningKey> signing_keys;
    std::vector<std::string> recognized_bundle_key_ids;
    std::string installed_upload_version;
    std::int64_t now_utc_seconds = 0;
    std::uint64_t client_protocol = 1;
};

struct SupportBundleIntakeControllerResult {
    SupportBundleIntakeControllerFailure failure =
        SupportBundleIntakeControllerFailure::state_load_failed;
    SupportBundleIntakeStateLoadStatus state_load_status =
        SupportBundleIntakeStateLoadStatus::invalid_state;
    SupportBundleIntakeRetrievalFailure retrieval_failure =
        SupportBundleIntakeRetrievalFailure::invalid_request;
    SupportBundleIntakeFailure validation_failure =
        SupportBundleIntakeFailure::invalid_manifest;
    SupportBundleIntakeStateCommitStatus state_commit_status =
        SupportBundleIntakeStateCommitStatus::invalid_input;
    SupportBundleIntakeManifest manifest;
    std::optional<SupportBundleIntakeValidationResult::UpgradeRequirement> upgrade;
    [[nodiscard]] bool ready() const noexcept {
        return failure == SupportBundleIntakeControllerFailure::none;
    }
};

SupportBundleIntakeControllerResult resolve_support_bundle_intake(
    const SupportBundleIntakeControllerRequest &request);

struct SupportBundleIntakeControllerDependencies {
    std::function<SupportBundleIntakeStateLoadResult(const std::filesystem::path &)> load_state;
    std::function<SupportBundleIntakeRetrievalResult(
        const SupportBundleIntakeRetrievalRequest &)> retrieve;
    std::function<SupportBundleIntakeValidationResult(
        const SupportBundleIntakeValidationRequest &)> validate;
    std::function<SupportBundleIntakeStateCommitResult(
        const std::filesystem::path &, const SupportBundleIntakeState &)> commit_state;
};

// Typed in-process dependency seam for deterministic tests; not runtime configuration.
SupportBundleIntakeControllerResult resolve_support_bundle_intake_for_test(
    const SupportBundleIntakeControllerRequest &request,
    const SupportBundleIntakeControllerDependencies &dependencies);
