#pragma once

#include "support_bundle_job_manager.hpp"

#include <cstdint>
#include <string>

inline constexpr std::uint64_t kSupportBundleProductionMaximumArchiveBytes =
    128ULL * 1024ULL * 1024ULL;

enum class SupportBundleDownloadFileFailure {
    none,
    reference_unavailable,
    missing,
    unsafe_file,
    ownership_mismatch,
    mode_mismatch,
    empty,
    oversized,
    open_failed,
};

class SupportBundleDownloadFile {
public:
    SupportBundleDownloadFile() = default;
    SupportBundleDownloadFile(int descriptor, std::uint64_t size, std::string basename);
    ~SupportBundleDownloadFile();

    SupportBundleDownloadFile(const SupportBundleDownloadFile &) = delete;
    SupportBundleDownloadFile &operator=(const SupportBundleDownloadFile &) = delete;
    SupportBundleDownloadFile(SupportBundleDownloadFile &&other) noexcept;
    SupportBundleDownloadFile &operator=(SupportBundleDownloadFile &&other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int descriptor() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] const std::string &basename() const noexcept;

private:
    int descriptor_ = -1;
    std::uint64_t size_ = 0;
    std::string basename_;
};

struct SupportBundleDownloadFileResult {
    SupportBundleDownloadFileFailure failure = SupportBundleDownloadFileFailure::open_failed;
    SupportBundleDownloadFile file;

    [[nodiscard]] bool available() const noexcept {
        return failure == SupportBundleDownloadFileFailure::none && file.valid();
    }
};

SupportBundleDownloadFileResult open_support_bundle_download_file(
    const SupportBundleDownloadReference &reference,
    std::uint64_t maximum_bytes = kSupportBundleProductionMaximumArchiveBytes);
