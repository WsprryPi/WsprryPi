// SPDX-License-Identifier: MIT
#pragma once
#include <mutex>
#include <utility>
namespace wsprrypi {
class PiPairHistory;
class PiPairMutationGuard {
public:
    PiPairMutationGuard(PiPairMutationGuard &&)=default;
    PiPairMutationGuard(const PiPairMutationGuard &)=delete;
private:
    friend class PiPairHistory;
    PiPairMutationGuard(std::unique_lock<std::recursive_mutex> configuration,std::unique_lock<std::mutex> admission,std::unique_lock<std::mutex> history)
        :configuration_(std::move(configuration)),admission_(std::move(admission)),history_(std::move(history)){}
    // Destruction releases history, admission, then configuration.
    std::unique_lock<std::recursive_mutex> configuration_;
    std::unique_lock<std::mutex> admission_,history_;
};
}
