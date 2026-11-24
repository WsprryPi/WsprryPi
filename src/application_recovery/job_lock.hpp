// SPDX-License-Identifier: MIT
#pragma once
#include "application_control/local_transport.hpp"
#include <cerrno>
#include <filesystem>
#ifdef _WIN32
#include "native_windows/file_api_client.hpp"
#include "paths.hpp"
#include <exception>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace wsprrypi {
// Native handoff imports/exports and the backend use the same job inode lock.
class RecoveryJobLock {
public:
#ifdef _WIN32
    struct NativeLibrary {std::string path;}; // Explicit software-fixture input.
    RecoveryJobLock(const std::filesystem::path &path,bool enabled):RecoveryJobLock(path,enabled,NativeLibrary{enabled?recovery_paths().native_library:std::string{}}){}
    RecoveryJobLock(const std::filesystem::path &path,bool enabled,NativeLibrary library){
        if(!enabled)return;
        api_=std::make_unique<recovery_windows::FileApiClient>(library.path);const auto bytes=path.u8string();
        const auto result=api_->acquire(reinterpret_cast<const char *>(bytes.data()),bytes.size(),&token_);
        if(result)throw LocalCommandRefused(result==5?LocalRefusal::Busy:LocalRefusal::InvalidRequest);
    }
#else
    RecoveryJobLock(const std::filesystem::path &path,bool enabled){
        if(!enabled)return;
        value_=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct stat info{},current{};
        if(value_<0||::fstat(value_,&info)||!S_ISDIR(info.st_mode)||info.st_uid!=::geteuid()||
            (info.st_mode&0777)!=0700||::lstat(path.c_str(),&current)||info.st_dev!=current.st_dev||info.st_ino!=current.st_ino){
            close();throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        }
        if(::flock(value_,LOCK_EX|LOCK_NB)){
            const auto error=errno;close();
            throw LocalCommandRefused(error==EWOULDBLOCK||error==EAGAIN?LocalRefusal::Busy:LocalRefusal::InvalidRequest);
        }
    }
#endif
    ~RecoveryJobLock(){close();}
    RecoveryJobLock(const RecoveryJobLock &)=delete;
    RecoveryJobLock &operator=(const RecoveryJobLock &)=delete;
private:
#ifdef _WIN32
    void close()noexcept{if(token_){if(api_->close(token_))std::terminate();token_=0;}}
    std::unique_ptr<recovery_windows::FileApiClient> api_;
    std::uint64_t token_{};
#else
    void close()noexcept{if(value_>=0){::close(value_);value_=-1;}}
    int value_{-1};
#endif
};
}
