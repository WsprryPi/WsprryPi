#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

enum class SupportBundleIntakeStateLoadStatus {
    loaded,
    absent,
    unsafe_root,
    unsafe_state,
    read_failed,
    invalid_state,
};

struct SupportBundleIntakeState {
    std::uint64_t generation = 0;
    std::string manifest_sha256;
};

struct SupportBundleIntakeStateLoadResult {
    SupportBundleIntakeStateLoadStatus status =
        SupportBundleIntakeStateLoadStatus::invalid_state;
    SupportBundleIntakeState state;
    [[nodiscard]] bool loaded() const noexcept {
        return status == SupportBundleIntakeStateLoadStatus::loaded;
    }
};

enum class SupportBundleIntakeStateCommitStatus {
    committed,
    unchanged,
    committed_sync_uncertain,
    invalid_input,
    unsafe_root,
    unsafe_existing_state,
    rollback,
    same_generation_mutated,
    temporary_collision,
    write_failed,
    publish_failed,
};

struct SupportBundleIntakeStateCommitResult {
    SupportBundleIntakeStateCommitStatus status =
        SupportBundleIntakeStateCommitStatus::invalid_input;
    [[nodiscard]] bool published() const noexcept {
        return status == SupportBundleIntakeStateCommitStatus::committed ||
               status == SupportBundleIntakeStateCommitStatus::unchanged ||
               status == SupportBundleIntakeStateCommitStatus::committed_sync_uncertain;
    }
    [[nodiscard]] bool durability_confirmed() const noexcept {
        return status == SupportBundleIntakeStateCommitStatus::committed ||
               status == SupportBundleIntakeStateCommitStatus::unchanged;
    }
};

SupportBundleIntakeStateLoadResult load_support_bundle_intake_state(
    const std::filesystem::path &storage_root);

SupportBundleIntakeStateCommitResult commit_support_bundle_intake_state(
    const std::filesystem::path &storage_root,
    const SupportBundleIntakeState &state);

enum class SupportBundleIntakeStateTestFault {
    none,
    write,
    file_sync,
    temporary_substitution,
    rename,
    directory_sync,
};

struct SupportBundleIntakeStateTestHooks {
    SupportBundleIntakeStateTestFault fault = SupportBundleIntakeStateTestFault::none;
    std::function<void()> after_locked_state_load;
};

// Typed in-process fault seam for unit tests; not a CLI, configuration, or UI control.
SupportBundleIntakeStateCommitResult commit_support_bundle_intake_state_for_test(
    const std::filesystem::path &storage_root,
    const SupportBundleIntakeState &state,
    const SupportBundleIntakeStateTestHooks &hooks);
