// SPDX-License-Identifier: MIT
// Private filesystem admission for the shared generation engine.
#pragma once
#ifdef _WIN32
namespace wf=windows_file;
std::filesystem::path disk_path(const std::string &text){return wf::path_from_utf8(text);}
std::string path_text(const std::filesystem::path &path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
bool absolute_path(const std::string &text){try{(void)disk_path(text);return true;}catch(...){return false;}}
bool present(const std::string &text){
    if(::GetFileAttributesW(wf::api_path(disk_path(text)).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
wf::Handle owned_directory(const std::filesystem::path &path){
    wf::ParentChain parents(path,wf::current_sid());
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),wf::current_sid());wf::verify_security(handle.get(),wf::current_sid(),true,true);return handle;
}
std::string normalized_root(const std::string &text){
    const auto path=disk_path(text);wf::ParentChain parents(path,wf::current_sid());
    if(present(text)){auto directory=owned_directory(path);return path_text(wf::normalized_path(directory.get()));}
    require(path.filename().native().find(L'~')==std::wstring::npos);
    auto parent=owned_directory(path.parent_path());return path_text(wf::normalized_path(parent.get())/path.filename());
}
using Admission=windows_generation::Admission;

void sync_parent(const std::string &path){wf::flush_directory(disk_path(path).parent_path(),wf::current_sid());}
bool move_no_replace(const std::string &from,const std::string &to){
    const auto source=disk_path(from),destination=disk_path(to);const auto owner=wf::current_sid();
    wf::ParentChain before(source,owner),after(destination,owner);
    (void)owned_directory(source.parent_path());(void)owned_directory(destination.parent_path());
    wf::flush_directory(source.parent_path(),owner);wf::flush_directory(destination.parent_path(),owner);
    // No REPLACE_EXISTING or COPY_ALLOWED: collision and cross-volume movement
    // retain the prepared evidence rather than publishing a different image.
    if(!::MoveFileExW(wf::api_path(source).c_str(),wf::api_path(destination).c_str(),MOVEFILE_WRITE_THROUGH))return false;
    wf::flush_directory(source.parent_path(),owner);wf::flush_directory(destination.parent_path(),owner);return true;
}
void make_directory(const std::string &path){require(!present(path));prepare_companion_directory(path);}
void remove_candidate(const std::string &text,const std::set<std::string> &files,const std::set<std::string> &directories){
    const auto root=disk_path(text);const auto owner=wf::current_sid();wf::ParentChain parents(root,owner);
    std::vector<std::filesystem::path> remove_files,remove_directories;
    {
        auto admission=owned_directory(root);
        // The outer application-root admission remains held throughout this
        // bounded cleanup. Validate every object before deleting any of them.
        unsigned count=0;
        for(const auto &entry:std::filesystem::recursive_directory_iterator(root)){
            require(++count<=1024);const auto relative=entry.path().lexically_relative(root).generic_u8string();
            const std::string name(relative.begin(),relative.end());
            wf::Handle file(::CreateFileW(wf::api_path(entry.path()).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            require(file.get()!=INVALID_HANDLE_VALUE);FILE_ATTRIBUTE_TAG_INFO attributes{};
            require(::GetFileInformationByHandleEx(file.get(),FileAttributeTagInfo,&attributes,sizeof attributes));
            const bool directory=(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
            const auto info=wf::inspect(file.get(),directory);wf::verify_exact_owner(file.get(),owner);wf::verify_security(file.get(),owner,true,directory);
            if(directory){require(directories.contains(name));remove_directories.push_back(entry.path());}
            else{require((files.contains(name)||name=="state.sqlite3.owner.lock")&&info.standard.EndOfFile.QuadPart>=0&&
                info.standard.EndOfFile.QuadPart<=64*1048576);remove_files.push_back(entry.path());}
        }
        for(const auto &file:remove_files)require(::DeleteFileW(wf::api_path(file).c_str()));
        for(auto directory=remove_directories.rbegin();directory!=remove_directories.rend();++directory)
            require(::RemoveDirectoryW(wf::api_path(*directory).c_str()));
        wf::flush_directory(root,owner);
    }
    require(::RemoveDirectoryW(wf::api_path(root).c_str()));sync_parent(text);
}
#endif
