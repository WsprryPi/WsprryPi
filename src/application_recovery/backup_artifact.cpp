// SPDX-License-Identifier: MIT
#include "backup_artifact.hpp"
#include "application_control/local_transport.hpp"
#include "application_control/wire_bytes.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
Json stamp(const struct stat &info){
#ifdef __APPLE__
    const auto modified=info.st_mtimespec,changed=info.st_ctimespec;
#else
    const auto modified=info.st_mtim,changed=info.st_ctim;
#endif
    return Json::array({info.st_dev,info.st_ino,info.st_size,static_cast<std::int64_t>(modified.tv_sec)*1000000000+modified.tv_nsec,
        static_cast<std::int64_t>(changed.tv_sec)*1000000000+changed.tv_nsec});
}
}
RecoveryBackupArtifact::RecoveryBackupArtifact(const RecoveryPaths &paths,const std::string &id){
    require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);
    const auto directory=std::filesystem::path(paths.root)/"jobs"/id;require(std::filesystem::canonical(directory)==directory);
    const auto record=PiManagementJournal::read_private_record((directory/"job.json").string());require(bool(record));
    const auto job=strict_browser_json(record->bytes,12);
    require(job.value("schema",Json(nullptr))=="recovery-job/1"&&job.value("job_id",Json(nullptr))==id&&job.value("kind",Json(nullptr))=="backup"&&
        job.value("state",Json(nullptr))=="ready"&&job.contains("archive")&&job["archive"].is_object());
    const auto receipt=job["archive"];require(receipt.size()==3&&receipt.contains("size")&&receipt["size"].is_string()&&receipt.contains("stamp")&&receipt.contains("sha256")&&receipt["sha256"].is_string());
    size_=wire_offset(receipt["size"].get<std::string>());require(size_>0&&size_<=2ULL*1024*1024*1024);
    hash_=receipt["sha256"].get<std::string>();require(hash_.size()==64&&hash_.find_first_not_of("0123456789abcdef")==std::string::npos);
    const int descriptor=::open((directory/"backup.age").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct Close {int fd;~Close(){if(fd>=0)::close(fd);}} close{descriptor};
    struct stat info{};require(descriptor>=0&&!::flock(descriptor,LOCK_SH|LOCK_NB)&&!::fstat(descriptor,&info)&&
        S_ISREG(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0600&&info.st_nlink==1&&stamp(info)==receipt["stamp"]);
    stamp_=receipt["stamp"];descriptor_=descriptor;close.fd=-1;
}
RecoveryBackupArtifact::~RecoveryBackupArtifact(){if(descriptor_>=0)::close(descriptor_);}
std::string RecoveryBackupArtifact::read(std::uint64_t offset,std::size_t maximum)const{
    require(offset<=size_&&maximum>0&&maximum<=65536);
    struct stat current{};require(!::fstat(descriptor_,&current)&&stamp(current)==stamp_);
    std::string bytes(std::min<std::uint64_t>(maximum,size_-offset),'\0');
    if(bytes.empty())return bytes;
    ssize_t count;do{count=::pread(descriptor_,bytes.data(),bytes.size(),static_cast<off_t>(offset));}while(count<0&&errno==EINTR);
    require(count>0);bytes.resize(static_cast<std::size_t>(count));return bytes;
}
}
