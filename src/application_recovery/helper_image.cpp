// SPDX-License-Identifier: MIT
#include "helper_image.hpp"
#include "recovery_age_image.inc"
#include <cerrno>
#include <string_view>
#include <unistd.h>
namespace wsprrypi {
std::optional<int> recovery_helper_image_main(int argc,char *argv[]){
    if(argc!=2||std::string_view(argv[1])!="--export-recovery-age-helper")return {};
    std::size_t offset=0;
    while(offset<recovery_age_size){
        const auto count=::write(STDOUT_FILENO,recovery_age_image+offset,recovery_age_size-offset);
        if(count<0&&errno==EINTR)continue;
        if(count<=0)return 2;
        offset+=static_cast<std::size_t>(count);
    }
    return 0;
}
}
