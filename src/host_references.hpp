// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include <algorithm>
#include <string>
#include <string_view>
namespace wsprrypi::host_references {
// Stored references describe the source host. They never authorize opening a
// destination path; native consumers must admit their own namespace separately.
inline bool windows_file(std::string_view value){
    if(value.size()<4||!((value[0]>='A'&&value[0]<='Z')||(value[0]>='a'&&value[0]<='z'))||
        value[1]!=':'||(value[2]!='/'&&value[2]!='\\'))return false;
    std::size_t offset=3;
    while(offset<value.size()){
        const auto end=value.find_first_of("/\\",offset);
        const auto part=value.substr(offset,end==value.npos?value.size()-offset:end-offset);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' '||
            std::any_of(part.begin(),part.end(),[](unsigned char c){return c<32||c==127||c==':'||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*';}))return false;
        std::string stem(part.substr(0,part.find('.')));
        while(!stem.empty()&&stem.back()==' ')stem.pop_back();
        for(auto &c:stem)if(c>='a'&&c<='z')c-=32;
        if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||stem=="CLOCK$"||stem=="CONIN$"||stem=="CONOUT$")return false;
        if(stem.starts_with("COM")||stem.starts_with("LPT")){
            const auto tail=stem.substr(3);
            if((tail.size()==1&&tail[0]>='0'&&tail[0]<='9')||tail=="\xc2\xb9"||tail=="\xc2\xb2"||tail=="\xc2\xb3")return false;
        }
        if(end==value.npos)return true;
        offset=end+1;
    }
    return false;
}
inline bool posix_file(std::string_view value){return value.starts_with('/');}
inline bool stored_file(std::string_view value){return posix_file(value)||windows_file(value);}
inline bool posix_usb(std::string_view value){return value.starts_with("/dev/")&&value.find("/../")==value.npos;}
inline bool windows_usb(std::string_view value){
    // SetupAPI interface names are opaque. Do not infer VID/PID, serial, or
    // role from their spelling; the native inventory supplies those bindings.
    return value.starts_with("\\\\?\\")&&value.size()>4&&std::all_of(value.begin()+4,value.end(),[](unsigned char c){
        return c>=32&&c<127&&c!='\\'&&c!='/'&&c!=':'&&c!='"'&&c!='<'&&c!='>'&&c!='|'&&c!='?'&&c!='*';});
}
inline bool stored_usb(std::string_view value){return posix_usb(value)||windows_usb(value);}
inline bool native_file(std::string_view value){
#ifdef _WIN32
    return windows_file(value);
#else
    return posix_file(value);
#endif
}
inline bool native_usb(std::string_view value){
#ifdef _WIN32
    return windows_usb(value);
#else
    return posix_usb(value);
#endif
}
}
