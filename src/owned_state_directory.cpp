// SPDX-License-Identifier: MIT
#include "owned_state_directory.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
namespace wsprrypi {
namespace {
struct FileDescriptor {
    int value;
    ~FileDescriptor(){if(value>=0)::close(value);}
    int close(){const auto fd=value;value=-1;return ::close(fd);}
};
void require(bool value){if(!value)throw std::runtime_error("owned_state_storage_hold");}
bool move_no_replace(const std::string &from,const std::string &to){
#ifdef __APPLE__
    return ::renamex_np(from.c_str(),to.c_str(),RENAME_EXCL)==0;
#elif defined(__linux__)
    return ::syscall(SYS_renameat2,AT_FDCWD,from.c_str(),AT_FDCWD,to.c_str(),RENAME_NOREPLACE)==0;
#else
    (void)from;(void)to;return false;
#endif
}
void sync_directory(const std::string &path){
    const auto fd=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0);
    const auto result=::fsync(fd);const auto closed=::close(fd);require(!result&&!closed);
}
bool private_directory(const struct stat &info){return S_ISDIR(info.st_mode)&&info.st_uid==geteuid()&&!(info.st_mode&0077);}
bool private_file(const struct stat &info){return S_ISREG(info.st_mode)&&info.st_uid==geteuid()&&info.st_nlink==1&&
    !(info.st_mode&0077)&&info.st_size>=0&&info.st_size<=1048576;}
}
OwnedStateDirectory prepare_owned_state_directory(const std::string &requested,
    const std::function<std::string()> &initial,const std::function<void(const std::string &)> &validate,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint){
    require(bool(initial)&&bool(validate));
    const auto path=std::filesystem::path(requested),name=path.filename();
    require(!name.empty()&&name!="."&&name!="..");
    const auto parent=std::filesystem::canonical(path.has_parent_path()?path.parent_path():".");
    struct stat info{};require(!::lstat(parent.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==geteuid()&&!(info.st_mode&0022));
    const auto directory=(parent/name).string();bool existed=true;
    if(::lstat(directory.c_str(),&info)){
        require(errno==ENOENT);const auto staged=directory+".prepare";
        require(!::mkdir(staged.c_str(),0700)||errno==EEXIST);
        require(!::lstat(staged.c_str(),&info)&&private_directory(info));
        FileDescriptor preparation{::open(staged.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
        require(preparation.value>=0&&!::flock(preparation.value,LOCK_EX|LOCK_NB));
        struct stat locked{};require(!::fstat(preparation.value,&locked)&&locked.st_dev==info.st_dev&&locked.st_ino==info.st_ino);
        for(const auto &entry:std::filesystem::directory_iterator(staged))
            require(entry.path().filename()=="authority.json"||entry.path().filename()=="writing");
        if(checkpoint)checkpoint(OwnedStatePublicationPoint::PreparedDirectory);
        const auto writing=staged+"/writing",ready=staged+"/authority.json";
        if(!::lstat(writing.c_str(),&info))require(private_file(info)&&!::unlink(writing.c_str()));
        else require(errno==ENOENT);
        if(::lstat(ready.c_str(),&info)){
            require(errno==ENOENT);const auto bytes=initial();require(!bytes.empty()&&bytes.size()<=1048576);validate(bytes);
            FileDescriptor output{::open(writing.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};require(output.value>=0);
            bool good=true;std::size_t offset=0;
            while(offset<bytes.size()){
                const auto count=::write(output.value,bytes.data()+offset,bytes.size()-offset);
                if(count<0&&errno==EINTR)continue;
                if(count<=0){good=false;break;}
                offset+=static_cast<std::size_t>(count);
            }
            if(checkpoint)checkpoint(OwnedStatePublicationPoint::WrittenImage);
            if(::fsync(output.value))good=false;
            if(output.close())good=false;
            require(good&&move_no_replace(writing,ready));
        }else {require(private_file(info));validate(PiManagementJournal::read_ini_input(ready).bytes);}
        if(checkpoint)checkpoint(OwnedStatePublicationPoint::StagedImage);
        sync_directory(staged);require(move_no_replace(staged,directory));
        if(checkpoint)checkpoint(OwnedStatePublicationPoint::PublishedDirectory);
        existed=false;sync_directory(parent);
        require(!::lstat(directory.c_str(),&info));
        if(checkpoint)checkpoint(OwnedStatePublicationPoint::SyncedDirectory);
    }
    require(private_directory(info));return {directory+"/authority.json",existed};
}
}
