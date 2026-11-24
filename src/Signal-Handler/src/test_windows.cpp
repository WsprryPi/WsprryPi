// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "signal_handler.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
struct Handle {HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)::CloseHandle(value);}};
int child(HANDLE ready) {
    using namespace std::chrono_literals;
    SignalHandler handler;std::atomic<unsigned> called{0};
    handler.setCallback([&](int signal,bool critical){if(signal==SIGINT&&!critical)++called;});
    handler.start();handler.start();if(!handler.running())return 2;
    DWORD count{};const char marker='R';
    if(!::WriteFile(ready,&marker,1,&count,nullptr)||count!=1)return 3;
    const auto deadline=std::chrono::steady_clock::now()+6s;
    while(!called.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(5ms);
    if(called.load()!=1||!handler.stop()||handler.stop()||handler.running())return 4;
    handler.start();if(!handler.running()||!handler.stop())return 5;
    SignalHandler failing([](const SignalWaitSet &){return SignalWaitResult{SignalWaitStatus::Failed,0,ERROR_INVALID_HANDLE};});
    failing.start();
    const auto failed_deadline=std::chrono::steady_clock::now()+1s;
    while(failing.running()&&std::chrono::steady_clock::now()<failed_deadline)std::this_thread::sleep_for(5ms);
    if(failing.running()||!failing.stop()||failing.stop())return 6;
    return 0;
}
}
int wmain(int argc,wchar_t **argv) {
    if(argc==3&&std::wstring(argv[1])==L"--console-child") {
        try{return child(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(argv[2]))));}
        catch(...){return 7;}
    }
    if(argc!=1)return 8;
    // A new process group receives the only generated event. No existing
    // process or interactive console group is signalled by this test.
    DWORD processes[1]{};
    const bool allocated=::GetConsoleProcessList(processes,1)==0;
    if(allocated&&!::AllocConsole())return 9;
    struct Console {bool allocated;~Console(){if(allocated)::FreeConsole();}} console{allocated};
    SECURITY_ATTRIBUTES attributes{sizeof attributes,nullptr,TRUE};
    Handle read,write;
    if(!::CreatePipe(&read.value,&write.value,&attributes,0)||!::SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))return 10;
    std::wstring path(32768,L'\0');const auto size=::GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!size||size>=path.size())return 11;path.resize(size);
    auto command=L"\""+path+L"\" --console-child "+std::to_wstring(reinterpret_cast<std::uintptr_t>(write.value));
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
    if(!::CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NEW_PROCESS_GROUP,nullptr,nullptr,&startup,&process))return 12;
    Handle application{process.hProcess},thread{process.hThread};
    struct Reap {HANDLE process;~Reap(){if(::WaitForSingleObject(process,0)!=WAIT_OBJECT_0){::TerminateProcess(process,99);::WaitForSingleObject(process,2000);}}} reap{application.value};
    ::CloseHandle(write.value);write.value=nullptr;
    char marker{};DWORD count{};
    if(!::ReadFile(read.value,&marker,1,&count,nullptr)||count!=1||marker!='R')return 13;
    if(!::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,process.dwProcessId))return 14;
    if(::WaitForSingleObject(application.value,8000)!=WAIT_OBJECT_0)return 15;
    DWORD exit{};if(!::GetExitCodeProcess(application.value,&exit)||exit)return 16;
    std::cout<<"PASS native Windows owned-group console callback, repeat start/stop, restart, and failed-wait teardown\n";
    return 0;
}
#endif
