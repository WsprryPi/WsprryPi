#pragma once

#include <chrono>
#include <filesystem>
#include <string>

inline constexpr const char *kWsprryPiIntakeManifestUrl =
    "https://raw.githubusercontent.com/WsprryPi/support-intake/main/wsprrypi/intake.json";
inline constexpr const char *kWsprryPiIntakeSignatureUrl =
    "https://raw.githubusercontent.com/WsprryPi/support-intake/main/wsprrypi/intake.json.sig";

enum class SupportBundleIntakeRetrievalFailure {
    none,
    invalid_request,
    executable_unavailable,
    launch_failed,
    manifest_timeout,
    manifest_failed,
    manifest_empty,
    manifest_oversized,
    signature_timeout,
    signature_failed,
    signature_empty,
    signature_oversized,
};

struct SupportBundleIntakeRetrievalRequest {
    std::string manifest_url = kWsprryPiIntakeManifestUrl;
    std::string signature_url = kWsprryPiIntakeSignatureUrl;
    std::filesystem::path curl_executable = "/usr/bin/curl";
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(5);
    std::chrono::milliseconds operation_timeout = std::chrono::seconds(15);
    std::size_t maximum_manifest_bytes = 16 * 1024;
    std::size_t maximum_signature_bytes = 2 * 1024;
};

struct SupportBundleIntakeRetrievalResult {
    SupportBundleIntakeRetrievalFailure failure =
        SupportBundleIntakeRetrievalFailure::invalid_request;
    std::string manifest_bytes;
    std::string signature_envelope_bytes;
    [[nodiscard]] bool retrieved() const noexcept {
        return failure == SupportBundleIntakeRetrievalFailure::none;
    }
};

SupportBundleIntakeRetrievalResult retrieve_support_bundle_intake(
    const SupportBundleIntakeRetrievalRequest &request);

struct SupportBundleIntakeRetrievalTestHooks {
    bool fail_parent_process_group_setup = false;
};

// Typed in-process fixture seam; production always requires root-owned /usr/bin/curl.
SupportBundleIntakeRetrievalResult retrieve_support_bundle_intake_for_test(
    const SupportBundleIntakeRetrievalRequest &request,
    const SupportBundleIntakeRetrievalTestHooks &hooks = {});
