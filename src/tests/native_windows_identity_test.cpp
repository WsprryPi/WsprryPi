// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_storage/controller_identity.hpp"
#include <iostream>
#define CHECK(value) do{if(!(value))throw std::runtime_error(#value);}while(false)
using wsprrypi::ControllerIdentityStore;
using Point=wsprrypi::OwnedStatePublicationPoint;
namespace wf=wsprrypi::windows_file;
template<class F> void refuses(F operation) {
    bool refused=false;try{operation();}catch(const std::exception &){refused=true;}
    CHECK(refused);
}
static std::string random_identity() {
    std::array<std::uint8_t,16> bytes{};wsprrypi::native_random(bytes);std::string value;
    for(auto byte:bytes){value+="0123456789abcdef"[byte>>4];value+="0123456789abcdef"[byte&15];}
    return value;
}
static void replace(const std::filesystem::path &path,const std::string &bytes) {
    wf::Handle output(::CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_FLAG_WRITE_THROUGH,nullptr));
    CHECK(output.get()!=INVALID_HANDLE_VALUE);DWORD count{};
    CHECK(::WriteFile(output.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&::FlushFileBuffers(output.get()));
}
int main() {
    try {
        NativeWindowsFixture first,second;
        ControllerIdentityStore store(NativeWindowsFixture::utf8(first.root));
        CHECK(!store.read());unsigned calls=0;
        const auto initial=[&]{++calls;return random_identity();};
        const auto identity=store.initialize(initial);
        CHECK(identity.size()==32&&calls==1&&store.read()==identity);
        ControllerIdentityStore reopened(NativeWindowsFixture::utf8(first.root));
        CHECK(reopened.read()==identity&&reopened.initialize(initial)==identity&&calls==1);
        ControllerIdentityStore other(NativeWindowsFixture::utf8(second.root));
        CHECK(other.initialize(random_identity)!=identity);
        const auto wrong_owner=wf::current_sid()==L"S-1-5-18"?L"S-1-5-32-544":L"S-1-5-18";
        refuses([&]{ControllerIdentityStore wrong(NativeWindowsFixture::utf8(first.root),std::wstring(wrong_owner));});
        const auto file=wf::path_from_utf8(store.directory())/L"authority.json";
        const auto saved=wf::read_protected(NativeWindowsFixture::utf8(file),true,512);
        replace(file,R"({"schema":"controller-identity/1","device_id":"11111111111111111111111111111111","extra":true})");
        refuses([&]{(void)store.read();});
        replace(file,R"({"schema":"controller-identity/1","device_id":"11111111111111111111111111111111","device_id":"22222222222222222222222222222222"})");
        refuses([&]{(void)store.read();});
        replace(file,std::string(saved.begin(),saved.end()));
        CHECK(store.read()==identity);
        CHECK(::DeleteFileW(file.c_str()));
        refuses([&]{(void)store.read();});refuses([&]{(void)store.initialize(initial);});CHECK(calls==1);
        std::filesystem::remove_all(wf::path_from_utf8(store.directory()));
        refuses([&]{(void)store.read();});refuses([&]{(void)store.initialize(initial);});CHECK(calls==1);
        NativeWindowsFixture interrupted;
        ControllerIdentityStore staged(NativeWindowsFixture::utf8(interrupted.root));
        std::string retained;
        refuses([&]{staged.initialize([&]{++calls;retained=random_identity();return retained;},
            [](Point point){if(point==Point::StagedImage)throw std::runtime_error("interrupted");});});
        refuses([&]{(void)staged.read();});
        CHECK(staged.initialize(initial)==retained&&calls==2);
        std::cout<<"PASS native persistent CSPRNG identity, exact SID ownership, strict records, restart, interrupted publication, and lost-authority hold\n";
    } catch(const std::exception &error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
