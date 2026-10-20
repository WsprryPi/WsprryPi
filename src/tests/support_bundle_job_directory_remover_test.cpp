#include "support_bundle_job_directory_remover.hpp"

#include <cassert>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
using Failure = SupportBundleJobDirectoryRemovalFailure;

constexpr char kJobId[] = "0123456789abcdef0123456789abcdef";
constexpr char kSiblingId[] = "fedcba9876543210fedcba9876543210";

fs::path make_private_directory(const char *prefix) {
    std::string pattern = std::string("/tmp/") + prefix + ".XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    assert(mkdtemp(writable.data()) != nullptr);
    const fs::path path(writable.data());
    assert(chmod(path.c_str(), 0700) == 0);
    return path;
}

void make_directory(const fs::path &path, mode_t mode = 0700) {
    assert(mkdir(path.c_str(), mode) == 0);
    assert(chmod(path.c_str(), mode) == 0);
}

void write_file(const fs::path &path) {
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(descriptor >= 0);
    assert(write(descriptor, "x", 1) == 1);
    assert(close(descriptor) == 0);
}

void remove_empty_directory(const fs::path &path) {
    assert(rmdir(path.c_str()) == 0);
}

void expect_failure(const fs::path &root, const std::string &id, Failure expected) {
    const auto result = remove_support_bundle_job_directory(root, id);
    assert(!result.removed());
    assert(result.failure == expected);
}

void test_normal_tree_and_containment() {
    const fs::path root = make_private_directory("wsprrypi-job-remover-root");
    const fs::path external = make_private_directory("wsprrypi-job-remover-external");
    const fs::path external_file = external / "sentinel";
    const fs::path external_directory = external / "directory";
    write_file(external_file);
    make_directory(external_directory);
    write_file(external_directory / "inside");

    const fs::path job = root / kJobId;
    const fs::path sibling = root / kSiblingId;
    make_directory(job);
    make_directory(sibling);
    write_file(sibling / "sibling-sentinel");
    write_file(job / "archive.tar.gz");
    make_directory(job / "nested");
    write_file(job / "nested" / "result.json");
    assert(symlink(external_file.c_str(), (job / "external-file").c_str()) == 0);
    assert(symlink(external_directory.c_str(), (job / "external-directory").c_str()) == 0);

    const auto result = remove_support_bundle_job_directory(root, kJobId);
    assert(result.removed());
    assert(!fs::exists(job));
    assert(fs::exists(sibling / "sibling-sentinel"));
    assert(fs::exists(external_file));
    assert(fs::exists(external_directory / "inside"));

    assert(remove_support_bundle_job_directory(root, kJobId).removed());
    assert(remove_support_bundle_job_directory(root, kSiblingId).removed());
    assert(unlink(external_file.c_str()) == 0);
    assert(unlink((external_directory / "inside").c_str()) == 0);
    remove_empty_directory(external_directory);
    remove_empty_directory(external);
    remove_empty_directory(root);
}

void test_root_and_target_rejection() {
    const fs::path root = make_private_directory("wsprrypi-job-remover-rejection");
    expect_failure(fs::path("relative-root"), kJobId, Failure::invalid_root);
    expect_failure(root / "missing", kJobId, Failure::root_unavailable);
    assert(chmod(root.c_str(), 0750) == 0);
    expect_failure(root, kJobId, Failure::unsafe_root);
    assert(chmod(root.c_str(), 0700) == 0);

    const fs::path linked_root = root / "linked-root";
    assert(symlink(root.c_str(), linked_root.c_str()) == 0);
    expect_failure(linked_root, kJobId, Failure::unsafe_root);
    assert(unlink(linked_root.c_str()) == 0);
    expect_failure(root, "too-short", Failure::invalid_job_id);

    const fs::path external = make_private_directory("wsprrypi-job-remover-target");
    const fs::path linked_job = root / kJobId;
    assert(symlink(external.c_str(), linked_job.c_str()) == 0);
    expect_failure(root, kJobId, Failure::unsafe_target);
    assert(fs::exists(external));
    assert(unlink(linked_job.c_str()) == 0);

    make_directory(linked_job, 0750);
    expect_failure(root, kJobId, Failure::unsafe_target);
    assert(chmod(linked_job.c_str(), 0700) == 0);
    assert(remove_support_bundle_job_directory(root, kJobId).removed());
    remove_empty_directory(external);
    remove_empty_directory(root);
}

void test_special_entries_and_hard_links() {
    const fs::path root = make_private_directory("wsprrypi-job-remover-special");
    const fs::path job = root / kJobId;
    make_directory(job);
    const fs::path fifo = job / "collector.fifo";
    assert(mkfifo(fifo.c_str(), 0600) == 0);

    assert(remove_support_bundle_job_directory(root, kJobId).removed());
    assert(!fs::exists(job));

    make_directory(job);
    const fs::path first = job / "first";
    const fs::path second = job / "second";
    write_file(first);
    assert(link(first.c_str(), second.c_str()) == 0);
    expect_failure(root, kJobId, Failure::unsupported_entry);
    assert(fs::exists(first));
    assert(unlink(first.c_str()) == 0);
    assert(unlink(second.c_str()) == 0);
    assert(remove_support_bundle_job_directory(root, kJobId).removed());
    remove_empty_directory(root);
}
}  // namespace

int main() {
    test_normal_tree_and_containment();
    test_root_and_target_rejection();
    test_special_entries_and_hard_links();
    std::cout << "support_bundle_job_directory_remover_test: PASS\n";
}
