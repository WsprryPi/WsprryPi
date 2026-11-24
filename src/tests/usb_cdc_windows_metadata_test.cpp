// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_cdc_windows_metadata.hpp"
#include <iostream>
using namespace wsprrypi;
namespace {
void check(bool value){if(!value)throw std::runtime_error("Windows CDC metadata assertion failed");}
template<class F>void refused(F action){try{action();}catch(const std::runtime_error &){return;}throw std::runtime_error("Unsafe Windows CDC metadata accepted");}
WindowsCdcProperties metadata(){return {LR"(\\?\USB#VID_CAFE&PID_4012&MI_04#7&1234&0&0004#{86e0d1e0-8089-11d0-9ce4-08003e301f73})",
    LR"(USB\VID_CAFE&PID_4012&MI_04\7&1234&0&0004)",LR"(USB\VID_CAFE&PID_4012\0123456789ABCDEF)",
    L"WsprryPico",L"WsprryPico Management",L"usbser",
    {LR"(USB\VID_CAFE&PID_4012&REV_0100&MI_04)",LR"(USB\VID_CAFE&PID_4012&MI_04)"},
    {LR"(USB\Class_02&SubClass_02&Prot_00)",LR"(USB\Class_02&SubClass_02)",LR"(USB\Class_02)"},
    {LR"(USB\VID_CAFE&PID_4012&REV_0100)",LR"(USB\VID_CAFE&PID_4012)"},0x10,17};}
}
int main(){try{
    const auto valid=metadata();const auto endpoint=windows_cdc_endpoint(valid);
    check(endpoint.identity.usb_serial=="0123456789ABCDEF"&&endpoint.identity.control_interface==4&&
        endpoint.identity.vendor_id==0xcafe&&endpoint.identity.product_id==0x4012&&endpoint.identity.device_number==17&&
        endpoint.interface_name=="WsprryPico Management"&&endpoint.device_instance==windows_cdc_metadata::ascii(valid.parent_instance));
    auto negative=[&](auto change){auto value=valid;change(value);refused([&]{(void)windows_cdc_endpoint(value);});};
    negative([](auto &v){v.service=L"other-driver";});
    negative([](auto &v){v.compatible={LR"(USB\Class_03&SubClass_02&Prot_00)"};});
    negative([](auto &v){v.function_hardware[1]=LR"(USB\VID_CAFE&PID_4012&MI_06)";});
    negative([](auto &v){v.function_instance=LR"(USB\VID_CAFE&PID_4012&MI_06\7&1234&0&0004)";});
    negative([](auto &v){v.function_hardware.push_back(v.function_hardware[0]);});
    negative([](auto &v){v.parent_capabilities=0;});
    negative([](auto &v){v.parent_instance=LR"(USB\VID_CAFE&PID_4012\7&000000&0&2)";});
    negative([](auto &v){v.parent_hardware[0]=LR"(USB\VID_CAFE&PID_4013&REV_0100)";});
    negative([](auto &v){v.interface_name=L"WsprryPico Logging";});
    negative([](auto &v){v.product=L"Other";});
    negative([](auto &v){v.path=L"COM17";});
    negative([](auto &v){v.path=LR"(\\?\USB#bad)";v.path+=L'\0';});
    negative([](auto &v){v.device_number=0;});
    std::cout<<"PASS: descriptor-derived Windows CDC role, physical VID/PID/serial binding, driver, bounded metadata, and rejection before device open\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
