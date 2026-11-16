// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "pi_management_journal.hpp"
#include <memory>
#include <optional>
#include <functional>
#include <mutex>
#include <stdexcept>
namespace wsprrypi {
// Device-owned fences, independent of the assignment file's name and flags.
// The Fleet owner serializes intent with its dispatch permits. This class never
// operates a carrier or infers that unknown output is idle.
class ControllerRetirementConflict final : public std::runtime_error {
public: ControllerRetirementConflict():std::runtime_error("controller_retirement_conflict"){}
};
enum class RetirementWritePoint {Prepared,Committed,Published,EstablishmentPrepared,EstablishmentCommitted,EstablishmentPublished};
class ControllerRetirementBook {
public:
    ControllerRetirementBook(const std::string &directory,const std::string &source,
        const std::string &self,const nlohmann::json &assignments,
        std::function<void(RetirementWritePoint)> checkpoint = {});
    nlohmann::json read(const std::string &incarnation,const nlohmann::json &assignments);
    nlohmann::json retire(const nlohmann::json &intent,const std::string &revision,
        const std::string &incarnation,const nlohmann::json &assignments,const nlohmann::json &observation = nullptr);
    std::optional<nlohmann::json> replay(const nlohmann::json &intent,const std::string &revision);
    bool permits(const std::string &target);
    bool pending(const std::string &target);
    void confirm_idle(const std::string &target,const nlohmann::json &observation);
private:
    std::optional<nlohmann::json> replay_locked(const nlohmann::json &,const std::string &) const;
    void validate(const nlohmann::json &) const;
    void verify();
    void publish(nlohmann::json);
    nlohmann::json projection(const std::string &,const nlohmann::json &) const;
    std::string self_,source_,revision_,anchor_bytes_;
    nlohmann::json data_;
    std::unique_ptr<PiManagementJournal> journal_;
    std::mutex mutex_;
    bool held_{};
    std::function<void(RetirementWritePoint)> checkpoint_;
};
}
