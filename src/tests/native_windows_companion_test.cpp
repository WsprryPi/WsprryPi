// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_storage/companion_files.hpp"
#include <cassert>
#include <iostream>
using namespace wsprrypi;
namespace {
template<class F>void refuses(F action){bool held=false;try{action();}catch(const std::exception &){held=true;}assert(held);}
void put(const std::string &text,const std::string &bytes){
    namespace wf=windows_file;const auto path=wf::path_from_utf8(text);wf::ParentChain parents(path,wf::current_sid());
    wf::PrivateSecurity security(wf::current_sid());
    wf::Handle output(::CreateFileW(wf::api_path(path).c_str(),GENERIC_WRITE,0,security.attributes(),CREATE_NEW,FILE_FLAG_WRITE_THROUGH,nullptr));
    assert(output.get()!=INVALID_HANDLE_VALUE);DWORD count{};
    assert(::WriteFile(output.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&::FlushFileBuffers(output.get()));
}
}
int main(){
    try{
        NativeWindowsFixture fixture;const auto text=[](const auto &path){return NativeWindowsFixture::utf8(path);};
        const auto prefix=text(fixture.root/L"configuration-\u03a9"),target=std::string(32,'a');
        prepare_companion_directory(prefix+".management");
        const auto path=prefix+".management/"+target+".json";const auto payload=std::string(2*1048576+37,'x');put(path,payload);
        const auto file=inspect_companion_file(prefix,CompanionKind::TargetManagement,target);assert(file&&file->size==payload.size());
        assert(parse_companion_manifest(companion_manifest({*file}))==std::vector{*file});
        auto invalid=companion_manifest({*file});invalid[0]["extra"]=true;refuses([&]{parse_companion_manifest(invalid);});
        refuses([&]{companion_manifest({*file,*file});});
        const auto destination=text(fixture.root/L"copied");copy_companion_file(path,destination,*file,true);
        verify_companion_file(destination,*file);copy_companion_file(path,destination,*file,true);
        const auto restart=text(fixture.root/L"restart");put(restart+".writing","partial");
        copy_companion_file(path,restart,*file,true);assert(!std::filesystem::exists(fixture.root/L"restart.writing"));
        assert(::DeleteFileW(windows_file::api_path(windows_file::path_from_utf8(path)).c_str()));put(path,std::string(payload.size(),'z'));
        const auto changed=text(fixture.root/L"changed");refuses([&]{copy_companion_file(path,changed,*file,true);});
        assert(!std::filesystem::exists(fixture.root/L"changed")&&!std::filesystem::exists(fixture.root/L"changed.writing"));
        const auto logging=prefix+".usb-logs.json";put(logging,"{}");
        const auto log=inspect_companion_file(prefix,CompanionKind::UsbLogging);assert(log&&read_logging_companion(prefix,*log)=="{}");
        assert(::DeleteFileW(windows_file::api_path(windows_file::path_from_utf8(logging)).c_str()));put(logging,"[]");
        refuses([&]{read_logging_companion(prefix,*log);});
        assert(::CreateHardLinkW((fixture.root/L"alias").c_str(),(fixture.root/L"copied").c_str(),nullptr));
        refuses([&]{verify_companion_file(destination,*file);});
        std::cout<<"Native companion streaming, Unicode, closed manifest, partial staging, idempotence, stale source, logging CAS, and hard-link refusal checks passed\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
