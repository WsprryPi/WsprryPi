// SPDX-License-Identifier: MIT
#pragma once
#include <string_view>
namespace wsprrypi {
// Historical source-host metadata, never permission to open a destination path.
// Preserve existing POSIX provenance, including when restored on Windows, and
// admit an absolute local-drive source from the selected Windows profile.
inline bool valid_provenance_namespace(std::string_view value){
    if(value.empty()||value.find('\0')!=std::string_view::npos)return false;
    if(value.front()=='/')return true;
    return value.size()>=4&&((value[0]>='A'&&value[0]<='Z')||(value[0]>='a'&&value[0]<='z'))&&
        value[1]==':'&&(value[2]=='/'||value[2]=='\\');
}
}
