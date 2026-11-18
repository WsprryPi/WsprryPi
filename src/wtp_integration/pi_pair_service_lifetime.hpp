// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_history.hpp"
#include "pi_pair_admission.hpp"
#include "management_browser_session.hpp"
#include <condition_variable>
namespace wsprrypi {
// The runtime closes admission, cancels exchanges, stops/joins its listeners
// and maintenance workers, then drains HTTP calls before attesting clean exit.
// No lifetime operation may run under configuration, admission or history locks.
class PiPairServiceLifetime {
    struct State;
public:
    class Call {
    public:
        ~Call();
        Call(Call &&) noexcept;
        Call(const Call &)=delete;
        Call &operator=(Call &&)=delete;
        bool cancelled() const noexcept;
    private:
        friend class PiPairServiceLifetime;
        explicit Call(std::shared_ptr<State>);
        std::shared_ptr<State> state_;
    };
    PiPairServiceLifetime(std::shared_ptr<PiPairHistory>,PiPairAdmission &,ManagementBrowserSessions &);
    ~PiPairServiceLifetime();
    PiPairServiceLifetime(const PiPairServiceLifetime &)=delete;
    // Does not bind a socket or initialize missing storage. Reopening requires
    // the previous lifetime to have drained completely.
    bool open();
    // Admit protected local recovery while storage is unavailable, without
    // initializing it or attesting a counter lifetime. Still exclusive.
    bool open_for_recovery();
    // After actual protected recovery, checkpoint running before exposing a
    // pairing code. A read or a failed recovery can never initialize history.
    bool ensure_history();
    std::optional<Call> enter();
    // Nonblocking: callable by a policy-change observer. The owner separately
    // cancels its window and listeners without joining from a request worker.
    void close() noexcept;
    // Owner thread only, after joining all non-HTTP workers. Never call from
    // an admitted Call. A storage failure propagates and retains dirty history.
    void drain();
private:
    bool claim_history();
    void release_history();
    std::shared_ptr<PiPairHistory> history_;
    PiPairAdmission &admission_;
    ManagementBrowserSessions &sessions_;
    std::mutex lifecycle_;
    std::shared_ptr<State> state_;
};
}
