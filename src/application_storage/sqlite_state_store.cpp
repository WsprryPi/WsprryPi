// SPDX-License-Identifier: MIT
#include "sqlite_state_store.hpp"
#ifdef _WIN32
#include "windows_sqlite_files.hpp"
#else
#include "native_private_acl.hpp"
#endif

#include <sqlite3.h>
#if SQLITE_VERSION_NUMBER < 3031000
#error SQLite 3.31 or later is required for private state storage
#endif
#include <array>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <set>
#include <string_view>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wsprrypi {
namespace {
thread_local unsigned execution_read_depth=0;
using Code = StateStorageError::Code;
constexpr int application_id = 0x57535052;
constexpr std::string_view metadata_schema =
    "CREATE TABLE metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID";
constexpr std::string_view records_schema =
    "CREATE TABLE records (kind TEXT PRIMARY KEY NOT NULL, payload BLOB NOT NULL CHECK(length(payload)<=1048576)) WITHOUT ROWID";
constexpr std::string_view versions_schema =
    "CREATE TABLE record_versions (kind TEXT PRIMARY KEY NOT NULL, revision TEXT NOT NULL) WITHOUT ROWID";

[[noreturn]] void fail(Code code = Code::Unavailable) { throw StateStorageError(code); }
void require(bool value, Code code = Code::Unavailable) { if (!value) fail(code); }
const char *message(Code code) {
    switch (code) {
    case Code::InvalidInput: return "state_invalid_input";
    case Code::Unavailable: return "state_storage_unavailable";
    case Code::Conflict: return "state_revision_conflict";
    case Code::Unsupported: return "state_storage_unsupported";
    case Code::CommitUncertain: return "state_commit_uncertain";
    }
    return "state_storage_unavailable";
}
std::uint64_t decimal(const std::string &text) {
    require(!text.empty() && (text.size() == 1 || text.front() != '0'), Code::InvalidInput);
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    require(result.ec == std::errc{} && result.ptr == text.data() + text.size(), Code::InvalidInput);
    return value;
}
std::string_view record_name(StateRecord kind) {
    switch (kind) {
    case StateRecord::Configuration: return "configuration";
    case StateRecord::ManagedSettings: return "managed-settings";
    case StateRecord::Operations: return "operations";
    case StateRecord::Catalog: return "catalog";
    case StateRecord::RecurrenceAuthority: return "recurrence-authority";
    case StateRecord::RecurrenceInitialization: return "recurrence-initialization";
    case StateRecord::Initialization: return "initialization";
    case StateRecord::RecurrenceRecovery: return "recurrence-recovery";
    }
    fail(Code::InvalidInput);
}
StateRecord record_kind(const std::string &name) {
    for (const auto kind : {StateRecord::Configuration, StateRecord::ManagedSettings,
             StateRecord::Operations, StateRecord::Catalog, StateRecord::RecurrenceAuthority,
             StateRecord::RecurrenceInitialization, StateRecord::Initialization, StateRecord::RecurrenceRecovery})
        if (record_name(kind) == name) return kind;
    fail();
}
bool allowed(StateRole role, StateRecord kind) {
    (void)record_name(kind);
    return role == StateRole::Pi || (kind != StateRecord::ManagedSettings &&
        kind != StateRecord::RecurrenceAuthority && kind != StateRecord::RecurrenceInitialization&&kind!=StateRecord::RecurrenceRecovery);
}
#ifndef _WIN32
bool same_file(const struct stat &a, const struct stat &b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool private_file(const struct stat &info, std::uint64_t maximum) {
    return S_ISREG(info.st_mode) && info.st_uid == geteuid() && info.st_nlink == 1 &&
        !(info.st_mode & 0077) && (info.st_mode & 0600) == 0600 && info.st_size >= 0 &&
        static_cast<std::uint64_t>(info.st_size) <= maximum;
}
void check_ancestors(const std::string &directory) {
    // A private final directory is insufficient if another OS user can rename
    // it through an unsafe parent. Trusted sticky directories such as /tmp
    // protect the child owner's name and remain valid fixture/state parents.
    auto ancestor = std::filesystem::path(directory).parent_path();
    for (;;) {
        struct stat info{};
        require(!lstat(ancestor.c_str(), &info) && S_ISDIR(info.st_mode) &&
            (info.st_uid == 0 || info.st_uid == geteuid()) &&
            (!(info.st_mode & 0022) || (info.st_mode & S_ISVTX)));
        require(native_private_acl_path(ancestor.c_str(),info.st_uid));
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
}
#endif
struct Statement {
    sqlite3_stmt *value{};
    Statement(sqlite3 *db, std::string_view sql) {
        if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &value, nullptr) != SQLITE_OK) {
            if (value) sqlite3_finalize(value);
            value = nullptr;
            fail();
        }
    }
    ~Statement() { if (value) sqlite3_finalize(value); }
    Statement(const Statement &) = delete;
    void text(int position, std::string_view value_) {
        require(sqlite3_bind_text(value, position, value_.data(), static_cast<int>(value_.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    }
    void blob(int position, const std::string &value_) {
        require(sqlite3_bind_blob(value, position, value_.data(), static_cast<int>(value_.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    }
    bool row() {
        const auto result = sqlite3_step(value);
        require(result == SQLITE_ROW || result == SQLITE_DONE);
        return result == SQLITE_ROW;
    }
    std::string text_column(int column) {
        require(sqlite3_column_type(value, column) == SQLITE_TEXT);
        const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(value, column));
        const auto size = sqlite3_column_bytes(value, column);
        require(data != nullptr && size >= 0);
        return {data, static_cast<std::size_t>(size)};
    }
};
}

StateStorageError::StateStorageError(Code code) : std::runtime_error(message(code)), code_(code) {}
SqliteExecutionReadScope::SqliteExecutionReadScope(){++execution_read_depth;}
SqliteExecutionReadScope::~SqliteExecutionReadScope(){--execution_read_depth;}

struct SqliteStateStore::Impl {
    sqlite3 *db{};
#ifdef _WIN32
    std::unique_ptr<WindowsSqliteFiles> native_files;
#else
    int directory_fd{-1}, file_fd{-1};
#ifdef __APPLE__
    // Closing any descriptor for a WAL sidecar releases this process's POSIX
    // SQLite locks. Keep ACL-inspection descriptors until SQLite is closed.
    std::array<int, 3> sidecar_fds{{-1, -1, -1}};
#endif
#endif
    std::string directory, path, identity, role;
    StateRole selected_role;
    Checkpoint checkpoint;
    std::mutex mutex;
    std::atomic_bool is_held{false};
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    std::atomic<std::shared_ptr<const StateSnapshot>> published;
#else
    // Apple libc++ still lacks the C++20 specialization. Keep its supported
    // shared_ptr atomic operations without using deprecated overloads on GCC.
    std::shared_ptr<const StateSnapshot> published;
#endif
    int data_version{};

    std::shared_ptr<const StateSnapshot> published_snapshot() const {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        return published.load();
#else
        return std::atomic_load(&published);
#endif
    }
    void publish_snapshot(std::shared_ptr<const StateSnapshot> image) {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        published.store(std::move(image));
#else
        std::atomic_store(&published,std::move(image));
#endif
    }

    Impl(std::string input, std::string self, StateRole selected, StateOpen mode, Checkpoint observer)
        : identity(std::move(self)), selected_role(selected), checkpoint(std::move(observer)) {
        require((selected == StateRole::Pi || selected == StateRole::Controller) &&
            (mode == StateOpen::CreateNew || mode == StateOpen::Existing) && identity.size() == 32 &&
            identity.find_first_not_of("0123456789abcdef") == std::string::npos, Code::InvalidInput);
        role = selected == StateRole::Pi ? "pi" : "controller";
        require(sqlite3_libversion_number() >= 3031000, Code::Unsupported);
        require(input.find('\0') == std::string::npos && std::filesystem::path(input).is_absolute(), Code::InvalidInput);
        try {
#ifdef _WIN32
            native_files=std::make_unique<WindowsSqliteFiles>(input,mode==StateOpen::CreateNew);
            directory=native_files->directory();path=native_files->path();
#else
            struct stat root{};
            require(!lstat(input.c_str(), &root) && S_ISDIR(root.st_mode) &&
                root.st_uid == geteuid() && (root.st_mode & 0777) == 0700);
            directory = std::filesystem::canonical(input).string();
            check_ancestors(directory);
            directory_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat opened{};
            require(directory_fd >= 0 && !fstat(directory_fd, &opened) && same_file(root, opened));
            require(native_private_acl(directory_fd,opened.st_uid));
            require(native_private_acl(directory_fd,root.st_uid));
            require(!flock(directory_fd, LOCK_EX | LOCK_NB));
            path = directory + "/state.sqlite3";
            if (mode == StateOpen::Existing) {
                struct stat existing{};
                require(!lstat(path.c_str(), &existing) && private_file(existing, 32 * 1048576));
            }
            file_fd = open(path.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
                (mode == StateOpen::CreateNew ? O_CREAT | O_EXCL : 0), 0600);
            require(file_fd >= 0);
#endif
            check_files();
            require(sqlite3_open_v2(path.c_str(), &db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_NOFOLLOW, nullptr) == SQLITE_OK);
            require(sqlite3_busy_timeout(db, 500) == SQLITE_OK);
            require(sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr) == SQLITE_OK);
            sqlite3_limit(db, SQLITE_LIMIT_LENGTH, static_cast<int>(maximum_record_bytes + 1024));
            sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, 16384);
            exec("PRAGMA trusted_schema=OFF");
            if (mode == StateOpen::CreateNew) exec("PRAGMA page_size=4096");
            require(integer("PRAGMA page_size") == 4096, Code::Unsupported);
            require(scalar(mode == StateOpen::CreateNew ? "PRAGMA journal_mode=WAL" : "PRAGMA journal_mode") == "wal", Code::Unsupported);
            exec("PRAGMA synchronous=FULL");
            exec("PRAGMA cache_size=-1024");
            exec("PRAGMA wal_autocheckpoint=1000");
            require(integer("PRAGMA synchronous") == 2 && integer("PRAGMA cache_size") == -1024 &&
                integer("PRAGMA wal_autocheckpoint") == 1000 && integer("PRAGMA busy_timeout") == 500);
            if (mode == StateOpen::CreateNew) initialize();
            require(integer("PRAGMA application_id") == application_id && integer("PRAGMA user_version") == 1, Code::Unsupported);
            {
                // Check the closed DDL before quick_check can evaluate CHECK
                // expressions from an untrusted/corrupt restored database.
                Statement schema(db, "SELECT name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY name");
                require(schema.row() && schema.text_column(0) == "metadata" && schema.text_column(1) == metadata_schema);
                require(schema.row() && schema.text_column(0) == "record_versions" && schema.text_column(1) == versions_schema);
                require(schema.row() && schema.text_column(0) == "records" && schema.text_column(1) == records_schema && !schema.row());
            }
            require(scalar("PRAGMA quick_check") == "ok");
            data_version = integer("PRAGMA data_version");
            publish_snapshot(std::make_shared<const StateSnapshot>(read()));
            check_files();
        } catch (const StateStorageError &) { close(); throw; }
        catch (...) { close(); fail(); }
    }
    ~Impl() { close(); }
    void close() noexcept {
        if (db) sqlite3_close_v2(db);
        db = nullptr;
#ifdef _WIN32
        native_files.reset();
#else
#ifdef __APPLE__
        for (auto &descriptor : sidecar_fds) {
            if (descriptor >= 0) ::close(descriptor);
            descriptor = -1;
        }
#endif
        if (file_fd >= 0) ::close(file_fd);
        if (directory_fd >= 0) ::close(directory_fd);
        file_fd = directory_fd = -1;
#endif
    }
    void exec(std::string_view sql) {
        const std::string terminated(sql);
        require(sqlite3_exec(db, terminated.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    std::string scalar(std::string_view sql) {
        Statement statement(db, sql);
        require(statement.row());
        const auto result = statement.text_column(0);
        require(!statement.row());
        return result;
    }
    int integer(std::string_view sql) {
        Statement statement(db, sql);
        require(statement.row() && sqlite3_column_type(statement.value, 0) == SQLITE_INTEGER);
        const auto value = sqlite3_column_int(statement.value, 0);
        require(!statement.row());
        return value;
    }
    void check_files() {
#ifdef _WIN32
        try{require(native_files!=nullptr);native_files->check();}catch(...){fail();}
#else
        check_ancestors(directory);
        struct stat root{}, named{}, opened{};
        require(!lstat(directory.c_str(), &root) && S_ISDIR(root.st_mode) &&
            root.st_uid == geteuid() && (root.st_mode & 0777) == 0700 &&
            !fstat(directory_fd, &opened) && same_file(root, opened));
        require(!lstat(path.c_str(), &named) && private_file(named, 32 * 1048576) &&
            !fstat(file_fd, &opened) && same_file(named, opened));
        require(native_private_acl(directory_fd,root.st_uid)&&native_private_acl(file_fd,named.st_uid));
#ifdef __APPLE__
        std::size_t sidecar_index = 0;
#endif
        for (const auto *suffix : {"-wal", "-shm", "-journal"}) {
            const auto sidecar = path + suffix;
            struct stat info{};
            if (!lstat(sidecar.c_str(), &info)) {
                require(private_file(info, 64 * 1048576));
#ifdef __APPLE__
                if (!db) {
                    // Before SQLite opens, no SQLite locks can be released,
                    // and its crash recovery may still replace a sidecar.
                    require(native_private_acl_path(sidecar.c_str(), info.st_uid));
                } else {
                    auto &descriptor = sidecar_fds[sidecar_index];
                    if (descriptor < 0) descriptor = open(sidecar.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
                    struct stat secured{};
                    require(descriptor >= 0 && !fstat(descriptor, &secured) && same_file(info, secured) &&
                        private_file(secured, 64 * 1048576) && native_private_acl(descriptor, secured.st_uid));
                }
#else
                require(native_private_acl_path(sidecar.c_str(), info.st_uid));
#endif
            } else {
                require(errno == ENOENT);
#ifdef __APPLE__
                require(sidecar_fds[sidecar_index] < 0);
#endif
            }
#ifdef __APPLE__
            ++sidecar_index;
#endif
        }
#endif
    }
    void flush_files() {
#ifdef _WIN32
        try{require(native_files!=nullptr);native_files->flush();}catch(...){fail();}
#else
        require(!fsync(file_fd) && !fsync(directory_fd));
#endif
    }
    void check_live() {
        require(!is_held);
        check_files();
        require(integer("PRAGMA data_version") == data_version);
    }
    void initialize() {
        exec("BEGIN IMMEDIATE");
        exec(metadata_schema);
        exec(records_schema);
        exec(versions_schema);
        for (const auto &[key, value] : std::map<std::string, std::string>{
                 {"identity", identity}, {"role", role}, {"revision", "0"}}) {
            Statement put(db, "INSERT INTO metadata(key,value) VALUES(?,?)");
            put.text(1, key); put.text(2, value); require(!put.row());
        }
        exec("PRAGMA application_id=1465077842");
        exec("PRAGMA user_version=1");
        exec("COMMIT");
        flush_files();
    }
    std::string read_revision() {
        std::map<std::string, std::string> metadata;
        Statement get(db, "SELECT key,value FROM metadata");
        while (get.row()) metadata.emplace(get.text_column(0), get.text_column(1));
        require(metadata.size() == 3 && metadata.contains("identity") && metadata.at("identity") == identity &&
            metadata.contains("role") && metadata.at("role") == role && metadata.contains("revision"));
        (void)decimal(metadata.at("revision"));
        return metadata.at("revision");
    }
    StateSnapshot read() {
        StateSnapshot result{read_revision(), {}, {}};
        require(integer("SELECT count(*) FROM record_versions LEFT JOIN records USING(kind) WHERE records.kind IS NULL") == 0);
        Statement records(db, "SELECT records.kind,payload,revision FROM records LEFT JOIN record_versions USING(kind)");
        while (records.row()) {
            const auto kind = record_kind(records.text_column(0));
            const auto size = sqlite3_column_bytes(records.value, 1);
            require(allowed(selected_role, kind) && sqlite3_column_type(records.value, 1) == SQLITE_BLOB &&
                size >= 0 && static_cast<std::size_t>(size) <= maximum_record_bytes);
            const auto *data = static_cast<const char *>(sqlite3_column_blob(records.value, 1));
            require(data != nullptr || size == 0);
            const auto revision = records.text_column(2);
            require(decimal(revision) > 0 && decimal(revision) <= decimal(result.revision));
            result.records.emplace(kind, size ? std::string(data, static_cast<std::size_t>(size)) : std::string{});
            result.record_revisions.emplace(kind, revision);
        }
        return result;
    }
};

SqliteStateStore::SqliteStateStore(std::string root, std::string identity, StateRole role,
                                 StateOpen mode, Checkpoint checkpoint)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(identity), role, mode, std::move(checkpoint))) {}
SqliteStateStore::~SqliteStateStore() = default;
const std::string &SqliteStateStore::directory() const noexcept { return impl_->directory; }
const std::string &SqliteStateStore::identity() const noexcept { return impl_->identity; }
StateRole SqliteStateStore::role() const noexcept { return impl_->selected_role; }

StateSnapshot SqliteStateStore::snapshot() {
    auto &owner = *impl_;
    if(execution_read_depth){
        const auto image=owner.published_snapshot();
        require(!owner.is_held.load()&&bool(image));return *image;
    }
    std::lock_guard lock(owner.mutex);
    try {
        owner.exec("BEGIN");
        owner.check_live();
        auto result = owner.read();
        owner.exec("COMMIT");
        owner.check_live();
        return result;
    } catch (...) {
        sqlite3_exec(owner.db, "ROLLBACK", nullptr, nullptr, nullptr);
        owner.is_held = true;
        throw;
    }
}

StateRecordSnapshot SqliteStateStore::record(StateRecord kind) {
    auto &owner = *impl_;
    require(allowed(owner.selected_role, kind), Code::InvalidInput);
    if(execution_read_depth){
        const auto image=owner.published_snapshot();
        require(!owner.is_held.load()&&bool(image));
        const auto found=image->records.find(kind);
        if(found==image->records.end())return {image->revision,{},{}};
        return {image->revision,found->second,image->record_revisions.at(kind)};
    }
    std::lock_guard lock(owner.mutex);
    try {
        owner.exec("BEGIN");
        owner.check_live();
        StateRecordSnapshot result{owner.read_revision(), {}, {}};
        {
            Statement query(owner.db, "SELECT payload,revision FROM records LEFT JOIN record_versions USING(kind) WHERE kind=?");
            query.text(1, record_name(kind));
            if (query.row()) {
                const auto size = sqlite3_column_bytes(query.value, 0);
                require(sqlite3_column_type(query.value, 0) == SQLITE_BLOB && size >= 0 &&
                    static_cast<std::size_t>(size) <= maximum_record_bytes);
                const auto *bytes = static_cast<const char *>(sqlite3_column_blob(query.value, 0));
                require(bytes || size == 0);
                result.bytes = size ? std::string(bytes, static_cast<std::size_t>(size)) : std::string{};
                result.record_revision = query.text_column(1);
                require(decimal(*result.record_revision) > 0 && decimal(*result.record_revision) <= decimal(result.revision));
                require(!query.row());
            }
        }
        owner.exec("COMMIT");
        owner.check_live();
        return result;
    } catch (...) {
        sqlite3_exec(owner.db, "ROLLBACK", nullptr, nullptr, nullptr);
        owner.is_held = true;
        throw;
    }
}

std::string SqliteStateStore::commit(const std::string &expected, const std::vector<StateChange> &changes) {
    require(!execution_read_depth,Code::InvalidInput);
    (void)decimal(expected);
    require(!changes.empty() && changes.size() <= 8, Code::InvalidInput);
    std::set<StateRecord> kinds;
    for (const auto &change : changes)
        require(allowed(impl_->selected_role, change.kind) && kinds.insert(change.kind).second &&
            (!change.bytes || change.bytes->size() <= maximum_record_bytes), Code::InvalidInput);
    auto &owner = *impl_;
    std::lock_guard lock(owner.mutex);
    bool wrote = false, commit_attempted = false, committed = false;
    try {
        owner.exec("BEGIN IMMEDIATE");
        owner.check_live();
        // The open/live checks validate identity and fence external writers.
        // A small receipt write need not materialize every configuration blob.
        const auto previous = owner.read_revision();
        require(previous == expected, Code::Conflict);
        if(kinds.contains(StateRecord::Initialization)) {
            Statement initialized(owner.db,"SELECT 1 FROM records WHERE kind='initialization'");
            require(previous=="0"&&!initialized.row(),Code::Conflict);
            for(const auto &change:changes)if(change.kind==StateRecord::Initialization)
                require(change.bytes.has_value(),Code::InvalidInput);
        }
        const auto revision = decimal(previous);
        require(revision < std::numeric_limits<std::uint64_t>::max(), Code::InvalidInput);
        const auto next = std::to_string(revision + 1);
        const auto previous_image=owner.published_snapshot();
        require(bool(previous_image)&&previous_image->revision==previous);
        auto image=std::make_shared<StateSnapshot>(*previous_image);image->revision=next;
        for (const auto &change : changes) {
            wrote = true;
            Statement put(owner.db, change.bytes ? "INSERT INTO records(kind,payload) VALUES(?,?) ON CONFLICT(kind) DO UPDATE SET payload=excluded.payload WHERE records.payload IS NOT excluded.payload" : "DELETE FROM records WHERE kind=?");
            put.text(1, record_name(change.kind));
            if (change.bytes) put.blob(2, *change.bytes);
            require(!put.row());
            Statement version(owner.db, change.bytes ?
                "INSERT INTO record_versions(kind,revision) VALUES(?,?) ON CONFLICT(kind) DO UPDATE SET revision=excluded.revision" :
                "DELETE FROM record_versions WHERE kind=?");
            version.text(1, record_name(change.kind));
            if (change.bytes) version.text(2, next);
            require(!version.row());
            if(change.bytes){image->records[change.kind]=*change.bytes;image->record_revisions[change.kind]=next;}
            else{image->records.erase(change.kind);image->record_revisions.erase(change.kind);}
        }
        Statement update(owner.db, "UPDATE metadata SET value=? WHERE key='revision'");
        update.text(1, next); require(!update.row());
        if (owner.checkpoint) owner.checkpoint(StateWritePoint::RecordsWritten);
        commit_attempted = true;
        owner.exec("COMMIT"); committed = true;
        if (owner.checkpoint) owner.checkpoint(StateWritePoint::Committed);
        owner.check_live();
        owner.publish_snapshot(std::shared_ptr<const StateSnapshot>(std::move(image)));
        return next;
    } catch (const StateStorageError &error) {
        const auto rollback = sqlite3_exec(owner.db, "ROLLBACK", nullptr, nullptr, nullptr);
        if (committed || wrote || (error.code() != Code::Conflict && error.code() != Code::InvalidInput) ||
            (!committed && rollback != SQLITE_OK)) owner.is_held = true;
        // A COMMIT error may describe an uncertain durable decision. Do not
        // tell callers that replay is safe merely because exec did not return.
        if (commit_attempted) fail(Code::CommitUncertain);
        throw;
    } catch (...) {
        sqlite3_exec(owner.db, "ROLLBACK", nullptr, nullptr, nullptr);
        owner.is_held = true;
        if (commit_attempted) fail(Code::CommitUncertain);
        throw;
    }
}

bool SqliteStateStore::held() const {
    return impl_->is_held.load();
}

std::uint64_t SqliteStateStore::snapshot_size_bytes() {
    require(!execution_read_depth,Code::InvalidInput);
    auto &owner=*impl_;std::lock_guard lock(owner.mutex);
    try {
        owner.check_live();const auto pages=owner.integer("PRAGMA page_count");
        require(pages>0&&pages<=8192);owner.check_live();
        return static_cast<std::uint64_t>(pages)*4096;
    }catch(...){owner.is_held=true;throw;}
}
void SqliteStateStore::snapshot_to(const std::string &directory) {
    require(!execution_read_depth,Code::InvalidInput);
    auto &owner = *impl_;
    std::lock_guard lock(owner.mutex);
    const auto check_source = [&] {
        try { owner.check_live(); }
        catch (...) { owner.is_held = true; throw; }
    };
    check_source();
    // A distinct private owner validates and locks staging with the same path
    // rules. A collision fails without overwriting existing state or holding
    // the healthy source. This is not a cross-owner application backup barrier.
    SqliteStateStore staging(directory, owner.identity, owner.selected_role, StateOpen::CreateNew);
    auto *copy = sqlite3_backup_init(staging.impl_->db, "main", owner.db, "main");
    require(copy != nullptr);
    int result = SQLITE_OK;
    unsigned steps = 0;
    while (result == SQLITE_OK && steps++ < 128)
        result = sqlite3_backup_step(copy, 64);
    const auto finished = sqlite3_backup_finish(copy);
    check_source();
    require(result == SQLITE_DONE && finished == SQLITE_OK);
    try {
        require(staging.snapshot() == owner.read());
        check_source();
        int wal_pages = -1, checkpointed_pages = -1;
        require(sqlite3_wal_checkpoint_v2(staging.impl_->db, "main", SQLITE_CHECKPOINT_TRUNCATE,
            &wal_pages, &checkpointed_pages) == SQLITE_OK && wal_pages == 0 && checkpointed_pages == 0);
        staging.impl_->flush_files();
    } catch (...) {
        // Destination failure is not permission to revoke source admission;
        // only independently observed source failure holds the live owner.
        check_source();
        throw;
    }
}
} // namespace wsprrypi
