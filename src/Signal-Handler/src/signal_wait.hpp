#pragma once

#include <csignal>
#include <functional>
#ifdef _WIN32
#include <memory>
struct NativeSignalWaitState;
struct SignalWaitSet {std::shared_ptr<NativeSignalWaitState> native;};
#else
using SignalWaitSet = sigset_t;
#endif

enum class SignalWaitStatus
{
    Received,
    Interrupted,
    Failed,
};

struct SignalWaitResult
{
    SignalWaitStatus status = SignalWaitStatus::Failed;
    int signal_number = 0;
    int error_number = 0;
};

using SignalWaitFunction = std::function<SignalWaitResult(const SignalWaitSet &)>;

SignalWaitResult normalize_sigwaitinfo_result(
    int returned_signal,
    int error_number) noexcept;
SignalWaitResult normalize_sigwait_result(
    int returned_error,
    int received_signal) noexcept;
SignalWaitResult wait_for_blocked_signal(const SignalWaitSet &set) noexcept;
#ifdef _WIN32
bool start_native_signal_wait(SignalWaitSet &) noexcept;
void wake_native_signal_wait(const SignalWaitSet &) noexcept;
bool stop_native_signal_wait(SignalWaitSet &) noexcept;
SignalWaitResult wait_for_windows_signal(const SignalWaitSet &) noexcept;
#endif
