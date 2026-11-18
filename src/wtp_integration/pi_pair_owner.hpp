// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_history.hpp"
namespace wsprrypi {
// Startup-only holder. Opening absence never initializes it. Only an actual
// trusted first-use event can provision; active worker references prevent
// replacement of an unavailable history owner behind those workers.
class PiPairOwner {
public:
    PiPairOwner(std::string directory,std::string self);
    void provision(PiPairInitializationProof);
    std::shared_ptr<PiPairHistory> history();
    // After all service/worker references are released, reopen the same selected
    // history. Never initialize or replace unavailable records. Dirty shutdown
    // accounting is recovered exactly as at process startup.
    bool reopen_history();
    const std::string &self() const noexcept{return self_;}
private:
    std::string directory_,self_;
    std::mutex mutex_;
    std::shared_ptr<PiPairHistory> history_;
};
// Ordinary process-selected device directory; typed endpoint fixtures provide
// their existing private-state root. Neither accessor creates blank history.
std::shared_ptr<PiPairOwner> prepare_pi_pair_owner(const std::string &self);
std::shared_ptr<PiPairOwner> pi_pair_owner();
}
