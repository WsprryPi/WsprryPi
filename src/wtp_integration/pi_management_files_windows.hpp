// SPDX-License-Identifier: MIT
// Included only by the journal's private implementation namespace.
#pragma once
#ifdef _WIN32
namespace wf=windows_file;
using FileMode=unsigned;
struct File {std::string bytes;FileMode mode;std::string signature;};
std::filesystem::path disk_path(const std::string &text){return wf::path_from_utf8(text);}
std::string path_text(const std::filesystem::path &path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
bool exists_file(const std::filesystem::path &path){
    if(::GetFileAttributesW(wf::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
std::filesystem::path normalized_handle_path(HANDLE handle){return wf::normalized_path(handle);}
wf::Handle private_directory_handle(const std::filesystem::path &path){
    wf::ParentChain ancestors(path,wf::current_sid());
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),wf::current_sid());wf::verify_security(handle.get(),wf::current_sid(),true,true);return handle;
}
std::filesystem::path owned_parent(const std::string &text){
    const auto path=disk_path(text);auto directory=private_directory_handle(path.parent_path());
    return normalized_handle_path(directory.get());
}
std::filesystem::path normalized_destination(const std::string &text){
    const auto input=disk_path(text);const auto path=owned_parent(text)/input.filename();
    if(!exists_file(path)){require(input.filename().native().find(L'~')==std::wstring::npos);return path;}
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(handle.get()!=INVALID_HANDLE_VALUE);
    FILE_ATTRIBUTE_TAG_INFO attributes{};require(::GetFileInformationByHandleEx(handle.get(),FileAttributeTagInfo,&attributes,sizeof attributes));
    (void)wf::inspect(handle.get(),(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0);
    return normalized_handle_path(handle.get());
}
std::string path_key(const std::filesystem::path &path){
    const auto value=path.native();
    const auto length=::LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr,0);
    require(length>0);std::wstring folded(static_cast<std::size_t>(length),L'\0');
    require(::LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,value.data(),static_cast<int>(value.size()),folded.data(),length,nullptr,nullptr,0)==length);
    return path_text(std::filesystem::path(folded));
}
std::optional<File> read_file(const std::string &text,bool private_file,bool stabilize=false){
    const auto path=disk_path(text);if(!exists_file(path))return {};
    const auto owner=wf::current_sid();wf::ParentChain parents(path,owner);
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL|(stabilize?GENERIC_WRITE:0),FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(handle.get()!=INVALID_HANDLE_VALUE);const auto before=wf::inspect(handle.get(),false);
    require(before.standard.EndOfFile.QuadPart>=0&&static_cast<std::uint64_t>(before.standard.EndOfFile.QuadPart)<=maximum_bytes);
    wf::verify_exact_owner(handle.get(),owner);
    wf::verify_security(handle.get(),owner,private_file,false);File result{{},0600,{}};
    std::array<char,4096> block{};
    while(true){DWORD count{};require(::ReadFile(handle.get(),block.data(),static_cast<DWORD>(block.size()),&count,nullptr));if(!count)break;
        result.bytes.append(block.data(),count);require(result.bytes.size()<=maximum_bytes);}
    wf::Handle named(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(named.get()!=INVALID_HANDLE_VALUE&&wf::same_snapshot(before,wf::inspect(handle.get(),false))&&wf::same_snapshot(before,wf::inspect(named.get(),false))&&
        result.bytes.size()==static_cast<std::uint64_t>(before.standard.EndOfFile.QuadPart));
    wf::verify_exact_owner(handle.get(),owner);wf::verify_security(handle.get(),owner,private_file,false);
    std::string identifier;for(const auto byte:before.file_id.FileId.Identifier){identifier+="0123456789abcdef"[byte>>4];identifier+="0123456789abcdef"[byte&15];}
    result.signature=PiManagementJournal::digest(Json::array({before.file_id.VolumeSerialNumber,identifier,before.basic.FileAttributes,
        before.standard.EndOfFile.QuadPart,before.standard.NumberOfLinks,before.basic.LastWriteTime.QuadPart,before.basic.ChangeTime.QuadPart,
        PiManagementJournal::digest(result.bytes)}).dump());
    if(stabilize)require(::FlushFileBuffers(handle.get()));
    return result;
}
std::optional<std::string> hash(const std::optional<File> &file){return file?std::optional<std::string>(PiManagementJournal::digest(file->bytes)):std::nullopt;}
void remove_file(const std::filesystem::path &path,bool missing=false){
    if(!exists_file(path)){require(missing);return;}
    const auto owner=wf::current_sid();wf::ParentChain parents(path,owner);
    {
        wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),DELETE|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        require(file.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(file.get(),false);
        wf::verify_exact_owner(file.get(),owner);wf::verify_security(file.get(),owner,true,false);
        FILE_DISPOSITION_INFO disposition{TRUE};require(::SetFileInformationByHandle(file.get(),FileDispositionInfo,&disposition,sizeof disposition));
    }
    wf::flush_directory(path.parent_path(),owner);
}
void write_file(const std::string &text,const std::string &bytes,FileMode,bool exclusive){
    require(bytes.size()<=maximum_bytes);const auto path=disk_path(text),temporary=exclusive?path:disk_path(text+".tmp."+fresh());
    const auto owner=wf::current_sid();wf::ParentChain parents(path,owner);
    (void)owned_parent(text);wf::flush_directory(path.parent_path(),owner);
    if(exists_file(path)){require(!exclusive);(void)read_file(text,true);}
    wf::PrivateSecurity security(owner);bool created=false;
    try{
        {
            wf::Handle output(::CreateFileW(wf::api_path(temporary).c_str(),GENERIC_WRITE|READ_CONTROL|FILE_READ_ATTRIBUTES,0,
                security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
            require(output.get()!=INVALID_HANDLE_VALUE);created=true;(void)wf::inspect(output.get(),false);
            wf::verify_exact_owner(output.get(),owner);wf::verify_security(output.get(),owner,true,false);
            DWORD count{};require(::WriteFile(output.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&::FlushFileBuffers(output.get()));
        }
        if(!exclusive){
            if(exists_file(path))(void)read_file(text,true);
            require(::MoveFileExW(wf::api_path(temporary).c_str(),wf::api_path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));created=false;
        }
        wf::flush_directory(path.parent_path(),owner);
    }catch(...){if(created)try{remove_file(temporary,true);}catch(...){}throw;}
}
#endif
