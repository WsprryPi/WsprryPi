// SPDX-License-Identifier: MIT
#include "backup_artifact.hpp"
#include "application_control/local_transport.hpp"
#include "application_control/wire_bytes.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#ifdef _WIN32
#include "windows_observation.hpp"
#include "windows_file_io.hpp"
#include "application_control/local_pipe_windows.hpp"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
#ifndef _WIN32
Json stamp(const struct stat &info){
#ifdef __APPLE__
    const auto modified=info.st_mtimespec,changed=info.st_ctimespec;
#else
    const auto modified=info.st_mtim,changed=info.st_ctim;
#endif
    return Json::array({info.st_dev,info.st_ino,info.st_size,static_cast<std::int64_t>(modified.tv_sec)*1000000000+modified.tv_nsec,
        static_cast<std::int64_t>(changed.tv_sec)*1000000000+changed.tv_nsec});
}
#endif
}
#ifdef _WIN32
struct RecoveryBackupArtifact::Native {
    std::wstring owner;
    std::unique_ptr<windows_file::ParentChain> parents;
    windows_file::Handle file;
};
#endif
RecoveryBackupArtifact::RecoveryBackupArtifact(const RecoveryPaths &paths,const std::string &id){
    require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);
#ifdef _WIN32
    const auto directory=windows_file::path_from_utf8(paths.root)/L"jobs"/id;
    const auto owner=paths.maintenance_sid.empty()?windows_file::current_sid():paths.maintenance_sid;
    std::string record_bytes;
    {
        recovery_windows::Input input(directory/L"job.json",1048576,recovery_windows::Input::Access::Read,recovery_windows::Input::Policy::PrivateState,owner);
        std::array<char,4096> block{};while(const auto count=input.read(block)){record_bytes.append(block.data(),count);require(record_bytes.size()<=1048576);}
        input.unchanged();require(!record_bytes.empty());
    }
#else
    const auto directory=std::filesystem::path(paths.root)/"jobs"/id;require(std::filesystem::canonical(directory)==directory);
    const auto record=PiManagementJournal::read_private_record((directory/"job.json").string());require(bool(record));
    const auto &record_bytes=record->bytes;
#endif
    const auto job=strict_browser_json(record_bytes,12);
    require(job.value("schema",Json(nullptr))=="recovery-job/1"&&job.value("job_id",Json(nullptr))==id&&job.value("kind",Json(nullptr))=="backup"&&
        job.value("state",Json(nullptr))=="ready"&&job.contains("archive")&&job["archive"].is_object());
    const auto receipt=job["archive"];require(receipt.size()==3&&receipt.contains("size")&&receipt["size"].is_string()&&receipt.contains("stamp")&&receipt.contains("sha256")&&receipt["sha256"].is_string());
    size_=wire_offset(receipt["size"].get<std::string>());require(size_>0&&size_<=2ULL*1024*1024*1024);
    hash_=receipt["sha256"].get<std::string>();require(hash_.size()==64&&hash_.find_first_not_of("0123456789abcdef")==std::string::npos);
#ifdef _WIN32
    native_=std::make_unique<Native>();const auto file=directory/L"backup.age";native_->owner=owner;
    native_->parents=std::make_unique<windows_file::ParentChain>(file,owner);
    native_->file=windows_file::Handle(::CreateFileW(windows_file::api_path(file).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OVERLAPPED,nullptr));
    require(native_->file.get()!=INVALID_HANDLE_VALUE);const auto info=windows_file::inspect(native_->file.get(),false);
    windows_file::verify_exact_owner(native_->file.get(),owner);windows_file::verify_security(native_->file.get(),owner,true,false);
    require(info.standard.EndOfFile.QuadPart==static_cast<std::int64_t>(size_)&&recovery_windows::observation(info)==receipt["stamp"]);
    stamp_=receipt["stamp"];
#else
    const int descriptor=::open((directory/"backup.age").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct Close {int fd;~Close(){if(fd>=0)::close(fd);}} close{descriptor};
    struct stat info{};require(descriptor>=0&&!::flock(descriptor,LOCK_SH|LOCK_NB)&&!::fstat(descriptor,&info)&&
        S_ISREG(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0600&&info.st_nlink==1&&stamp(info)==receipt["stamp"]);
    stamp_=receipt["stamp"];descriptor_=descriptor;close.fd=-1;
#endif
}
RecoveryBackupArtifact::~RecoveryBackupArtifact(){
#ifndef _WIN32
    if(descriptor_>=0)::close(descriptor_);
#endif
}
std::string RecoveryBackupArtifact::read(std::uint64_t offset,std::size_t maximum)const{
    require(offset<=size_&&maximum>0&&maximum<=65536);
#ifdef _WIN32
    require(recovery_windows::observation(windows_file::inspect(native_->file.get(),false))==stamp_);
    windows_file::verify_exact_owner(native_->file.get(),native_->owner);windows_file::verify_security(native_->file.get(),native_->owner,true,false);
#else
    struct stat current{};require(!::fstat(descriptor_,&current)&&stamp(current)==stamp_);
#endif
    std::string bytes(std::min<std::uint64_t>(maximum,size_-offset),'\0');
    if(bytes.empty())return bytes;
#ifdef _WIN32
    local_pipe::Pending pending(native_->file.get());auto *operation=pending.start();
    operation->Offset=static_cast<DWORD>(offset);operation->OffsetHigh=static_cast<DWORD>(offset>>32);DWORD count{};
    if(::ReadFile(native_->file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,operation))pending.completed();
    else{const auto error=::GetLastError();if(error!=ERROR_IO_PENDING){pending.completed();require(false);}
        count=pending.wait(std::chrono::steady_clock::now()+std::chrono::seconds(5));}
    require(count>0&&count<=bytes.size()&&recovery_windows::observation(windows_file::inspect(native_->file.get(),false))==stamp_);
#else
    ssize_t count;do{count=::pread(descriptor_,bytes.data(),bytes.size(),static_cast<off_t>(offset));}while(count<0&&errno==EINTR);
    require(count>0);
#endif
    bytes.resize(static_cast<std::size_t>(count));return bytes;
}
}
