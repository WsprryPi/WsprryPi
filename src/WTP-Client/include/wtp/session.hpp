// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "wtp/transport.hpp"
#include "wtp/wire.hpp"

namespace wsprrypi::wtp {
enum class SessionPhase { Disconnected, Hello, Status, Caps, Ready, IdentityChanged, Fault };
enum class ResultKind { Acknowledged, Rejected, NotSent, Unknown, Reconciled };
struct TransactionResult {
    Operation operation;
    ResultKind kind;
    std::optional<Response> response;
    std::string explanation;
};
struct SessionOptions {
    std::string session_id, owner_id, expected_device_id;
    std::string client_name{"WsprryPi"}, client_version{"development"};
    std::uint64_t transaction_timeout_ms{8000}, idle_timeout_ms{5000};
    bool learn_device_identity{false}; // Explicit Plain LAN binding only.
};
struct JobEvidence {
    std::string device_id, boot_id, job_id;
    std::optional<State> state;
    std::optional<bool> output_active;
    bool authoritative{};
    std::optional<bool> device_output_active{};
    bool completed() const noexcept {
        return authoritative && state == State::Complete && output_active == false &&
               device_output_active == false;
    }
    bool cancelled() const noexcept {
        return authoritative && state == State::Aborted && output_active == false &&
               device_output_active == false;
    }
};

// Single-owner, externally polled session. No threads, OS I/O, automatic
// retransmission, renewal, ABORT or RELEASE. Keep this object across reconnects.
class Session {
  public:
    explicit Session(SessionOptions options);
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Session(Session &&) = delete;
    Session &operator=(Session &&) = delete;
    // Caller owns stream lifetime, closes via disconnect before destroying it,
    // and supplies the same authenticated principal on resumed streams.
    bool connect(ByteStream &stream, std::uint64_t now_ms);
    void poll(std::uint64_t now_ms);
    void disconnect(); // Local shutdown only; preserves uncertain mutation evidence.
    bool request(Operation operation, RequestBody body, std::uint64_t now_ms);
    bool retry_uncertain(std::uint64_t now_ms);
    std::optional<TransactionResult> take_result();

    SessionPhase phase() const noexcept { return phase_; }
    bool busy() const noexcept { return pending_.has_value(); }
    bool uncertain() const noexcept { return uncertain_.has_value(); }
    bool needs_status() const noexcept { return needs_status_; }
    bool safety_fault() const noexcept { return safety_fault_; }
    bool owns() const noexcept { return owns_; }
    bool lease_valid(std::uint64_t now_ms) const noexcept;
    bool renewal_due(std::uint64_t now_ms) const noexcept;
    const std::optional<HelloResponse> &identity() const noexcept { return identity_; }
    const std::optional<Capabilities> &capabilities() const noexcept { return caps_; }
    const std::optional<Status> &status() const noexcept { return status_; }
    const std::optional<JobEvidence> &job_evidence() const noexcept { return evidence_; }
    const std::optional<Event> &last_event() const noexcept { return last_event_; }
    const std::string &diagnostic() const noexcept { return diagnostic_; }
    std::uint64_t event_gaps() const noexcept { return event_gaps_; }
    std::uint64_t stale_messages() const noexcept { return stale_messages_; }

  private:
    enum class Internal { None, Hello, Status, Caps };
    struct Transaction {
        Request request;
        RequestPacket packet;
        std::uint64_t started_ms;
        ProgressBudget budget;
        Internal internal;
        std::uint64_t observation_revision;
        bool may_have_written{};
        bool retry{};
    };
    bool observe_time(std::uint64_t now_ms);
    bool begin(Operation op, RequestBody body, Internal internal, std::uint64_t now_ms);
    bool admit(const Request &request, std::uint64_t now_ms);
    void lose_stream(std::string reason, SessionPhase phase = SessionPhase::Disconnected);
    void invalidate_status();
    void receive(const Message &message, std::uint64_t now_ms);
    void response(const Response &response, std::uint64_t now_ms);
    void event(const Event &event);
    bool validate_response(const Transaction &transaction, const Response &response) const;
    bool validate_arm(const ArmResponse &response, const ArmRequest &request) const;
    void accept(const Transaction &transaction, const Response &response, std::uint64_t now_ms);
    void accept_status(const Status &status);
    void reconcile();
    void resolve(std::string explanation);
    void remember_uncertain(Transaction transaction);

    SessionOptions options_;
    ByteStream *stream_{};
    SessionPhase phase_{SessionPhase::Disconnected};
    FrameParser parser_;
    FrameWriter writer_;
    std::optional<Transaction> pending_, uncertain_;
    std::optional<TransactionResult> result_, recovery_result_;
    std::optional<HelloResponse> identity_;
    std::optional<Capabilities> caps_;
    std::optional<Status> status_;
    std::optional<Job> tracked_job_;
    std::optional<LoadResponse> load_ack_;
    std::optional<JobEvidence> evidence_;
    std::optional<Event> last_event_;
    std::optional<std::uint64_t> last_event_id_;
    std::optional<std::uint64_t> lease_started_ms_;
    std::optional<std::uint64_t> connect_started_ms_;
    std::uint64_t lease_duration_ms_{}, observed_ms_{}, request_sequence_{};
    std::uint64_t event_gaps_{}, stale_messages_{};
    std::uint64_t observation_revision_{};
    bool needs_status_{true}, owns_{}, claim_attempted_{}, safety_fault_{};
    std::string diagnostic_;
};
} // namespace wsprrypi::wtp
