#pragma once

#include "support_bundle_job_manager.hpp"

#include <cstddef>

inline constexpr std::size_t kSupportBundleMaximumChecksumSidecarBytes = 512;

enum class SupportBundleChecksumFileFailure {
    none,
    reference_unavailable,
    missing,
    unsafe_file,
    ownership_mismatch,
    mode_mismatch,
    empty,
    oversized,
    open_failed,
    malformed,
};

struct SupportBundleChecksumFileValidation {
    SupportBundleChecksumFileFailure failure = SupportBundleChecksumFileFailure::malformed;

    [[nodiscard]] bool valid() const noexcept {
        return failure == SupportBundleChecksumFileFailure::none;
    }
};

SupportBundleChecksumFileValidation validate_support_bundle_checksum_file(
    const SupportBundleDownloadReference &reference,
    std::size_t maximum_bytes = kSupportBundleMaximumChecksumSidecarBytes);
