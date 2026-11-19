// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <chrono>
#include <functional>
namespace wsprrypi {
// One application owner, shared by management, rotation, retirement and gateway
// work. Reserve before accepting/handshaking or starting outgoing resolution.
// No queues: exhaustion refuses admission. Cancellation never frees a slot
// until the actual worker and any mutation reservation have released it.
class PiPairAdmission {
    struct State;
    struct Slot;
public:
    class Connection;
    class Mutation {
    public:
        ~Mutation();
        Mutation(Mutation &&) noexcept;
        Mutation &operator=(Mutation &&) noexcept;
        Mutation(const Mutation &)=delete;
    private:
        friend class Connection;
        Mutation(std::shared_ptr<Slot>,std::string);
        void release() noexcept;
        std::shared_ptr<Slot> slot_;
        std::string target_;
        bool admitted_{};
    };
    class Connection {
    public:
        Connection(Connection &&) noexcept=default;
        Connection &operator=(Connection &&) noexcept=default;
        Connection(const Connection &)=delete;
        bool cancelled() const noexcept;
        std::function<bool()> cancellation() const;
        void cancel() noexcept;
        std::optional<Mutation> try_mutation(const std::string &target_id);
    private:
        friend class PiPairAdmission;
        explicit Connection(std::shared_ptr<Slot>);
        std::shared_ptr<Slot> slot_;
    };
    using Clock=std::function<std::chrono::steady_clock::time_point()>;
    explicit PiPairAdmission(Clock = []{return std::chrono::steady_clock::now();});
    ~PiPairAdmission();
    PiPairAdmission(const PiPairAdmission &)=delete;
    std::optional<Connection> try_incoming();
    std::optional<Connection> try_outgoing();
    // Shared by bootstrap and authenticated listeners, after their real peer
    // checks. This is a quota only, never authorization: 10/s, burst 20, at
    // most 256 source buckets. Equivalent address spellings share one bucket.
    bool allow_request(const std::string &verified_peer_address) noexcept;
    // Cancel existing workers without disabling later admission. Existing
    // leases still own their slots and mutation reservations until release.
    void cancel_connections() noexcept;
    // Permanent for this lifetime. Wake workers through their tokens, without
    // closing another thread's potentially reused descriptor or reusing slots.
    void stop() noexcept;
private:
    std::optional<Connection> acquire(bool outgoing);
    std::shared_ptr<State> state_;
};
// Shared process budget for the public bootstrap, TLS listener and outgoing
// management workers. A stopped listener cancels its own lifetime, never this
// process-wide owner's permanent stop while another listener can restart.
PiPairAdmission &pi_pair_admission();
} // namespace wsprrypi
