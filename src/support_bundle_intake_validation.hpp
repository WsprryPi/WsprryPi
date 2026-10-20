#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class SupportBundleIntakeFailure {
    none,
    manifest_oversized,
    signature_envelope_oversized,
    invalid_signature_envelope,
    unknown_signing_key,
    invalid_signature,
    invalid_manifest,
    wrong_project,
    invalid_generation,
    rollback,
    same_generation_mutated,
    invalid_time,
    outside_validity_window,
    unsupported_status,
    incompatible_client,
    invalid_client_version,
    upgrade_required,
    invalid_request_url,
    invalid_release_url,
    unknown_bundle_key,
};

struct SupportBundleIntakeSigningKey {
    std::string key_id;
    std::array<unsigned char, 32> public_key{};
};

struct SupportBundleIntakePreviousState {
    std::uint64_t generation = 0;
    std::string manifest_sha256;
};

struct SupportBundleIntakeValidationRequest {
    std::string manifest_bytes;
    std::string signature_envelope_bytes;
    std::vector<SupportBundleIntakeSigningKey> signing_keys;
    std::vector<std::string> recognized_bundle_key_ids;
    std::string installed_upload_version;
    std::int64_t now_utc_seconds = 0;
    std::uint64_t client_protocol = 1;
    std::optional<SupportBundleIntakePreviousState> previous;
};

struct SupportBundleIntakeManifest {
    std::uint64_t generation = 0;
    std::string published_at;
    std::string expires_at;
    std::string status;
    std::uint64_t minimum_client_protocol = 0;
    std::string minimum_upload_version;
    std::optional<std::string> request_url;
    std::string release_url;
    std::optional<std::string> user_message;
    std::string bundle_encryption_key_id;
    std::string manifest_sha256;
    std::string signing_key_id;
};

struct SupportBundleIntakeValidationResult {
    SupportBundleIntakeFailure failure = SupportBundleIntakeFailure::invalid_manifest;
    SupportBundleIntakeManifest manifest;
    struct UpgradeRequirement {
        std::string minimum_upload_version;
        std::string release_url;
        std::optional<std::string> user_message;
    };
    std::optional<UpgradeRequirement> upgrade;
    std::optional<SupportBundleIntakePreviousState> accepted_state;
    [[nodiscard]] bool valid() const noexcept {
        return failure == SupportBundleIntakeFailure::none;
    }
};

SupportBundleIntakeValidationResult validate_support_bundle_intake(
    const SupportBundleIntakeValidationRequest &request);

// Shared strict SemVer 2 syntax check for trusted runtime construction.
bool valid_support_bundle_semver(const std::string &value) noexcept;
