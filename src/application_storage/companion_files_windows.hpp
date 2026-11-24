// SPDX-License-Identifier: MIT
// Private native I/O for the common companion manifest and recovery engine.
#pragma once
#ifdef _WIN32
namespace wf=windows_file;
std::filesystem::path disk_path(const std::string &text){return wf::path_from_utf8(text);}
bool native_exists(const std::filesystem::path &path){
    if(::GetFileAttributesW(wf::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
std::string signature(const wf::Snapshot &info){
    std::string id;for(auto byte:info.file_id.FileId.Identifier){id+="0123456789abcdef"[byte>>4];id+="0123456789abcdef"[byte&15];}
    return std::to_string(info.file_id.VolumeSerialNumber)+":"+id+":"+std::to_string(info.standard.EndOfFile.QuadPart)+":"+
        std::to_string(info.basic.FileAttributes)+":"+std::to_string(info.standard.NumberOfLinks)+":"+
        std::to_string(info.basic.LastWriteTime.QuadPart)+":"+std::to_string(info.basic.ChangeTime.QuadPart);
}
wf::Handle open_private(const std::filesystem::path &path,DWORD access,bool directory=false){
    const auto owner=wf::current_sid();wf::ParentChain parents(path,owner);
    wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),access|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
    require(file.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(file.get(),directory);
    wf::verify_exact_owner(file.get(),owner);wf::verify_security(file.get(),owner,true,directory);return file;
}
std::optional<ReadResult> stream(const std::string &text,CompanionKind kind,HANDLE output=INVALID_HANDLE_VALUE,
    const std::string &expected={},std::string *captured=nullptr){
    const auto path=disk_path(text);
    const auto owner=wf::current_sid();wf::ParentChain parents(path,owner);
    if(kind!=CompanionKind::UdpLogging)(void)open_private(path.parent_path(),0,true);
    if(!native_exists(path))return {};
    wf::Handle input(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    require(input.get()!=INVALID_HANDLE_VALUE);const auto before=wf::inspect(input.get(),false);
    wf::verify_exact_owner(input.get(),owner);
    const bool private_data=kind!=CompanionKind::UdpLogging;
    wf::verify_security(input.get(),owner,private_data,false);
    require(before.standard.EndOfFile.QuadPart>=0&&static_cast<std::uint64_t>(before.standard.EndOfFile.QuadPart)<=companion_maximum_bytes(kind));
    const auto stamp=signature(before);require(expected.empty()||stamp==expected);
    auto digest=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    require(digest&&EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)==1);
    std::array<unsigned char,65536> buffer{};std::uint64_t total=0;
    for(;;){
        DWORD count{};require(::ReadFile(input.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr));if(!count)break;
        total+=count;require(total<=companion_maximum_bytes(kind));require(EVP_DigestUpdate(digest.get(),buffer.data(),count)==1);
        if(captured)captured->append(reinterpret_cast<const char *>(buffer.data()),count);
        if(output!=INVALID_HANDLE_VALUE){DWORD written{};require(::WriteFile(output,buffer.data(),count,&written,nullptr)&&written==count);}
    }
    wf::Handle named(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(named.get()!=INVALID_HANDLE_VALUE&&wf::same_snapshot(before,wf::inspect(input.get(),false))&&
        wf::same_snapshot(before,wf::inspect(named.get(),false))&&total==static_cast<std::uint64_t>(before.standard.EndOfFile.QuadPart));
    wf::verify_exact_owner(input.get(),owner);wf::verify_security(input.get(),owner,private_data,false);
    std::array<unsigned char,EVP_MAX_MD_SIZE> bytes{};unsigned length{};require(EVP_DigestFinal_ex(digest.get(),bytes.data(),&length)==1&&length==32);
    std::string result;for(unsigned i=0;i<length;++i){result+="0123456789abcdef"[bytes[i]>>4];result+="0123456789abcdef"[bytes[i]&15];}
    return ReadResult{total,result,stamp};
}
void sync_parent(const std::string &text){wf::flush_directory(disk_path(text).parent_path(),wf::current_sid());}
void remove_writing(const std::filesystem::path &path,CompanionKind kind){
    auto file=open_private(path,DELETE);const auto info=wf::inspect(file.get(),false);
    require(info.standard.EndOfFile.QuadPart>=0&&static_cast<std::uint64_t>(info.standard.EndOfFile.QuadPart)<=companion_maximum_bytes(kind));
    FILE_DISPOSITION_INFO disposition{TRUE};require(::SetFileInformationByHandle(file.get(),FileDispositionInfo,&disposition,sizeof disposition));
}
#endif
