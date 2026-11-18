// SPDX-License-Identifier: MIT
#pragma once
#include "pi_management_journal.hpp"
#include <functional>
#include <memory>
#include <mutex>
namespace wsprrypi {
enum class PiPairCatalogWritePoint {Prepared,Committed,Published};
// Private namespace storage primitive. Opening requires an already published,
// authoritative catalog; this class never initializes missing history, selects
// another namespace, grants access or changes scheduling. Its caller owns the
// authorization/replay policy and holds the appropriate commit admission lock.
// A separate OS directory lock rejects concurrent owners across processes.
class PiPairCatalogStore {
public:
    struct Authority {std::string path,namespace_id;};
    enum class Checkpoint {Preserve,Running,CleanShutdown};
    PiPairCatalogStore(std::string directory,std::string self,
        std::function<void(PiPairCatalogWritePoint)> checkpoint = {},std::optional<Authority> authority = {});
    ~PiPairCatalogStore();
    PiPairCatalogStore(const PiPairCatalogStore &)=delete;
    PiPairCatalogStore &operator=(const PiPairCatalogStore &)=delete;
    struct Snapshot {PiManagementJournal::Observation catalog,operations;std::optional<PiManagementJournal::Observation> authority,peers{};};
    Snapshot snapshot();
    void publish(const Snapshot &expected,const nlohmann::json &catalog);
    void publish(const Snapshot &expected,const nlohmann::json &catalog,const nlohmann::json &operations,Checkpoint = Checkpoint::Preserve);
    void publish(const Snapshot &expected,const nlohmann::json &catalog,const nlohmann::json &operations,
        const nlohmann::json &peers,Checkpoint = Checkpoint::Preserve,const std::function<void()> &before_commit = {});
    // Live failure recovery only for this writer's exact captured WAL decision
    // or its own fully verified publication. Never reopen changed external
    // files as a new baseline, initialize absence, or adopt an unrelated WAL.
    bool recover_own_failure() noexcept;
private:
    void verify_directory() const;
    void validate_decision();
    void validate_snapshot(const Snapshot &) const;
    Snapshot read();
    struct Lock;
    std::unique_ptr<Lock> lock_;
    std::unique_ptr<PiManagementJournal> journal_;
    std::string directory_,self_;
    std::optional<Snapshot> observed_;
    std::mutex mutex_;
    std::function<void(PiPairCatalogWritePoint)> checkpoint_;
    std::optional<Authority> authority_;
    std::optional<nlohmann::json> owned_decision_;
    std::optional<Snapshot> owned_completed_;
    bool held_{};
};
} // namespace wsprrypi
