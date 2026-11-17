// SPDX-License-Identifier: MIT
#pragma once
#include "wtp_integration/pi_management_journal.hpp"
#include <array>
#include <mutex>
namespace wsprrypi {
using RecurrenceListDigests=std::array<std::string,5>;
bool valid_recurrence_progress(const nlohmann::json &,std::string_view self) noexcept;
nlohmann::json parse_recurrence_progress(std::string_view,std::string_view self);
nlohmann::json initial_recurrence_progress(std::string_view self,const RecurrenceListDigests &);
// One target-owned record plus its bounded transaction recovery. The caller
// holds configuration/admission serialization. No RF, selector, or clock I/O.
class RecurrenceProgress {
public:
    RecurrenceProgress(std::string directory,std::string path,std::string self);
    // Creation is permitted only by the target's authoritative staged -> native
    // transaction, before any native admission. Missing native history holds.
    void recover(bool may_initialize,const RecurrenceListDigests &);
    nlohmann::json snapshot();
    void reserve(const std::string &occurrence,std::uint64_t first,std::uint64_t last,
        const std::string &intent,std::uint64_t authority,std::uint64_t configuration,
        std::uint64_t clock,const RecurrenceListDigests &,const std::array<std::uint32_t,5> &sizes,
        std::size_t scope,bool explicit_skip=false);
    // accepted=false is a positive refusal, never a guess after restart.
    void resolve(bool accepted,const RecurrenceListDigests &current);
    bool held() const;
    std::uint64_t publication_count() const;
private:
    void check();
    void publish(const nlohmann::json &);
    std::string self_;
    PiManagementJournal journal_;
    std::optional<PiManagementJournal::Observation> observation_;
    nlohmann::json record_;
    mutable std::mutex mutex_;
    bool held_=true;
    std::uint64_t publications_{};
};
}
