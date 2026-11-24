// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "native_windows_process.hpp"
#include "native_windows_arguments.hpp"
#include "application_recovery/native_windows/file_api.hpp"
#include "application_storage/windows_generation_admission.hpp"
#include "application_storage/windows_sqlite_owner_lock.hpp"
#include "native_windows_directory_owner.hpp"
#include "application_recovery/job_lock.hpp"
#include "Singleton/src/installation_lock.hpp"
#include <future>
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
struct Record {std::string bytes,signature;bool present;};
Record read(const std::string &path){
    std::vector<char> bytes(1048576);char signature[65]{};uint64_t size{};uint32_t present{};
    assert(wrn_record_read(path.data(),path.size(),bytes.data(),bytes.size(),&size,signature,sizeof signature,&present)==0);
    assert(size<=bytes.size()&&present<=1);return {std::string(bytes.data(),static_cast<std::size_t>(size)),signature,present==1};
}
uint32_t publish(const std::string &path,const Record &before,const std::string &bytes){
    return wrn_record_publish(path.data(),path.size(),before.present?1:0,before.bytes.data(),before.bytes.size(),
        before.signature.data(),before.signature.size(),bytes.data(),bytes.size());
}
}
int main(){try{
    assert(wrn_version()==1&&wrn_info_size()==sizeof(WrnInfo));
    const auto arguments=wsprrypi::native_windows_arguments();
    if(arguments.size()==3&&arguments[1]=="--directory-hold"){
        uint64_t token{};assert(wrn_directory_acquire(arguments[2].data(),arguments[2].size(),&token)==0&&token);
        DWORD count{};assert(::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),"ready",5,&count,nullptr)&&count==5);for(;;)::Sleep(50);
    }
    uint64_t service_token=123,command_token=123;uint32_t state{},pid{},done{},code{},size{};std::array<char,8192> command_buffer{};
    assert(wrn_service_query(0,&state,&pid)==3&&wrn_service_close(0)==3);
    assert(wrn_backend_poll(0,command_buffer.data(),command_buffer.size(),&size,&done,&code)==3&&wrn_backend_close(0)==3);
    // The restricted fixture account is never an installed service owner.
    assert(wrn_service_open(&service_token)==1&&!service_token);
    assert(wrn_backend_start("observe",7,nullptr,0,nullptr,0,&command_token)==1&&!command_token);
    NativeWindowsFixture fixture;
    if(arguments.size()!=1){
        std::cerr<<"Python fixture: private root created"<<std::endl;
        assert((arguments.size()==7||arguments.size()==9)&&arguments[1]=="--python"&&arguments[3]=="--script"&&arguments[5]=="--library");
        if(arguments.size()==9)assert(arguments[7]=="--age");
        wsprrypi::windows_file::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));assert(null.get()!=INVALID_HANDLE_VALUE);
        std::cerr<<"Python fixture: starting selected interpreter"<<std::endl;
        wsprrypi::windows_process::OwnedChild child(arguments[2],NativeWindowsFixture::utf8(fixture.root),
            {null.get(),::GetStdHandle(STD_OUTPUT_HANDLE),::GetStdHandle(STD_ERROR_HANDLE)},{},[&](const auto &){std::cerr<<"Python fixture: program and working directory admitted"<<std::endl;auto values=std::vector<std::wstring>{
                L"-I",L"-B",L"-X",L"utf8",wsprrypi::windows_file::path_from_utf8(arguments[4]).native(),
                wsprrypi::windows_file::path_from_utf8(arguments[6]).native(),fixture.root.native()};
                if(arguments.size()==9)values.push_back(wsprrypi::windows_file::path_from_utf8(arguments[8]).native());
                return values;});
        std::cerr<<"Python fixture: interpreter launched"<<std::endl;
        const auto result=child.wait(std::chrono::seconds(60));if(result!=0){fixture.retain=true;std::cerr<<"Retained failed Python fixture: "<<NativeWindowsFixture::utf8(fixture.root)<<std::endl;throw std::runtime_error("Native Python fixture failed: "+std::to_string(result));}return 0;
    }
    const auto root=NativeWindowsFixture::utf8(fixture.root),directory=root+"/private-\xce\xa9";
    {
        const auto location=fixture.root/L".wsprrypi-controller-instance";const auto name=NativeWindowsFixture::utf8(location);uint64_t owner=123,other{};
        assert(wrn_installation_acquire(name.data(),name.size(),&owner)==1&&!owner&&!std::filesystem::exists(location));
        {InstallationLock application(location);assert(application());assert(wrn_installation_acquire(name.data(),name.size(),&owner)==5&&!owner);}
        assert(wrn_installation_acquire(name.data(),name.size(),&owner)==0&&owner);
        assert(wrn_installation_acquire(name.data(),name.size(),&other)==5&&!other);
        {InstallationLock application(location);assert(!application());}
        assert(wrn_installation_close(owner)==0&&wrn_installation_close(owner)==3);
        {InstallationLock application(location);assert(application());}
        wsprrypi::windows_file::Handle damaged(::CreateFileW((location/L"instance.lock").c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));assert(damaged.get()!=INVALID_HANDLE_VALUE);
        DWORD count{};assert(::WriteFile(damaged.get(),"x",1,&count,nullptr)&&count==1);damaged=wsprrypi::windows_file::Handle();
        assert(wrn_installation_acquire(name.data(),name.size(),&owner)==1&&!owner&&std::filesystem::file_size(location/L"instance.lock")==1);
    }
    const auto generation=root+"/generation";assert(wrn_directory(generation.data(),generation.size(),1)==0);
    const auto removable=root+"/removable";assert(wrn_directory(removable.data(),removable.size(),1)==0);
    std::array<wchar_t,32768> library_path{};const auto library_length=::GetModuleFileNameW(::GetModuleHandleW(L"wsprrypi-recovery-native.dll"),library_path.data(),static_cast<DWORD>(library_path.size()));
    assert(library_length&&library_length<library_path.size());
    const wsprrypi::RecoveryJobLock::NativeLibrary native_library{NativeWindowsFixture::utf8(std::filesystem::path(std::wstring(library_path.data(),library_length)))};
    uint64_t job_owner{},job_competing{};
    {
        wsprrypi::WindowsDirectoryOwner application(wsprrypi::windows_file::path_from_utf8(removable));
        assert(wrn_directory_acquire(removable.data(),removable.size(),&job_competing)==5&&!job_competing);
        assert(!::RemoveDirectoryW(wsprrypi::windows_file::path_from_utf8(removable).c_str()));
        application.check();
    }

    assert(wrn_directory_acquire(removable.data(),removable.size(),&job_owner)==0&&job_owner);
    {
        bool held=false;try{wsprrypi::WindowsDirectoryOwner application(wsprrypi::windows_file::path_from_utf8(removable));}
        catch(const wsprrypi::WindowsDirectoryOwner::Busy &){held=true;}assert(held);
    }

    assert(wrn_directory_acquire(removable.data(),removable.size(),&job_competing)==5&&!job_competing);
    auto wrong_thread=std::async(std::launch::async,[&]{return wrn_directory_close(job_owner);});assert(wrong_thread.get()==3);
    bool job_busy=false;try{wsprrypi::RecoveryJobLock blocked(wsprrypi::windows_file::path_from_utf8(removable),true,native_library);}catch(const wsprrypi::LocalCommandRefused &error){job_busy=error.reason()==wsprrypi::LocalRefusal::Busy;}assert(job_busy);
    assert(wrn_directory_close(job_owner)==0&&wrn_directory_close(job_owner)==3);
    {wsprrypi::RecoveryJobLock backend(wsprrypi::windows_file::path_from_utf8(removable),true,native_library);
        assert(wrn_directory_acquire(removable.data(),removable.size(),&job_competing)==5&&!job_competing);
        assert(::RemoveDirectoryW(wsprrypi::windows_file::path_from_utf8(removable).c_str()));}
    assert(wrn_directory_acquire(removable.data(),removable.size(),&job_owner)==1&&!job_owner);
    assert(wrn_directory(removable.data(),removable.size(),1)==0&&wrn_directory_acquire(removable.data(),removable.size(),&job_owner)==0&&wrn_directory_close(job_owner)==0);
    {
        const auto witness=removable+"/state.sqlite3.owner.lock";uint64_t file{};
        assert(wrn_open(witness.data(),witness.size(),2,0,&file)==0&&wrn_close(file)==0);
        assert(wrn_directory_acquire(removable.data(),removable.size(),&job_owner)==1&&!job_owner);
        assert(::DeleteFileW(wsprrypi::windows_file::path_from_utf8(witness).c_str()));
    }
    {
        std::array<wchar_t,32768> program{};const auto length=::GetModuleFileNameW(nullptr,program.data(),static_cast<DWORD>(program.size()));assert(length&&length<program.size());
        uint64_t first{},second{};assert(wrn_pipe(&first,&second)==0);
        wsprrypi::windows_file::Handle input(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(first))),output(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(second)));
        wsprrypi::windows_file::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));assert(null.get()!=INVALID_HANDLE_VALUE);
        wsprrypi::windows_process::OwnedChild holder(NativeWindowsFixture::utf8(std::filesystem::path(std::wstring(program.data(),length))),root,
            {null.get(),output.get(),null.get()},{},[&](const auto &){return std::vector<std::wstring>{L"--directory-hold",wsprrypi::windows_file::path_from_utf8(removable).native()};});
        output=wsprrypi::windows_file::Handle();char ready[5]{};DWORD count{};
        assert(::ReadFile(input.get(),ready,5,&count,nullptr)&&count==5&&std::string(ready,5)=="ready");
        assert(wrn_directory_acquire(removable.data(),removable.size(),&job_owner)==5&&!job_owner);
        holder.stop();assert(wrn_directory_acquire(removable.data(),removable.size(),&job_owner)==0&&wrn_directory_close(job_owner)==0);
    }
    uint64_t admission{},competing{};
    assert(wrn_generation_acquire(generation.data(),generation.size(),&admission)==0&&admission);
    assert(wrn_generation_acquire(generation.data(),generation.size(),&competing)==5&&!competing);
    bool busy=false;try{wsprrypi::windows_generation::Admission blocked(generation);}catch(const wsprrypi::windows_generation::Busy &){busy=true;}assert(busy);
    assert(wrn_generation_close(admission)==0&&wrn_generation_close(admission)==3);
    {wsprrypi::windows_generation::Admission application(generation);assert(wrn_generation_acquire(generation.data(),generation.size(),&competing)==5&&!competing);}
    assert(wrn_generation_acquire(generation.data(),generation.size(),&admission)==0&&wrn_generation_close(admission)==0);
    {
        const auto candidate=root+"/candidate",published=generation+"/published",wrong=root+"/wrong";
        assert(wrn_directory(candidate.data(),candidate.size(),1)==0);
        uint64_t database{},second{};
        assert(wrn_database_acquire(0,candidate.data(),candidate.size(),&database)==3&&!database);
        assert(wrn_generation_acquire(generation.data(),generation.size(),&admission)==0);
        // Established state is never silently repaired by opening an owner.
        assert(wrn_database_acquire(admission,candidate.data(),candidate.size(),&database)==1&&!database);
        assert(wrn_database_initialize(candidate.data(),candidate.size())==0);
        {
            wsprrypi::WindowsSqliteOwnerLock application(wsprrypi::windows_file::path_from_utf8(candidate),false);
            assert(wrn_database_acquire(admission,candidate.data(),candidate.size(),&database)==5&&!database);
        }
        assert(wrn_database_acquire(admission,candidate.data(),candidate.size(),&database)==0&&database);
        assert(wrn_database_acquire(admission,candidate.data(),candidate.size(),&second)==5&&!second);
        bool sql_busy=false;
        try{wsprrypi::WindowsSqliteOwnerLock application(wsprrypi::windows_file::path_from_utf8(candidate),false);}
        catch(const wsprrypi::WindowsSqliteOwnerLock::Busy &){sql_busy=true;}
        assert(sql_busy&&wrn_database_verify(database,candidate.data(),candidate.size())==0);
        assert(wrn_database_verify(database,generation.data(),generation.size())==1);
        assert(wrn_database_move(database,candidate.data(),candidate.size(),wrong.data(),wrong.size())==3);
        assert(wrn_directory(published.data(),published.size(),1)==0);
        assert(wrn_database_move(database,candidate.data(),candidate.size(),published.data(),published.size())==4);
        assert(::RemoveDirectoryW(wsprrypi::windows_file::path_from_utf8(published).c_str()));
        // Retiring the original token must not release admission beneath SQL.
        assert(wrn_generation_close(admission)==0);
        assert(wrn_generation_acquire(generation.data(),generation.size(),&competing)==5&&!competing);
        assert(wrn_database_move(database,candidate.data(),candidate.size(),published.data(),published.size())==0);
        assert(wrn_database_verify(database,published.data(),published.size())==0);
        assert(wrn_database_verify(database,candidate.data(),candidate.size())==1);
        assert(wrn_database_close(database)==0&&wrn_database_close(database)==3);
        assert(wrn_database_verify(database,published.data(),published.size())==3);
        assert(wrn_generation_acquire(generation.data(),generation.size(),&admission)==0&&wrn_generation_close(admission)==0);
        wsprrypi::WindowsSqliteOwnerLock reopened(wsprrypi::windows_file::path_from_utf8(published),false);
    }
    uint64_t reader{},writer{};assert(wrn_pipe(&reader,&writer)==0&&reader&&writer);
    {
        wsprrypi::windows_file::Handle input(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(reader))),output(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(writer)));
        for(const auto handle:{input.get(),output.get()}){DWORD flags{};assert(::GetHandleInformation(handle,&flags)&&!(flags&HANDLE_FLAG_INHERIT));
            wsprrypi::windows_file::verify_exact_owner(handle,wsprrypi::windows_file::current_sid());
            wsprrypi::windows_file::verify_security(handle,wsprrypi::windows_file::current_sid(),true,false);}
        DWORD count{};const char bytes[]={'\0','\r','\n','x'};char received[4]{};
        assert(::WriteFile(output.get(),bytes,4,&count,nullptr)&&count==4);
        assert(::ReadFile(input.get(),received,4,&count,nullptr)&&count==4&&std::memcmp(bytes,received,4)==0);
    }
    assert(wrn_directory(directory.data(),directory.size(),1)==0&&wrn_directory(directory.data(),directory.size(),1)==4);
    assert(wrn_directory(directory.data(),directory.size(),2)==0);
    const auto file=directory+"/stream";uint64_t token{},duplicate{};
    assert(wrn_open(file.data(),file.size(),1,64,&token)==2);
    assert(wrn_open(file.data(),file.size(),2,64,&token)==0&&token);
    assert(wrn_duplicate(token,&duplicate)==0&&duplicate);DWORD count{};
    {
        wsprrypi::windows_file::Handle output(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(duplicate)));
        DWORD flags{};assert(::GetHandleInformation(output.get(),&flags)&&!(flags&HANDLE_FLAG_INHERIT));
        assert(::WriteFile(output.get(),"hello",5,&count,nullptr)&&count==5);
    }
    assert(wrn_flush(token)==0);WrnInfo info{};
    assert(wrn_file_info(token,&info,sizeof info)==0&&info.size==5&&info.directory==0&&info.links==1&&info.owner_sid[0]);
    const auto original=info;assert(wrn_file_info(token,&info,sizeof info-1)==3);
    assert(wrn_close(token)==0&&wrn_close(token)==3&&wrn_duplicate(token,&duplicate)==3);
    assert(wrn_path_info(file.data(),file.size(),&info,sizeof info)==0&&info.volume==original.volume&&std::memcmp(info.file_id,original.file_id,16)==0);
    assert(wrn_open(file.data(),file.size(),1,4,&token)==1);
    assert(wrn_open(file.data(),file.size(),1,64,&token)==0&&wrn_duplicate(token,&duplicate)==0);
    {
        wsprrypi::windows_file::Handle input(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(duplicate)));char bytes[5]{};
        assert(::ReadFile(input.get(),bytes,5,&count,nullptr)&&count==5&&std::string(bytes,5)=="hello");
    }
    assert(wrn_close(token)==0);
    const auto record=directory+"/record.json";const auto absent=read(record);assert(!absent.present);
    assert(publish(record,absent,"{}")==0);const auto before=read(record);assert(before.present&&before.bytes=="{}"&&before.signature.size()==64);
    assert(publish(record,before,"{\"value\":1}")==0&&publish(record,before,"{\"value\":2}")==1);
    assert(read(record).bytes=="{\"value\":1}");
    const auto alias=fixture.root/L"alias";
    assert(::CreateHardLinkW(alias.c_str(),wsprrypi::windows_file::path_from_utf8(file).c_str(),nullptr));
    assert(wrn_open(file.data(),file.size(),1,64,&token)==1);
    std::cout<<"Native recovery file ABI, shared installation ownership, fixed-service refusal, opaque tokens, metadata, Unicode, protected streams, hard-link refusal, and journal CAS passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<" (Windows error "<<::GetLastError()<<")\n";return 1;}}
