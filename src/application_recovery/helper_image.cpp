// SPDX-License-Identifier: MIT
#include "helper_image.hpp"
#include "recovery_age_image.inc"
#include <cerrno>
#include <string_view>
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include <unistd.h>
#endif
namespace wsprrypi {
std::optional<int> recovery_helper_image_main(int argc,char *argv[]){
    if(argc!=2||std::string_view(argv[1])!="--export-recovery-age-helper")return {};
    std::size_t offset=0;
    while(offset<recovery_age_size){
#ifdef _WIN32
        const auto output=::GetStdHandle(STD_OUTPUT_HANDLE);if(!output||output==INVALID_HANDLE_VALUE)return 2;
        DWORD written{};const auto size=static_cast<DWORD>(std::min<std::size_t>(65536,recovery_age_size-offset));
        if(!::WriteFile(output,recovery_age_image+offset,size,&written,nullptr)||!written)return 2;
        const auto count=written;
#else
        const auto count=::write(STDOUT_FILENO,recovery_age_image+offset,recovery_age_size-offset);
        if(count<0&&errno==EINTR)continue;
        if(count<=0)return 2;
#endif
        offset+=static_cast<std::size_t>(count);
    }
    return 0;
}
}
