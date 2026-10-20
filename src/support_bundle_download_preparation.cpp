#include "support_bundle_download_preparation.hpp"

#include <utility>

namespace {
SupportBundleDownloadPreparationResult failure(
    SupportBundleDownloadPreparationFailure category) {
    return {category, {}};
}

SupportBundleDownloadPreparationFailure map_checksum_failure(
    SupportBundleChecksumFileFailure category) {
    switch (category) {
    case SupportBundleChecksumFileFailure::reference_unavailable:
    case SupportBundleChecksumFileFailure::missing:
        return SupportBundleDownloadPreparationFailure::unavailable;
    case SupportBundleChecksumFileFailure::unsafe_file:
    case SupportBundleChecksumFileFailure::ownership_mismatch:
    case SupportBundleChecksumFileFailure::mode_mismatch:
        return SupportBundleDownloadPreparationFailure::unsafe;
    case SupportBundleChecksumFileFailure::empty:
    case SupportBundleChecksumFileFailure::oversized:
    case SupportBundleChecksumFileFailure::malformed:
        return SupportBundleDownloadPreparationFailure::corrupt;
    case SupportBundleChecksumFileFailure::open_failed:
        return SupportBundleDownloadPreparationFailure::internal;
    case SupportBundleChecksumFileFailure::none:
        break;
    }
    return SupportBundleDownloadPreparationFailure::internal;
}

SupportBundleDownloadPreparationFailure map_archive_failure(
    SupportBundleDownloadFileFailure category) {
    switch (category) {
    case SupportBundleDownloadFileFailure::reference_unavailable:
    case SupportBundleDownloadFileFailure::missing:
        return SupportBundleDownloadPreparationFailure::unavailable;
    case SupportBundleDownloadFileFailure::unsafe_file:
    case SupportBundleDownloadFileFailure::ownership_mismatch:
    case SupportBundleDownloadFileFailure::mode_mismatch:
        return SupportBundleDownloadPreparationFailure::unsafe;
    case SupportBundleDownloadFileFailure::empty:
    case SupportBundleDownloadFileFailure::oversized:
        return SupportBundleDownloadPreparationFailure::corrupt;
    case SupportBundleDownloadFileFailure::open_failed:
        return SupportBundleDownloadPreparationFailure::internal;
    case SupportBundleDownloadFileFailure::none:
        break;
    }
    return SupportBundleDownloadPreparationFailure::internal;
}

SupportBundleDownloadPreparationFailure map_digest_failure(
    SupportBundleArchiveDigestFailure category) {
    switch (category) {
    case SupportBundleArchiveDigestFailure::invalid_expected_digest:
        return SupportBundleDownloadPreparationFailure::unavailable;
    case SupportBundleArchiveDigestFailure::truncated:
    case SupportBundleArchiveDigestFailure::size_mismatch:
    case SupportBundleArchiveDigestFailure::digest_mismatch:
        return SupportBundleDownloadPreparationFailure::corrupt;
    case SupportBundleArchiveDigestFailure::invalid_file:
    case SupportBundleArchiveDigestFailure::seek_failed:
    case SupportBundleArchiveDigestFailure::read_failed:
    case SupportBundleArchiveDigestFailure::digest_failed:
        return SupportBundleDownloadPreparationFailure::internal;
    case SupportBundleArchiveDigestFailure::none:
        break;
    }
    return SupportBundleDownloadPreparationFailure::internal;
}
}  // namespace

SupportBundlePreparedDownload::SupportBundlePreparedDownload(
    SupportBundleDownloadFile archive)
    : archive_(std::move(archive)) {}

bool SupportBundlePreparedDownload::valid() const noexcept {
    return archive_.valid();
}

int SupportBundlePreparedDownload::descriptor() const noexcept {
    return archive_.descriptor();
}

std::uint64_t SupportBundlePreparedDownload::size() const noexcept {
    return archive_.size();
}

const std::string &SupportBundlePreparedDownload::basename() const noexcept {
    return archive_.basename();
}

SupportBundleDownloadPreparationResult prepare_support_bundle_download(
    const SupportBundleDownloadReference &reference,
    std::uint64_t maximum_archive_bytes) {
    const auto checksum = validate_support_bundle_checksum_file(reference);
    if (!checksum.valid()) {
        return failure(map_checksum_failure(checksum.failure));
    }

    auto archive = open_support_bundle_download_file(reference, maximum_archive_bytes);
    if (!archive.available()) {
        return failure(map_archive_failure(archive.failure));
    }

    const auto digest = verify_support_bundle_archive_digest(
        archive.file, reference.expected_sha256);
    if (!digest.valid()) {
        return failure(map_digest_failure(digest.failure));
    }

    return {SupportBundleDownloadPreparationFailure::none,
            SupportBundlePreparedDownload(std::move(archive.file))};
}
