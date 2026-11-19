// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_window.hpp"
#include "pi_pair_connect.hpp"
#include "pi_pair_renewal.hpp"
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
namespace wsprrypi {
// Runtime-owned maintenance only. It never dispatches settings or RF work.
// Window expiration has a separate fixed worker so a bounded outgoing exchange
// cannot defer expiry. Stop/join before draining the service lifetime.
class PiPairMaintenance {
public:
    using Observe=std::function<bool(const std::string &,const std::function<bool()> &)>;
    PiPairMaintenance(PiPairHistory &,PiPairWindow &,PiPairConnect &,PiPairRenewal &,std::string self,Observe = {});
    ~PiPairMaintenance();
    PiPairMaintenance(const PiPairMaintenance &)=delete;
    bool start();
    // Signal before closing shared runtime admission; join separately outside
    // configuration/history locks and outside every management worker.
    void request_stop() noexcept;
    void stop() noexcept;
private:
    struct Task {std::string id;bool renewal{};bool observation{};};
    struct Backoff {std::chrono::steady_clock::time_point next{};unsigned failures{};};
    std::vector<Task> tasks();
    bool cancelled() const;
    void timer() noexcept;
    void outgoing() noexcept;
    void wait(std::chrono::milliseconds);
    PiPairHistory &history_;
    PiPairWindow &window_;
    PiPairConnect &connect_;
    PiPairRenewal &renewal_;
    std::string self_;
    Observe observe_;
    std::mutex lifecycle_,wait_mutex_;
    std::condition_variable wake_;
    std::atomic_bool stopping_{true};
    std::thread timer_,outgoing_;
};
}
