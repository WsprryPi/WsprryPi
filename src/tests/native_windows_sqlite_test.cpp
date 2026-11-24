// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "native_windows_arguments.hpp"
#include "native_windows_child_pipe.hpp"
#include "native_windows_process.hpp"
#include "application_storage/sqlite_state_store.hpp"
#include "application_storage/windows_sqlite_owner_lock.hpp"
#include "application_storage/windows_generation_admission.hpp"
#include "application_recovery/native_windows/sealed_database.hpp"
#include <sqlite3.h>
#include <iostream>
#define CHECK(value) do{if(!(value))throw std::runtime_error(std::string(#value)+" (Windows error "+std::to_string(::GetLastError())+")");}while(false)
using namespace wsprrypi;
using Code=StateStorageError::Code;
template<class F> void refuses(Code code,F operation) {
    bool refused=false;try{operation();}catch(const StateStorageError &error){CHECK(error.code()==code);refused=true;}
    CHECK(refused);
}
static void privacy(const std::filesystem::path &path,bool expose,bool directory=false,bool inherit_only=false) {
    const auto owner=windows_file::current_sid();
    const auto flags=directory?std::wstring(L"OICI"):std::wstring();
    const auto sddl=L"D:P(A;"+flags+L";FA;;;"+owner+L")(A;"+flags+L";FA;;;SY)(A;"+flags+L";FA;;;BA)"+
        (expose?(inherit_only?L"(A;OIIO;FR;;;WD)":L"(A;;FR;;;WD)"):L"");
    PSECURITY_DESCRIPTOR descriptor{};
    CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr));
    struct Free {PSECURITY_DESCRIPTOR value;~Free(){::LocalFree(value);}} free{descriptor};
    BOOL present{},defaulted{};PACL dacl{};
    CHECK(::GetSecurityDescriptorDacl(descriptor,&present,&dacl,&defaulted)&&present&&dacl);
    windows_file::Handle file(::CreateFileW(path.c_str(),READ_CONTROL|WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
    CHECK(file.get()!=INVALID_HANDLE_VALUE);
    const auto changed=::SetSecurityInfo(file.get(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr,nullptr,dacl,nullptr);
    if(changed!=ERROR_SUCCESS)throw std::runtime_error("Fixture DACL update failed: "+std::to_string(changed)+" at "+NativeWindowsFixture::utf8(path));
}
static void sidecar(const std::filesystem::path &path,const std::string &bytes){
    windows_file::PrivateSecurity security(windows_file::current_sid());
    windows_file::Handle file(::CreateFileW(windows_file::api_path(path).c_str(),GENERIC_WRITE,FILE_SHARE_READ,
        security.attributes(),CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));CHECK(file.get()!=INVALID_HANDLE_VALUE);
    DWORD written{};CHECK(::WriteFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&::FlushFileBuffers(file.get()));
}
int main() {
    try {
        const auto arguments=native_windows_arguments();
        if(arguments.size()==3&&arguments[1]=="--hold-owner"){
            WindowsSqliteOwnerLock owner(windows_file::path_from_utf8(arguments[2]),false);
            DWORD written{};CHECK(::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),"ready",5,&written,nullptr)&&written==5);
            for(;;)::Sleep(50);
        }
        CHECK(arguments.size()==1);
        NativeWindowsFixture first,copy,unsafe,imported;
        const auto directory=NativeWindowsFixture::utf8(first.root),destination=NativeWindowsFixture::utf8(copy.root);
        const std::string identity(32,'1');
        privacy(unsafe.root,true,true,true);
        refuses(Code::Unavailable,[&]{SqliteStateStore rejected(NativeWindowsFixture::utf8(unsafe.root),identity,StateRole::Controller,StateOpen::CreateNew);});
        CHECK(std::filesystem::is_empty(unsafe.root)); // Before any WAL/database or ownership-lock creation.
        refuses(Code::Unavailable,[&]{SqliteStateStore absent(directory,identity,StateRole::Controller,StateOpen::Existing);});
        CHECK(std::filesystem::is_empty(first.root));
        bool missing_owner=false;try{WindowsSqliteOwnerLock missing(first.root,false);}catch(const std::exception &){missing_owner=true;}
        CHECK(missing_owner&&std::filesystem::is_empty(first.root));
        StateSnapshot saved;
        {
            SqliteStateStore store(directory,identity,StateRole::Controller,StateOpen::CreateNew);
            CHECK(store.snapshot().revision=="0");
            bool sealing_busy=false;try{WindowsSqliteOwnerLock sealing(first.root,false);}catch(const std::exception &){sealing_busy=true;}CHECK(sealing_busy);
            refuses(Code::Unavailable,[&]{SqliteStateStore duplicate(directory,identity,StateRole::Controller,StateOpen::Existing);});
            CHECK(store.commit("0",{{StateRecord::Catalog,"private-targets"},{StateRecord::Configuration,"controller-settings"}})=="1");
            saved=store.snapshot();
            refuses(Code::Conflict,[&]{store.commit("0",{{StateRecord::Catalog,"stale"}});});
            refuses(Code::InvalidInput,[&]{store.commit("1",{{StateRecord::ManagedSettings,"local-output"}});});
            CHECK(!store.held()&&store.snapshot()==saved);
            {
                SqliteExecutionReadScope scope;
                CHECK(store.snapshot()==saved);
                refuses(Code::InvalidInput,[&]{store.commit("1",{{StateRecord::Catalog,"not-an-execution-read"}});});
            }
            store.snapshot_to(destination);
            CHECK(!::DeleteFileW((first.root/L"state.sqlite3").c_str()));
            CHECK(store.snapshot()==saved);
            privacy(first.root/L"state.sqlite3",true);
            refuses(Code::Unavailable,[&]{(void)store.snapshot();});CHECK(store.held());
            privacy(first.root/L"state.sqlite3",false);
            refuses(Code::Unavailable,[&]{(void)store.snapshot();});
        }
        {
            WindowsSqliteOwnerLock sealing(first.root,false);sealing.check();
            refuses(Code::Unavailable,[&]{SqliteStateStore blocked(directory,identity,StateRole::Controller,StateOpen::Existing);});
            windows_file::Handle input(::CreateFileW((first.root/L"state.sqlite3").c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));CHECK(input.get()!=INVALID_HANDLE_VALUE);
            CHECK(windows_file::inspect(input.get(),false).standard.EndOfFile.QuadPart>0);
        }
        WindowsSqliteOwnerLock::initialize_snapshot(imported.root);
        CHECK(::CopyFileW(windows_file::api_path(copy.root/L"state.sqlite3").c_str(),
            windows_file::api_path(imported.root/L"state.sqlite3").c_str(),TRUE));
        {SqliteStateStore snapshot(NativeWindowsFixture::utf8(imported.root),identity,StateRole::Controller,StateOpen::Existing);CHECK(snapshot.snapshot()==saved);}
        {
            NativeWindowsFixture workspace;
            windows_process::ChildPipe output(windows_process::ChildPipe::Direction::FromChild);
            windows_file::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
            CHECK(null.get()!=INVALID_HANDLE_VALUE);std::array<wchar_t,32768> executable{};
            const auto length=::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));CHECK(length&&length<executable.size());
            windows_process::OwnedChild holder(NativeWindowsFixture::utf8(std::filesystem::path(std::wstring(executable.data(),length))),
                NativeWindowsFixture::utf8(workspace.root),{null.get(),output.child(),null.get()},{},[&](const auto &){
                    return std::vector<std::wstring>{L"--hold-owner",copy.root.native()};});
            output.close_child();const auto ready=output.read(std::chrono::steady_clock::now()+std::chrono::seconds(5));
            CHECK(std::string(ready.begin(),ready.end())=="ready");
            refuses(Code::Unavailable,[&]{SqliteStateStore busy(destination,identity,StateRole::Controller,StateOpen::Existing);});
            holder.stop();
        }
        {SqliteStateStore recovered(destination,identity,StateRole::Controller,StateOpen::Existing);CHECK(recovered.snapshot()==saved);}
        {
            recovery_windows::SealedDatabase sealing(copy.root);sealing.retire_empty_sidecars();
            refuses(Code::Unavailable,[&]{SqliteStateStore blocked(destination,identity,StateRole::Controller,StateOpen::Existing);});
            const auto wal=copy.root/L"state.sqlite3-wal",shm=copy.root/L"state.sqlite3-shm";
            sidecar(wal,"uncheckpointed");sidecar(shm,"index");
            bool refused=false;try{sealing.retire_empty_sidecars();}catch(const std::exception &){refused=true;}
            CHECK(refused&&std::filesystem::file_size(wal)==14&&std::filesystem::exists(shm));
            CHECK(::DeleteFileW(windows_file::api_path(wal).c_str())&&::DeleteFileW(windows_file::api_path(shm).c_str()));
            sidecar(wal,"");sidecar(shm,"index");privacy(shm,true);
            refused=false;try{sealing.retire_empty_sidecars();}catch(const std::exception &){refused=true;}
            CHECK(refused&&std::filesystem::exists(wal)&&std::filesystem::exists(shm));privacy(shm,false);
            sealing.retire_empty_sidecars();CHECK(!std::filesystem::exists(wal)&&!std::filesystem::exists(shm));
            CHECK(std::filesystem::exists(copy.root/L"state.sqlite3.owner.lock"));
        }
        {
            NativeWindowsFixture moved;
            const auto application=moved.root/L"application-state",target=application/L"generation-2";
            windows_file::PrivateSecurity security(windows_file::current_sid());
            CHECK(::CreateDirectoryW(windows_file::api_path(application).c_str(),security.attributes()));
            windows_generation::Admission admission(NativeWindowsFixture::utf8(application));
            CHECK(::MoveFileExW(windows_file::api_path(copy.root).c_str(),windows_file::api_path(target).c_str(),MOVEFILE_WRITE_THROUGH));
            CHECK(::MoveFileExW(windows_file::api_path(target).c_str(),windows_file::api_path(copy.root).c_str(),MOVEFILE_WRITE_THROUGH));
            WindowsSqliteOwnerLock sealing(copy.root,false,WindowsSqliteOwnerLock::Mobility::StoppedCandidate);
            sealing.verify_at(copy.root);
            CHECK(::MoveFileExW(windows_file::api_path(copy.root).c_str(),windows_file::api_path(target).c_str(),MOVEFILE_WRITE_THROUGH));
            sealing.verify_at(target);
            refuses(Code::Unavailable,[&]{SqliteStateStore blocked(NativeWindowsFixture::utf8(target),identity,StateRole::Controller,StateOpen::Existing);});
            bool replaced=false;try{sealing.verify_at(first.root);}catch(const std::exception &){replaced=true;}CHECK(replaced);
            CHECK(::MoveFileExW(windows_file::api_path(target).c_str(),windows_file::api_path(copy.root).c_str(),MOVEFILE_WRITE_THROUGH));
            sealing.verify_at(copy.root);
        }
        {
            SqliteStateStore reopened(directory,identity,StateRole::Controller,StateOpen::Existing);
            SqliteStateStore staged(destination,identity,StateRole::Controller,StateOpen::Existing);
            CHECK(reopened.snapshot()==saved&&staged.snapshot()==saved);
            sqlite3 *external{};const auto file=NativeWindowsFixture::utf8(first.root/L"state.sqlite3");
            CHECK(sqlite3_open_v2(file.c_str(),&external,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);
            CHECK(sqlite3_exec(external,"UPDATE metadata SET value='7' WHERE key='revision'",nullptr,nullptr,nullptr)==SQLITE_OK);
            CHECK(sqlite3_close(external)==SQLITE_OK);
            refuses(Code::Unavailable,[&]{(void)reopened.snapshot();});CHECK(reopened.held());
        }
        CHECK(::DeleteFileW(windows_file::api_path(copy.root/L"state.sqlite3.owner.lock").c_str()));
        refuses(Code::Unavailable,[&]{SqliteStateStore lost(destination,identity,StateRole::Controller,StateOpen::Existing);});
        CHECK(!std::filesystem::exists(copy.root/L"state.sqlite3.owner.lock"));
        bool repair=false;try{WindowsSqliteOwnerLock::initialize_snapshot(copy.root);}catch(const std::exception &){repair=true;}CHECK(repair);
        CHECK(!std::filesystem::exists(copy.root/L"state.sqlite3.owner.lock"));
        std::cout<<"PASS native SQLite ownership/locking, transactions, imported snapshots, ACL hold, process-loss recovery, stopped moves/sidecars, missing-marker refusal, and external-edit fencing\n";
    } catch(const std::exception &error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
