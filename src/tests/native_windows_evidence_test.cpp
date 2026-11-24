// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_recovery/evidence.hpp"
#include "application_recovery/windows_file_io.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <stdexcept>
#define CHECK(condition) do { if(!(condition)) throw std::runtime_error(std::string("Evidence check failed at line ")+std::to_string(__LINE__)+": "+#condition+" (Windows error "+std::to_string(::GetLastError())+")"); } while(false)
#include <iostream>
using namespace wsprrypi;
namespace wf=windows_file;
namespace {
void stage(const char *value){std::cerr<<"Evidence fixture: "<<value<<std::endl;}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception &){refused=true;}CHECK(refused);}
void directory(const std::filesystem::path &path){wf::PrivateSecurity security(wf::current_sid());CHECK(::CreateDirectoryW(wf::api_path(path).c_str(),security.attributes()));}
void write(const std::filesystem::path &path,const std::string &data){
    wf::PrivateSecurity security(wf::current_sid());wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
        FILE_SHARE_READ,security.attributes(),CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));CHECK(file.get()!=INVALID_HANDLE_VALUE);
    DWORD count{};CHECK(::WriteFile(file.get(),data.data(),static_cast<DWORD>(data.size()),&count,nullptr)&&count==data.size()&&::FlushFileBuffers(file.get()));
}
}
int main(){try{
    stage("private generation");
    NativeWindowsFixture fixture;stage("fixture created");const auto generation=fixture.root/L"generation-\u03a9";directory(generation);stage("generation created");
    const auto root=NativeWindowsFixture::utf8(generation);CHECK(capture_recovery_evidence(root).empty());stage("absent inventory accepted");
    directory(generation/L"evidence");
    const std::string member="target-"+std::string(32,'b')+".json",payload(1048607,'x');
    const nlohmann::json source{{"identity",std::string(32,'a')},{"role","controller"},{"application_version","4-test"},{"storage","application-state/1"},{"platform","windows"}};
    const auto files=nlohmann::json::array({{{"name",member},{"size",payload.size()},{"sha256",PiManagementJournal::digest(payload)}}});
    const nlohmann::json content{{"schema","recovery-evidence/1"},{"source",source},{"files",files}};
    const auto digest=PiManagementJournal::digest(content.dump(-1,' ',true)),id=digest.substr(0,32);
    const auto group=generation/L"evidence"/id;directory(group);write(group/member,payload);
    stage("payload written");
    const auto record=NativeWindowsFixture::utf8(group/L"index.json");
    PiManagementJournal::publish_private_record(record,{},nlohmann::json{{"schema","recovery-evidence/1"},{"source",source},{"created_utc",1800000000},
        {"archive_sha256",std::string(64,'c')},{"content_digest",digest},{"files",files}}.dump());
    stage("manifest published");
    const auto admitted=capture_recovery_evidence(root);CHECK(admitted.size()==2&&admitted.at("application/evidence/"+id+"/"+member)==NativeWindowsFixture::utf8(group/member));
    stage("inventory admitted");
    {recovery_windows::Input stream(group/member,payload.size());CHECK(recovery_windows::digest(stream,payload.size())==PiManagementJournal::digest(payload));refuses([&]{stream.flush();});}
    {recovery_windows::Input stream(group/member,payload.size(),recovery_windows::Input::Access::DurableRead);
        CHECK(recovery_windows::digest(stream,payload.size())==PiManagementJournal::digest(payload));stream.flush();}
    const auto copied=fixture.root/L"copied",failed=fixture.root/L"failed";
    recovery_windows::copy(group/member,copied,payload.size(),PiManagementJournal::digest(payload));
    const auto archive=fixture.root/L"archive";directory(archive);directory(archive/L"application");
    const auto portable_member=archive/L"application/authority.json";
    recovery_windows::copy(group/member,portable_member,payload.size(),PiManagementJournal::digest(payload));
    {recovery_windows::Input stream(archive/L"application"/L"authority.json",payload.size());CHECK(recovery_windows::digest(stream,payload.size())==PiManagementJournal::digest(payload));}
    refuses([&]{recovery_windows::copy(group/member,portable_member,payload.size(),{});});
    stage("streaming copy complete");
    refuses([&]{recovery_windows::copy(group/member,copied,payload.size(),{});});
    {recovery_windows::Input stream(copied,payload.size());CHECK(recovery_windows::digest(stream,payload.size())==PiManagementJournal::digest(payload));}
    refuses([&]{recovery_windows::copy(group/member,failed,payload.size(),std::string(64,'0'));});CHECK(!std::filesystem::exists(failed));
    const auto credential=fixture.root/L"credential",credential_copy=fixture.root/L"credential-copy",cancelled=fixture.root/L"cancelled";
    write(credential,"synthetic public certificate");
    auto grant=[&](const wchar_t *access){
        const auto sid=wf::current_sid();const auto sddl=L"O:"+sid+L"D:P(A;;FA;;;"+sid+L")(A;;FA;;;SY)(A;;FA;;;BA)(A;;"+std::wstring(access)+L";;;WD)";
        PSECURITY_DESCRIPTOR descriptor{};CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr));
        struct Free{PSECURITY_DESCRIPTOR value;~Free(){::LocalFree(value);}} free{descriptor};
        BOOL present{},defaulted{};PACL acl{};CHECK(::GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted)&&present&&acl);
        wf::Handle handle(::CreateFileW(wf::api_path(credential).c_str(),WRITE_DAC|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        CHECK(handle.get()!=INVALID_HANDLE_VALUE&&::SetSecurityInfo(handle.get(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr)==ERROR_SUCCESS);
    };
    grant(L"FR");
    using Input=recovery_windows::Input;
    refuses([&]{Input stream(credential,1048576);});
    {Input stream(credential,1048576,Input::Access::Read,Input::Policy::Credential);CHECK(recovery_windows::digest(stream,28)==PiManagementJournal::digest("synthetic public certificate"));}
    refuses([&]{Input stream(credential,1048576,Input::Access::DurableRead,Input::Policy::Credential);});
    recovery_windows::copy(credential,credential_copy,28,{},Input::Policy::Credential);
    {Input stream(credential_copy,28);CHECK(recovery_windows::digest(stream,28)==PiManagementJournal::digest("synthetic public certificate"));}
    refuses([&]{recovery_windows::copy(credential,cancelled,28,{},Input::Policy::Credential,[]{throw std::runtime_error("explicit cancellation");});});
    CHECK(!std::filesystem::exists(cancelled));
    grant(L"FW");refuses([&]{Input stream(credential,1048576,Input::Access::Read,Input::Policy::Credential);});
    stage("failed-copy retirement complete");
    write(group/L"unknown", "unlisted");refuses([&]{capture_recovery_evidence(root);});CHECK(::DeleteFileW(wf::api_path(group/L"unknown").c_str()));
    const auto alias=group/L"alias";CHECK(::CreateHardLinkW(wf::api_path(alias).c_str(),wf::api_path(group/member).c_str(),nullptr));refuses([&]{capture_recovery_evidence(root);});CHECK(::DeleteFileW(wf::api_path(alias).c_str()));
    stage("unknown member and hard-link refusal complete");
    if(!::DeleteFileW(wf::api_path(group/member).c_str()))throw std::runtime_error("payload retirement: "+std::to_string(::GetLastError()));
    stage("old payload retired");write(group/member,std::string(payload.size(),'z'));stage("changed payload written");refuses([&]{capture_recovery_evidence(root);});stage("changed payload refused");
    std::cout<<"Native retained-evidence validation, Unicode paths, bounded streaming above 1 MiB, exact inventory, changed payload and hard-link refusals passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<" (Windows error "<<::GetLastError()<<")\n";return 1;}}
