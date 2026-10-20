#include "wtp_server/job_service.hpp"

#include "wtp_server/memory_budget.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace wsprrypico::wtp {
namespace {

constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000ULL;
constexpr std::uint64_t kLeapExclusionNs = 1'000'000'000ULL;
constexpr std::uint64_t kMaximumOutputDisableTimeoutNs = 5'000'000'000ULL;
constexpr std::uint64_t kMaximumArmAheadNs = 604'800'000'000'000ULL;
constexpr std::size_t kMaximumSessions = 16;

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

bool contains(const std::vector<std::string>& values, std::string_view expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

bool unique(const std::vector<std::string>& values) {
    for (auto current = values.begin(); current != values.end(); ++current) {
        if (std::find(values.begin(), current, *current) != current) {
            return false;
        }
    }
    return true;
}

bool capability_text(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '-';
    });
}

bool valid_protocol_version(std::string_view version) {
    if (!version.starts_with("WTP/") || version.size() < 5 || version[4] == '0') {
        return false;
    }
    return std::all_of(version.begin() + 4, version.end(),
                       [](char character) { return character >= '0' && character <= '9'; });
}

} // namespace

// Hash typed job values, independent of JSON member ordering/escaping. Retain
// this compact identity instead of keeping eight complete 512-event jobs.
void JobDigestBuilder::number(std::uint64_t n) {
    std::array<std::uint8_t, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i)
        bytes[i] = static_cast<std::uint8_t>(n >> (i * 8));
    digest_.update(bytes);
}
void JobDigestBuilder::text(std::string_view value) {
    number(value.size());
    digest_.update({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}
JobDigestBuilder::JobDigestBuilder(const Job& job, std::size_t count) {
    text(job.job_id);
    text(job.profile);
    text(job.mode);
    number(job.total_duration_ns);
    number(job.allow_frequency_adjustment);
    number(count);
}
void JobDigestBuilder::append(const RfEvent& event) {
    number(event.offset_ns);
    number(event.duration_ns);
    number(event.rf_on);
    number(event.frequency_nhz.has_value());
    number(event.frequency_nhz.value_or(0));
}
PayloadDigest job_digest(const Job& job) {
    JobDigestBuilder digest(job, job.events.size());
    for (const auto& event : job.events)
        digest.append(event);
    return digest.finish();
}

JobService::JobService(Clock& clock, RfEngine& engine, IdentitySource& identities,
                       ServiceConfig config)
    : clock_(clock), engine_(engine), identities_(identities), config_(config),
      boot_id_(identities_.new_boot_id()) {
    const bool configuration_valid =
        capability_text(config_.capability_engine) && !config_.supported_modes.empty() &&
        unique(config_.supported_modes) &&
        std::all_of(config_.supported_modes.begin(), config_.supported_modes.end(),
                    capability_text) &&
        config_.minimum_frequency_nhz > 0 &&
        config_.minimum_frequency_nhz <= config_.maximum_frequency_nhz && config_.max_events > 0 &&
        config_.max_events <= 512 && config_.max_job_duration_ns > 0 &&
        config_.max_job_duration_ns <= 86'400'000'000'000ULL && config_.maximum_arm_ahead_ns > 0 &&
        config_.maximum_arm_ahead_ns <= kMaximumArmAheadNs &&
        config_.minimum_arm_lead_ns <= config_.maximum_arm_ahead_ns &&
        config_.output_disable_timeout_ns > 0 &&
        config_.output_disable_timeout_ns <= kMaximumOutputDisableTimeoutNs &&
        config_.response_cache_entries >= 8 &&
        config_.response_cache_ttl_ns >= 300'000'000'000ULL &&
        config_.terminal_record_entries >= 8 &&
        config_.terminal_record_ttl_ns >= 3'600'000'000'000ULL;
    const auto now = clock_.snapshot().monotonic_now_ns;
    const auto shutdown_timeout =
        config_.output_disable_timeout_ns > 0 &&
                config_.output_disable_timeout_ns <= kMaximumOutputDisableTimeoutNs
            ? config_.output_disable_timeout_ns
            : ServiceConfig{}.output_disable_timeout_ns;
    ready_ = engine_.disable(saturating_add(now, shutdown_timeout)) && !engine_.output_active() &&
             configuration_valid && valid_id(boot_id_);
    if (!ready_) {
        state_ = State::Failed;
    }
}

Response JobService::handle(const Request& request) {
    Request copy = request;
    return handle_owned(copy, {});
}

Response JobService::handle(Request&& request) {
    return handle_owned(request, {});
}

Response JobService::handle(Request&& request, const Admission& admission) {
    return handle_owned(request, admission);
}

Response JobService::handle_owned(Request& request, const Admission& admission) {
    const auto now = clock_.snapshot();
    expire_resources(now.monotonic_now_ns);
    prune_replay(now.monotonic_now_ns);
    prune_terminals(now.monotonic_now_ns);
    prune_sessions(now.monotonic_now_ns);

    if (!ready_) {
        return reject(ErrorCode::DeviceFault);
    }

    if (!valid_id(request.session_id) || !valid_id(request.request_id) ||
        std::all_of(request.payload_digest.begin(), request.payload_digest.end(),
                    [](std::uint8_t byte) { return byte == 0; })) {
        return reject(ErrorCode::InvalidMessage);
    }
    if (request.principal.empty()) {
        return reject(ErrorCode::AuthenticationRequired);
    }
    if (request.protocol != "WTP/1") {
        auto response = reject(ErrorCode::UnsupportedVersion);
        response.close_connection = true;
        return response;
    }

    auto session = std::find_if(sessions_.begin(), sessions_.end(), [&](const Session& candidate) {
        return candidate.session_id == request.session_id;
    });
    if (request.operation == "HELLO") {
        const auto* body = std::get_if<HelloBody>(&request.body);
        if (body == nullptr || body->versions.empty() || body->versions.size() > 16 ||
            !std::all_of(body->versions.begin(), body->versions.end(), valid_protocol_version) ||
            !unique(body->versions)) {
            return reject(ErrorCode::InvalidMessage);
        }
        if (!contains(body->versions, "WTP/1")) {
            auto response = reject(ErrorCode::UnsupportedVersion);
            response.close_connection = true;
            return response;
        }
        if (session != sessions_.end() && session->principal != request.principal) {
            return reject(ErrorCode::AuthenticationRequired);
        }
        if (session == sessions_.end()) {
            if (!request.body_valid)
                return reject(ErrorCode::InvalidMessage);
            if (sessions_.size() >= kMaximumSessions) {
                return reject(ErrorCode::Busy);
            }
            sessions_.push_back({request.session_id, request.principal, now.monotonic_now_ns});
        } else {
            session->last_seen_monotonic_ns = now.monotonic_now_ns;
        }
    } else {
        if (session == sessions_.end()) {
            return reject(ErrorCode::HelloRequired);
        }
        if (session->principal != request.principal) {
            return reject(ErrorCode::AuthenticationRequired);
        }
        session->last_seen_monotonic_ns = now.monotonic_now_ns;
    }

    bool replay_conflict = false;
    if (auto cached = replay(request, now.monotonic_now_ns, replay_conflict)) {
        return *cached;
    }
    if (replay_conflict) {
        auto response = reject(ErrorCode::RequestIdReuse);
        response.close_connection = true;
        return response;
    }

    auto response = dispatch(request, admission);
    remember(request, response, now.monotonic_now_ns);
    return response;
}

Response JobService::dispatch(Request& request, const Admission& admission) {
    const auto now = clock_.snapshot();
    constexpr std::array<std::string_view, 11> operations{"HELLO",   "CAPS",      "CLAIM", "RENEW",
                                                          "RELEASE", "LOAD",      "ARM",   "ABORT",
                                                          "STATUS",  "GET_CLOCK", "PING"};
    if (std::find(operations.begin(), operations.end(), request.operation) == operations.end())
        return reject(ErrorCode::UnknownOperation);
    if (!request.body_valid)
        return reject(ErrorCode::InvalidMessage);
    if (request.operation == "HELLO" || request.operation == "CAPS" ||
        request.operation == "STATUS" || request.operation == "GET_CLOCK" ||
        request.operation == "PING") {
        const auto* ping = std::get_if<PingBody>(&request.body);
        const bool empty_body = std::holds_alternative<std::monostate>(request.body);
        const bool valid_ping =
            request.operation == "PING" &&
            (empty_body || (ping != nullptr && (!ping->token || ping->token->size() <= 64)));
        if (request.operation != "HELLO" && request.operation != "PING" && !empty_body) {
            return reject(ErrorCode::InvalidMessage);
        }
        if (request.operation == "PING" && !valid_ping) {
            return reject(ErrorCode::InvalidMessage);
        }
        auto response = success();
        response.boot_id = boot_id_;
        if (request.operation == "STATUS")
            response.status_snapshot = status();
        if (request.operation == "GET_CLOCK")
            response.clock_snapshot = now;
        if (ping != nullptr) {
            response.ping_token = ping->token;
        }
        return response;
    }
    if (request.operation == "CLAIM") {
        const auto* body = std::get_if<ClaimBody>(&request.body);
        if (body == nullptr || !valid_id(body->owner_id) || body->lease_ms < 5000 ||
            body->lease_ms > 60000) {
            return reject(ErrorCode::InvalidMessage);
        }
        if (admission && !admission(request))
            return reject(ErrorCode::Busy);
        if (owner_) {
            return reject(ErrorCode::Busy);
        }
        owner_ =
            Owner{body->owner_id, request.session_id, request.principal,
                  saturating_add(now.monotonic_now_ns, static_cast<std::uint64_t>(body->lease_ms) *
                                                           kNanosecondsPerMillisecond),
                  false};
        auto response = success();
        response.owner_id = owner_->owner_id;
        response.granted_lease_ms = body->lease_ms;
        response.expires_monotonic_ns = owner_->expires_monotonic_ns;
        return response;
    }

    constexpr std::array<std::string_view, 5> owner_operations{"RENEW", "RELEASE", "LOAD", "ARM",
                                                               "ABORT"};
    if (std::find(owner_operations.begin(), owner_operations.end(), request.operation) ==
        owner_operations.end()) {
        return reject(ErrorCode::UnknownOperation);
    }
    if ((request.operation == "RENEW" && std::get_if<RenewBody>(&request.body) == nullptr) ||
        (request.operation == "RELEASE" && !std::holds_alternative<std::monostate>(request.body)) ||
        (request.operation == "LOAD" && std::get_if<Job>(&request.body) == nullptr &&
         std::get_if<LoadReplayBody>(&request.body) == nullptr) ||
        (request.operation == "ARM" && std::get_if<ArmBody>(&request.body) == nullptr) ||
        (request.operation == "ABORT" && std::get_if<AbortBody>(&request.body) == nullptr)) {
        return reject(ErrorCode::InvalidMessage);
    }
    if (!owner_) {
        return reject(ErrorCode::LeaseExpired);
    }
    if (!owns(request)) {
        return reject(ErrorCode::NotOwner);
    }
    if ((request.operation == "LOAD" || request.operation == "ARM") &&
        admission && !admission(request)) {
        return reject(ErrorCode::Busy);
    }

    if (request.operation == "RENEW") {
        const auto* body = std::get_if<RenewBody>(&request.body);
        if (body == nullptr || !valid_id(body->owner_id) || body->lease_ms < 5000 ||
            body->lease_ms > 60000) {
            return reject(ErrorCode::InvalidMessage);
        }
        if (body->owner_id != owner_->owner_id) {
            return reject(ErrorCode::NotOwner);
        }
        if (owner_->release_after_terminal) {
            return reject(ErrorCode::LeaseExpired);
        }
        owner_->expires_monotonic_ns =
            saturating_add(now.monotonic_now_ns,
                           static_cast<std::uint64_t>(body->lease_ms) * kNanosecondsPerMillisecond);
        auto response = success();
        response.owner_id = owner_->owner_id;
        response.granted_lease_ms = body->lease_ms;
        response.expires_monotonic_ns = owner_->expires_monotonic_ns;
        return response;
    }
    if (request.operation == "RELEASE") {
        if (!std::holds_alternative<std::monostate>(request.body)) {
            return reject(ErrorCode::InvalidMessage);
        }
        if (state_ == State::Armed || state_ == State::Running || state_ == State::Failed) {
            return reject(ErrorCode::InvalidState);
        }
        clear_job();
        owner_.reset();
        return success();
    }
    if (request.operation == "LOAD") {
        if (const auto* replay = std::get_if<LoadReplayBody>(&request.body)) {
            if (engine_.output_active() || state_ == State::Failed)
                return reject(ErrorCode::InvalidState);
            const auto retained = std::find_if(
                retained_jobs_.begin(), retained_jobs_.end(),
                [&](const RetainedJob& item) { return item.job_id == replay->job_id; });
            if (retained != retained_jobs_.end()) {
                if (retained->digest != replay->digest)
                    return reject(ErrorCode::JobIdConflict);
                auto response = retained->load_response;
                touch_terminal(replay->job_id);
                return response;
            }
            if (!job_ || job_->job_id != replay->job_id)
                return reject(ErrorCode::InvalidState);
            if (job_digest_ != replay->digest)
                return reject(ErrorCode::JobIdConflict);
            auto response = success();
            response.state = State::Loaded;
            response.job_id = replay->job_id;
            response.adjustments = adjustments_;
            return response;
        }
        auto* body = std::get_if<Job>(&request.body);
        if (body == nullptr) {
            return reject(ErrorCode::InvalidMessage);
        }
        if (engine_.output_active()) {
            return reject(ErrorCode::InvalidState);
        }
        if (state_ == State::Failed) {
            return reject(ErrorCode::InvalidState);
        }
        const auto retained =
            std::find_if(retained_jobs_.begin(), retained_jobs_.end(),
                         [&](const RetainedJob& item) { return item.job_id == body->job_id; });
        if (retained != retained_jobs_.end()) {
            if (retained->digest != job_digest(*body))
                return reject(ErrorCode::JobIdConflict);
            auto response = retained->load_response;
            touch_terminal(body->job_id);
            return response;
        }
        if (job_ && job_->job_id == body->job_id) {
            if (job_digest_ == job_digest(*body)) {
                auto response = success();
                response.state = State::Loaded;
                response.job_id = body->job_id;
                response.adjustments = adjustments_;
                return response;
            }
            return reject(ErrorCode::JobIdConflict);
        }
        if (state_ == State::Armed || state_ == State::Running) {
            return reject(ErrorCode::InvalidState);
        }
        if (const auto error = validate_job(*body); error != ErrorCode::None) {
            return reject(error);
        }
        // The production adapters transfer the already decoded event pages.
        // Reserve bounded preparation/adjustment work here; serialization has
        // its own paged gate and memory_admitted keeps the separate 32 KiB
        // authority/RF reserve.
        if (!memory_admitted(32768))
            return reject(ErrorCode::InternalError);
        const auto accepted_digest = job_digest(*body);
        Job accepted = std::move(*body);
        if (!accepted.events.valid())
            return reject(ErrorCode::InternalError);
        auto preparation = engine_.prepare(accepted);
        if (!preparation.accepted) {
            return reject(ErrorCode::FrequencyRejected);
        }
        if (!accepted.allow_frequency_adjustment && !preparation.adjustments.empty()) {
            return reject(ErrorCode::FrequencyRejected);
        }
        for (auto current = preparation.adjustments.begin();
             current != preparation.adjustments.end(); ++current) {
            const auto& adjustment = *current;
            const bool duplicate = std::any_of(
                preparation.adjustments.begin(), current, [&](const FrequencyAdjustment& prior) {
                    return prior.event_index == adjustment.event_index;
                });
            if (adjustment.event_index >= accepted.events.size() ||
                !accepted.events[adjustment.event_index].frequency_nhz ||
                *accepted.events[adjustment.event_index].frequency_nhz !=
                    adjustment.requested_frequency_nhz ||
                adjustment.realized_frequency_nhz == 0 || duplicate) {
                return reject(ErrorCode::DeviceFault);
            }
        }
        job_ = std::move(accepted);
        job_digest_ = accepted_digest;
        // Different jobs often have the same complete adjustment sequence.
        // Retain their identities/digests independently while sharing exactly
        // equal immutable values; eight histories must not multiply this list.
        const auto identical = std::find_if(
            retained_jobs_.begin(), retained_jobs_.end(), [&](const RetainedJob& retained) {
                const auto& values = retained.load_response.adjustments;
                return values.size() == preparation.adjustments.size() &&
                       (values.empty() ||
                        std::equal(values.begin(), values.end(), preparation.adjustments.begin()));
            });
        adjustments_ = identical == retained_jobs_.end()
                           ? AdjustmentList(std::move(preparation.adjustments))
                           : identical->load_response.adjustments;
        arm_.reset();
        state_ = State::Loaded;
        auto response = success();
        response.state = state_;
        response.job_id = job_->job_id;
        response.adjustments = adjustments_;
        return response;
    }
    if (request.operation == "ARM") {
        const auto* body = std::get_if<ArmBody>(&request.body);
        if (body == nullptr || !valid_id(body->job_id)) {
            return reject(ErrorCode::InvalidMessage);
        }
        const auto retained =
            std::find_if(retained_jobs_.begin(), retained_jobs_.end(),
                         [&](const RetainedJob& item) { return item.job_id == body->job_id; });
        if (retained != retained_jobs_.end()) {
            if (!retained->arm || retained->arm->start_utc_ns != body->start_utc_ns ||
                retained->arm->max_uncertainty_ns != body->max_start_uncertainty_ns)
                return reject(ErrorCode::InvalidState);
            auto response = retained->arm->response;
            touch_terminal(body->job_id);
            return response;
        }
        if (arm_ && arm_->job_id == body->job_id) {
            if (arm_->start_utc_ns == body->start_utc_ns &&
                arm_->max_uncertainty_ns == body->max_start_uncertainty_ns) {
                return arm_->response;
            }
            if (state_ == State::Armed || state_ == State::Running) {
                return reject(ErrorCode::ArmConflict);
            }
        }
        if (!job_ || job_->job_id != body->job_id) {
            return reject(ErrorCode::JobNotFound);
        }
        if (state_ != State::Loaded) {
            return reject(ErrorCode::InvalidState);
        }
        if (engine_.output_active()) {
            return reject(ErrorCode::InvalidState);
        }
        std::uint64_t start_monotonic_ns = 0;
        if (const auto error = validate_arm(*body, now, start_monotonic_ns);
            error != ErrorCode::None) {
            return reject(error);
        }
        state_ = State::Armed;
        auto response = success();
        response.state = state_;
        response.job_id = body->job_id;
        response.start_monotonic_ns = start_monotonic_ns;
        response.start_utc_ns = body->start_utc_ns;
        response.clock_snapshot = now;
        arm_ = ArmRecord{body->job_id,       body->start_utc_ns, body->max_start_uncertainty_ns,
                         start_monotonic_ns, response,           engine_.schedules_locally()};
        if (arm_->scheduled_locally &&
            (!engine_.schedule(
                 *job_, start_monotonic_ns,
                 {&clock_, body->start_utc_ns,
                  std::min(body->max_start_uncertainty_ns, config_.maximum_arm_uncertainty_ns),
                  config_.maximum_holdover_age_ns}) ||
             engine_.output_active())) {
            arm_.reset();
            const auto stopped = engine_.disable(
                saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns));
            const auto error = stopped && !engine_.output_active() ? ErrorCode::DeviceFault
                                                                   : ErrorCode::OutputStateUnknown;
            record_terminal(State::Failed, error, now.monotonic_now_ns);
            return reject(error);
        }
        if (arm_->scheduled_locally)
            release_execution_input();
        return response;
    }

    const auto* body = std::get_if<AbortBody>(&request.body);
    if (body == nullptr || !valid_id(body->job_id)) {
        return reject(ErrorCode::InvalidMessage);
    }
    return abort_job(body->job_id);
}
Response JobService::abort_job(std::string_view job_id) {
    const auto now = clock_.snapshot();
    if (!job_ || job_->job_id != job_id) {
        return reject(ErrorCode::JobNotFound);
    }
    if (state_ == State::Aborted) {
        auto response = success();
        response.state = State::Aborted;
        response.job_id = std::string(job_id);
        return response;
    }
    if (state_ == State::Complete || state_ == State::Missed) {
        return reject(ErrorCode::InvalidState);
    }
    if (state_ == State::Failed) {
        return reject(ErrorCode::OutputStateUnknown);
    }
    const auto deadline = saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns);
    if (!engine_.disable(deadline) || engine_.output_active()) {
        record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
        return reject(ErrorCode::OutputStateUnknown);
    }
    record_terminal(State::Aborted, ErrorCode::None, now.monotonic_now_ns);
    auto response = success();
    response.state = State::Aborted;
    response.job_id = std::string(job_id);
    return response;
}

Response JobService::local_abort() {
    poll();
    if (state_ == State::Failed || !ready_)
        return reject(ErrorCode::OutputStateUnknown);
    Response response;
    if (job_ && state_ != State::Complete && state_ != State::Missed)
        response = abort_job(job_->job_id);
    else if (engine_.output_active())
        response = reject(ErrorCode::OutputStateUnknown);
    else
        response = success();
    if (response.ok)
        owner_.reset();
    return response;
}

void JobService::poll() {
    const auto now = clock_.snapshot();
    expire_resources(now.monotonic_now_ns);
    prune_replay(now.monotonic_now_ns);
    prune_terminals(now.monotonic_now_ns);
    std::optional<EngineReport> local_report;
    if (state_ == State::Armed && arm_ && job_ && arm_->scheduled_locally) {
        const auto report = engine_.poll(now.monotonic_now_ns);
        local_report = report;
        if (report.state == EngineState::Armed && !report.output_active) {
            return;
        }
        // The local engine has launched, missed, or failed. Process its terminal
        // report below; foreground polling is not the launch trigger.
        state_ = State::Running;
    }
    if (state_ == State::Armed && arm_ && job_) {
        if (now.monotonic_now_ns < arm_->start_monotonic_ns) {
            return;
        }
        const bool clock_state_ok =
            now.state == ClockState::Synchronized ||
            (now.state == ClockState::Holdover && config_.maximum_holdover_age_ns > 0 &&
             now.sync_age_ns <= config_.maximum_holdover_age_ns);
        const bool pending_leap =
            now.leap == LeapState::InsertPending || now.leap == LeapState::DeletePending;
        bool leap_ok = now.leap != LeapState::Unknown &&
                       (pending_leap == now.leap_transition_utc_ns.has_value());
        if (now.leap_transition_utc_ns) {
            const auto transition = *now.leap_transition_utc_ns;
            const auto exclusion_start =
                transition > kLeapExclusionNs ? transition - kLeapExclusionNs : 0;
            const auto exclusion_end = saturating_add(transition, kLeapExclusionNs);
            const auto job_end = arm_->start_utc_ns + start_window_ns(arm_->start_utc_ns) - 1 +
                                 job_->total_duration_ns;
            leap_ok = arm_->start_utc_ns > exclusion_end || job_end < exclusion_start;
        }
        const auto delay = now.monotonic_now_ns - arm_->start_monotonic_ns;
        const auto mapped = now.utc_now_ns >= delay ? now.utc_now_ns - delay : 0;
        const auto utc_error =
            mapped > arm_->start_utc_ns ? mapped - arm_->start_utc_ns : arm_->start_utc_ns - mapped;
        const bool clock_ok = clock_state_ok && now.uncertainty_ns <= arm_->max_uncertainty_ns &&
                              now.uncertainty_ns <= config_.maximum_arm_uncertainty_ns &&
                              utc_error <= now.uncertainty_ns && leap_ok;
        if (delay >= start_window_ns(arm_->start_utc_ns) || !clock_ok) {
            if (!engine_.disable(
                    saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) ||
                engine_.output_active()) {
                record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
            } else {
                record_terminal(State::Missed, ErrorCode::MissedStart, now.monotonic_now_ns);
            }
            return;
        }
        if (!engine_.begin(*job_, now.monotonic_now_ns)) {
            if (!engine_.disable(
                    saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) ||
                engine_.output_active()) {
                record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
            } else {
                record_terminal(State::Failed, ErrorCode::DeviceFault, now.monotonic_now_ns);
            }
            return;
        }
        arm_->launch_monotonic_ns = now.monotonic_now_ns;
        release_execution_input();
        state_ = State::Running;
    }
    if (state_ != State::Running) {
        return;
    }
    const auto report = local_report ? *local_report : engine_.poll(now.monotonic_now_ns);
    if (arm_ && report.launch_monotonic_ns)
        arm_->launch_monotonic_ns = report.launch_monotonic_ns;
    if (report.state == EngineState::Missed) {
        if (!engine_.disable(
                saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) ||
            engine_.output_active()) {
            record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
        } else {
            record_terminal(State::Missed, ErrorCode::MissedStart, now.monotonic_now_ns);
        }
    } else if (report.state == EngineState::Failed) {
        if (!engine_.disable(
                saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) ||
            engine_.output_active()) {
            record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
        } else {
            record_terminal(State::Failed, ErrorCode::DeviceFault, now.monotonic_now_ns);
        }
    } else if (report.state == EngineState::Complete) {
        if (!engine_.disable(
                saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) ||
            engine_.output_active()) {
            record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
        } else {
            record_terminal(State::Complete, ErrorCode::None, now.monotonic_now_ns);
        }
    } else if (arm_ && job_ &&
               now.monotonic_now_ns >=
                   saturating_add(arm_->launch_monotonic_ns.value_or(arm_->start_monotonic_ns),
                                  job_->total_duration_ns)) {
        const auto acknowledgement_ns =
            arm_->scheduled_locally ? std::min(engine_.completion_acknowledgement_ns(),
                                               RfEngine::maximum_completion_acknowledgement_ns)
                                    : 0;
        const auto nominal_end = saturating_add(
            arm_->launch_monotonic_ns.value_or(arm_->start_monotonic_ns), job_->total_duration_ns);
        if (report.state == EngineState::Running && acknowledgement_ns &&
            now.monotonic_now_ns <= saturating_add(nominal_end, acknowledgement_ns)) {
            return; // Keep authority/state until the bounded final acknowledgement.
        }
        if (!engine_.disable(
                saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) ||
            engine_.output_active()) {
            record_terminal(State::Failed, ErrorCode::OutputStateUnknown, now.monotonic_now_ns);
        } else {
            record_terminal(State::Failed, ErrorCode::DeviceFault, now.monotonic_now_ns);
        }
    }
}

void JobService::reset() {
    const auto now = clock_.snapshot();
    const auto stopped =
        engine_.disable(saturating_add(now.monotonic_now_ns, config_.output_disable_timeout_ns)) &&
        !engine_.output_active();
    sessions_.clear();
    owner_.reset();
    job_.reset();
    job_digest_ = {};
    adjustments_.clear();
    arm_.reset();
    replay_cache_.clear();
    terminal_records_.clear();
    retained_jobs_.clear();
    state_ = State::Empty;
    const auto previous_boot_id = boot_id_;
    boot_id_ = identities_.new_boot_id();
    ready_ = valid_id(boot_id_) && boot_id_ != previous_boot_id && stopped;
    if (!ready_) {
        state_ = State::Failed;
    }
}

ServiceActivity JobService::activity() const {
    ServiceActivity result{state_, engine_.output_active(), owner_.has_value()};
    // A local timer may launch after poll() captured an armed/inactive report.
    // The live output observation then establishes that this locally scheduled
    // job has started. Do not combine it with the stale foreground armed state.
    // Unexpected output in other states/engines remains visible as a fault;
    // terminal transitions still require poll() and verified engine shutdown.
    if (result.state == State::Armed && result.output_active && job_ && arm_ &&
        arm_->scheduled_locally)
        result.state = State::Running;
    return result;
}

ServiceStatus JobService::status() const {
    const auto current = activity();
    ServiceStatus result{boot_id_,     current.state, current.output_active,
                         std::nullopt, std::nullopt,  {}};
    if (owner_) {
        result.owner_id = owner_->owner_id;
    }
    if (job_) {
        result.job_id = job_->job_id;
    }
    result.terminal_records.assign(terminal_records_.begin(), terminal_records_.end());
    return result;
}

Response JobService::reject(ErrorCode code) const {
    Response response;
    response.error = code;
    response.state = state_;
    response.output_active = engine_.output_active();
    response.boot_id = boot_id_;
    return response;
}

Response JobService::success() const {
    Response response;
    response.ok = true;
    response.state = state_;
    response.output_active = engine_.output_active();
    response.boot_id = boot_id_;
    return response;
}

bool JobService::valid_id(std::string_view value) const {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool JobService::owns(const Request& request) const {
    return owner_ && owner_->session_id == request.session_id &&
           owner_->principal == request.principal;
}

ErrorCode JobService::validate_job(const Job& job) const {
    if (!job.events.valid()) {
        return job.events.failure() == EventList::Failure::LimitExceeded
                   ? ErrorCode::JobLimitExceeded
                   : ErrorCode::InternalError;
    }
    if (!valid_id(job.job_id) || job.events.empty()) {
        return ErrorCode::InvalidMessage;
    }
    if (job.profile != "rf-events/1") {
        return ErrorCode::UnsupportedProfile;
    }
    if (!contains(config_.supported_modes, job.mode)) {
        return ErrorCode::UnsupportedMode;
    }
    if (job.events.size() > config_.max_events ||
        job.total_duration_ns > config_.max_job_duration_ns) {
        return ErrorCode::JobLimitExceeded;
    }
    std::uint64_t expected_offset = 0;
    for (const auto& event : job.events) {
        if (event.offset_ns != expected_offset || event.duration_ns == 0 ||
            event.duration_ns > std::numeric_limits<std::uint64_t>::max() - expected_offset) {
            return ErrorCode::InvalidMessage;
        }
        if (event.rf_on != event.frequency_nhz.has_value() ||
            (event.frequency_nhz && *event.frequency_nhz == 0)) {
            return ErrorCode::InvalidMessage;
        }
        if (event.frequency_nhz && (*event.frequency_nhz < config_.minimum_frequency_nhz ||
                                    *event.frequency_nhz > config_.maximum_frequency_nhz))
            return ErrorCode::FrequencyRejected;
        expected_offset += event.duration_ns;
    }
    return expected_offset == job.total_duration_ns ? ErrorCode::None : ErrorCode::InvalidMessage;
}

ErrorCode JobService::validate_arm(const ArmBody& arm, const ClockSnapshot& now,
                                   std::uint64_t& start_monotonic_ns) const {
    if (now.state == ClockState::Unsynchronized) {
        return ErrorCode::ClockUnsynchronized;
    }
    if (now.state == ClockState::Holdover && (config_.maximum_holdover_age_ns == 0 ||
                                              now.sync_age_ns > config_.maximum_holdover_age_ns)) {
        return ErrorCode::ClockUnsynchronized;
    }
    if (now.uncertainty_ns > arm.max_start_uncertainty_ns ||
        now.uncertainty_ns > config_.maximum_arm_uncertainty_ns) {
        return ErrorCode::ClockUncertain;
    }
    const bool pending_leap =
        now.leap == LeapState::InsertPending || now.leap == LeapState::DeletePending;
    if (now.leap == LeapState::Unknown || pending_leap != now.leap_transition_utc_ns.has_value()) {
        return ErrorCode::LeapUnsafe;
    }
    if (arm.start_utc_ns < now.utc_now_ns ||
        arm.start_utc_ns - now.utc_now_ns < config_.minimum_arm_lead_ns) {
        return ErrorCode::ArmTooLate;
    }
    const auto ahead = arm.start_utc_ns - now.utc_now_ns;
    if (ahead > config_.maximum_arm_ahead_ns) {
        return ErrorCode::ArmTooFar;
    }
    if (ahead > std::numeric_limits<std::uint64_t>::max() - now.monotonic_now_ns ||
        (job_ &&
         job_->total_duration_ns > std::numeric_limits<std::uint64_t>::max() - arm.start_utc_ns)) {
        return ErrorCode::InvalidMessage;
    }
    const auto resolution = engine_.start_resolution_ns();
    if (resolution == 0)
        return ErrorCode::DeviceFault;
    const auto nominal_start = now.monotonic_now_ns + ahead;
    const auto adjustment = (resolution - nominal_start % resolution) % resolution;
    const auto window = start_window_ns(arm.start_utc_ns);
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    if (nominal_start > limit - window || arm.start_utc_ns > limit - window ||
        adjustment >= window || job_->total_duration_ns > limit - (nominal_start + window) ||
        job_->total_duration_ns > limit - (arm.start_utc_ns + window))
        return ErrorCode::InvalidMessage;
    const auto budget = std::min(arm.max_start_uncertainty_ns, config_.maximum_arm_uncertainty_ns);
    if (adjustment > budget - now.uncertainty_ns)
        return ErrorCode::ClockUncertain;
    // WTP reports the exact mapping. Round the hardware target upward so timer
    // quantization never requests an early launch. Clock uncertainty remains
    // independent of permitted scheduling delay within the requested UTC second.
    start_monotonic_ns = nominal_start;
    if (job_->total_duration_ns > std::numeric_limits<std::uint64_t>::max() - start_monotonic_ns)
        return ErrorCode::InvalidMessage;
    if (now.leap_transition_utc_ns) {
        const auto transition = *now.leap_transition_utc_ns;
        const auto exclusion_start =
            transition > kLeapExclusionNs ? transition - kLeapExclusionNs : 0;
        const auto exclusion_end = saturating_add(transition, kLeapExclusionNs);
        const auto job_end = arm.start_utc_ns + window - 1 + job_->total_duration_ns;
        if (arm.start_utc_ns <= exclusion_end && job_end >= exclusion_start) {
            return ErrorCode::LeapUnsafe;
        }
    }
    return ErrorCode::None;
}

void JobService::expire_resources(std::uint64_t monotonic_now_ns) {
    if (!owner_ || monotonic_now_ns < owner_->expires_monotonic_ns) {
        return;
    }
    if (state_ == State::Armed || state_ == State::Running) {
        owner_->release_after_terminal = true;
        return;
    }
    if (state_ == State::Loaded) {
        if (!engine_.disable(saturating_add(monotonic_now_ns, config_.output_disable_timeout_ns)) ||
            engine_.output_active()) {
            record_terminal(State::Failed, ErrorCode::OutputStateUnknown, monotonic_now_ns);
            owner_.reset();
            return;
        }
        record_terminal(State::Aborted, ErrorCode::None, monotonic_now_ns);
        clear_job();
    }
    owner_.reset();
}

void JobService::record_terminal(State state, ErrorCode error, std::uint64_t now_ns) {
    state_ = state;
    terminal_records_.push_front(TerminalRecord{job_ ? job_->job_id : std::string{}, state, now_ns,
                                                engine_.output_active(), error});
    if (job_) {
        auto load = success();
        load.state = State::Loaded;
        load.job_id = job_->job_id;
        load.adjustments = adjustments_;
        retained_jobs_.push_front({job_->job_id, job_digest_, std::move(load), arm_});
        if (state == State::Complete) {
            // Completion has authoritatively stopped the engine. Status needs
            // the identity and duration; retained replay uses the digest above.
            // Keeping a maximum event list here blocks the next decoded job's
            // admission while ordinary TLS management remains connected.
            job_->events.clear();
        }
    }
    while (terminal_records_.size() > config_.terminal_record_entries) {
        const auto id = terminal_records_.back().job_id;
        std::erase_if(retained_jobs_, [&](const RetainedJob& item) { return item.job_id == id; });
        terminal_records_.pop_back();
    }
    if (owner_ && owner_->release_after_terminal) {
        owner_.reset();
    }
}

void JobService::touch_terminal(std::string_view id) {
    const auto record = std::find_if(terminal_records_.begin(), terminal_records_.end(),
                                     [&](const TerminalRecord& item) { return item.job_id == id; });
    if (record != terminal_records_.end())
        std::rotate(terminal_records_.begin(), record, record + 1);
    const auto job = std::find_if(retained_jobs_.begin(), retained_jobs_.end(),
                                  [&](const RetainedJob& item) { return item.job_id == id; });
    if (job != retained_jobs_.end())
        std::rotate(retained_jobs_.begin(), job, job + 1);
}

void JobService::release_execution_input() {
    if (job_ && engine_.owns_execution_plan()) {
        // The engine has acknowledged its independent plan. Keep identity,
        // duration and the original digest for authority and replay; the event
        // storage is no longer consulted by this execution path.
        job_->events.clear();
    }
}

void JobService::clear_job() {
    job_.reset();
    job_digest_ = {};
    adjustments_.clear();
    arm_.reset();
    state_ = State::Empty;
}

void JobService::prune_replay(std::uint64_t now_ns) {
    std::erase_if(replay_cache_, [&](const ReplayEntry& entry) {
        return now_ns >= entry.completed_monotonic_ns &&
               now_ns - entry.completed_monotonic_ns >= config_.response_cache_ttl_ns;
    });
}

void JobService::prune_terminals(std::uint64_t now_ns) {
    bool current_record_expired = false;
    std::erase_if(terminal_records_, [&](const TerminalRecord& record) {
        const bool expired = now_ns >= record.ended_monotonic_ns &&
                             now_ns - record.ended_monotonic_ns >= config_.terminal_record_ttl_ns;
        if (expired && job_ && record.job_id == job_->job_id) {
            current_record_expired = true;
        }
        return expired;
    });
    std::erase_if(retained_jobs_, [&](const RetainedJob& item) {
        return std::none_of(
            terminal_records_.begin(), terminal_records_.end(),
            [&](const TerminalRecord& record) { return record.job_id == item.job_id; });
    });
    if (current_record_expired &&
        (state_ == State::Complete || state_ == State::Aborted || state_ == State::Missed)) {
        clear_job();
    }
}

void JobService::prune_sessions(std::uint64_t now_ns) {
    std::erase_if(sessions_, [&](const Session& session) {
        const bool owns_transmitter = owner_ && owner_->session_id == session.session_id;
        return !owns_transmitter && now_ns >= session.last_seen_monotonic_ns &&
               now_ns - session.last_seen_monotonic_ns >= config_.response_cache_ttl_ns;
    });
}

std::optional<Response> JobService::replay(const Request& request, std::uint64_t, bool& conflict) {
    auto entry =
        std::find_if(replay_cache_.begin(), replay_cache_.end(), [&](const ReplayEntry& item) {
            return item.session_id == request.session_id && item.request_id == request.request_id;
        });
    if (entry == replay_cache_.end()) {
        return std::nullopt;
    }
    if (entry->payload_digest != request.payload_digest) {
        conflict = true;
        return std::nullopt;
    }
    entry->lru = ++lru_sequence_;
    return entry->response;
}

void JobService::remember(const Request& request, const Response& response, std::uint64_t now_ns) {
    const auto session_entries = static_cast<std::size_t>(
        std::count_if(replay_cache_.begin(), replay_cache_.end(), [&](const ReplayEntry& entry) {
            return entry.session_id == request.session_id;
        }));
    if (session_entries >= config_.response_cache_entries) {
        const auto victim =
            std::min_element(replay_cache_.begin(), replay_cache_.end(),
                             [&](const ReplayEntry& left, const ReplayEntry& right) {
                                 if (left.session_id != request.session_id) {
                                     return false;
                                 }
                                 if (right.session_id != request.session_id) {
                                     return true;
                                 }
                                 return left.lru < right.lru;
                             });
        replay_cache_.erase(victim);
    }
    replay_cache_.push_back({request.session_id, request.request_id, request.payload_digest,
                             response, now_ns, ++lru_sequence_});
}

} // namespace wsprrypico::wtp
