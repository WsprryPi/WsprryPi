#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"

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
                            WtpRevocationJournal* journal = nullptr);

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
    };
    Snapshot snapshot() const;

private:
    EnableResult enable_locked(bool finish);
    void finish_takeover_locked();
    bool can_start_local_locked(bool requires_enable) const;
    void end_local_work_locked(bool output_inactive);
    static bool remote_job_active(wsprrypico::wtp::State state);

    wsprrypico::wtp::JobService& service_;
    WtpRevocationJournal* journal_ = nullptr;
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
    std::map<std::string, std::uint64_t> session_generations_;
};

} // namespace wsprrypi
