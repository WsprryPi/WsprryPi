#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"
#include "managed_output_observation.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>

namespace wsprrypi {

class WtpRevocationJournal;

// Serializes WTP ownership with local output admission. The process singleton
// remains the separate outer guard against another WsprryPi runtime.
class WtpPiAuthority : public wsprrypico::wtp::IJobService {
public:
    enum class EnableChoice { EndNow, FinishCurrent, Decline };
    enum class EnableResult { Enabled, PendingJob, PendingLocalWork,
                              ConfirmationRequired, Declined, OutputUnknown,
                              RevocationUnavailable };

    explicit WtpPiAuthority(wsprrypico::wtp::JobService& service,
                            bool local_enabled = false,
                            WtpRevocationJournal* journal = nullptr,
                            std::function<bool()> admission_ready = {});

    wsprrypico::wtp::Response handle(wsprrypico::wtp::Request&& request) override;
    wsprrypico::wtp::Response handle(const wsprrypico::wtp::Request& request);
    EnableResult enable_interactive(EnableChoice choice, bool confirmed,
                                   const std::function<void()>& persist = {},
                                   std::optional<std::pair<std::string, std::string>> observed = {});
    EnableResult enable_noninteractive();
    void disable_local();
    bool begin_scheduled_work();
    bool begin_test_tone();
    void end_local_work(bool output_inactive);
    void end_scheduled_work(bool output_inactive);
    bool begin_configuration();
    void end_configuration();
    bool recover_output(bool output_inactive);
    void suspend_remote_admission();
    bool reserve_reconfiguration();
    bool local_stop();
    bool recover_remote_output();
    void poll() override;
    wsprrypico::wtp::ServiceStatus status() const override;
    std::string active_load_replay_id() const override;
    const wsprrypico::wtp::ServiceConfig& config() const override;

    struct Snapshot {
        bool requested_local_enable;
        bool effective_local_enable;
        bool local_work_active;
        bool remote_admission_closed;
        bool output_unknown;
        bool revocation_unavailable;
        bool finishing_remote_job;
        std::uint64_t revocation_generation;
        wsprrypico::wtp::ServiceStatus remote;
        bool stop_reserved{};
    };
    Snapshot snapshot() const;

    using ObservedOutput=ManagedOutputObservation;
    ObservedOutput observed_output(const std::string &self) const;
    std::pair<Snapshot,ObservedOutput> management_snapshot(const std::string &self) const;
    // Compare and close all new output admission under the same authority lock.
    // The parent retains this reservation while persisting/cancelling the
    // observed work; it cannot accidentally stop a later replacement job.
    bool reserve_observed_stop(const std::string &self,const std::string &boot,
        const ObservedOutput &);
    void release_observed_stop();

private:
    EnableResult enable_locked(bool finish);
    void finish_takeover_locked();
    bool can_start_local_locked(bool requires_enable) const;
    void end_local_work_locked(bool output_inactive);
    static bool remote_job_active(wsprrypico::wtp::State state);
    struct OutputState {
        std::string boot;
        std::optional<std::string> owner,job;
        wsprrypico::wtp::State state;
        bool active,local,tone,unknown;
        bool operator==(const OutputState &) const = default;
    };
    OutputState output_state_locked() const;
    Snapshot snapshot_locked() const;
    ObservedOutput observed_output_locked(const std::string &self) const;
    struct OutputTransition {
        WtpPiAuthority &owner;
        OutputState before;
        explicit OutputTransition(WtpPiAuthority &);
        ~OutputTransition();
    };

    wsprrypico::wtp::JobService& service_;
    WtpRevocationJournal* journal_ = nullptr;
    std::function<bool()> admission_ready_;
    mutable std::mutex mutex_;
    bool requested_local_enable_ = false;
    bool effective_local_enable_ = false;
    bool local_work_active_ = false;
    bool local_test_tone_ = false;
    unsigned configuration_users_ = 0;
    bool configuration_pending_ = false;
    bool reconfiguration_hold_ = false;
    bool remote_admission_closed_ = false;
    bool output_unknown_ = false;
    bool revocation_unavailable_ = false;
    bool finishing_remote_job_ = false;
    bool revocation_recorded_for_enable_ = false;
    std::uint64_t revocation_generation_ = 0;
    std::uint64_t output_generation_ = 1;
    bool output_generation_exhausted_ = false, observed_stop_reserved_ = false;
    std::map<std::string, std::uint64_t> session_generations_;
};

} // namespace wsprrypi
