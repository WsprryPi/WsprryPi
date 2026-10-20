#include "support_bundle_startup_cleanup.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string job_id(char character) {
    return std::string(32, character);
}

fs::path make_private_root(const char *prefix) {
    std::string pattern = std::string("/tmp/") + prefix + ".XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    assert(mkdtemp(writable.data()) != nullptr);
    const fs::path root(writable.data());
    assert(chmod(root.c_str(), 0700) == 0);
    return root;
}

void make_job(const fs::path &root, const std::string &id) {
    const fs::path job = root / id;
    assert(mkdir(job.c_str(), 0700) == 0);
    assert(chmod(job.c_str(), 0700) == 0);
    std::ofstream(job / "artifact") << "artifact";
}
}  // namespace

int main() {
    const fs::path root = make_private_root("wsprrypi-startup-cleanup");
    const std::string first = job_id('a');
    const std::string second = job_id('b');
    make_job(root, first);
    make_job(root, second);
    std::ofstream(root / "unrelated-file") << "keep";
    assert(mkdir((root / "unrelated-directory").c_str(), 0700) == 0);
    std::ofstream(root / "unrelated-directory" / "keep") << "keep";

    const auto initial = cleanup_stale_support_bundle_jobs(root);
    assert(initial.status == SupportBundleStartupCleanupStatus::completed);
    assert(initial.removed_count == 2 && initial.failed_count == 0);
    assert(!fs::exists(root / first) && !fs::exists(root / second));
    assert(fs::exists(root / "unrelated-file"));
    assert(fs::exists(root / "unrelated-directory" / "keep"));

    const auto repeated = cleanup_stale_support_bundle_jobs(root);
    assert(repeated.status == SupportBundleStartupCleanupStatus::completed);
    assert(repeated.removed_count == 0 && repeated.failed_count == 0);

    const fs::path external = make_private_root("wsprrypi-startup-cleanup-external");
    std::ofstream(external / "sentinel") << "keep";
    const std::string symlink_id = job_id('c');
    const std::string safe_id = job_id('d');
    assert(symlink(external.c_str(), (root / symlink_id).c_str()) == 0);
    make_job(root, safe_id);
    const auto partial = cleanup_stale_support_bundle_jobs(root);
    assert(partial.status == SupportBundleStartupCleanupStatus::partial_failure);
    assert(partial.removed_count == 1 && partial.failed_count == 1);
    assert(!fs::exists(root / safe_id));
    assert(fs::is_symlink(root / symlink_id));
    assert(fs::exists(external / "sentinel"));
    assert(unlink((root / symlink_id).c_str()) == 0);

    const std::string failing_id = job_id('e');
    make_job(root, failing_id);
    int remover_calls = 0;
    const auto injected_failure = cleanup_stale_support_bundle_jobs(
        root, [&remover_calls](const fs::path &, const std::string &) {
            ++remover_calls;
            return SupportBundleJobDirectoryRemovalResult{
                SupportBundleJobDirectoryRemovalFailure::removal_failed};
        });
    assert(injected_failure.status == SupportBundleStartupCleanupStatus::partial_failure);
    assert(injected_failure.removed_count == 0 && injected_failure.failed_count == 1);
    assert(remover_calls == 1 && fs::exists(root / failing_id));
    assert(remove_support_bundle_job_directory(root, failing_id).removed());

    const fs::path missing = root / "missing";
    const auto unavailable = cleanup_stale_support_bundle_jobs(missing);
    assert(unavailable.status == SupportBundleStartupCleanupStatus::unavailable);
    assert(!fs::exists(missing));
    const fs::path linked_root = root / "linked-root";
    assert(symlink(root.c_str(), linked_root.c_str()) == 0);
    assert(cleanup_stale_support_bundle_jobs(linked_root).status ==
           SupportBundleStartupCleanupStatus::unsafe_root);
    assert(unlink(linked_root.c_str()) == 0);
    assert(chmod(root.c_str(), 0750) == 0);
    assert(cleanup_stale_support_bundle_jobs(root).status ==
           SupportBundleStartupCleanupStatus::unsafe_root);
    assert(chmod(root.c_str(), 0700) == 0);

    assert(unlink((external / "sentinel").c_str()) == 0);
    assert(rmdir(external.c_str()) == 0);
    assert(unlink((root / "unrelated-file").c_str()) == 0);
    assert(unlink((root / "unrelated-directory" / "keep").c_str()) == 0);
    assert(rmdir((root / "unrelated-directory").c_str()) == 0);
    assert(rmdir(root.c_str()) == 0);
    std::cout << "support_bundle_startup_cleanup_test: PASS\n";
}
