// SPDX-License-Identifier: MIT
// Included only inside pico_management_journal.cpp's private namespace.
#ifdef _WIN32
namespace wf=windows_file;
std::filesystem::path disk_path(const std::string &value){return wf::path_from_utf8(value);}
std::string path_text(const std::filesystem::path &value){const auto bytes=value.u8string();return {bytes.begin(),bytes.end()};}
bool native_present(const std::filesystem::path &path){
    wf::ParentChain parents(path,wf::current_sid());
    if(::GetFileAttributesW(wf::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();wf::require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
wf::Handle native_directory(const std::filesystem::path &path){
    wf::ParentChain parents(path,wf::current_sid());
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),wf::current_sid());wf::verify_security(handle.get(),wf::current_sid(),true,true);return handle;
}
struct FileStamp {
    wf::Snapshot native{};std::wstring owner;std::uint64_t size{};
    bool operator==(const FileStamp &other)const{return owner==other.owner&&size==other.size&&wf::same_snapshot(native,other.native);}
};
FileStamp stamp(const std::string &value){
    const auto path=disk_path(value);wf::ParentChain parents(path,wf::current_sid());
    wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(file.get()!=INVALID_HANDLE_VALUE);const auto info=wf::inspect(file.get(),false);
    wf::verify_exact_owner(file.get(),wf::current_sid());wf::verify_security(file.get(),wf::current_sid(),true,false);
    wf::require(info.standard.EndOfFile.QuadPart>=0);return {info,wf::file_owner_sid(file.get()),static_cast<std::uint64_t>(info.standard.EndOfFile.QuadPart)};
}
bool native_prepare_directory(const std::string &value,bool create){
    const auto path=disk_path(value);wf::ParentChain parents(path,wf::current_sid());
    if(!native_present(path)){
        if(!create)return false;
        (void)native_directory(path.parent_path());wf::flush_directory(path.parent_path(),wf::current_sid());wf::PrivateSecurity security(wf::current_sid());
        if(!::CreateDirectoryW(wf::api_path(path).c_str(),security.attributes()))wf::require(::GetLastError()==ERROR_ALREADY_EXISTS);
    }
    (void)native_directory(path);
    if(create)wf::flush_directory(path.parent_path(),wf::current_sid());
    return true;
}
void native_save(const std::string &value,const std::string &bytes){
    wf::require(bytes.size()<=maximum_bytes);const auto path=disk_path(value);wf::ParentChain parents(path,wf::current_sid());
    auto root=native_directory(path.parent_path());wf::flush_directory(path.parent_path(),wf::current_sid());
    if(native_present(path))(void)stamp(value);
    wf::PrivateSecurity security(wf::current_sid());wf::Handle output;std::filesystem::path temporary;
    for(unsigned attempt=0;attempt<8;++attempt){
        std::array<std::uint8_t,6> random{};native_random(random);std::string suffix;
        constexpr char alphabet[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        for(const auto byte:random)suffix+=alphabet[byte%62];
        temporary=disk_path(value+".tmp."+suffix);
        output=wf::Handle(::CreateFileW(wf::api_path(temporary).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE|READ_CONTROL,0,
            security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
        if(output.get()!=INVALID_HANDLE_VALUE)break;
        const auto error=::GetLastError();wf::require(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS);
    }
    wf::require(output.get()!=INVALID_HANDLE_VALUE);bool published=false;
    try{
        const auto created=wf::inspect(output.get(),false);wf::verify_exact_owner(output.get(),wf::current_sid());wf::verify_security(output.get(),wf::current_sid(),true,false);
        for(std::size_t offset=0;offset<bytes.size();){DWORD written{};const auto count=static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-offset));
            wf::require(::WriteFile(output.get(),bytes.data()+offset,count,&written,nullptr)&&written&&written<=count);offset+=written;}
        wf::require(::FlushFileBuffers(output.get()));output=wf::Handle();
        output=wf::Handle(::CreateFileW(wf::api_path(temporary).c_str(),GENERIC_READ|DELETE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));wf::require(output.get()!=INVALID_HANDLE_VALUE);
        const auto ready=wf::inspect(output.get(),false);wf::require(created.file_id.VolumeSerialNumber==ready.file_id.VolumeSerialNumber&&
            std::memcmp(created.file_id.FileId.Identifier,ready.file_id.FileId.Identifier,sizeof ready.file_id.FileId.Identifier)==0&&ready.standard.EndOfFile.QuadPart==static_cast<std::int64_t>(bytes.size()));
        wf::verify_exact_owner(output.get(),wf::current_sid());wf::verify_security(output.get(),wf::current_sid(),true,false);
        if(native_present(path))(void)stamp(value);
        wf::require(::MoveFileExW(wf::api_path(temporary).c_str(),wf::api_path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));published=true;
        output=wf::Handle();const auto observed=stamp(value);
        // Rename can advance ChangeTime. Keep the actual object identity,
        // length, and last-write observation instead of comparing that field.
        wf::require(ready.file_id.VolumeSerialNumber==observed.native.file_id.VolumeSerialNumber&&
            std::memcmp(ready.file_id.FileId.Identifier,observed.native.file_id.FileId.Identifier,sizeof ready.file_id.FileId.Identifier)==0&&
            ready.standard.EndOfFile.QuadPart==observed.native.standard.EndOfFile.QuadPart&&ready.basic.LastWriteTime.QuadPart==observed.native.basic.LastWriteTime.QuadPart);
        wf::flush_directory(path.parent_path(),wf::current_sid());
    }catch(...){
        if(!published&&output.get()!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO remove{TRUE};wf::require(::SetFileInformationByHandle(output.get(),FileDispositionInfo,&remove,sizeof remove));output=wf::Handle();wf::flush_directory(path.parent_path(),wf::current_sid());}
        throw;
    }
}
void native_remove(const std::string &value){
    const auto path=disk_path(value);wf::ParentChain parents(path,wf::current_sid());
    wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|DELETE|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE){wf::require(::GetLastError()==ERROR_FILE_NOT_FOUND);return;}
    (void)wf::inspect(file.get(),false);wf::verify_exact_owner(file.get(),wf::current_sid());wf::verify_security(file.get(),wf::current_sid(),true,false);
    FILE_DISPOSITION_INFO remove{TRUE};wf::require(::SetFileInformationByHandle(file.get(),FileDispositionInfo,&remove,sizeof remove));
    file=wf::Handle();wf::flush_directory(path.parent_path(),wf::current_sid());
}
#endif
