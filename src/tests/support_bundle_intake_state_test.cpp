#include "support_bundle_intake_state.hpp"
#include "json.hpp"

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char *kDigestA =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char *kDigestB =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

struct TemporaryRoot {
    fs::path path;
    TemporaryRoot() {
        std::string pattern = (fs::temp_directory_path() / "wsprrypi-intake-state-XXXXXX").string();
        auto buffer = std::vector<char>(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const char *created = mkdtemp(buffer.data());
        assert(created);
        path = created;
        assert(chmod(path.c_str(), 0700) == 0);
    }
    ~TemporaryRoot() { fs::remove_all(path); }
};

fs::path state_path(const fs::path &root) { return root / "intake-state.json"; }
fs::path temporary_path(const fs::path &root) { return root / ".intake-state.json.partial"; }

void write_state(const fs::path &root, const std::string &bytes, mode_t mode = 0600) {
    std::ofstream output(state_path(root), std::ios::binary | std::ios::trunc);
    output << bytes;
    output.close();
    assert(chmod(state_path(root).c_str(), mode) == 0);
}

std::string valid_bytes(std::uint64_t generation = 1, const std::string &digest = kDigestA) {
    return json({{"schema_version", 1}, {"project_id", "wsprrypi"},
                 {"generation", generation}, {"manifest_sha256", digest}}).dump() + "\n";
}

void test_first_commit_load_higher_and_idempotent() {
    TemporaryRoot root;
    auto loaded = load_support_bundle_intake_state(root.path);
    assert(loaded.status == SupportBundleIntakeStateLoadStatus::absent);

    const auto previous_umask = umask(0777);
    auto committed = commit_support_bundle_intake_state(root.path, {1, kDigestA});
    umask(previous_umask);
    assert(committed.status == SupportBundleIntakeStateCommitStatus::committed);
    struct stat information{};
    assert(lstat(state_path(root.path).c_str(), &information) == 0);
    assert(S_ISREG(information.st_mode) && (information.st_mode & 07777) == 0600);
    assert(information.st_uid == geteuid() && information.st_nlink == 1);
    assert(!fs::exists(temporary_path(root.path)));
    const auto first_bytes = [&] {
        std::ifstream input(state_path(root.path), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }();
    assert(first_bytes.find("request_url") == std::string::npos);
    assert(first_bytes.find("release_url") == std::string::npos);

    loaded = load_support_bundle_intake_state(root.path);
    assert(loaded.loaded() && loaded.state.generation == 1 &&
           loaded.state.manifest_sha256 == kDigestA);
    committed = commit_support_bundle_intake_state(root.path, {1, kDigestA});
    assert(committed.status == SupportBundleIntakeStateCommitStatus::unchanged);
    committed = commit_support_bundle_intake_state(root.path, {2, kDigestB});
    assert(committed.status == SupportBundleIntakeStateCommitStatus::committed);
    loaded = load_support_bundle_intake_state(root.path);
    assert(loaded.loaded() && loaded.state.generation == 2 &&
           loaded.state.manifest_sha256 == kDigestB);
}

void test_rejected_updates_preserve_prior_state() {
    TemporaryRoot root;
    assert(commit_support_bundle_intake_state(root.path, {5, kDigestA}).published());
    const auto before = fs::file_size(state_path(root.path));
    auto result = commit_support_bundle_intake_state(root.path, {4, kDigestB});
    assert(result.status == SupportBundleIntakeStateCommitStatus::rollback);
    result = commit_support_bundle_intake_state(root.path, {5, kDigestB});
    assert(result.status == SupportBundleIntakeStateCommitStatus::same_generation_mutated);
    result = commit_support_bundle_intake_state(root.path, {0, kDigestA});
    assert(result.status == SupportBundleIntakeStateCommitStatus::invalid_input);
    result = commit_support_bundle_intake_state(root.path, {6, "NOT-A-DIGEST"});
    assert(result.status == SupportBundleIntakeStateCommitStatus::invalid_input);
    const auto loaded = load_support_bundle_intake_state(root.path);
    assert(loaded.loaded() && loaded.state.generation == 5 &&
           loaded.state.manifest_sha256 == kDigestA);
    assert(fs::file_size(state_path(root.path)) == before);
}

void test_strict_documents() {
    const std::vector<std::string> invalid = {
        "", "[]", "{}", "{} trailing", valid_bytes(0),
        json({{"schema_version", 2}, {"project_id", "wsprrypi"}, {"generation", 1},
              {"manifest_sha256", kDigestA}}).dump(),
        json({{"schema_version", 1}, {"project_id", "other"}, {"generation", 1},
              {"manifest_sha256", kDigestA}}).dump(),
        json({{"schema_version", 1}, {"project_id", "wsprrypi"}, {"generation", "1"},
              {"manifest_sha256", kDigestA}}).dump(),
        json({{"schema_version", 1}, {"project_id", "wsprrypi"}, {"generation", 1},
              {"manifest_sha256", std::string(64, 'A')}}).dump(),
        json({{"schema_version", 1}, {"project_id", "wsprrypi"}, {"generation", 1},
              {"manifest_sha256", kDigestA}, {"unexpected", true}}).dump(),
        "{\"schema_version\":1,\"project_id\":\"wsprrypi\",\"generation\":1,"
        "\"generation\":1,\"manifest_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}",
    };
    for (const auto &bytes : invalid) {
        TemporaryRoot root;
        write_state(root.path, bytes);
        const auto loaded = load_support_bundle_intake_state(root.path);
        assert(loaded.status == (bytes.empty() ? SupportBundleIntakeStateLoadStatus::unsafe_state
                                               : SupportBundleIntakeStateLoadStatus::invalid_state));
    }
    TemporaryRoot oversized;
    write_state(oversized.path, std::string(1025, 'x'));
    assert(load_support_bundle_intake_state(oversized.path).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_state);
}

void test_filesystem_safety_and_collisions() {
    TemporaryRoot root;
    assert(chmod(root.path.c_str(), 0755) == 0);
    assert(load_support_bundle_intake_state(root.path).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_root);
    assert(commit_support_bundle_intake_state(root.path, {1, kDigestA}).status ==
           SupportBundleIntakeStateCommitStatus::unsafe_root);
    assert(chmod(root.path.c_str(), 0700) == 0);

    TemporaryRoot target;
    const auto link = root.path.parent_path() / (root.path.filename().string() + "-link");
    fs::create_directory_symlink(root.path, link);
    assert(load_support_bundle_intake_state(link).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_root);
    fs::remove(link);

    write_state(root.path, valid_bytes());
    assert(chmod(state_path(root.path).c_str(), 0644) == 0);
    assert(load_support_bundle_intake_state(root.path).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_state);
    assert(commit_support_bundle_intake_state(root.path, {2, kDigestB}).status ==
           SupportBundleIntakeStateCommitStatus::unsafe_existing_state);
    fs::remove(state_path(root.path));

    assert(mkfifo(state_path(root.path).c_str(), 0600) == 0);
    assert(load_support_bundle_intake_state(root.path).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_state);
    fs::remove(state_path(root.path));

    write_state(target.path, valid_bytes());
    fs::create_symlink(state_path(target.path), state_path(root.path));
    assert(load_support_bundle_intake_state(root.path).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_state);
    fs::remove(state_path(root.path));

    write_state(root.path, valid_bytes());
    const auto second_link = root.path / "second-link.json";
    fs::create_hard_link(state_path(root.path), second_link);
    assert(load_support_bundle_intake_state(root.path).status ==
           SupportBundleIntakeStateLoadStatus::unsafe_state);
    fs::remove(second_link);
    fs::remove(state_path(root.path));

    std::ofstream collision(temporary_path(root.path));
    collision << "collision";
    collision.close();
    assert(commit_support_bundle_intake_state(root.path, {1, kDigestA}).status ==
           SupportBundleIntakeStateCommitStatus::temporary_collision);
    assert(!fs::exists(state_path(root.path)));
    assert(fs::exists(temporary_path(root.path)));
}

void test_atomic_failure_branches_preserve_prior() {
    for (const auto fault : {
             SupportBundleIntakeStateTestFault::write,
             SupportBundleIntakeStateTestFault::file_sync,
             SupportBundleIntakeStateTestFault::temporary_substitution,
             SupportBundleIntakeStateTestFault::rename}) {
        TemporaryRoot root;
        assert(commit_support_bundle_intake_state(root.path, {1, kDigestA}).published());
        const auto result = commit_support_bundle_intake_state_for_test(
            root.path, {2, kDigestB}, {fault, {}});
        const auto expected = fault == SupportBundleIntakeStateTestFault::write ||
                                      fault == SupportBundleIntakeStateTestFault::file_sync
                                  ? SupportBundleIntakeStateCommitStatus::write_failed
                                  : SupportBundleIntakeStateCommitStatus::publish_failed;
        assert(result.status == expected);
        const auto loaded = load_support_bundle_intake_state(root.path);
        assert(loaded.loaded() && loaded.state.generation == 1 &&
               loaded.state.manifest_sha256 == kDigestA);
        assert(!fs::exists(temporary_path(root.path)));
    }

    TemporaryRoot uncertain;
    assert(commit_support_bundle_intake_state(uncertain.path, {1, kDigestA}).published());
    const auto result = commit_support_bundle_intake_state_for_test(
        uncertain.path, {2, kDigestB},
        {SupportBundleIntakeStateTestFault::directory_sync, {}});
    assert(result.status == SupportBundleIntakeStateCommitStatus::committed_sync_uncertain);
    assert(result.published() && !result.durability_confirmed());
    const auto loaded = load_support_bundle_intake_state(uncertain.path);
    assert(loaded.loaded() && loaded.state.generation == 2 &&
           loaded.state.manifest_sha256 == kDigestB);
    const auto durability_retry = commit_support_bundle_intake_state(
        uncertain.path, {2, kDigestB});
    assert(durability_retry.status == SupportBundleIntakeStateCommitStatus::unchanged);
    assert(durability_retry.durability_confirmed());
}

void test_concurrent_stale_writer_cannot_roll_back() {
    using namespace std::chrono_literals;
    TemporaryRoot root;
    assert(commit_support_bundle_intake_state(root.path, {5, kDigestA}).published());
    std::mutex mutex;
    std::condition_variable condition;
    bool higher_loaded = false;
    bool release_higher = false;
    std::atomic<bool> lower_finished = false;
    SupportBundleIntakeStateCommitResult higher;
    SupportBundleIntakeStateCommitResult lower;

    std::thread higher_writer([&] {
        SupportBundleIntakeStateTestHooks hooks;
        hooks.after_locked_state_load = [&] {
            std::unique_lock lock(mutex);
            higher_loaded = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release_higher; });
        };
        higher = commit_support_bundle_intake_state_for_test(
            root.path, {7, kDigestB}, hooks);
    });
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return higher_loaded; });
    }
    std::thread stale_lower_writer([&] {
        lower = commit_support_bundle_intake_state(root.path, {6, kDigestA});
        lower_finished = true;
    });
    std::this_thread::sleep_for(50ms);
    assert(!lower_finished.load());
    {
        std::lock_guard lock(mutex);
        release_higher = true;
    }
    condition.notify_all();
    higher_writer.join();
    stale_lower_writer.join();
    assert(higher.status == SupportBundleIntakeStateCommitStatus::committed);
    assert(lower.status == SupportBundleIntakeStateCommitStatus::rollback);
    const auto loaded = load_support_bundle_intake_state(root.path);
    assert(loaded.loaded() && loaded.state.generation == 7 &&
           loaded.state.manifest_sha256 == kDigestB);
}

} // namespace

int main() {
    test_first_commit_load_higher_and_idempotent();
    test_rejected_updates_preserve_prior_state();
    test_strict_documents();
    test_filesystem_safety_and_collisions();
    test_atomic_failure_branches_preserve_prior();
    test_concurrent_stale_writer_cannot_roll_back();
    std::cout << "support_bundle_intake_state_test: PASS\n";
}
