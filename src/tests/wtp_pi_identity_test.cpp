#include "wtp_endpoint/identity.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>
#else
#include <unistd.h>
#endif
#include <iostream>

int main() {
#ifdef _WIN32
    wchar_t filename[MAX_PATH]{};
    const auto temporary=std::filesystem::temp_directory_path();
    if(!GetTempFileNameW(temporary.c_str(),L"wtp",0,filename))
        throw std::runtime_error("Unable to create identity fixture");
    const auto fixture=std::filesystem::path(filename);
    const auto path=fixture.string();
#else
    char path[] = "/tmp/wtp-pi-machine-id-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) throw std::runtime_error("Unable to create identity fixture");
    close(fd);
#endif
    {
        std::ofstream output(path);
        output << "0123456789abcdef0123456789abcdef\n";
    }
    const auto first = wsprrypi::wtp_pi_device_id(path);
    const auto second = wsprrypi::wtp_pi_device_id(path);
#ifdef _WIN32
    std::filesystem::remove(fixture);
    bool unavailable=false;
    try{(void)wsprrypi::wtp_legacy_device_id();}catch(const std::runtime_error &){unavailable=true;}
    if(!unavailable)throw std::runtime_error("Windows cannot invent a legacy identity");
#else
    unlink(path);
#endif
    if (first != "dfa2eb043ac5925afbb80bfc556bb807" || first != second)
        throw std::runtime_error("Device identity must be stable and canonical");
    std::array<std::uint8_t,16> uuid{};
    for(std::size_t index=0;index<uuid.size();++index)uuid[index]=static_cast<std::uint8_t>(index);
    const auto mac=wsprrypi::wtp_pi_macos_device_id(uuid);
    if(mac!="ae03ebba4f806d5d26bcbd666a5f4a83"||mac!=wsprrypi::wtp_pi_macos_device_id(uuid)||mac==first)
        throw std::runtime_error("Mac identity mapping must be stable, canonical, and domain-separated");
    for(const auto byte:{std::uint8_t{0},std::uint8_t{255}}){
        uuid.fill(byte);bool refused=false;
        try{(void)wsprrypi::wtp_pi_macos_device_id(uuid);}catch(const std::runtime_error&){refused=true;}
        if(!refused)throw std::runtime_error("Unavailable Mac host UUID cannot fabricate identity");
    }
#ifdef __APPLE__
    const auto native=wsprrypi::wtp_pi_device_id();
    if(native.size()!=32||native.find_first_not_of("0123456789abcdef")!=std::string::npos||native!=wsprrypi::wtp_pi_device_id())
        throw std::runtime_error("Actual Mac OS identity must be stable and canonical");
#endif
    wsprrypi::WtpPiBootIdentity boot;
    const auto prior = boot.new_boot_id();
    const auto next = boot.new_boot_id();
    if (prior.size() != 32 || next.size() != 32 || prior == next)
        throw std::runtime_error("Boot identities must be fresh");
    std::cout<<"PASS: unchanged Linux identity golden, domain-separated Mac UUID mapping/refusals, native OS identity where applicable, and fresh boot identities\n";
}
