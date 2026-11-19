// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "pi_management_journal.hpp"
#include "pi_pair_initialization.hpp"
#include <memory>
#include <optional>
#include <functional>
#include <mutex>
#include <chrono>
#include <stdexcept>
namespace wsprrypi {
// Device-owned fences, independent of the assignment file's name and flags.
// The Fleet owner serializes intent with its dispatch permits. This class never
// operates a carrier or infers that unknown output is idle.
// Pure P62-16 after-image construction only. The caller must independently
// verify current normal TLS, grant reservation, boot, binding, source fence and
// one-use publication. This helper grants no permission and performs no I/O.
nlohmann::json controller_resume_assignment(const nlohmann::json &frozen,const nlohmann::json &live,
    const nlohmann::json &grant,const std::string &authenticated_host);
class ControllerRetirementConflict final : public std::runtime_error {
public: ControllerRetirementConflict():std::runtime_error("controller_retirement_conflict"){}
};
enum class RetirementWritePoint {Prepared,Committed,Published,EstablishmentPrepared,EstablishmentCommitted,EstablishmentPublished};
class ControllerRetirementBook {
public:
    ControllerRetirementBook(const std::string &directory,const std::string &source,
        const std::string &self,const nlohmann::json &assignments,
        std::function<void(RetirementWritePoint)> checkpoint = {},
        std::function<void(PiPairInitializationProof)> pairing = {});
    nlohmann::json read(const std::string &incarnation,const nlohmann::json &assignments);
    nlohmann::json retire(const nlohmann::json &intent,const std::string &revision,
        const std::string &incarnation,const nlohmann::json &assignments,const nlohmann::json &observation = nullptr,
        const nlohmann::json &machine_request = nullptr);
    class ResumeIntent {
    public:
        const nlohmann::json &acknowledgement() const noexcept{return acknowledgement_;}
    private:
        friend class ControllerRetirementBook;
        ResumeIntent(const ControllerRetirementBook *owner,std::string revision,nlohmann::json grant,nlohmann::json assignments,
            nlohmann::json receipt,nlohmann::json ack,std::optional<PiManagementJournal::Observation> source)
            :owner_(owner),revision_(std::move(revision)),grant_(std::move(grant)),assignments_(std::move(assignments)),receipt_(std::move(receipt)),
            acknowledgement_(std::move(ack)),source_(std::move(source)),until_(std::chrono::steady_clock::now()+std::chrono::seconds(30)){}
        const ControllerRetirementBook *owner_;
        std::string revision_;
        nlohmann::json grant_,assignments_,receipt_,acknowledgement_;
        std::optional<PiManagementJournal::Observation> source_;
        std::chrono::steady_clock::time_point until_;
    };
    // Internal owner boundary: an actual normal-management grant and fresh
    // disabled-target readbacks must precede this call. No wire handler accepts
    // caller-supplied acknowledgement bodies for signing. Sign outside the book
    // lock, then publish under the real final outgoing-pair guard.
    std::optional<nlohmann::json> resume_receipt(const nlohmann::json &grant);
    ResumeIntent prepare_resume(const nlohmann::json &grant,const std::string &incarnation,
        const nlohmann::json &assignments,const std::string &authenticated_host,bool current_target_valid);
    nlohmann::json publish_resume(const ResumeIntent &,const nlohmann::json &signed_acknowledgement);
    std::optional<nlohmann::json> replay(const nlohmann::json &intent,const std::string &revision);
    bool permits(const std::string &target);
    std::string active_resume(const std::string &target);
    bool pending(const std::string &target);
    // Root supplies a current authenticated native target readback. Closure
    // requires this owner's permanent fences and a matching target receipt;
    // mere absence, retirement intent, or a terminal resume receipt is insufficient.
    bool retirement_scope_closed(const nlohmann::json &revoked_pair,const nlohmann::json &migration);
    using RetirementSigner=std::function<std::optional<nlohmann::json>(const nlohmann::json &,const nlohmann::json &)>;
    void confirm_idle(const std::string &target,const nlohmann::json &observation,const RetirementSigner &signer = {});
    std::optional<nlohmann::json> machine_receipt(const nlohmann::json &request);
    // A previously retired local/browser assignment already has a permanent
    // fence. Bind its first machine proof without replacing that old receipt.
    std::optional<nlohmann::json> retired_fence(const nlohmann::json &request);
    void bind_retired(const nlohmann::json &request,const std::string &before_revision,const nlohmann::json &response);
private:
    std::optional<nlohmann::json> replay_locked(const nlohmann::json &,const std::string &) const;
    void validate(const nlohmann::json &) const;
    void verify();
    void publish(nlohmann::json,const std::optional<PiManagementJournal::Image> &assignment = {});
    nlohmann::json projection(const std::string &,const nlohmann::json &) const;
    std::string self_,source_,revision_,anchor_bytes_;
    nlohmann::json data_;
    std::unique_ptr<PiManagementJournal> journal_;
    std::mutex mutex_;
    bool held_{};
    std::function<void(RetirementWritePoint)> checkpoint_;
};
}
