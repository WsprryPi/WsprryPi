// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wsprrypi {

enum class StateRole { Pi, Controller };
enum class StateRecord {
    Configuration, ManagedSettings, Operations, Catalog,
    RecurrenceAuthority, RecurrenceInitialization, Initialization, RecurrenceRecovery
};
enum class StateOpen { CreateNew, Existing };
enum class StateWritePoint { RecordsWritten, Committed };

class StateStorageError : public std::runtime_error {
public:
    enum class Code { InvalidInput, Unavailable, Conflict, Unsupported, CommitUncertain };
    explicit StateStorageError(Code);
    Code code() const noexcept { return code_; }
private:
    Code code_;
};

struct StateSnapshot {
    // Canonical unsigned decimal, independent of protocol/configuration revisions.
    std::string revision;
    std::map<StateRecord, std::string> records;
    std::map<StateRecord, std::string> record_revisions{};
    bool operator==(const StateSnapshot &) const = default;
};

struct StateChange {
    StateRecord kind;
    // Absence removes a record; an empty string is a distinct stored payload.
    std::optional<std::string> bytes;
};
struct StateRecordSnapshot {
    std::string revision;
    std::optional<std::string> bytes;
    std::optional<std::string> record_revision{};
    bool operator==(const StateRecordSnapshot &) const = default;
};

// Native launch admission consumes the last fully committed, validated memory
// image. This thread-local scope performs no SQLite/file checks or SQL mutex
// acquisition. Ordinary owner reads continue detecting unsupported external
// edits; a detected/uncertain hold immediately closes cached reads as well.
// No write or backup operation is allowed in this scope.
class SqliteExecutionReadScope {
public:
    SqliteExecutionReadScope();
    ~SqliteExecutionReadScope();
    SqliteExecutionReadScope(const SqliteExecutionReadScope &)=delete;
    SqliteExecutionReadScope &operator=(const SqliteExecutionReadScope &)=delete;
};

// Backend-private storage primitive, not an authorization or settings validator.
// The caller selects a trusted existing owner-only directory and validates the
// complete typed after-image before commit. This class never initializes output,
// parses INI, performs remote work, or turns records into runnable commands.
// POSIX descriptors or native Windows handles enforce the same private-owner
// admission around the shared SQLite engine.
class SqliteStateStore {
public:
    static constexpr std::size_t maximum_record_bytes = 1048576;
    using Checkpoint = std::function<void(StateWritePoint)>;

    SqliteStateStore(std::string directory, std::string identity, StateRole,
                     StateOpen, Checkpoint = {});
    ~SqliteStateStore();
    SqliteStateStore(const SqliteStateStore &) = delete;
    SqliteStateStore &operator=(const SqliteStateStore &) = delete;

    StateSnapshot snapshot();
    // Read one owner's record without materializing unrelated settings blobs.
    StateRecordSnapshot record(StateRecord);
    const std::string &directory() const noexcept;
    const std::string &identity() const noexcept;
    StateRole role() const noexcept;
    // All records and the storage revision change together. A conflict never
    // changes state. Failure after durable commit is explicitly uncertain and
    // holds this live owner; reopen/reconcile rather than retrying the mutation.
    std::string commit(const std::string &expected_revision,
                       const std::vector<StateChange> &);
    // SQLite-consistent private staging snapshot, never an operator/download
    // artifact. The caller coordinates companion owners, encrypts/publishes the
    // archive, and cleans failed staging. Refuses an existing destination DB.
    void snapshot_to(const std::string &private_directory);
    // Current logical page image for recovery staging-space calculation. This
    // is ordinary owner I/O and is forbidden in the execution-read scope.
    std::uint64_t snapshot_size_bytes();
    bool held() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wsprrypi
