#pragma once

#include "support_bundle_archive_digest.hpp"
#include "support_bundle_checksum_file.hpp"

#include <cstdint>
#include <string>

enum class SupportBundleDownloadPreparationFailure {
    none,
    unavailable,
    unsafe,
    corrupt,
    internal,
};

class SupportBundlePreparedDownload {
public:
    SupportBundlePreparedDownload() = default;
    explicit SupportBundlePreparedDownload(SupportBundleDownloadFile archive);

    SupportBundlePreparedDownload(const SupportBundlePreparedDownload &) = delete;
    SupportBundlePreparedDownload &operator=(const SupportBundlePreparedDownload &) = delete;
    SupportBundlePreparedDownload(SupportBundlePreparedDownload &&) noexcept = default;
    SupportBundlePreparedDownload &operator=(SupportBundlePreparedDownload &&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int descriptor() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] const std::string &basename() const noexcept;

private:
    SupportBundleDownloadFile archive_;
};

struct SupportBundleDownloadPreparationResult {
    SupportBundleDownloadPreparationFailure failure =
        SupportBundleDownloadPreparationFailure::internal;
    SupportBundlePreparedDownload download;

    [[nodiscard]] bool available() const noexcept {
        return failure == SupportBundleDownloadPreparationFailure::none && download.valid();
    }
};

SupportBundleDownloadPreparationResult prepare_support_bundle_download(
    const SupportBundleDownloadReference &reference,
    std::uint64_t maximum_archive_bytes = kSupportBundleProductionMaximumArchiveBytes);
