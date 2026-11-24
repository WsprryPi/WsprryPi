// SPDX-License-Identifier: MIT
#pragma once
#include "usb_cdc_native.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
namespace wsprrypi {
// One bounded, typed PnP snapshot. Production fills this from SetupAPI/CM,
// never from request JSON, COM numbering, or a registry path supplied by peers.
struct WindowsCdcProperties {
    std::wstring path,function_instance,parent_instance,product,interface_name,service;
    std::vector<std::wstring> function_hardware,compatible,parent_hardware;
    std::uint32_t parent_capabilities{};
    std::uint64_t device_number{};
};
namespace windows_cdc_metadata {
inline void require(bool value){if(!value)throw std::runtime_error("Windows USB interface metadata is unavailable or inconsistent");}
inline std::string ascii(const std::wstring &value){
    require(!value.empty()&&value.size()<=4096&&std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=32&&c<127;}));
    std::string result;result.reserve(value.size());for(const auto c:value)result+=static_cast<char>(c);return result;
}
inline std::wstring uppercase(std::wstring value){
    (void)ascii(value);for(auto &c:value)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';return value;
}
inline bool physical(const std::wstring &id){
    static const std::wregex pattern(LR"(USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\[^\\]+)",std::regex::ECMAScript|std::regex::icase);
    require(id.size()<=4096);return std::regex_match(id,pattern);
}
}
inline NativeCdcEndpoint windows_cdc_endpoint(const WindowsCdcProperties &row){
    using namespace windows_cdc_metadata;
    require(row.product==L"WsprryPico"&&uppercase(row.service)==L"USBSER"&&row.device_number&&
        row.function_hardware.size()>0&&row.function_hardware.size()<=128&&row.compatible.size()<=128&&
        row.parent_hardware.size()>0&&row.parent_hardware.size()<=128);
    static const std::wregex function_pattern(LR"(USB\\VID_([0-9A-F]{4})&PID_([0-9A-F]{4})(?:&REV_[0-9A-F]{4})?&MI_([0-9A-F]{2}))",std::regex::ECMAScript|std::regex::icase);
    std::optional<std::array<unsigned,3>> identifiers;std::set<std::wstring> seen;
    for(const auto &value:row.function_hardware){
        require(value.size()<=4096&&seen.insert(uppercase(value)).second);std::wsmatch match;
        require(std::regex_match(value,match,function_pattern));
        const std::array<unsigned,3> found{static_cast<unsigned>(std::stoul(match[1].str(),nullptr,16)),
            static_cast<unsigned>(std::stoul(match[2].str(),nullptr,16)),static_cast<unsigned>(std::stoul(match[3].str(),nullptr,16))};
        require(found[0]&&found[1]&&(!identifiers||*identifiers==found));identifiers=found;
    }
    require(std::any_of(row.compatible.begin(),row.compatible.end(),[](const auto &value){return uppercase(value)==L"USB\\CLASS_02&SUBCLASS_02&PROT_00";}));
    require(physical(row.parent_instance)&&(row.parent_capabilities&0x10)); // CM_DEVCAP_UNIQUEID
    const auto separator=row.parent_instance.rfind(L'\\');const auto serial=ascii(row.parent_instance.substr(separator+1));
    require(serial.size()==16&&serial.find_first_not_of("0123456789ABCDEF")==std::string::npos);
    const auto parent_id=uppercase(row.parent_instance.substr(0,separator));
    const auto function_id=uppercase(row.function_instance);const auto function_separator=function_id.rfind(L'\\');
    require(function_separator!=std::wstring::npos);
    std::wsmatch bound;const auto function_prefix=function_id.substr(0,function_separator);
    require(std::regex_match(function_prefix,bound,function_pattern)&&
        std::stoul(bound[1].str(),nullptr,16)==(*identifiers)[0]&&std::stoul(bound[2].str(),nullptr,16)==(*identifiers)[1]&&
        std::stoul(bound[3].str(),nullptr,16)==(*identifiers)[2]);
    static const std::wregex parent_pattern(LR"(USB\\VID_([0-9A-F]{4})&PID_([0-9A-F]{4})(?:&REV_[0-9A-F]{4})?)",std::regex::ECMAScript|std::regex::icase);
    auto valid_parent=[&](const std::wstring &value){
        require(value.size()<=4096);std::wsmatch match;require(std::regex_match(value,match,parent_pattern)&&
            std::stoul(match[1].str(),nullptr,16)==(*identifiers)[0]&&std::stoul(match[2].str(),nullptr,16)==(*identifiers)[1]);
    };
    valid_parent(parent_id);for(const auto &value:row.parent_hardware)valid_parent(value);
    const auto path=ascii(row.path);require(path.starts_with("\\\\?\\")&&path.find('"')==std::string::npos&&path.find('/')==std::string::npos);
    const auto owner=ascii(row.parent_instance),instance=ascii(row.function_instance),name=ascii(row.interface_name);
    const auto control=(*identifiers)[2];
    require((control==0&&name=="WsprryPico Console")||(control==2&&(name=="WsprryPico Reserved"||name=="WsprryPico WTP"))||
        (control==4&&name=="WsprryPico Management")||(control==6&&name=="WsprryPico Logging"));
    return {{path,"windows-pnp:"+instance,serial,row.device_number,static_cast<std::uint16_t>((*identifiers)[0]),
        static_cast<std::uint16_t>((*identifiers)[1]),control},owner,"WsprryPico",name,{}};
}
}
