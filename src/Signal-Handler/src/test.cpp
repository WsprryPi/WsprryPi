/**
 * @file test.cpp
 * @brief Bounded, unprivileged SignalHandler lifecycle test.
 *
 * Licensed under the repository-root LICENSE.md.
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "signal_handler.hpp"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <thread>
#include <unistd.h>

int main()
{
    block_signals();

    const SignalWaitResult linux_received =
        normalize_sigwaitinfo_result(SIGTERM, EIO);
    const SignalWaitResult linux_interrupted =
        normalize_sigwaitinfo_result(-1, EINTR);
    const SignalWaitResult mac_received = normalize_sigwait_result(0, SIGTERM);
    errno = EBUSY;
    const SignalWaitResult mac_failed = normalize_sigwait_result(EINVAL, 0);
    if (linux_received.status != SignalWaitStatus::Received ||
        linux_received.signal_number != SIGTERM ||
        linux_interrupted.status != SignalWaitStatus::Interrupted ||
        mac_received.status != SignalWaitStatus::Received ||
        mac_received.signal_number != SIGTERM ||
        mac_failed.status != SignalWaitStatus::Failed ||
        mac_failed.error_number != EINVAL)
    {
        std::cerr << "Signal wait result normalization failed.\n";
        return 1;
    }

    std::atomic<bool> callbackCalled(false);
    std::atomic<int> callbackSignal(0);
    std::atomic<bool> callbackCritical(true);

    SignalHandler handler;
    handler.setCallback([&](int signum, bool critical) {
        callbackSignal.store(signum);
        callbackCritical.store(critical);
        callbackCalled.store(true);
    });
    handler.start();

    // This controlled signal targets only the current test process. The handled
    // set was blocked before the worker started, so the dedicated waiter owns it.
    if (::kill(::getpid(), SIGTERM) != 0)
    {
        std::cerr << "Unable to send the controlled self-test signal.\n";
        handler.stop();
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!callbackCalled.load() &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool stopped = handler.stop();
    const bool stoppedAgain = handler.stop();
    const bool valid = callbackCalled.load() &&
                       callbackSignal.load() == SIGTERM &&
                       !callbackCritical.load() && stopped && !stoppedAgain &&
                       SignalHandler::signalToString(SIGTERM) == "SIGTERM" &&
                       SignalHandler::signalToString(-1) == "UNKNOWN";
    if (!valid)
    {
        std::cerr << "SignalHandler callback or shutdown contract failed.\n";
        return 1;
    }

    handler.start();
    if (!handler.stop() || handler.stop())
    {
        std::cerr << "SignalHandler restart/stop cycle failed.\n";
        return 1;
    }

    std::atomic<int> failed_wait_calls(0);
    SignalHandler failing_handler([&](const sigset_t &) {
        failed_wait_calls.fetch_add(1);
        return SignalWaitResult{SignalWaitStatus::Failed, 0, EIO};
    });
    failing_handler.start();
    const auto failure_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(2);
    while (failed_wait_calls.load() == 0 &&
           std::chrono::steady_clock::now() < failure_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool failed_worker_stopped = failing_handler.stop();
    if (failed_wait_calls.load() != 1 || !failed_worker_stopped ||
        failing_handler.stop())
    {
        std::cerr << "Signal wait failure must stop without a busy loop.\n";
        return 1;
    }

    {
        SignalHandler destructed_handler;
        destructed_handler.start();
    }

    return 0;
}
