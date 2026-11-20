// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <vector>
extern char **environ;
namespace wsprrypi::testing {
// A fresh executable models process loss without using SQLite/system logging
// in a forked copy of an initialized process (unsafe on native macOS).
inline pid_t launch_software_child(const std::string &executable,std::vector<std::string> arguments){
    arguments.insert(arguments.begin(),executable);
    std::vector<char *> pointers;for(auto &argument:arguments)pointers.push_back(argument.data());pointers.push_back(nullptr);
    pid_t child{};const int error=::posix_spawn(&child,executable.c_str(),nullptr,nullptr,pointers.data(),environ);
    if(error)throw std::runtime_error("Software child launch failed: "+std::to_string(error));
    return child;
}
inline int wait_software_child(pid_t child){
    int status{};pid_t waited;
    do{waited=::waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    if(waited!=child||!WIFEXITED(status))throw std::runtime_error("Software child wait failed: status="+
        std::to_string(status)+" errno="+std::to_string(waited<0?errno:0));
    return WEXITSTATUS(status);
}
} // namespace wsprrypi::testing
