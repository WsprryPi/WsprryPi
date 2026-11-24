// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <array>
#include <memory>
#include <span>
#include <functional>
#include <openssl/evp.h>
namespace wsprrypi::recovery_windows {
namespace wf=windows_file;
inline std::string text(const std::filesystem::path &path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
inline wf::Handle private_directory(const std::filesystem::path &path){
    wf::ParentChain parents(path,wf::current_sid());
    wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(file.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(file.get(),true);
    wf::verify_exact_owner(file.get(),wf::current_sid());wf::verify_security(file.get(),wf::current_sid(),true,true);return file;
}
class Input {
public:
    enum class Access { Read, DurableRead };
    enum class Policy { PrivateState, Credential };
    const wf::Snapshot &observed()const noexcept{return observed_;}
    Input(const std::filesystem::path &path,std::uint64_t maximum,Access access=Access::Read,Policy policy=Policy::PrivateState,const std::wstring &owner={}):owner_(owner.empty()?wf::current_sid():owner),parents_(path,owner_),durable_(access==Access::DurableRead),private_(policy==Policy::PrivateState){
        wf::require(maximum<=std::uint64_t{2}*1024*1024*1024&&(access==Access::Read||access==Access::DurableRead));
        wf::require((policy==Policy::PrivateState||policy==Policy::Credential)&&(!durable_||private_));
        file_=wf::Handle(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL|(durable_?GENERIC_WRITE:0),FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(file_.get()!=INVALID_HANDLE_VALUE);observed_=wf::inspect(file_.get(),false);
        wf::require(observed_.standard.EndOfFile.QuadPart>=0&&static_cast<std::uint64_t>(observed_.standard.EndOfFile.QuadPart)<=maximum);
        if(private_)wf::verify_exact_owner(file_.get(),owner_);
        wf::verify_security(file_.get(),owner_,private_,false);
    }
    DWORD read(std::span<char> buffer){
        wf::require(!buffer.empty()&&buffer.size()<=65536);DWORD count{};
        wf::require(::ReadFile(file_.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)&&count<=buffer.size());return count;
    }
    void unchanged(){
        wf::require(wf::same_snapshot(observed_,wf::inspect(file_.get(),false)));
        if(private_)wf::verify_exact_owner(file_.get(),owner_);
        wf::verify_security(file_.get(),owner_,private_,false);
    }
    void flush(){wf::require(durable_&&::FlushFileBuffers(file_.get()));unchanged();}
private:
    std::wstring owner_;wf::ParentChain parents_;wf::Handle file_;wf::Snapshot observed_{};bool durable_{},private_{};
};
inline std::string digest(Input &input,std::uint64_t expected){
    wf::require(input.observed().standard.EndOfFile.QuadPart==static_cast<std::int64_t>(expected));
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    wf::require(hash&&EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)==1);
    std::array<char,65536> buffer{};std::uint64_t total{};
    while(const auto count=input.read(buffer)){total+=count;wf::require(total<=expected&&EVP_DigestUpdate(hash.get(),buffer.data(),count)==1);}
    wf::require(total==expected);input.unchanged();
    std::array<unsigned char,32> bytes{};unsigned size{};wf::require(EVP_DigestFinal_ex(hash.get(),bytes.data(),&size)==1&&size==bytes.size());
    std::string result;for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
// Copy only into an admitted inactive staging directory. Active publication
// continues to use the shared journal/generation engine, never this primitive.
inline void copy(const std::filesystem::path &source,const std::filesystem::path &destination,std::uint64_t size,const std::string &expected,
    Input::Policy policy=Input::Policy::PrivateState,const std::function<void()> &check_continue={}){
    Input input(source,size,Input::Access::Read,policy);wf::require(input.observed().standard.EndOfFile.QuadPart==static_cast<std::int64_t>(size));
    wf::require(expected.empty()||(expected.size()==64&&expected.find_first_not_of("0123456789abcdef")==std::string::npos));
    wf::ParentChain parents(destination,wf::current_sid());auto directory=private_directory(destination.parent_path());
    wf::flush_directory(destination.parent_path(),wf::current_sid());wf::PrivateSecurity security(wf::current_sid());
    wf::Handle output(::CreateFileW(wf::api_path(destination).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE|READ_CONTROL,FILE_SHARE_READ,
        security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));wf::require(output.get()!=INVALID_HANDLE_VALUE);
    try{
        std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);
        wf::require(hash&&EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)==1);std::array<char,65536> buffer{};std::uint64_t total{};
        while(const auto count=input.read(buffer)){
            if(check_continue)check_continue();
            total+=count;wf::require(total<=size&&EVP_DigestUpdate(hash.get(),buffer.data(),count)==1);DWORD offset{};
            while(offset<count){DWORD written{};wf::require(::WriteFile(output.get(),buffer.data()+offset,count-offset,&written,nullptr)&&written>0&&written<=count-offset);offset+=written;}
        }
        wf::require(total==size);input.unchanged();std::array<unsigned char,32> bytes{};unsigned length{};
        wf::require(EVP_DigestFinal_ex(hash.get(),bytes.data(),&length)==1&&length==bytes.size());std::string actual;
        for(const auto byte:bytes){actual+="0123456789abcdef"[byte>>4];actual+="0123456789abcdef"[byte&15];}
        wf::require(expected.empty()||actual==expected);const auto result=wf::inspect(output.get(),false);
        wf::require(result.standard.EndOfFile.QuadPart==static_cast<std::int64_t>(size));
        wf::verify_exact_owner(output.get(),wf::current_sid());wf::verify_security(output.get(),wf::current_sid(),true,false);
        wf::require(::FlushFileBuffers(output.get()));
    }catch(...){
        // Delete this exact created handle, not a second path lookup; an
        // exclusive-create collision never enters this cleanup scope.
        FILE_DISPOSITION_INFO remove{TRUE};wf::require(::SetFileInformationByHandle(output.get(),FileDispositionInfo,&remove,sizeof remove));
        output=wf::Handle();wf::flush_directory(destination.parent_path(),wf::current_sid());throw;
    }
    // Close the last writer before the namespace barrier. If that final
    // barrier fails, retain the inactive staging file for its owning recovery
    // operation; no successful copy/publication is reported.
    output=wf::Handle();wf::flush_directory(destination.parent_path(),wf::current_sid());
}
}
#endif
