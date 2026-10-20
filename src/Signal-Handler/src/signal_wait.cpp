#include "signal_wait.hpp"

#include <cerrno>

SignalWaitResult normalize_sigwaitinfo_result(
    int returned_signal,
    int error_number) noexcept
{
    if (returned_signal >= 0)
    {
        return {SignalWaitStatus::Received, returned_signal, 0};
    }
    if (error_number == EINTR)
    {
        return {SignalWaitStatus::Interrupted, 0, EINTR};
    }
    return {SignalWaitStatus::Failed, 0, error_number};
}

SignalWaitResult normalize_sigwait_result(
    int returned_error,
    int received_signal) noexcept
{
    if (returned_error == 0)
    {
        return {SignalWaitStatus::Received, received_signal, 0};
    }
    if (returned_error == EINTR)
    {
        return {SignalWaitStatus::Interrupted, 0, EINTR};
    }
    return {SignalWaitStatus::Failed, 0, returned_error};
}

SignalWaitResult wait_for_blocked_signal(const sigset_t &set) noexcept
{
#if defined(__linux__)
    siginfo_t signal_info{};
    const int returned_signal = sigwaitinfo(&set, &signal_info);
    return normalize_sigwaitinfo_result(returned_signal, errno);
#elif defined(__APPLE__)
    int received_signal = 0;
    const int returned_error = sigwait(&set, &received_signal);
    return normalize_sigwait_result(returned_error, received_signal);
#else
#error "Signal-Handler synchronous waiting is supported only on Linux and macOS"
#endif
}
