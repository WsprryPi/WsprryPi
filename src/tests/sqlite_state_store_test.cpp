// SPDX-License-Identifier: MIT
#include "application_storage/sqlite_state_store.hpp"
#include "software_child_process.hpp"
#include <sqlite3.h>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace wsprrypi;
namespace {
using Code = StateStorageError::Code;
const std::string identity(32, 'a');
std::string executable;
// A real SQLite VFS fsync failure, scoped to this executable and its temporary
// databases. It works with system SQLite without process-global extensions.
sqlite3_vfs *underlying_vfs{};
bool fail_next_sync = false;
unsigned vfs_reads_and_locks=0;
struct FaultFile { sqlite3_file file; sqlite3_file *underlying; };
constexpr auto file_prefix = (sizeof(FaultFile) + alignof(std::max_align_t) - 1) /
    alignof(std::max_align_t) * alignof(std::max_align_t);
sqlite3_file *real_file(sqlite3_file *file) { return reinterpret_cast<FaultFile *>(file)->underlying; }
int io_close(sqlite3_file *f) { auto *r = real_file(f); return r->pMethods->xClose(r); }
int io_read(sqlite3_file *f, void *p, int n, sqlite3_int64 offset) { ++vfs_reads_and_locks;auto *r = real_file(f); return r->pMethods->xRead(r, p, n, offset); }
int io_write(sqlite3_file *f, const void *p, int n, sqlite3_int64 offset) { auto *r = real_file(f); return r->pMethods->xWrite(r, p, n, offset); }
int io_truncate(sqlite3_file *f, sqlite3_int64 size) { auto *r = real_file(f); return r->pMethods->xTruncate(r, size); }
int io_sync(sqlite3_file *f, int flags) {
    if (fail_next_sync) { fail_next_sync = false; return SQLITE_IOERR_FSYNC; }
    auto *r = real_file(f); return r->pMethods->xSync(r, flags);
}
int io_size(sqlite3_file *f, sqlite3_int64 *size) { auto *r = real_file(f); return r->pMethods->xFileSize(r, size); }
int io_lock(sqlite3_file *f, int kind) { auto *r = real_file(f); return r->pMethods->xLock(r, kind); }
int io_unlock(sqlite3_file *f, int kind) { auto *r = real_file(f); return r->pMethods->xUnlock(r, kind); }
int io_reserved(sqlite3_file *f, int *held) { auto *r = real_file(f); return r->pMethods->xCheckReservedLock(r, held); }
int io_control(sqlite3_file *f, int operation, void *value) { auto *r = real_file(f); return r->pMethods->xFileControl(r, operation, value); }
int io_sector(sqlite3_file *f) { auto *r = real_file(f); return r->pMethods->xSectorSize(r); }
int io_device(sqlite3_file *f) { auto *r = real_file(f); return r->pMethods->xDeviceCharacteristics(r); }
int io_map(sqlite3_file *f, int page, int size, int extend, void volatile **value) { auto *r = real_file(f); return r->pMethods->xShmMap(r, page, size, extend, value); }
int io_shm_lock(sqlite3_file *f, int offset, int count, int flags) { ++vfs_reads_and_locks;auto *r = real_file(f); return r->pMethods->xShmLock(r, offset, count, flags); }
void io_barrier(sqlite3_file *f) { auto *r = real_file(f); r->pMethods->xShmBarrier(r); }
int io_unmap(sqlite3_file *f, int remove) { auto *r = real_file(f); return r->pMethods->xShmUnmap(r, remove); }
const sqlite3_io_methods fault_methods{2, io_close, io_read, io_write, io_truncate, io_sync,
    io_size, io_lock, io_unlock, io_reserved, io_control, io_sector, io_device,
    io_map, io_shm_lock, io_barrier, io_unmap, nullptr, nullptr};
int fault_open(sqlite3_vfs *, const char *name, sqlite3_file *file, int flags, int *actual) {
    auto *wrapper = reinterpret_cast<FaultFile *>(file);
    wrapper->underlying = reinterpret_cast<sqlite3_file *>(reinterpret_cast<char *>(file) + file_prefix);
    const auto result = underlying_vfs->xOpen(underlying_vfs, name, wrapper->underlying, flags, actual);
    file->pMethods = result == SQLITE_OK ? &fault_methods : nullptr;
    return result;
}
struct Temporary {
    std::filesystem::path path;
    Temporary() {
        char pattern[] = "/tmp/wsprrypi-sqlite-store-XXXXXX";
        const auto created = mkdtemp(pattern);
        assert(created);
        path = std::filesystem::canonical(created);
    }
    ~Temporary() { std::filesystem::remove_all(path); }
    std::string root() const { return path.string(); }
    std::string database() const { return (path / "state.sqlite3").string(); }
};
template<class Function> void refuses(Code code, Function action) {
    bool caught = false;
    try { action(); }
    catch (const StateStorageError &error) { assert(error.code() == code); caught = true; }
    assert(caught);
}
void edit(const Temporary &temporary, const char *sql) {
    sqlite3 *db{};
    assert(sqlite3_open_v2(temporary.database().c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    assert(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_close(db) == SQLITE_OK);
}
std::string file_bytes(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), {}};
}
void ordinary_transactions() {
    Temporary temporary;
    refuses(Code::Unavailable, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing); });
    assert(std::filesystem::is_empty(temporary.path));
    StateSnapshot committed;
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew);
        assert(store.snapshot() == (StateSnapshot{"0", {}}));
        assert(store.record(StateRecord::Catalog) == (StateRecordSnapshot{"0", {}, {}}));
        refuses(Code::Unavailable, [&] { SqliteStateStore other(temporary.root(), identity, StateRole::Pi, StateOpen::Existing); });
        const auto private_bytes = std::string("station\0private", 15);
        assert(store.commit("0", {{StateRecord::Configuration, private_bytes}, {StateRecord::Operations, "receipt"},
            {StateRecord::Catalog, "authority"}, {StateRecord::RecurrenceAuthority, "native"}}) == "1");
        committed = store.snapshot();
        assert(committed.records.at(StateRecord::Configuration) == private_bytes);
        assert(committed.records.at(StateRecord::Operations) == "receipt");
        assert(committed.records.at(StateRecord::Catalog) == "authority");
        assert(committed.records.at(StateRecord::RecurrenceAuthority) == "native");
        assert(store.record(StateRecord::Configuration) == (StateRecordSnapshot{"1", private_bytes, "1"}));
        const auto database_bytes = file_bytes(temporary.database());
        const auto wal_bytes = file_bytes(temporary.database() + "-wal");
        for (unsigned i = 0; i < 50; ++i) {
            assert(store.snapshot() == committed);
            assert(store.record(StateRecord::Operations).bytes == "receipt");
        }
        assert(file_bytes(temporary.database()) == database_bytes && file_bytes(temporary.database() + "-wal") == wal_bytes);
        refuses(Code::Conflict, [&] { store.commit("0", {{StateRecord::Configuration, "stale"}}); });
        assert(!store.held() && store.snapshot() == committed);
        refuses(Code::InvalidInput, [&] { store.commit("01", {{StateRecord::Configuration, "bad"}}); });
        refuses(Code::InvalidInput, [&] { store.commit("1", {{StateRecord::Configuration, "x"}, {StateRecord::Configuration, "y"}}); });
        refuses(Code::InvalidInput, [&] { store.commit("1", {}); });
        refuses(Code::InvalidInput, [&] { store.commit("1", {{static_cast<StateRecord>(100), "bad"}}); });
        refuses(Code::InvalidInput, [&] { store.commit("1", {{StateRecord::Configuration,
            std::string(SqliteStateStore::maximum_record_bytes + 1, 'x')}}); });
        assert(!store.held() && store.snapshot() == committed);
    }
    refuses(Code::Unavailable, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew); });
    refuses(Code::Unavailable, [&] { SqliteStateStore store(temporary.root(), std::string(32, 'b'), StateRole::Pi, StateOpen::Existing); });
    refuses(Code::Unavailable, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Controller, StateOpen::Existing); });
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing);
        assert(store.snapshot() == committed);
        assert(store.commit("1", {{StateRecord::Configuration, ""}, {StateRecord::Operations, std::nullopt}}) == "2");
        const auto saved = store.snapshot();
        assert(saved.records.at(StateRecord::Configuration).empty() && !saved.records.contains(StateRecord::Operations));
        assert(store.record(StateRecord::Configuration) == (StateRecordSnapshot{"2", "", "2"}));
        assert(store.record(StateRecord::Operations) == (StateRecordSnapshot{"2", {}, {}}));
        assert(saved.records.at(StateRecord::Catalog) == "authority");
    }
    struct stat info{};
    assert(!lstat(temporary.database().c_str(), &info) && (info.st_mode & 0777) == 0600);
}
void role_and_path_refusals() {
    Temporary temporary;
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Controller, StateOpen::CreateNew);
        refuses(Code::InvalidInput, [&] { store.commit("0", {{StateRecord::RecurrenceAuthority, "native"}}); });
        refuses(Code::InvalidInput, [&] { store.commit("0", {{StateRecord::ManagedSettings, "station"}}); });
        refuses(Code::InvalidInput, [&] { store.record(StateRecord::ManagedSettings); });
        assert(store.commit("0", {{StateRecord::Configuration, "listeners"}, {StateRecord::Catalog, ""}}) == "1");
    }
    Temporary unsafe;
    assert(!chmod(unsafe.root().c_str(), 0755));
    refuses(Code::Unavailable, [&] { SqliteStateStore store(unsafe.root(), identity, StateRole::Pi, StateOpen::CreateNew); });
    assert(std::filesystem::is_empty(unsafe.path));
    assert(!chmod(unsafe.root().c_str(), 0700));
    assert(!symlink(temporary.database().c_str(), unsafe.database().c_str()));
    refuses(Code::Unavailable, [&] { SqliteStateStore store(unsafe.root(), identity, StateRole::Controller, StateOpen::Existing); });
    std::filesystem::remove(unsafe.database());
    assert(!link(temporary.database().c_str(), unsafe.database().c_str()));
    refuses(Code::Unavailable, [&] { SqliteStateStore store(unsafe.root(), identity, StateRole::Controller, StateOpen::Existing); });
    std::filesystem::remove(unsafe.database());
    assert(!mkfifo(unsafe.database().c_str(), 0600));
    refuses(Code::Unavailable, [&] { SqliteStateStore store(unsafe.root(), identity, StateRole::Controller, StateOpen::Existing); });
    std::filesystem::remove(unsafe.database());
    refuses(Code::InvalidInput, [&] { SqliteStateStore store("relative", identity, StateRole::Pi, StateOpen::CreateNew); });
    refuses(Code::InvalidInput, [&] { SqliteStateStore store(unsafe.root() + std::string("\0suffix", 7), identity, StateRole::Pi, StateOpen::CreateNew); });
    refuses(Code::InvalidInput, [&] { SqliteStateStore store(unsafe.root(), "wrong", StateRole::Pi, StateOpen::CreateNew); });
    assert(std::filesystem::is_empty(unsafe.path));
}
void unsafe_sidecars_and_live_permissions() {
    Temporary temporary, target;
    { SqliteStateStore create(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew); }
    { SqliteStateStore create(target.root(), identity, StateRole::Pi, StateOpen::CreateNew); }
    const auto original = file_bytes(target.database());
    for (const auto *suffix : {"-wal", "-shm", "-journal"}) {
        const auto sidecar = temporary.database() + suffix;
        // Some system builds retain checkpointed WAL/SHM files on last close.
        // This namespace is private to the closed synthetic test database.
        std::filesystem::remove(sidecar);
        assert(!symlink(target.database().c_str(), sidecar.c_str()));
        refuses(Code::Unavailable, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing); });
        assert(file_bytes(target.database()) == original);
        std::filesystem::remove(sidecar);
    }
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing);
        assert(!chmod(temporary.database().c_str(), 0644));
        refuses(Code::Unavailable, [&] { (void)store.snapshot(); });
        assert(store.held());
        assert(!chmod(temporary.database().c_str(), 0600));
        refuses(Code::Unavailable, [&] { (void)store.snapshot(); });
    }
    Temporary parent;
    const auto child = parent.path / "private-state";
    std::filesystem::create_directory(child);
    assert(!chmod(child.c_str(), 0700) && !chmod(parent.root().c_str(), 0777));
    refuses(Code::Unavailable, [&] { SqliteStateStore store(child.string(), identity, StateRole::Pi, StateOpen::CreateNew); });
    assert(std::filesystem::is_empty(child));
    assert(!chmod(parent.root().c_str(), 0700));
}
void sqlite_commit_refusal() {
    underlying_vfs = sqlite3_vfs_find(nullptr);
    assert(underlying_vfs);
    auto faults = *underlying_vfs;
    faults.zName = "wsprrypi-state-test-fsync";
    faults.szOsFile += static_cast<int>(file_prefix);
    faults.xOpen = fault_open;
    assert(sqlite3_vfs_register(&faults, 1) == SQLITE_OK);
    Temporary temporary;
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew,
            [](StateWritePoint point) { if (point == StateWritePoint::RecordsWritten) fail_next_sync = true; });
        refuses(Code::CommitUncertain, [&] { store.commit("0", {{StateRecord::Configuration, "unconfirmed"}}); });
        assert(!fail_next_sync && store.held());
    }
    assert(sqlite3_vfs_register(underlying_vfs, 1) == SQLITE_OK);
    assert(sqlite3_vfs_unregister(&faults) == SQLITE_OK);
    SqliteStateStore recovered(temporary.root(), identity, StateRole::Pi, StateOpen::Existing);
    const auto saved = recovered.snapshot();
    assert(saved == (StateSnapshot{"0", {}}) ||
        saved == (StateSnapshot{"1", {{StateRecord::Configuration, "unconfirmed"}}, {{StateRecord::Configuration, "1"}}}));
}
void execution_reads(){
    underlying_vfs=sqlite3_vfs_find(nullptr);assert(underlying_vfs);
    auto traced=*underlying_vfs;traced.zName="wsprrypi-state-test-execution";
    traced.szOsFile+=static_cast<int>(file_prefix);traced.xOpen=fault_open;
    assert(sqlite3_vfs_register(&traced,1)==SQLITE_OK);
    Temporary temporary;
    {
        SqliteStateStore *owner=nullptr;StateSnapshot before{"0",{}};
        unsigned boundaries=0;
        SqliteStateStore store(temporary.root(),identity,StateRole::Pi,StateOpen::CreateNew,[&](StateWritePoint){
            ++boundaries;const auto activity=vfs_reads_and_locks;
            SqliteExecutionReadScope execution;
            assert(owner&&owner->snapshot()==before);
            refuses(Code::InvalidInput,[&]{owner->commit(before.revision,{{StateRecord::Configuration,"forbidden"}});});
            assert(activity==vfs_reads_and_locks);
        });
        owner=&store;
        store.commit("0",{{StateRecord::Configuration,"committed"}});
        assert(boundaries==2);
        before=store.snapshot();const auto activity=vfs_reads_and_locks;
        {
            SqliteExecutionReadScope execution;
            for(unsigned index=0;index<50;++index){
                assert(store.snapshot()==before);
                assert(store.record(StateRecord::Configuration)==(StateRecordSnapshot{"1","committed","1"}));
                assert(!store.record(StateRecord::Operations).bytes);
            }
            refuses(Code::InvalidInput,[&]{store.snapshot_to(temporary.root()+"/forbidden");});
            refuses(Code::InvalidInput,[&]{store.snapshot_size_bytes();});
        }
        assert(activity==vfs_reads_and_locks&&!std::filesystem::exists(temporary.root()+"/forbidden"));
        edit(temporary,"UPDATE records SET payload=X'65787465726e616c' WHERE kind='configuration'");
        refuses(Code::Unavailable,[&]{store.snapshot();});
        SqliteExecutionReadScope execution;
        refuses(Code::Unavailable,[&]{store.record(StateRecord::Configuration);});
    }
    assert(sqlite3_vfs_register(underlying_vfs,1)==SQLITE_OK);
    assert(sqlite3_vfs_unregister(&traced)==SQLITE_OK);
}
void failed_and_uncertain_commit() {
    for (const auto point : {StateWritePoint::RecordsWritten, StateWritePoint::Committed}) {
        Temporary temporary;
        {
            SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew,
                [point](StateWritePoint observed) { if (observed == point) throw std::runtime_error("injected"); });
            bool caught = false;
            try { store.commit("0", {{StateRecord::Configuration, "new"}, {StateRecord::Operations, "receipt"}}); }
            catch (const StateStorageError &error) { assert(point == StateWritePoint::Committed && error.code() == Code::CommitUncertain); caught = true; }
            catch (const std::runtime_error &) { assert(point == StateWritePoint::RecordsWritten); caught = true; }
            assert(caught && store.held());
            refuses(Code::Unavailable, [&] { (void)store.snapshot(); });
        }
        SqliteStateStore recovered(temporary.root(), identity, StateRole::Pi, StateOpen::Existing);
        const auto saved = recovered.snapshot();
        if (point == StateWritePoint::Committed) {
            assert(saved.revision == "1" && saved.records.at(StateRecord::Configuration) == "new" &&
                saved.records.at(StateRecord::Operations) == "receipt");
        } else assert(saved == (StateSnapshot{"0", {}}));
    }
}
void process_loss() {
    for (const auto point : {StateWritePoint::RecordsWritten, StateWritePoint::Committed}) {
        Temporary temporary;
        { SqliteStateStore create(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew); }
        const auto child=testing::launch_software_child(executable,{"--crash-write",temporary.root(),
            point==StateWritePoint::RecordsWritten?"records":"committed"});
        assert(testing::wait_software_child(child)==37);
        SqliteStateStore recovered(temporary.root(), identity, StateRole::Pi, StateOpen::Existing);
        const auto saved = recovered.snapshot();
        if (point == StateWritePoint::Committed) assert(saved.revision == "1" && saved.records.size() == 2);
        else assert(saved == (StateSnapshot{"0", {}}));
    }
}
void external_edits_and_versions() {
    Temporary temporary;
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::CreateNew);
        store.commit("0", {{StateRecord::Configuration, "original"}});
        edit(temporary, "UPDATE records SET payload=X'74616d7065726564' WHERE kind='configuration'");
        refuses(Code::Unavailable, [&] { (void)store.snapshot(); });
        assert(store.held());
    }
    edit(temporary, "PRAGMA user_version=2");
    refuses(Code::Unsupported, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing); });
    edit(temporary, "PRAGMA user_version=1; UPDATE metadata SET value='01' WHERE key='revision'");
    refuses(Code::InvalidInput, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing); });
    edit(temporary, "UPDATE metadata SET value='18446744073709551615' WHERE key='revision'");
    {
        SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing);
        assert(store.snapshot().revision == "18446744073709551615");
        refuses(Code::InvalidInput, [&] { store.commit("18446744073709551615", {{StateRecord::Catalog, "x"}}); });
        assert(!store.held());
    }
    edit(temporary, "CREATE TABLE unexpected(value TEXT)");
    refuses(Code::Unavailable, [&] { SqliteStateStore store(temporary.root(), identity, StateRole::Pi, StateOpen::Existing); });
}
void private_snapshot() {
    Temporary source, target, standalone;
    StateSnapshot saved;
    {
        SqliteStateStore store(source.root(), identity, StateRole::Pi, StateOpen::CreateNew);
        store.commit("0", {{StateRecord::Configuration, std::string(SqliteStateStore::maximum_record_bytes, 'x')},
            {StateRecord::Operations, "durable unresolved evidence"}, {StateRecord::Catalog, "authority"}});
        saved = store.snapshot();
        store.snapshot_to(target.root());
        assert(store.snapshot_size_bytes()==std::filesystem::file_size(target.root()+"/state.sqlite3"));
        assert(store.snapshot() == saved && !store.held());
        refuses(Code::Unavailable, [&] { store.snapshot_to(target.root()); });
        refuses(Code::Unavailable, [&] { store.snapshot_to(source.root()); });
        assert(store.snapshot() == saved && !store.held());
        store.commit("1", {{StateRecord::Configuration, "later"}});
    }
    {
        SqliteStateStore snapshot(target.root(), identity, StateRole::Pi, StateOpen::Existing);
        assert(snapshot.snapshot() == saved);
    }
    // The completed staging main file is independently usable; its contents
    // do not depend on copying a live WAL beside it into the future archive.
    std::filesystem::copy_file(target.database(), standalone.database());
    SqliteStateStore independent(standalone.root(), identity, StateRole::Pi, StateOpen::Existing);
    assert(independent.snapshot() == saved);
}
}
int main(int argc,char **argv) {
    if(argc==4&&std::string(argv[1])=="--crash-write") {
        const std::string selected=argv[3];
        if(selected!="records"&&selected!="committed")return 2;
        const auto point=selected=="records"?StateWritePoint::RecordsWritten:StateWritePoint::Committed;
        SqliteStateStore store(argv[2],identity,StateRole::Pi,StateOpen::Existing,
            [point](StateWritePoint observed){if(observed==point)_exit(37);});
        store.commit("0",{{StateRecord::Configuration,"new"},{StateRecord::Operations,"receipt"}});
        return 38;
    }
    if(argc!=1)return 2;
    executable=argv[0];
    ordinary_transactions();
    role_and_path_refusals();
    unsafe_sidecars_and_live_permissions();
    sqlite_commit_refusal();
    execution_reads();
    failed_and_uncertain_commit();
    process_loss();
    external_edits_and_versions();
    private_snapshot();
    std::cout << "SQLite private state: transaction, crash recovery, identity, role, CAS and path checks passed\n";
}
