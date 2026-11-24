// SPDX-License-Identifier: MIT
#include "signal_wait.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <mutex>

struct NativeSignalWaitState {
    HANDLE wake{::CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    HANDLE stopped{::CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::atomic<unsigned> pending{0};
    std::atomic<bool> accepting{true};
    ~NativeSignalWaitState(){if(wake)::CloseHandle(wake);if(stopped)::CloseHandle(stopped);}
};
namespace {
constexpr unsigned stop_bit=1,interrupt_bit=2,terminate_bit=4;
struct Registration {
    std::mutex mutex;
    std::atomic<std::shared_ptr<NativeSignalWaitState>> active;
    bool poisoned{};
};
Registration &registration() {
    // Registration outlives global application objects and late OS callbacks.
    // Events themselves are reference-counted and released at normal stop.
    static auto *value=new Registration;
    return *value;
}
BOOL WINAPI console_event(DWORD event) {
    const bool interrupt=event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT;
    const bool closing=event==CTRL_CLOSE_EVENT||event==CTRL_SHUTDOWN_EVENT||event==CTRL_LOGOFF_EVENT;
    if(!interrupt&&!closing)return FALSE;
    const auto state=registration().active.load();
    if(!state||!state->accepting.load())return FALSE;
    state->pending.fetch_or(interrupt?interrupt_bit:terminate_bit);
    if(!::SetEvent(state->wake))return FALSE;
    // Windows terminates the process when a closing callback returns. Give the
    // application a bounded chance to drain; OS shutdown deadlines still apply.
    if(closing)(void)::WaitForSingleObject(state->stopped,5000);
    return TRUE;
}
}
bool start_native_signal_wait(SignalWaitSet &set) noexcept {
    try {
        auto state=std::make_shared<NativeSignalWaitState>();
        if(!state->wake||!state->stopped)return false;
        auto &owner=registration();std::lock_guard lock(owner.mutex);
        if(owner.active.load()||owner.poisoned)return false;
        owner.active.store(state);
        if(!::SetConsoleCtrlHandler(console_event,TRUE)){owner.active.store({});owner.poisoned=true;return false;}
        set.native=std::move(state);return true;
    }catch(...){return false;}
}
void wake_native_signal_wait(const SignalWaitSet &set) noexcept {
    if(set.native){set.native->pending.fetch_or(stop_bit);(void)::SetEvent(set.native->wake);}
}
bool stop_native_signal_wait(SignalWaitSet &set) noexcept {
    if(!set.native)return true;
    auto &owner=registration();std::lock_guard lock(owner.mutex);
    set.native->accepting.store(false);
    (void)::SetEvent(set.native->stopped);
    bool result=true;
    if(owner.active.load()==set.native){result=::SetConsoleCtrlHandler(console_event,FALSE)!=0;owner.poisoned=!result;owner.active.store({});}
    set.native.reset();return result;
}
SignalWaitResult wait_for_windows_signal(const SignalWaitSet &set) noexcept {
    if(!set.native)return {SignalWaitStatus::Failed,0,ERROR_INVALID_HANDLE};
    while(true) {
        const auto pending=set.native->pending.load();
        if(pending) {
            const auto bit=(pending&stop_bit)?stop_bit:(pending&interrupt_bit)?interrupt_bit:terminate_bit;
            set.native->pending.fetch_and(~bit);
            return {SignalWaitStatus::Received,bit==interrupt_bit?SIGINT:bit==terminate_bit?SIGTERM:0,0};
        }
        if(!::ResetEvent(set.native->wake))return {SignalWaitStatus::Failed,0,static_cast<int>(::GetLastError())};
        if(set.native->pending.load())continue;
        if(::WaitForSingleObject(set.native->wake,INFINITE)!=WAIT_OBJECT_0)
            return {SignalWaitStatus::Failed,0,static_cast<int>(::GetLastError())};
    }
}
#endif
