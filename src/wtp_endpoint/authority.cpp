#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/revocation_journal.hpp"

#include <utility>
#include <limits>

namespace wsprrypi {

WtpPiAuthority::OutputState WtpPiAuthority::output_state_locked() const {
    const auto remote=service_.status();
    return {remote.boot_id,remote.owner_id,remote.job_id,remote.state,remote.output_active,
        local_work_active_,local_test_tone_,output_unknown_};
}
WtpPiAuthority::OutputTransition::OutputTransition(WtpPiAuthority &value)
    :owner(value),before(value.output_state_locked()){}
WtpPiAuthority::OutputTransition::~OutputTransition(){
    try{
    if(before==owner.output_state_locked())return;
    if(owner.output_generation_==std::numeric_limits<std::uint64_t>::max()){
        owner.output_generation_exhausted_=owner.output_unknown_=true;
        owner.effective_local_enable_=false;owner.remote_admission_closed_=true;
    }else ++owner.output_generation_;
    }catch(...){
        owner.output_generation_exhausted_=owner.output_unknown_=true;
        owner.effective_local_enable_=false;owner.remote_admission_closed_=true;
    }
}
WtpPiAuthority::ObservedOutput WtpPiAuthority::observed_output_locked(const std::string &self) const {
    const auto remote=service_.status();
    return {output_generation_,local_work_active_?std::optional<std::string>(self):remote.owner_id,
        local_work_active_?std::nullopt:remote.job_id};
}
WtpPiAuthority::ObservedOutput WtpPiAuthority::observed_output(const std::string &self) const {
    std::lock_guard lock(mutex_);return observed_output_locked(self);
}
bool WtpPiAuthority::reserve_observed_stop(const std::string &self,const std::string &boot,const ObservedOutput &expected){
    std::lock_guard lock(mutex_);
    if(observed_stop_reserved_||output_generation_exhausted_||configuration_users_||boot!=service_.status().boot_id||expected!=observed_output_locked(self))return false;
    observed_stop_reserved_=true;return true;
}
void WtpPiAuthority::release_observed_stop(){std::lock_guard lock(mutex_);observed_stop_reserved_=false;}

WtpPiAuthority::WtpPiAuthority(wsprrypico::wtp::JobService& service,
                               bool local_enabled, WtpRevocationJournal* journal,
                               std::function<bool()> admission_ready)
    : service_(service), journal_(journal),admission_ready_(std::move(admission_ready)), requested_local_enable_(local_enabled),
      effective_local_enable_(local_enabled),
      remote_admission_closed_(local_enabled) {
    if (journal_ && !journal_->load()) {
        effective_local_enable_ = false;
        remote_admission_closed_ = true;
        revocation_unavailable_ = true;
    } else if (journal_) {
        revocation_generation_ = journal_->generation();
    }
}

bool WtpPiAuthority::remote_job_active(wsprrypico::wtp::State state) {
    return state == wsprrypico::wtp::State::Armed ||
           state == wsprrypico::wtp::State::Running;
}

wsprrypico::wtp::Response WtpPiAuthority::handle(wsprrypico::wtp::Request&& request) {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    const auto session = request.session_id;
    const bool hello = request.operation == "HELLO";
    auto response = service_.handle(std::move(request), [this](const auto& candidate) {
        if(admission_ready_&&(candidate.operation=="CLAIM"||candidate.operation=="LOAD"||candidate.operation=="ARM")){
            try{if(!admission_ready_())return false;}catch(...){return false;}
        }
        if((observed_stop_reserved_||output_generation_exhausted_)&&
            (candidate.operation=="CLAIM"||candidate.operation=="LOAD"||candidate.operation=="ARM"))return false;
        const auto status = service_.status();
        if (candidate.operation == "CLAIM") {
            const auto epoch = session_generations_.find(candidate.session_id);
            return epoch != session_generations_.end() && epoch->second == revocation_generation_ &&
                   !(requested_local_enable_ || effective_local_enable_ ||
                     local_work_active_ || remote_admission_closed_ ||
                     configuration_users_ != 0 || configuration_pending_ ||
                     output_unknown_ || status.output_active);
        }
        if (configuration_pending_ && (candidate.operation == "LOAD" || candidate.operation == "ARM"))
            return false;
        if (!finishing_remote_job_) return true;
        if (candidate.operation == "LOAD") {
            const auto* job = std::get_if<wsprrypico::wtp::Job>(&candidate.body);
            const auto* replay =
                std::get_if<wsprrypico::wtp::LoadReplayBody>(&candidate.body);
            return status.job_id &&
                   ((job && job->job_id == *status.job_id) ||
                    (replay && replay->job_id == *status.job_id));
        }
        if (candidate.operation == "ARM") {
            const auto* arm = std::get_if<wsprrypico::wtp::ArmBody>(&candidate.body);
            return status.job_id && arm && arm->job_id == *status.job_id;
        }
        return true;
    });
    // Retain the first observed epoch for a session, including across reconnect.
    // A controller queued before takeover cannot reacquire after local Disable;
    // it must reconcile the durable record and create a fresh assignment/session.
    // Exhaustion admits inspection only until service restart/recovery.
    if (hello && response.ok && session_generations_.size() < 512)
        session_generations_.try_emplace(session, revocation_generation_);
    return response;
}

wsprrypico::wtp::Response WtpPiAuthority::handle(
    const wsprrypico::wtp::Request& request) {
    auto copy = request;
    return handle(std::move(copy));
}

WtpPiAuthority::EnableResult WtpPiAuthority::enable_interactive(
    EnableChoice choice, bool confirmed, const std::function<void()>& persist,
    std::optional<std::pair<std::string, std::string>> observed) {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if (choice == EnableChoice::Decline)
        return EnableResult::Declined;
    if (observed_stop_reserved_)
        return EnableResult::ConfirmationRequired;
    const auto status = service_.status();
    if (status.owner_id && !confirmed)
        return EnableResult::ConfirmationRequired;
    if (confirmed && observed &&
        (observed->first != status.owner_id.value_or("") ||
         observed->second != status.job_id.value_or("")))
        return EnableResult::ConfirmationRequired;
    // Persistence failure leaves ownership and admission untouched. Holding
    // this gate prevents a CLAIM racing the operator's confirmation.
    if (persist) persist();
    return enable_locked(choice == EnableChoice::FinishCurrent);
}

WtpPiAuthority::EnableResult WtpPiAuthority::enable_noninteractive() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if (observed_stop_reserved_) return EnableResult::ConfirmationRequired;
    return enable_locked(false);
}

WtpPiAuthority::EnableResult WtpPiAuthority::enable_locked(bool finish) {
    requested_local_enable_ = true;
    remote_admission_closed_ = true;
    if(output_generation_exhausted_){effective_local_enable_=false;return EnableResult::OutputUnknown;}
    bool durable = !revocation_unavailable_;
    if (!revocation_recorded_for_enable_) {
        durable = durable && (!journal_ || journal_->advance());
        if (durable) {
            revocation_generation_ = journal_ ? journal_->generation() :
                                                        revocation_generation_ + 1;
            revocation_recorded_for_enable_ = true;
        } else revocation_unavailable_ = true;
    }
    const auto status = service_.status();
    if (!durable) finish = false;
    if (status.owner_id && finish && remote_job_active(status.state)) {
        finishing_remote_job_ = true;
        effective_local_enable_ = false;
        return EnableResult::PendingJob;
    }
    if (status.owner_id || status.output_active) {
        if (!service_.local_abort().ok) {
            output_unknown_ = true;
            effective_local_enable_ = false;
            return EnableResult::OutputUnknown;
        }
    }
    finishing_remote_job_ = false;
    if (!durable) {
        effective_local_enable_ = false;
        return EnableResult::RevocationUnavailable;
    }
    if (local_work_active_) {
        // Repeated configuration commits must not revoke a schedule that
        // already holds effective local Enable. A new Enable while a Test
        // Tone or disabled schedule is draining still waits for cleanup.
        if (effective_local_enable_ && !local_test_tone_ && !output_unknown_ &&
            status.state != wsprrypico::wtp::State::Failed)
            return EnableResult::Enabled;
        effective_local_enable_ = false;
        return EnableResult::PendingLocalWork;
    }
    if (service_.status().output_active || output_unknown_ ||
        service_.status().state == wsprrypico::wtp::State::Failed) {
        effective_local_enable_ = false;
        return EnableResult::OutputUnknown;
    }
    effective_local_enable_ = true;
    return EnableResult::Enabled;
}

void WtpPiAuthority::finish_takeover_locked() {
    if (!finishing_remote_job_) return;
    const auto status = service_.status();
    if (remote_job_active(status.state)) return;
    if (!service_.local_abort().ok || service_.status().output_active) {
        output_unknown_ = true;
        effective_local_enable_ = false;
    } else if (!local_work_active_ && !output_unknown_) {
        effective_local_enable_ = true;
    }
    finishing_remote_job_ = false;
}

void WtpPiAuthority::disable_local() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    requested_local_enable_ = false;
    effective_local_enable_ = false;
    finishing_remote_job_ = false;
    remote_admission_closed_ = revocation_unavailable_;
    revocation_recorded_for_enable_ = false;
}

bool WtpPiAuthority::can_start_local_locked(bool requires_enable) const {
    if(observed_stop_reserved_||output_generation_exhausted_||configuration_users_)return false;
    if (reconfiguration_hold_) return false;
    if (requires_enable && !effective_local_enable_) return false;
    const auto status = service_.status();
    return !status.owner_id && !status.output_active && !output_unknown_ &&
           status.state != wsprrypico::wtp::State::Failed &&
           !revocation_unavailable_ && !local_work_active_ && !finishing_remote_job_;
}

bool WtpPiAuthority::begin_scheduled_work() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if(observed_stop_reserved_||output_generation_exhausted_)return false;
    if (reconfiguration_hold_) return false;
    if (local_work_active_)
        return local_test_tone_ || effective_local_enable_;
    if (!can_start_local_locked(true)) return false;
    local_work_active_ = true;
    local_test_tone_ = false;
    return true;
}

bool WtpPiAuthority::begin_test_tone() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if (configuration_users_ || !can_start_local_locked(false)) return false;
    local_work_active_ = true;
    local_test_tone_ = true;
    return true;
}

void WtpPiAuthority::end_local_work(bool output_inactive) {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    end_local_work_locked(output_inactive);
}

void WtpPiAuthority::end_scheduled_work(bool output_inactive) {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if (!local_test_tone_) end_local_work_locked(output_inactive);
}

void WtpPiAuthority::end_local_work_locked(bool output_inactive) {
    local_work_active_ = false;
    local_test_tone_ = false;
    if (!output_inactive) output_unknown_ = true;
    effective_local_enable_ = requested_local_enable_ && !output_unknown_ &&
        !revocation_unavailable_ && !finishing_remote_job_ &&
        !service_.status().owner_id && !service_.status().output_active &&
        service_.status().state != wsprrypico::wtp::State::Failed;
}

bool WtpPiAuthority::begin_configuration() {
    std::lock_guard lock(mutex_);
    const auto status = service_.status();
    if (observed_stop_reserved_ || output_generation_exhausted_ ||
        status.owner_id || status.output_active || output_unknown_ ||
        revocation_unavailable_ || finishing_remote_job_ ||
        status.state == wsprrypico::wtp::State::Failed)
        return false;
    ++configuration_users_;
    return true;
}

void WtpPiAuthority::end_configuration() {
    std::lock_guard lock(mutex_);
    if (configuration_users_) --configuration_users_;
}

bool WtpPiAuthority::recover_output(bool output_inactive) {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if (observed_stop_reserved_ || output_generation_exhausted_ || !output_inactive || local_work_active_ || revocation_unavailable_ ||
        service_.status().output_active ||
        service_.status().state == wsprrypico::wtp::State::Failed)
        return false;
    output_unknown_ = false;
    if (requested_local_enable_ && !finishing_remote_job_ &&
        !service_.status().owner_id)
        effective_local_enable_ = true;
    return true;
}

void WtpPiAuthority::suspend_remote_admission() {
    std::lock_guard lock(mutex_);
    configuration_pending_ = true;
}

bool WtpPiAuthority::reserve_reconfiguration() {
    std::lock_guard lock(mutex_);
    const auto state = service_.status();
    if (observed_stop_reserved_ || output_generation_exhausted_ || local_work_active_ || configuration_users_ || state.owner_id ||
        state.output_active || output_unknown_ || finishing_remote_job_)
        return false;
    configuration_pending_ = reconfiguration_hold_ = true;
    return true;
}

bool WtpPiAuthority::local_stop() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    const bool had_remote = service_.status().owner_id.has_value() ||
                            service_.status().output_active;
    if (had_remote) (void)enable_locked(false);
    requested_local_enable_ = effective_local_enable_ = finishing_remote_job_ = false;
    remote_admission_closed_ = output_unknown_ || revocation_unavailable_;
    revocation_recorded_for_enable_ = false;
    return !output_unknown_ && !service_.status().output_active &&
           service_.status().state != wsprrypico::wtp::State::Failed;
}

bool WtpPiAuthority::recover_remote_output() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    if (observed_stop_reserved_ || local_work_active_ || configuration_users_ || revocation_unavailable_) return false;
    remote_admission_closed_ = true;
    // Recovery is a local takeover even while local scheduling stays disabled.
    // Record it durably before replacing sessions so an absent controller
    // removes its saved assignment when it returns.
    if (journal_ && !journal_->advance()) {
        revocation_unavailable_ = true;
        (void)service_.local_abort();
        effective_local_enable_ = false;
        return false;
    }
    revocation_generation_ = journal_ ? journal_->generation() : revocation_generation_ + 1;
    service_.reset(); // New boot identity invalidates every old session/replay.
    session_generations_.clear();
    const auto state = service_.status();
    output_unknown_ = state.output_active || state.state == wsprrypico::wtp::State::Failed;
    finishing_remote_job_ = false;
    effective_local_enable_ = requested_local_enable_ && !output_unknown_;
    remote_admission_closed_ = requested_local_enable_ || output_unknown_;
    return !output_unknown_;
}

void WtpPiAuthority::poll() {
    std::lock_guard lock(mutex_);
    OutputTransition transition(*this);
    service_.poll();
    finish_takeover_locked();
}

wsprrypico::wtp::ServiceStatus WtpPiAuthority::status() const {
    std::lock_guard lock(mutex_);
    return service_.status();
}

std::string WtpPiAuthority::active_load_replay_id() const {
    std::lock_guard lock(mutex_);
    return service_.active_load_replay_id();
}

const wsprrypico::wtp::ServiceConfig& WtpPiAuthority::config() const {
    return service_.config();
}

WtpPiAuthority::Snapshot WtpPiAuthority::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_locked();
}
std::pair<WtpPiAuthority::Snapshot,WtpPiAuthority::ObservedOutput>
WtpPiAuthority::management_snapshot(const std::string &self) const {
    std::lock_guard lock(mutex_);
    return {snapshot_locked(),observed_output_locked(self)};
}
WtpPiAuthority::Snapshot WtpPiAuthority::snapshot_locked() const {
    return {requested_local_enable_, effective_local_enable_, local_work_active_,
            remote_admission_closed_ || configuration_pending_ || reconfiguration_hold_, output_unknown_, revocation_unavailable_,
            finishing_remote_job_,
            revocation_generation_, service_.status(),observed_stop_reserved_};
}
} // namespace wsprrypi
