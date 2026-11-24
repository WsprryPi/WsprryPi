// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_cdc_native.hpp"
#include <iostream>
#include <set>
int main(){try{
    const auto rows=wsprrypi::windows_cdc_inventory();std::set<std::string> paths;
    if(rows.size()>4096)throw std::runtime_error("Inventory bound");
    for(const auto &row:rows){
        if(!paths.insert(row.identity.path).second||row.device_instance.empty()||row.product!="WsprryPico"||
            row.identity.usb_serial.size()!=16||!row.identity.vendor_id||!row.identity.product_id)
            throw std::runtime_error("Invalid native Pico metadata");
    }
    std::cout<<"PASS: actual Windows cached serial/PnP inventory; "<<rows.size()<<" Pico interfaces; no port opened\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
