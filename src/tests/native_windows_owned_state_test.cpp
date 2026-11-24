// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "owned_state_directory.hpp"
#include <iostream>
#define CHECK(value) do{if(!(value))throw std::runtime_error(#value);}while(false)
namespace wf=wsprrypi::windows_file;
using Point=wsprrypi::OwnedStatePublicationPoint;
using wsprrypi::prepare_owned_state_directory;
template<class F> void refuses(F operation) {
    bool refused=false;try{operation();}catch(const std::runtime_error &){refused=true;}
    CHECK(refused);
}
int main() {
    try {
        NativeWindowsFixture fixture;
        const auto validate=[](const std::string &bytes){CHECK(bytes=="first"||bytes=="second");};
        for(const auto point:{Point::PreparedDirectory,Point::WrittenImage,Point::StagedImage,Point::PublishedDirectory,Point::SyncedDirectory}) {
            const auto root=fixture.root/(L"phase-"+std::to_wstring(static_cast<unsigned>(point)));
            unsigned calls=0;
            const auto initial=[&]{return ++calls==1?std::string("first"):std::string("second");};
            refuses([&]{prepare_owned_state_directory(NativeWindowsFixture::utf8(root),initial,validate,
                [&](Point reached){if(reached==point)throw std::runtime_error("interrupted");});});
            const auto resumed=prepare_owned_state_directory(NativeWindowsFixture::utf8(root),initial,validate);
            const auto bytes=wf::read_protected(resumed.file,true,64);
            CHECK(std::string(bytes.begin(),bytes.end())==(point==Point::WrittenImage?"second":"first"));
            CHECK(calls==(point==Point::WrittenImage?2u:1u));
            CHECK(prepare_owned_state_directory(NativeWindowsFixture::utf8(root),initial,validate).existed);
            CHECK(calls==(point==Point::WrittenImage?2u:1u));
        }
        const auto root=fixture.root/L"single-owner";
        unsigned calls=0,refused_calls=0;
        const auto first=prepare_owned_state_directory(NativeWindowsFixture::utf8(root),[&]{++calls;return std::string("first");},validate,
            [&](Point point){
                if(point==Point::StagedImage)refuses([&]{prepare_owned_state_directory(NativeWindowsFixture::utf8(root),
                    [&]{++refused_calls;return std::string("second");},validate);});
            });
        CHECK(calls==1&&refused_calls==0&&!first.existed);
        std::filesystem::remove_all(root);
        refuses([&]{prepare_owned_state_directory(NativeWindowsFixture::utf8(root),
            [&]{++refused_calls;return std::string("second");},validate);});
        CHECK(refused_calls==0);
        const auto lost=fixture.root/L"prepared-loss";
        refuses([&]{prepare_owned_state_directory(NativeWindowsFixture::utf8(lost),[]{return std::string("first");},validate,
            [](Point point){if(point==Point::StagedImage)throw std::runtime_error("interrupted");});});
        CHECK(std::filesystem::remove(std::filesystem::path(lost.native()+L".prepare")/L"authority.json"));
        refuses([&]{prepare_owned_state_directory(NativeWindowsFixture::utf8(lost),
            [&]{++refused_calls;return std::string("second");},validate);});
        CHECK(refused_calls==0);
        std::cout<<"PASS native bootstrap interruption boundaries, retained image, competing writer, and lost-image no-regeneration\n";
    } catch(const std::exception &error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
