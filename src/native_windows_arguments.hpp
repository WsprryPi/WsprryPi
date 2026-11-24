// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <shellapi.h>
namespace wsprrypi {
inline std::string windows_argument_utf8(const std::wstring &value){
    windows_file::require(value.size()<=32767&&value.find(L'\0')==std::wstring::npos);
    if(value.empty())return {};
    const auto length=::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    windows_file::require(length>0);std::string bytes(static_cast<std::size_t>(length),'\0');
    windows_file::require(::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),bytes.data(),length,nullptr,nullptr)==length);
    return bytes;
}
inline std::vector<std::string> native_windows_arguments(){
    int count{};auto **arguments=::CommandLineToArgvW(::GetCommandLineW(),&count);
    windows_file::require(arguments!=nullptr);struct Free {wchar_t **value;~Free(){::LocalFree(value);}} free{arguments};
    windows_file::require(count>0&&count<=4096);std::vector<std::string> result;result.reserve(static_cast<std::size_t>(count));
    for(int index=0;index<count;++index)result.push_back(windows_argument_utf8(arguments[index]));
    return result;
}
}
#endif
