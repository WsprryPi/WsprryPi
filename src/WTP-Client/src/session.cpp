// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp/session.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace wsprrypi::wtp {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
bool mutation(Operation op) {
    return op == Operation::Claim || op == Operation::Renew || op == Operation::Release ||
           op == Operation::Load || op == Operation::Arm || op == Operation::Abort;
}
bool terminal(State s) {
    return s == State::Complete || s == State::Aborted || s == State::Missed || s == State::Failed;
}
void increment(std::uint64_t &n) {
    if (n != maximum)
        ++n;
}
bool valid_id(std::string_view s) {
    return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
bool valid_io(IoResult result, std::size_t limit) {
    return result.state == IoState::Progress
               ? result.count > 0 && result.count <= limit
               : result.count == 0 &&
                     (result.state == IoState::WouldBlock || result.state == IoState::Closed ||
                      result.state == IoState::Failed);
}
} // namespace
Session::Session(SessionOptions options) : options_(std::move(options)) {
    Request hello{options_.session_id, std::string(32, '0'), Operation::Hello,
                  HelloRequest{{"WTP/1"}, options_.client_name, options_.client_version}};
    if (!valid_id(options_.owner_id) ||
        !(valid_id(options_.expected_device_id) ||
          (options_.learn_device_identity && options_.expected_device_id.empty())) ||
        !encode_request(hello) || !options_.transaction_timeout_ms || !options_.idle_timeout_ms)
        throw std::invalid_argument("Invalid WTP session options");
}
bool Session::observe_time(std::uint64_t now) {
    if (now < observed_ms_) {
        lose_stream("Monotonic clock regressed", SessionPhase::Fault);
        return false;
    }
    observed_ms_ = now;
    return true;
}
bool Session::connect(ByteStream &stream, std::uint64_t now) {
    if (!observe_time(now) || stream_ || phase_ == SessionPhase::IdentityChanged ||
        phase_ == SessionPhase::Fault)
        return false;
    stream_ = &stream;
    connect_started_ms_ = now;
    parser_ = FrameParser{};
    writer_ = FrameWriter{};
    invalidate_status();
    phase_ = SessionPhase::Hello;
    return begin(Operation::Hello,
                 HelloRequest{{"WTP/1"}, options_.client_name, options_.client_version},
                 Internal::Hello, now);
}
void Session::invalidate_status() {
    needs_status_ = true;
    if (evidence_) {
        evidence_->authoritative = false;
        evidence_->output_active.reset();
        evidence_->device_output_active.reset();
    }
}
void Session::remember_uncertain(Transaction transaction) {
    if (transaction.request.op == Operation::Claim)
        claim_attempted_ = true;
    if (auto job = std::get_if<Job>(&transaction.request.body)) {
        tracked_job_ = *job;
        load_ack_.reset();
    }
    if (tracked_job_ && identity_) {
        evidence_ = JobEvidence{
            identity_->device_id, identity_->boot_id, tracked_job_->job_id, {}, {}, false};
    }
    // ABORT may replace an earlier unknown LOAD/ARM: its unresolved result still
    // blocks all new work, and the complete immutable job stays retained.
    uncertain_ = std::move(transaction);
}
void Session::lose_stream(std::string reason, SessionPhase phase) {
    diagnostic_ = std::move(reason);
    if (stream_)
        stream_->close();
    stream_ = nullptr;
    connect_started_ms_.reset();
    writer_.cancel();
    parser_.end_of_stream();
    invalidate_status();
    owns_ = false;
    phase_ = phase;
    if (pending_) {
        auto transaction = std::move(*pending_);
        pending_.reset();
        if (transaction.internal == Internal::None) {
            const bool unknown = transaction.may_have_written;
            result_ = TransactionResult{transaction.request.op,
                                        unknown ? ResultKind::Unknown : ResultKind::NotSent,
                                        {},
                                        diagnostic_};
            if (unknown && mutation(transaction.request.op))
                remember_uncertain(std::move(transaction));
        }
    }
}
void Session::disconnect() { lose_stream("Stream closed locally; remote output is not confirmed"); }
bool Session::lease_valid(std::uint64_t now) const noexcept {
    return owns_ && lease_started_ms_ && now >= observed_ms_ && now >= *lease_started_ms_ &&
           now - *lease_started_ms_ < lease_duration_ms_;
}
bool Session::renewal_due(std::uint64_t now) const noexcept {
    return lease_valid(now) && now - *lease_started_ms_ >= lease_duration_ms_ / 2;
}
bool Session::begin(Operation op, RequestBody body, Internal internal, std::uint64_t now) {
    if (!stream_ || pending_)
        return false;
    if (request_sequence_ == maximum) {
        lose_stream("Request identity space exhausted", SessionPhase::Fault);
        return false;
    }
    auto sequence = ++request_sequence_;
    std::string request_id(32, '0');
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 32; sequence; sequence >>= 4)
        request_id[--i] = digits[sequence & 15];
    Request request{options_.session_id, std::move(request_id), op, std::move(body)};
    auto packet = RequestPacket::create(request);
    if (!packet) {
        diagnostic_ = "Request violates WTP wire contract";
        return false;
    }
    if (internal == Internal::None && !admit(request, now))
        return false;
    if (!writer_.start(packet->payload(), now, options_.transaction_timeout_ms,
                       options_.idle_timeout_ms))
        return false;
    pending_.emplace(
        Transaction{std::move(request), std::move(*packet), now,
                    ProgressBudget(now, options_.transaction_timeout_ms, options_.idle_timeout_ms),
                    internal, observation_revision_});
    if (mutation(op))
        invalidate_status();
    return true;
}
bool Session::request(Operation op, RequestBody body, std::uint64_t now) {
    if (!observe_time(now) || phase_ != SessionPhase::Ready || pending_ || result_ ||
        recovery_result_ || op == Operation::Hello)
        return false;
    return begin(op, std::move(body), Internal::None, now);
}
bool Session::admit(const Request &r, std::uint64_t now) {
    if (!mutation(r.op))
        return true;
    diagnostic_ = "Mutation requires reconciled ownership, state and lease";
    if ((safety_fault_ && r.op != Operation::Abort) || needs_status_ || !status_ || !caps_ ||
        !identity_)
        return false;
    const auto &s = *status_;
    if (uncertain_ && r.op != Operation::Abort)
        return false;
    if (r.op == Operation::Claim) {
        return !s.owner_id && !s.output_active && s.state != State::Armed &&
               s.state != State::Running &&
               std::get<LeaseRequest>(r.body).owner_id == options_.owner_id;
    }
    if (!owns_ || s.owner_id != options_.owner_id)
        return false;
    if (r.op == Operation::Renew) {
        // A STATUS-resolved lost CLAIM has no acknowledged lease duration.
        // Only RENEW may reestablish it; the server remains the expiry authority.
        return (!lease_started_ms_ || lease_valid(now)) &&
               std::get<LeaseRequest>(r.body).owner_id == options_.owner_id;
    }
    if (r.op == Operation::Release)
        return !s.output_active && s.state != State::Armed && s.state != State::Running &&
               s.state != State::Failed;
    if (r.op == Operation::Abort)
        return tracked_job_ && s.job_id == tracked_job_->job_id &&
               std::get<AbortRequest>(r.body).job_id == tracked_job_->job_id &&
               (s.state == State::Loaded || s.state == State::Armed || s.state == State::Running ||
                s.state == State::Aborted);
    if (!lease_valid(now) || s.output_active)
        return false;
    if (r.op == Operation::Arm)
        return tracked_job_ && load_ack_ && s.job_id == tracked_job_->job_id &&
               s.state == State::Loaded &&
               std::get<ArmRequest>(r.body).job_id == tracked_job_->job_id;
    const auto &j = std::get<Job>(r.body);
    if (s.state != State::Empty && !terminal(s.state))
        return false;
    if (tracked_job_ && j.job_id == tracked_job_->job_id)
        return false;
    if (j.events.size() > static_cast<std::size_t>(caps_->max_events) ||
        j.total_duration_ns > caps_->max_job_duration_ns ||
        std::find(caps_->modes.begin(), caps_->modes.end(), j.mode) == caps_->modes.end())
        return false;
    return std::all_of(j.events.begin(), j.events.end(), [&](const RfEvent &e) {
        return !e.frequency_nhz ||
               std::any_of(caps_->frequency_ranges.begin(), caps_->frequency_ranges.end(),
                           [&](const FrequencyRange &range) {
                               return *e.frequency_nhz >= range.minimum_nhz &&
                                      *e.frequency_nhz <= range.maximum_nhz;
                           });
    });
}
void Session::poll(std::uint64_t now) {
    if (!observe_time(now) || !stream_)
        return;
    if (connect_started_ms_ && now - *connect_started_ms_ >= options_.transaction_timeout_ms) {
        lose_stream("Negotiation deadline expired");
        return;
    }
    if (pending_ && pending_->budget.expired(now)) {
        lose_stream("Transaction deadline or progress timeout expired");
        return;
    }
    if (!parser_.check_timeout(now).empty()) {
        lose_stream("Partial frame timed out");
        return;
    }
    try {
        if (pending_ && writer_.state() == WriteState::Pending) {
            auto bytes = writer_.remaining(now);
            bytes = bytes.first(std::min(bytes.size(), kInputChunkBytes));
            if (bytes.empty()) {
                lose_stream("Write deadline expired");
                return;
            }
            // An adapter exception has unknown effects. WouldBlock clears this
            // pessimism only when no earlier bytes have been accepted.
            const bool earlier = pending_->may_have_written;
            pending_->may_have_written = true;
            const auto io = stream_->write(bytes);
            if (!valid_io(io, bytes.size())) {
                lose_stream("Invalid transport write count", SessionPhase::Fault);
                return;
            }
            if (io.state == IoState::WouldBlock)
                pending_->may_have_written = earlier;
            else if (io.state != IoState::Progress) {
                lose_stream("Transport write failed or closed");
                return;
            } else {
                writer_.consume(io.count, now);
                pending_->budget.progress(now);
            }
        }
        std::array<std::uint8_t, kInputChunkBytes> buffer{};
        const auto io = stream_->read(buffer);
        if (!valid_io(io, buffer.size())) {
            lose_stream("Invalid transport read count", SessionPhase::Fault);
            return;
        }
        if (io.state == IoState::Closed || io.state == IoState::Failed) {
            lose_stream("Transport read failed or closed");
            return;
        }
        if (io.state == IoState::Progress) {
            if (pending_)
                pending_->budget.progress(now);
            auto read = parser_.feed(std::span(buffer).first(io.count), now);
            for (const auto &frame : read.events) {
                if (!stream_)
                    break;
                if (frame.kind == FrameEventKind::Closed) {
                    lose_stream("Framing limits exceeded");
                    break;
                }
                if (frame.kind == FrameEventKind::InvalidFrame) {
                    if (observation_revision_ == maximum) {
                        lose_stream("Observation revision exhausted", SessionPhase::Fault);
                        break;
                    }
                    ++observation_revision_;
                    invalidate_status();
                    continue;
                }
                const auto decoded = decode(
                    {reinterpret_cast<const char *>(frame.payload.data()), frame.payload.size()});
                if (!decoded) {
                    lose_stream("Invalid peer message", SessionPhase::Fault);
                    break;
                }
                receive(*decoded.message, now);
            }
        }
        if (!stream_ || pending_)
            return;
        if (phase_ == SessionPhase::Ready && !needs_status_)
            connect_started_ms_.reset();
        if (phase_ == SessionPhase::Status || (phase_ == SessionPhase::Ready && needs_status_))
            begin(Operation::Status, Empty{}, Internal::Status, now);
        else if (phase_ == SessionPhase::Caps)
            begin(Operation::Caps, Empty{}, Internal::Caps, now);
    } catch (const std::exception &) {
        lose_stream("Transport or message processing exception", SessionPhase::Fault);
    }
}
void Session::receive(const Message &message, std::uint64_t now) {
    if (auto r = std::get_if<Response>(&message))
        response(*r, now);
    else if (auto e = std::get_if<Event>(&message))
        event(*e);
    else
        lose_stream("Peer sent a request", SessionPhase::Fault);
}
void Session::event(const Event &e) {
    if (e.session_id != options_.session_id) {
        increment(stale_messages_);
        return;
    }
    if (!identity_ || e.boot_id != identity_->boot_id) {
        lose_stream("Event boot identity changed", SessionPhase::IdentityChanged);
        return;
    }
    if (last_event_id_ && e.event_id <= *last_event_id_) {
        increment(stale_messages_);
        return;
    }
    if ((!last_event_id_ && e.event_id != 0) ||
        (last_event_id_ && e.event_id - *last_event_id_ != 1))
        increment(event_gaps_);
    last_event_id_ = e.event_id;
    last_event_ = e;
    if (observation_revision_ == maximum) {
        lose_stream("Observation revision exhausted", SessionPhase::Fault);
        return;
    }
    ++observation_revision_;
    invalidate_status();
    if (e.event == EventKind::DeviceFault)
        safety_fault_ = true;
    if (const auto job = std::get_if<JobStateEvent>(&e.body)) {
        if (job->state == State::Failed || (terminal(job->state) && job->output_active) ||
            (job->error && (job->error->code == ErrorCode::OutputStateUnknown ||
                            job->error->code == ErrorCode::DeviceFault)))
            safety_fault_ = true;
    }
    if (e.event == EventKind::SessionReplaced)
        lose_stream("Session replaced by another stream");
}
bool Session::validate_arm(const ArmResponse &a, const ArmRequest &r) const {
    if (!caps_ || !tracked_job_ || a.job_id != r.job_id || a.start_utc_ns != r.start_utc_ns)
        return false;
    const auto &c = a.clock;
    if (c.state == ClockState::Unsynchronized || c.leap == Leap::Unknown ||
        (c.state == ClockState::Holdover &&
         (!caps_->maximum_holdover_age_ns || c.sync_age_ns > caps_->maximum_holdover_age_ns)) ||
        c.uncertainty_ns > r.max_start_uncertainty_ns ||
        c.uncertainty_ns > caps_->maximum_arm_uncertainty_ns || r.start_utc_ns < c.utc_now_ns)
        return false;
    const auto ahead = r.start_utc_ns - c.utc_now_ns;
    if (ahead < caps_->minimum_arm_lead_ns || ahead > caps_->maximum_arm_ahead_ns ||
        ahead > maximum - c.monotonic_now_ns ||
        a.start_monotonic_ns != c.monotonic_now_ns + ahead ||
        tracked_job_->total_duration_ns > maximum - r.start_utc_ns ||
        tracked_job_->total_duration_ns > maximum - a.start_monotonic_ns)
        return false;
    if (c.leap_transition_utc_ns) {
        const auto t = *c.leap_transition_utc_ns;
        const auto lower = t > 1'000'000'000 ? t - 1'000'000'000 : 0;
        const auto upper = t > maximum - 1'000'000'000 ? maximum : t + 1'000'000'000;
        if (r.start_utc_ns <= upper && r.start_utc_ns + tracked_job_->total_duration_ns >= lower)
            return false;
    }
    return true;
}
bool Session::validate_response(const Transaction &t, const Response &r) const {
    if (!r.ok())
        return true;
    if (const auto s = std::get_if<Status>(&r.body)) {
        // A record retains historical output evidence; current output may have
        // changed since a failure. State/identity contradictions are different.
        for (std::size_t i = 0; i < s->terminal_records.size(); ++i) {
            const auto &record = s->terminal_records[i];
            for (std::size_t j = 0; j < i; ++j)
                if (record.job_id == s->terminal_records[j].job_id)
                    return false;
            if (s->job_id == record.job_id && s->state != record.state)
                return false;
        }
    }
    if (auto lease = std::get_if<LeaseResponse>(&r.body))
        return lease->owner_id == options_.owner_id &&
               lease->expires_monotonic_ns >=
                   static_cast<std::uint64_t>(lease->granted_lease_ms) * 1'000'000;
    if (auto load = std::get_if<LoadResponse>(&r.body)) {
        const auto &j = std::get<Job>(t.request.body);
        if (load->job_id != j.job_id ||
            (!j.allow_frequency_adjustment.value_or(false) && !load->adjustments.empty()))
            return false;
        std::array<bool, 512> seen{};
        for (const auto &a : load->adjustments) {
            const auto index = static_cast<std::size_t>(a.event_index);
            if (index >= j.events.size() || seen[index] ||
                j.events[index].frequency_nhz != a.requested_frequency_nhz)
                return false;
            seen[index] = true;
        }
    }
    if (auto arm = std::get_if<ArmResponse>(&r.body))
        return validate_arm(*arm, std::get<ArmRequest>(t.request.body));
    if (auto abort = std::get_if<AbortResponse>(&r.body))
        return abort->job_id == std::get<AbortRequest>(t.request.body).job_id;
    if (auto ping = std::get_if<Ping>(&r.body))
        return ping->token == std::get<Ping>(t.request.body).token;
    return true;
}
void Session::response(const Response &r, std::uint64_t now) {
    if (!pending_ || r.session_id != options_.session_id ||
        r.request_id != pending_->request.request_id) {
        increment(stale_messages_);
        return;
    }
    if (r.op != operation_name(pending_->request.op) || writer_.state() != WriteState::Complete ||
        !validate_response(*pending_, r)) {
        lose_stream("Response does not match the transaction", SessionPhase::Fault);
        return;
    }
    const auto internal = pending_->internal;
    if (r.ok()) {
        // Validate boot identity while the pending transaction is still retained.
        if (auto hello = std::get_if<HelloResponse>(&r.body)) {
            if (options_.learn_device_identity && options_.expected_device_id.empty())
                options_.expected_device_id = hello->device_id;
            if (hello->device_id != options_.expected_device_id ||
                (identity_ && hello->boot_id != identity_->boot_id)) {
                lose_stream("Device or boot identity changed", SessionPhase::IdentityChanged);
                return;
            }
        }
        if (auto status = std::get_if<Status>(&r.body)) {
            if (!identity_ || status->boot_id != identity_->boot_id) {
                lose_stream("STATUS boot identity changed", SessionPhase::IdentityChanged);
                return;
            }
        }
        // Retain the original request until state and result publication both
        // finish, so an exception still leaves an uncertain mutation to retain.
        const auto &transaction = *pending_;
        accept(transaction, r, now);
        if (internal == Internal::None)
            result_ = TransactionResult{transaction.request.op, ResultKind::Acknowledged, r,
                                        "Matching acknowledgment; execution status is separate"};
        pending_.reset();
    } else {
        const auto e = std::get<Error>(r.body);
        if (e.code == ErrorCode::OutputStateUnknown || e.code == ErrorCode::DeviceFault)
            safety_fault_ = true;
        if (e.code == ErrorCode::NotOwner || e.code == ErrorCode::LeaseExpired)
            owns_ = false;
        if (e.code == ErrorCode::NotOwner) {
            claim_attempted_ = false;
            lease_started_ms_.reset();
        }
        auto transaction = std::move(*pending_);
        pending_.reset();
        if (transaction.request.op == Operation::Claim && !transaction.retry)
            claim_attempted_ = false;
        if (internal == Internal::None) {
            result_ =
                TransactionResult{transaction.request.op, ResultKind::Rejected, r,
                                  "Request rejected; an earlier unknown outcome remains separate"};
            invalidate_status();
        } else {
            lose_stream("Negotiation or authoritative observation rejected", SessionPhase::Fault);
        }
        if (e.code == ErrorCode::SessionReplaced || e.code == ErrorCode::RequestIdReuse ||
            e.code == ErrorCode::HelloRequired)
            lose_stream("Session no longer valid", SessionPhase::Fault);
    }
}
void Session::accept(const Transaction &t, const Response &r, std::uint64_t now) {
    switch (t.request.op) {
    case Operation::Hello:
        identity_ = std::get<HelloResponse>(r.body);
        phase_ = SessionPhase::Status;
        break;
    case Operation::Status:
        if (t.observation_revision != observation_revision_) {
            // The snapshot may predate an event observed after this request
            // started. Only a subsequently issued STATUS can reconcile it.
            invalidate_status();
            break;
        }
        accept_status(std::get<Status>(r.body));
        reconcile();
        if (phase_ == SessionPhase::Status)
            phase_ = SessionPhase::Caps;
        break;
    case Operation::Caps:
        caps_ = std::get<Capabilities>(r.body);
        if (phase_ == SessionPhase::Caps)
            phase_ = SessionPhase::Ready;
        break;
    case Operation::Claim:
    case Operation::Renew: {
        const auto &lease = std::get<LeaseResponse>(r.body);
        claim_attempted_ = true;
        owns_ = true;
        lease_started_ms_ = t.started_ms;
        lease_duration_ms_ = static_cast<std::uint64_t>(lease.granted_lease_ms);
        invalidate_status();
        break;
    }
    case Operation::Load:
        tracked_job_ = std::get<Job>(t.request.body);
        load_ack_ = std::get<LoadResponse>(r.body);
        evidence_ = JobEvidence{identity_->device_id,
                                identity_->boot_id,
                                tracked_job_->job_id,
                                State::Loaded,
                                {},
                                false};
        invalidate_status();
        if (t.retry)
            uncertain_.reset();
        break;
    case Operation::Arm:
        if (evidence_)
            evidence_->state = State::Armed;
        invalidate_status();
        if (t.retry)
            uncertain_.reset();
        break;
    case Operation::Abort:
        uncertain_.reset();
        invalidate_status();
        // Correlated ABORT provides explicit inactive output, but follow-up
        // STATUS is still required before admitting another mutation.
        evidence_ = JobEvidence{identity_->device_id,
                                identity_->boot_id,
                                std::get<AbortRequest>(t.request.body).job_id,
                                State::Aborted,
                                false,
                                true,
                                false};
        break;
    case Operation::Release:
        owns_ = false;
        claim_attempted_ = false;
        lease_started_ms_.reset();
        load_ack_.reset();
        invalidate_status();
        break;
    default:
        break;
    }
    (void)now;
}
void Session::accept_status(const Status &s) {
    status_ = s;
    needs_status_ = false;
    owns_ = claim_attempted_ && s.owner_id == options_.owner_id;
    if (!owns_) {
        lease_started_ms_.reset();
        claim_attempted_ = false;
    }
    if (s.state == State::Failed || (s.output_active && s.state != State::Running))
        safety_fault_ = true;
    if (!tracked_job_ || !identity_)
        return;
    evidence_ =
        JobEvidence{identity_->device_id, identity_->boot_id, tracked_job_->job_id, {}, {}, false};
    evidence_->device_output_active = s.output_active;
    if (s.job_id == tracked_job_->job_id) {
        evidence_->state = s.state;
        evidence_->output_active = s.output_active;
        evidence_->authoritative = true;
    } else {
        for (const auto &record : s.terminal_records)
            if (record.job_id == tracked_job_->job_id) {
                evidence_->state = record.state;
                evidence_->output_active = record.output_active;
                evidence_->authoritative = true;
            }
    }
    if (evidence_->state == State::Failed ||
        (evidence_->state && terminal(*evidence_->state) && evidence_->output_active == true))
        safety_fault_ = true;
}
void Session::resolve(std::string explanation) {
    recovery_result_ = TransactionResult{
        uncertain_->request.op, ResultKind::Reconciled, {}, std::move(explanation)};
    uncertain_.reset();
}
void Session::reconcile() {
    if (!uncertain_ || needs_status_ || !status_)
        return;
    const auto op = uncertain_->request.op;
    if (op == Operation::Claim || op == Operation::Renew || op == Operation::Release) {
        if ((op == Operation::Claim || op == Operation::Renew) && owns_)
            lease_started_ms_.reset();
        resolve("STATUS establishes current ownership only; no historical lease grant or release "
                "acknowledgment is inferred");
        return;
    }
    if (!evidence_ || !evidence_->authoritative || !evidence_->state)
        return;
    const auto state = *evidence_->state;
    if (terminal(state)) {
        if (state == State::Failed || evidence_->output_active != false)
            safety_fault_ = true;
        resolve("STATUS establishes the retained terminal state and explicit output evidence");
    } else if (op == Operation::Arm && (state == State::Armed || state == State::Running)) {
        resolve("STATUS establishes that this job is armed/running; original ARM mapping is not "
                "reconstructed");
    }
}
bool Session::retry_uncertain(std::uint64_t now) {
    if (!observe_time(now) || !uncertain_ || pending_ || result_ || recovery_result_ ||
        phase_ != SessionPhase::Ready || needs_status_ || safety_fault_ || !owns_ || !status_ ||
        !tracked_job_ || status_->job_id != tracked_job_->job_id)
        return false;
    auto &t = *uncertain_;
    if (now < t.started_ms || now - t.started_ms >= options_.transaction_timeout_ms)
        return false;
    const auto op = t.request.op;
    if (op == Operation::Load) {
        if (status_->state != State::Loaded || !lease_valid(now) || status_->output_active)
            return false;
    } else if (op == Operation::Arm) {
        if (status_->state != State::Loaded || !lease_valid(now) || status_->output_active)
            return false;
    } else if (op == Operation::Abort) {
        if (status_->state != State::Loaded && status_->state != State::Armed &&
            status_->state != State::Running && status_->state != State::Aborted)
            return false;
    } else
        return false;
    const auto remaining = options_.transaction_timeout_ms - (now - t.started_ms);
    if (!writer_.start(t.packet.payload(), now, remaining, options_.idle_timeout_ms))
        return false;
    pending_ = t;
    pending_->budget = ProgressBudget(now, remaining, options_.idle_timeout_ms);
    pending_->internal = Internal::None;
    pending_->retry = true;
    pending_->may_have_written = false;
    invalidate_status();
    return true;
}
std::optional<TransactionResult> Session::take_result() {
    auto &slot = result_ ? result_ : recovery_result_;
    auto result = std::move(slot);
    slot.reset();
    return result;
}
} // namespace wsprrypi::wtp
