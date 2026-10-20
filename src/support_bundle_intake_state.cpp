#include "support_bundle_intake_state.hpp"

#include "json.hpp"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace {

using json = nlohmann::json;
using Descriptor = std::unique_ptr<int, void (*)(int *)>;
constexpr const char *kStateName = "intake-state.json";
constexpr const char *kTemporaryName = ".intake-state.json.partial";
constexpr std::size_t kMaximumStateBytes = 1024;

void close_descriptor(int *descriptor) {
    if (descriptor) {
        if (*descriptor >= 0) close(*descriptor);
        delete descriptor;
    }
}

Descriptor descriptor(int value) {
    return Descriptor(new int(value), close_descriptor);
}

bool valid_digest(const std::string &digest) {
    if (digest.size() != 64) return false;
    for (const unsigned char character : digest)
        if (!std::isdigit(character) && !(character >= 'a' && character <= 'f')) return false;
    return true;
}

Descriptor open_root(const std::filesystem::path &root) {
    if (!root.is_absolute()) return descriptor(-1);
    const int opened = open(root.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (opened < 0) return descriptor(-1);
    struct stat information{};
    if (fstat(opened, &information) != 0 || !S_ISDIR(information.st_mode) ||
        information.st_uid != geteuid() || (information.st_mode & 07777) != 0700) {
        close(opened);
        return descriptor(-1);
    }
    return descriptor(opened);
}

struct ParseResult {
    bool valid = false;
    SupportBundleIntakeState state;
};

ParseResult parse_state(const std::string &bytes) {
    bool duplicate = false;
    std::vector<std::set<std::string>> keys;
    try {
        auto value = json::parse(bytes, [&](int, json::parse_event_t event, json &item) {
            if (event == json::parse_event_t::object_start) keys.emplace_back();
            if (event == json::parse_event_t::key) {
                if (keys.empty() || !keys.back().insert(item.get<std::string>()).second)
                    duplicate = true;
            }
            if (event == json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
            return true;
        });
        const std::set<std::string> exact = {
            "schema_version", "project_id", "generation", "manifest_sha256"};
        if (duplicate || !value.is_object() || value.size() != exact.size()) return {};
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
            if (!exact.contains(iterator.key())) return {};
        for (const auto &field : exact)
            if (!value.contains(field)) return {};
        if (!value.at("schema_version").is_number_unsigned() ||
            value.at("schema_version").get<std::uint64_t>() != 1 ||
            !value.at("project_id").is_string() ||
            value.at("project_id").get<std::string>() != "wsprrypi" ||
            !value.at("generation").is_number_unsigned() ||
            !value.at("manifest_sha256").is_string()) return {};
        SupportBundleIntakeState state{
            value.at("generation").get<std::uint64_t>(),
            value.at("manifest_sha256").get<std::string>()};
        if (state.generation == 0 || !valid_digest(state.manifest_sha256)) return {};
        return {true, std::move(state)};
    } catch (const json::exception &) {
        return {};
    }
}

SupportBundleIntakeStateLoadResult load_from_directory(int directory) {
    const int opened = openat(directory, kStateName,
                              O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (opened < 0) {
        return {errno == ENOENT ? SupportBundleIntakeStateLoadStatus::absent
                               : SupportBundleIntakeStateLoadStatus::unsafe_state, {}};
    }
    auto file = descriptor(opened);
    struct stat information{};
    if (fstat(opened, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_uid != geteuid() || information.st_nlink != 1 ||
        (information.st_mode & 07777) != 0600 || information.st_size <= 0 ||
        static_cast<std::uint64_t>(information.st_size) > kMaximumStateBytes)
        return {SupportBundleIntakeStateLoadStatus::unsafe_state, {}};
    std::string bytes(static_cast<std::size_t>(information.st_size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = read(opened, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return {SupportBundleIntakeStateLoadStatus::read_failed, {}};
        offset += static_cast<std::size_t>(count);
    }
    char extra = 0;
    while (true) {
        const auto count = read(opened, &extra, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return {SupportBundleIntakeStateLoadStatus::read_failed, {}};
        if (count > 0) return {SupportBundleIntakeStateLoadStatus::unsafe_state, {}};
        break;
    }
    const auto parsed = parse_state(bytes);
    if (!parsed.valid) return {SupportBundleIntakeStateLoadStatus::invalid_state, {}};
    return {SupportBundleIntakeStateLoadStatus::loaded, parsed.state};
}

bool write_all(int file, const std::string &bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = write(file, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool same_temporary_file(int directory, int opened, std::size_t expected_size) {
    struct stat descriptor_information{};
    struct stat path_information{};
    return fstat(opened, &descriptor_information) == 0 &&
           fstatat(directory, kTemporaryName, &path_information, AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISREG(descriptor_information.st_mode) && S_ISREG(path_information.st_mode) &&
           descriptor_information.st_dev == path_information.st_dev &&
           descriptor_information.st_ino == path_information.st_ino &&
           descriptor_information.st_uid == geteuid() && path_information.st_uid == geteuid() &&
           descriptor_information.st_nlink == 1 && path_information.st_nlink == 1 &&
           (descriptor_information.st_mode & 07777) == 0600 &&
           (path_information.st_mode & 07777) == 0600 &&
           descriptor_information.st_size == static_cast<off_t>(expected_size) &&
           path_information.st_size == static_cast<off_t>(expected_size);
}

SupportBundleIntakeStateCommitResult commit_internal(
    const std::filesystem::path &storage_root,
    const SupportBundleIntakeState &state,
    const SupportBundleIntakeStateTestHooks &hooks) {
    if (state.generation == 0 || !valid_digest(state.manifest_sha256))
        return {SupportBundleIntakeStateCommitStatus::invalid_input};
    auto root = open_root(storage_root);
    if (*root < 0) return {SupportBundleIntakeStateCommitStatus::unsafe_root};
    if (flock(*root, LOCK_EX) != 0)
        return {SupportBundleIntakeStateCommitStatus::unsafe_root};
    const auto current = load_from_directory(*root);
    if (hooks.after_locked_state_load) hooks.after_locked_state_load();
    if (current.status != SupportBundleIntakeStateLoadStatus::loaded &&
        current.status != SupportBundleIntakeStateLoadStatus::absent)
        return {SupportBundleIntakeStateCommitStatus::unsafe_existing_state};
    if (current.loaded()) {
        if (state.generation < current.state.generation)
            return {SupportBundleIntakeStateCommitStatus::rollback};
        if (state.generation == current.state.generation) {
            if (state.manifest_sha256 != current.state.manifest_sha256)
                return {SupportBundleIntakeStateCommitStatus::same_generation_mutated};
            if (hooks.fault == SupportBundleIntakeStateTestFault::directory_sync ||
                fsync(*root) != 0)
                return {SupportBundleIntakeStateCommitStatus::committed_sync_uncertain};
            return {SupportBundleIntakeStateCommitStatus::unchanged};
        }
    }

    const auto bytes = (json({{"schema_version", 1}, {"project_id", "wsprrypi"},
                              {"generation", state.generation},
                              {"manifest_sha256", state.manifest_sha256}}).dump(2) + "\n");
    const int opened = openat(*root, kTemporaryName,
                              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (opened < 0)
        return {errno == EEXIST ? SupportBundleIntakeStateCommitStatus::temporary_collision
                               : SupportBundleIntakeStateCommitStatus::write_failed};
    auto temporary = descriptor(opened);
    struct stat temporary_information{};
    if (fchmod(opened, 0600) != 0 || fstat(opened, &temporary_information) != 0 ||
        !S_ISREG(temporary_information.st_mode) || temporary_information.st_uid != geteuid() ||
        temporary_information.st_nlink != 1 || (temporary_information.st_mode & 07777) != 0600) {
        unlinkat(*root, kTemporaryName, 0);
        return {SupportBundleIntakeStateCommitStatus::write_failed};
    }
    bool published = false;
    auto cleanup = [&] {
        if (!published) unlinkat(*root, kTemporaryName, 0);
    };
    if (hooks.fault == SupportBundleIntakeStateTestFault::write ||
        !write_all(opened, bytes)) {
        cleanup();
        return {SupportBundleIntakeStateCommitStatus::write_failed};
    }
    if (hooks.fault == SupportBundleIntakeStateTestFault::file_sync || fsync(opened) != 0) {
        cleanup();
        return {SupportBundleIntakeStateCommitStatus::write_failed};
    }
    if (hooks.fault == SupportBundleIntakeStateTestFault::temporary_substitution) {
        unlinkat(*root, kTemporaryName, 0);
        const int replacement = openat(*root, kTemporaryName,
                                       O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (replacement >= 0) {
            static constexpr char replacement_bytes[] = "{}\n";
            const bool replacement_written = write_all(
                replacement,
                std::string(replacement_bytes, sizeof(replacement_bytes) - 1));
            const int replacement_close = close(replacement);
            if (!replacement_written || replacement_close != 0) {
                cleanup();
                return {SupportBundleIntakeStateCommitStatus::publish_failed};
            }
        }
    }
    if (!same_temporary_file(*root, opened, bytes.size())) {
        cleanup();
        return {SupportBundleIntakeStateCommitStatus::publish_failed};
    }
    if (hooks.fault == SupportBundleIntakeStateTestFault::rename ||
        renameat(*root, kTemporaryName, *root, kStateName) != 0) {
        cleanup();
        return {SupportBundleIntakeStateCommitStatus::publish_failed};
    }
    published = true;
    if (hooks.fault == SupportBundleIntakeStateTestFault::directory_sync || fsync(*root) != 0)
        return {SupportBundleIntakeStateCommitStatus::committed_sync_uncertain};
    return {SupportBundleIntakeStateCommitStatus::committed};
}

} // namespace

SupportBundleIntakeStateLoadResult load_support_bundle_intake_state(
    const std::filesystem::path &storage_root) {
    auto root = open_root(storage_root);
    if (*root < 0) return {SupportBundleIntakeStateLoadStatus::unsafe_root, {}};
    return load_from_directory(*root);
}

SupportBundleIntakeStateCommitResult commit_support_bundle_intake_state(
    const std::filesystem::path &storage_root,
    const SupportBundleIntakeState &state) {
    return commit_internal(storage_root, state, {});
}

SupportBundleIntakeStateCommitResult commit_support_bundle_intake_state_for_test(
    const std::filesystem::path &storage_root,
    const SupportBundleIntakeState &state,
    const SupportBundleIntakeStateTestHooks &hooks) {
    return commit_internal(storage_root, state, hooks);
}
