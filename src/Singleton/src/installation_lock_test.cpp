// SPDX-License-Identifier: MIT
#include "installation_lock.hpp"
#include <iostream>
#include <fstream>
#ifndef _WIN32
#include <sys/wait.h>
#include <signal.h>
#endif
namespace fs=std::filesystem;
void require(bool value){if(!value)throw std::runtime_error("installation lock test failed");}
template<class F> void refuses(F fn){bool refused=false;try{fn();}catch(const std::exception&){refused=true;}require(refused);}
#ifdef _WIN32
fs::path private_test_root() {
    wchar_t parent[MAX_PATH]{},path[MAX_PATH]{};
    const auto size=::GetTempPathW(MAX_PATH,parent);require(size&&size<MAX_PATH);
    require(::GetTempFileNameW(parent,L"WSP",0,path)!=0);
    HANDLE token{};require(::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&token)!=0);
    DWORD bytes{};::GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<unsigned char> buffer(bytes);
    const auto read=::GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes);::CloseHandle(token);require(read!=0);
    LPWSTR sid{};require(::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid)!=0);
    const auto sddl=L"O:"+std::wstring(sid)+L"D:P(A;OICI;FA;;;"+sid+L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";::LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor{};
    require(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0);
    SECURITY_ATTRIBUTES attributes{sizeof attributes,descriptor,FALSE};
    const auto removed=::DeleteFileW(path);const auto created=removed?::CreateDirectoryW(path,&attributes):FALSE;::LocalFree(descriptor);
    require(created!=0);return path;
}
int probe(const fs::path &directory) {
    wchar_t executable[32768]{};require(::GetModuleFileNameW(nullptr,executable,32768)>0);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --probe \""+directory.native()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
    require(::CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0);
    ::CloseHandle(process.hThread);
    const auto waited=::WaitForSingleObject(process.hProcess,5000);
    if(waited!=WAIT_OBJECT_0){::TerminateProcess(process.hProcess,90);::WaitForSingleObject(process.hProcess,1000);}
    DWORD code{};const auto read=::GetExitCodeProcess(process.hProcess,&code);::CloseHandle(process.hProcess);
    require(waited==WAIT_OBJECT_0&&read!=0);return static_cast<int>(code);
}
int wmain(int argc,wchar_t *argv[]){
    if(argc==3&&std::wstring_view(argv[1])==L"--probe"){
        try{InstallationLock lock(argv[2]);return lock()?0:3;}catch(...){return 4;}
    }
#else
int main(){
#endif
    fs::path temporary;
    try {
#ifdef _WIN32
        temporary=private_test_root();
#else
#ifdef __linux__
        std::string linux_path="/tmp/wsprrypi-lock-test.XXXXXX";
        char *created=::mkdtemp(linux_path.data());
#else
        char path[]="/private/tmp/wsprrypi-lock-test.XXXXXX";
        char *created=::mkdtemp(path);
#endif
        require(created!=nullptr);temporary=fs::canonical(created);
#endif
        const auto directory=temporary/"owner";
        {
            InstallationLock first(directory),second(directory);require(first()&&first()&&!second());
#ifndef _WIN32
            const auto child=::fork();require(child>=0);
            if(child==0){InstallationLock concurrent(directory);::_exit(concurrent()?1:0);}
            int status{};require(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
#endif
#ifdef _WIN32
            require(probe(directory)==3);
            require(!::MoveFileExW(directory.c_str(),(directory.native()+L"-moved").c_str(),0));
            require(!::MoveFileExW(temporary.c_str(),(temporary.native()+L"-moved").c_str(),0));
#endif
            InstallationLock independent(temporary/"other");require(independent());
        }
        {InstallationLock after(directory);require(after());}
#ifdef _WIN32
        require(probe(directory)==0);
#else
        // Abrupt death must release OS ownership without deleting the inode.
        int ready[2]{};require(::pipe(ready)==0);
        const auto child=::fork();require(child>=0);
        if(child==0){::close(ready[0]);InstallationLock lock(directory);const char acquired=lock()?'Y':'N';(void)::write(ready[1],&acquired,1);::alarm(5);for(;;)::pause();}
        ::close(ready[1]);char acquired{};require(::read(ready[0],&acquired,1)==1&&acquired=='Y');::close(ready[0]);
        {InstallationLock busy(directory);require(!busy());require(::kill(child,SIGKILL)==0);
        int status{};require(::waitpid(child,&status,0)==child&&WIFSIGNALED(status));
        require(busy());}
#endif
        require(fs::exists(directory/"instance.lock")); // release never unlinks
        refuses([&]{InstallationLock relative("relative");});
#ifndef _WIN32
        if(::geteuid()!=0)refuses([&]{InstallationLock wrong_owner(directory,::geteuid()+1);});
#endif
        fs::remove(directory/"instance.lock");
        {std::ofstream out(directory/"instance.lock");out<<"unexpected state";}
        refuses([&]{InstallationLock dirty(directory);(void)dirty();});
        fs::remove(directory/"instance.lock");
#ifndef _WIN32
        fs::create_symlink(temporary/"missing",directory/"instance.lock");
        refuses([&]{InstallationLock linked(directory);(void)linked();});
        fs::remove(directory/"instance.lock");
        {InstallationLock create(directory);require(create());}
        fs::create_hard_link(directory/"instance.lock",temporary/"hardlink");
        refuses([&]{InstallationLock linked(directory);(void)linked();});
        fs::remove(temporary/"hardlink");
        require(::chmod((directory/"instance.lock").c_str(),0644)==0);
        refuses([&]{InstallationLock public_file(directory);(void)public_file();});
        require(::chmod((directory/"instance.lock").c_str(),0600)==0);
        fs::create_directory_symlink(directory,temporary/"alias");
        refuses([&]{InstallationLock alias(temporary/"alias");(void)alias();});
        require(::chmod(directory.c_str(),0755)==0);
        refuses([&]{InstallationLock public_directory(directory);(void)public_directory();});
#endif
        fs::remove_all(temporary);return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';if(!temporary.empty())fs::remove_all(temporary);return 1;}
}
