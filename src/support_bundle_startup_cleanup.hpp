#pragma once

#include "support_bundle_job_directory_remover.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

enum class SupportBundleStartupCleanupStatus {
    completed,
    unavailable,
    unsafe_root,
    partial_failure,
};

struct SupportBundleStartupCleanupResult {
    SupportBundleStartupCleanupStatus status = SupportBundleStartupCleanupStatus::unavailable;
    std::size_t removed_count = 0;
    std::size_t failed_count = 0;
};

using SupportBundleStartupJobRemover = std::function<SupportBundleJobDirectoryRemovalResult(
    const std::filesystem::path &, const std::string &)>;

// Startup-only cleanup for stale direct-child job directories. The result never
// includes names, paths, or filesystem diagnostics.
SupportBundleStartupCleanupResult cleanup_stale_support_bundle_jobs(
    const std::filesystem::path &storage_root,
    SupportBundleStartupJobRemover remover = remove_support_bundle_job_directory);
