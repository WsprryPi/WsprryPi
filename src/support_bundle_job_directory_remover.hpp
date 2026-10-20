#pragma once

#include <filesystem>
#include <string>

enum class SupportBundleJobDirectoryRemovalFailure {
    none,
    invalid_root,
    invalid_job_id,
    root_unavailable,
    unsafe_root,
    unsafe_target,
    unsupported_entry,
    removal_failed,
};

struct SupportBundleJobDirectoryRemovalResult {
    SupportBundleJobDirectoryRemovalFailure failure =
        SupportBundleJobDirectoryRemovalFailure::removal_failed;

    [[nodiscard]] bool removed() const noexcept {
        return failure == SupportBundleJobDirectoryRemovalFailure::none;
    }
};

// Removes only the private direct child identified by job_id.  The caller must
// supply the daemon-owned storage root; no path is accepted from an HTTP client.
SupportBundleJobDirectoryRemovalResult remove_support_bundle_job_directory(
    const std::filesystem::path &storage_root,
    const std::string &job_id);
