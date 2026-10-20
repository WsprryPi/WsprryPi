#pragma once

#include "support_bundle_download_file.hpp"

#include <string>

enum class SupportBundleArchiveDigestFailure {
    none,
    invalid_file,
    invalid_expected_digest,
    seek_failed,
    read_failed,
    truncated,
    size_mismatch,
    digest_failed,
    digest_mismatch,
};

struct SupportBundleArchiveDigestValidation {
    SupportBundleArchiveDigestFailure failure = SupportBundleArchiveDigestFailure::digest_failed;

    [[nodiscard]] bool valid() const noexcept {
        return failure == SupportBundleArchiveDigestFailure::none;
    }
};

SupportBundleArchiveDigestValidation verify_support_bundle_archive_digest(
    SupportBundleDownloadFile &archive,
    const std::string &expected_sha256);
