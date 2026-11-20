// SPDX-License-Identifier: MIT
#include "application_recovery/evidence.hpp"
#include <iostream>
int main(int argc,char **argv){try{
    if(argc!=2)return 2;
    const auto files=wsprrypi::capture_recovery_evidence(argv[1]);std::cout<<files.size()<<'\n';return 0;
}catch(...){return 1;}}
