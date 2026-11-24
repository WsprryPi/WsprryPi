// SPDX-License-Identifier: MIT
#include "controller_identity.hpp"
#ifdef _WIN32
#include "controller_identity_record.hpp"
#include "native_windows_file.hpp"

namespace wsprrypi {
namespace {
namespace wf=windows_file;
void require(bool valid){if(!valid)throw std::runtime_error("controller_identity_storage_hold");}
std::string utf8(const std::filesystem::path &path){const auto value=path.u8string();return {value.begin(),value.end()};}
bool path_present(const std::filesystem::path &path) {
    if(::GetFileAttributesW(wf::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
wf::Handle owned_directory(const std::filesystem::path &path,const std::wstring &owner) {
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),owner);wf::verify_security(handle.get(),owner,true,true);return handle;
}
}
ControllerIdentityStore::ControllerIdentityStore(std::string home,std::optional<std::wstring> owner)
    :home_(std::move(home)),owner_sid_(owner.value_or(wf::current_sid())) {
    validate_home();
}
void ControllerIdentityStore::validate_home() const {
    const auto home=wf::path_from_utf8(home_);
    wf::ParentChain parents(home/L".identity-owner-check",owner_sid_);
    auto directory=owned_directory(home,owner_sid_);
}
std::string ControllerIdentityStore::directory() const {
    return utf8(wf::path_from_utf8(home_)/L".wsprrypi-controller-identity");
}
std::optional<std::string> ControllerIdentityStore::read() const {
    const auto home=wf::path_from_utf8(home_),path=wf::path_from_utf8(directory());
    wf::ParentChain parents(home/L".identity-owner-check",owner_sid_);
    auto home_handle=owned_directory(home,owner_sid_);
    if(!path_present(path)) {
        require(!path_present(std::filesystem::path(path.native()+L".prepare"))&&
            !path_present(std::filesystem::path(path.native()+L".prepare.lock")));
        return {};
    }
    auto directory_handle=owned_directory(path,owner_sid_);
    const auto authority=path/L"authority.json";
    wf::Handle identity(::CreateFileW(wf::api_path(authority).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(identity.get()!=INVALID_HANDLE_VALUE);
    (void)wf::inspect(identity.get(),false);wf::verify_exact_owner(identity.get(),owner_sid_);
    const auto bytes=wf::read_protected(utf8(authority),true,512,owner_sid_);
    return parse_controller_identity_record(std::string(bytes.begin(),bytes.end()));
}
std::string ControllerIdentityStore::initialize(const std::function<std::string()> &random,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint) const {
    require(bool(random)&&wf::current_sid()==owner_sid_);
    const auto home=wf::path_from_utf8(home_);
    wf::ParentChain parents(home/L".identity-owner-check",owner_sid_);
    auto home_handle=owned_directory(home,owner_sid_);
    if(!path_present(wf::path_from_utf8(directory()))) {
        unsigned count=0;
        for(const auto &entry:std::filesystem::directory_iterator(std::filesystem::path(wf::api_path(home)))) {
            require(++count<=4096);const auto name=utf8(entry.path().filename());
            require(name!=".wsprrypi-wtp"&&name!=".wsprrypi-recovery"&&!name.starts_with(".wsprrypi-pairing-")&&
                !name.starts_with(".wsprrypi-controller-retirements-")&&!name.starts_with(".wsprrypi-recurrence-"));
        }
    }
    (void)prepare_owned_state_directory(directory(),[&]{
        const auto bytes=nlohmann::json{{"schema","controller-identity/1"},{"device_id",random()}}.dump();
        (void)parse_controller_identity_record(bytes);return bytes;
    },[](const auto &bytes){(void)parse_controller_identity_record(bytes);},checkpoint);
    const auto stored=read();require(stored.has_value());return *stored;
}
} // namespace wsprrypi
#endif
