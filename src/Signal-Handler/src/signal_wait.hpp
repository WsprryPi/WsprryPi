#pragma once

#include <csignal>
#include <functional>

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

using SignalWaitFunction = std::function<SignalWaitResult(const sigset_t &)>;

SignalWaitResult normalize_sigwaitinfo_result(
    int returned_signal,
    int error_number) noexcept;
SignalWaitResult normalize_sigwait_result(
    int returned_error,
    int received_signal) noexcept;
SignalWaitResult wait_for_blocked_signal(const sigset_t &set) noexcept;
