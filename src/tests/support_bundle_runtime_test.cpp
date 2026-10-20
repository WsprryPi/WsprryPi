#include "support_bundle_runtime.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
class FakeExecutor final : public SupportBundleJobExecutor {
public:
    SupportBundleExecutionResult run(const SupportBundleExecutionContext &) override {
        ++runs;
        return {false, "test", "test"};
    }

    void request_stop() noexcept override {}

    int runs = 0;
};

bool is_lowercase_hex_id(const std::string &value) {
    if (value.size() != 32) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool within_web_root(const fs::path &path) {
    const fs::path web_root("/var/www");
    const fs::path normalized = path.lexically_normal();
    auto web_part = web_root.begin();
    for (auto path_part = normalized.begin();
         web_part != web_root.end() && path_part != normalized.end();
         ++web_part, ++path_part) {
        if (*web_part != *path_part) {
            return false;
        }
    }
    return web_part == web_root.end();
}
}  // namespace

int main() {
    const fs::path collector_path{std::string(kSupportBundleProductionCollectorPath)};
    const fs::path storage_root{std::string(kSupportBundleProductionStorageRoot)};
    assert(collector_path.is_absolute());
    assert(storage_root.is_absolute());
    assert(!within_web_root(collector_path));
    assert(!within_web_root(storage_root));

    std::set<std::string> ids;
    for (int index = 0; index < 128; ++index) {
        const std::string id = SupportBundleRuntime::generate_secure_job_id();
        assert(is_lowercase_hex_id(id));
        assert(ids.insert(id).second);
    }

    const std::string template_string =
        (fs::canonical(fs::temp_directory_path()) /
         "wsprrypi-runtime-test.XXXXXX").string();
    std::vector<char> template_path(template_string.begin(), template_string.end());
    template_path.push_back('\0');
    assert(mkdtemp(template_path.data()) != nullptr);
    const fs::path temporary_root(template_path.data());
    assert(chmod(temporary_root.c_str(), 0700) == 0);

    const auto fake_executor = std::make_shared<FakeExecutor>();
    SupportBundleRuntimeTestDependencies dependencies;
    dependencies.collector_executable = "/test/collector";
    dependencies.storage_root = temporary_root;
    dependencies.id_generator = [] { return std::string(32, 'a'); };
    dependencies.executor = fake_executor;
    dependencies.run_startup_cleanup = true;
    int cleanup_calls = 0;
    dependencies.startup_cleanup = [&cleanup_calls](const fs::path &root) {
        ++cleanup_calls;
        return cleanup_stale_support_bundle_jobs(root);
    };
    const fs::path stale_job = temporary_root / std::string(32, 'z');
    assert(mkdir(stale_job.c_str(), 0700) == 0);
    assert(chmod(stale_job.c_str(), 0700) == 0);
    std::ofstream(stale_job / "stale") << "stale";
    const auto manager = SupportBundleRuntime::create_for_testing(std::move(dependencies));
    assert(manager);
    assert(cleanup_calls == 1 && !fs::exists(stale_job) && fake_executor->runs == 0);
    assert(!manager->lookup(std::string(32, 'a')));

    SupportBundleRuntimeTestDependencies missing_dependencies;
    missing_dependencies.collector_executable = "/test/collector";
    missing_dependencies.storage_root = temporary_root / "unprovisioned";
    missing_dependencies.id_generator = [] { return std::string(32, 'b'); };
    missing_dependencies.executor = fake_executor;
    missing_dependencies.run_startup_cleanup = true;
    missing_dependencies.startup_cleanup = [](const fs::path &root) {
        return cleanup_stale_support_bundle_jobs(root);
    };
    const auto unavailable_manager =
        SupportBundleRuntime::create_for_testing(std::move(missing_dependencies));
    assert(unavailable_manager);
    assert(!fs::exists(temporary_root / "unprovisioned") && fake_executor->runs == 0);

    fs::remove_all(temporary_root);
    std::cout << "support_bundle_runtime_test: PASS\n";
}
