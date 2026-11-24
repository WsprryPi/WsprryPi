// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_cdc_native.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace wsprrypi;
template<class F> void refused(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}assert(failed);}
NativeCdcEndpoint endpoint(std::string parent,std::string serial,unsigned role,std::string name) {
    const auto path="/dev/cu.test-"+parent+"-"+std::to_string(role);
    return {{path,"iokit:"+parent+"/"+std::to_string(role),serial,100+role,0xcafe,0x4012,role},parent,
        "WsprryPico",name,{{"/dev/tty.test-"+parent+"-"+std::to_string(role),200+role}}};
}
int main(int argc,char **argv) {
    if(argc==2&&std::string_view(argv[1])=="--native-inventory") {
        const auto rows=macos_cdc_inventory();
        std::cout<<"PASS native cached macOS CDC inventory; "<<rows.size()<<" Pico interfaces; no serial port opened\n";return 0;
    }
    assert(argc==1);
    auto rows=std::vector{endpoint("a","serial-a",0,"WsprryPico Console"),
        endpoint("a","serial-a",2,"WsprryPico Reserved"),endpoint("a","serial-a",4,"WsprryPico Management"),
        endpoint("a","serial-a",6,"WsprryPico Logging"),endpoint("b","serial-b",4,"WsprryPico Management")};
    PicoCdcSelection legacy{rows[1].identity.path,"serial-a",0xcafe,0x4012,PicoCdcRole::Wtp};
    assert(select_native_cdc_role(rows,legacy,PicoCdcRole::Management).path==rows[2].identity.path);
    const auto management=select_native_cdc_role(rows,legacy,PicoCdcRole::Management);
    assert(select_native_cdc_role(rows,management,PicoCdcRole::Logging).path==rows[3].identity.path);
    const auto alias=select_native_cdc_identity(rows,rows[2].aliases[0].path);
    assert(alias.path==rows[2].aliases[0].path&&alias.device_number==204&&alias.control_interface==4);
    for(const auto role:{PicoCdcRole::Console,PicoCdcRole::Wtp})refused([&]{select_native_cdc_role(rows,legacy,role);});
    auto wrong=legacy;wrong.usb_serial="serial-b";refused([&]{select_native_cdc_role(rows,wrong,PicoCdcRole::Management);});
    wrong=legacy;wrong.vendor_id=1;refused([&]{select_native_cdc_role(rows,wrong,PicoCdcRole::Management);});
    wrong=legacy;wrong.product_id=1;refused([&]{select_native_cdc_role(rows,wrong,PicoCdcRole::Management);});
    wrong=legacy;wrong.path=rows[0].identity.path;refused([&]{select_native_cdc_role(rows,wrong,PicoCdcRole::Management);});
    auto duplicate=rows;duplicate.push_back(rows[2]);refused([&]{select_native_cdc_role(duplicate,legacy,PicoCdcRole::Management);});
    refused([&]{select_native_cdc_identity(duplicate,rows[2].identity.path);});
    duplicate=rows;duplicate.push_back(endpoint("clone","serial-a",4,"WsprryPico Management"));
    refused([&]{select_native_cdc_role(duplicate,legacy,PicoCdcRole::Management);});
    auto absent=rows;absent.erase(absent.begin()+2);refused([&]{select_native_cdc_role(absent,legacy,PicoCdcRole::Management);});
    auto renamed=rows;renamed[2].interface_name="WsprryPico Console";
    refused([&]{select_native_cdc_role(renamed,legacy,PicoCdcRole::Management);});
    refused([]{inspect_native_cdc("relative-port");});
    std::cout<<"PASS physical-parent/serial/VID/PID role binding, distinct alias device numbers, duplicate/missing-role refusal, and no Console fallback\n";
}
