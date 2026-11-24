// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_cdc_native.hpp"
#include <array>
#include <cerrno>
#include <iostream>
int main(){try{
    using namespace wsprrypi;NativeCdcSystem system;UsbCdcStream stream(system);
    std::array<std::uint8_t,1> byte{};
    if(stream.read(byte).state!=wtp::IoState::Closed||stream.write(byte).state!=wtp::IoState::Closed)
        throw std::runtime_error("Unopened stream admitted I/O");
    for(const auto &path:{"COM1","relative-port","\\\\?\\WsprryPi.invalid-no-device-interface"}){
        if(stream.begin_open({path,"0123456789ABCDEF",0xcafe,0x4012,PicoCdcRole::Management},0)||
            stream.state()!=CdcState::Failed||stream.identity()||stream.diagnostic().empty())
            throw std::runtime_error("Unknown native endpoint admitted before selection");
        stream.close();
    }
    if(system.close(123)!=EINVAL)throw std::runtime_error("Unknown descriptor admitted");
    std::cout<<"PASS: actual Windows CDC metadata admission refuses unknown endpoints before port open; unopened I/O remains closed\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
