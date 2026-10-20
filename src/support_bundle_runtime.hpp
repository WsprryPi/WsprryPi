#pragma once

#include "support_bundle_job_manager.hpp"
#include "support_bundle_startup_cleanup.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

// The installer provisions the collector root-owned and executable at this
// fixed path; production runtime requests cannot override it.
inline constexpr std::string_view kSupportBundleProductionCollectorPath =
    "/usr/local/lib/wsprrypi/collect-support-bundle.sh";
inline constexpr std::string_view kSupportBundleProductionStorageRoot =
    "/var/lib/wsprrypi/support-bundles";

struct SupportBundleRuntimeTestDependencies {
    std::filesystem::path collector_executable;
    std::filesystem::path storage_root;
    std::function<std::string()> id_generator;
    std::shared_ptr<SupportBundleJobExecutor> executor;
    std::string project_version;
    std::chrono::milliseconds timeout = std::chrono::minutes(10);
    std::chrono::milliseconds term_grace = std::chrono::seconds(2);
    bool run_startup_cleanup = false;
    std::function<SupportBundleStartupCleanupResult(const std::filesystem::path &)>
        startup_cleanup = [](const std::filesystem::path &root) {
            return cleanup_stale_support_bundle_jobs(root);
        };
};

class SupportBundleRuntime {
public:
    static std::string generate_secure_job_id();

    static std::unique_ptr<SupportBundleJobManager> create_production();

    // This override seam is for native tests only. Runtime requests never
    // supply paths, executors, timeouts, ID generators, or cleanup behavior.
    static std::unique_ptr<SupportBundleJobManager> create_for_testing(
        SupportBundleRuntimeTestDependencies dependencies);
};
