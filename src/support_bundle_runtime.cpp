#include "support_bundle_runtime.hpp"

#include "support_bundle_collector_executor.hpp"
#include "build_metadata.hpp"

#include <array>
#include <openssl/rand.h>
#include <stdexcept>
#include <utility>

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
}

std::string SupportBundleRuntime::generate_secure_job_id() {
    std::array<unsigned char, 16> random_bytes{};
    if (RAND_bytes(random_bytes.data(),
                   static_cast<int>(random_bytes.size())) != 1) {
        throw std::runtime_error("secure randomness unavailable");
    }

    std::string id;
    id.reserve(random_bytes.size() * 2);
    for (const unsigned char byte : random_bytes) {
        id.push_back(kHexDigits[byte >> 4]);
        id.push_back(kHexDigits[byte & 0x0f]);
    }
    return id;
}

std::unique_ptr<SupportBundleJobManager> SupportBundleRuntime::create_production() {
    SupportBundleRuntimeTestDependencies dependencies;
    dependencies.collector_executable =
        std::string(kSupportBundleProductionCollectorPath);
    dependencies.storage_root = std::string(kSupportBundleProductionStorageRoot);
    dependencies.id_generator = generate_secure_job_id;
    dependencies.timeout = std::chrono::minutes(10);
    dependencies.term_grace = std::chrono::seconds(2);
    dependencies.run_startup_cleanup = true;
    dependencies.project_version = MAKE_TAG;
    dependencies.executor = std::make_shared<SupportBundleCollectorExecutor>(
        dependencies.collector_executable.string(),
        dependencies.timeout,
        dependencies.term_grace,
        dependencies.project_version);
    return create_for_testing(std::move(dependencies));
}

std::unique_ptr<SupportBundleJobManager> SupportBundleRuntime::create_for_testing(
    SupportBundleRuntimeTestDependencies dependencies) {
    if (dependencies.run_startup_cleanup && dependencies.startup_cleanup) {
        (void)dependencies.startup_cleanup(dependencies.storage_root);
    }
    if (!dependencies.executor) {
        dependencies.executor = std::make_shared<SupportBundleCollectorExecutor>(
            dependencies.collector_executable.string(),
            dependencies.timeout,
            dependencies.term_grace,
            dependencies.project_version);
    }

    return std::make_unique<SupportBundleJobManager>(
        std::move(dependencies.executor),
        std::move(dependencies.id_generator),
        std::move(dependencies.storage_root),
        remove_support_bundle_job_directory,
        SupportBundleJobManager::kProductionRetention,
        SupportBundleJobManager::kProductionRetryDelay,
        [] { return generate_support_bundle_case_id({}); });
}
