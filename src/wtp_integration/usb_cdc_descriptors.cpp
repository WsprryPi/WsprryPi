// SPDX-License-Identifier: MIT
#include "usb_cdc_descriptors.hpp"
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::runtime_error("USB CDC interface descriptors are unavailable or inconsistent");}
struct Interface {unsigned type,subclass,protocol;};
}
CdcDescriptorBinding cdc_descriptor_binding(std::span<const std::uint8_t> bytes,unsigned selected) {
    require(selected<=255&&bytes.size()>=9&&bytes.size()<=65535&&bytes[0]==9&&bytes[1]==2&&
        (unsigned(bytes[2])|(unsigned(bytes[3])<<8))==bytes.size()&&bytes[4]>0&&bytes[4]<=32);
    std::map<unsigned,Interface> interfaces;
    std::vector<CdcDescriptorBinding> unions;
    std::optional<unsigned> current;
    bool primary=false;
    for(std::size_t offset=9;offset<bytes.size();) {
        require(bytes.size()-offset>=2);
        const auto size=bytes[offset],type=bytes[offset+1];
        require(size>=2&&size<=bytes.size()-offset);
        const auto row=bytes.subspan(offset,size);
        if(type==4) {
            require(size>=9);current=row[2];primary=row[3]==0;
            if(primary)require(interfaces.emplace(*current,Interface{row[5],row[6],row[7]}).second);
        }else if(type==0x24) {
            require(current&&size>=3);
            if(primary&&row[2]==6) {
                require(size==5&&row[3]==*current&&row[3]!=row[4]);
                unions.push_back({row[3],row[4]});
            }
        }
        offset+=size;
    }
    require(interfaces.size()==bytes[4]&&interfaces.contains(selected));
    std::map<unsigned,unsigned> bound;
    std::optional<CdcDescriptorBinding> result;
    for(const auto &pair:unions) {
        require(interfaces.contains(pair.control_interface)&&interfaces.contains(pair.data_interface));
        const auto control=interfaces.at(pair.control_interface),data=interfaces.at(pair.data_interface);
        require(control.type==2&&control.subclass==2&&control.protocol==0&&data.type==10&&
            data.subclass==0&&data.protocol==0&&++bound[pair.control_interface]==1&&++bound[pair.data_interface]==1);
        if(selected==pair.control_interface||selected==pair.data_interface){require(!result);result=pair;}
    }
    require(result.has_value());return *result;
}
}
