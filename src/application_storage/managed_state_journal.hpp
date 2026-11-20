// SPDX-License-Identifier: MIT
#pragma once
#include "sqlite_state_store.hpp"
#include "wtp_integration/pi_management_journal.hpp"

namespace wsprrypi {
// The native settings owner's persistence adapter. File mode remains only for
// its existing one-time import/recovery source; database mode stages a bounded
// complete after-image and commits it in one SQLite transaction. Pairing and
// execution-progress owners continue using their independent journals.
class ManagedStateJournal {
public:
    using Image=PiManagementJournal::Image;
    using Observation=PiManagementJournal::Observation;
    ManagedStateJournal(std::string directory,std::string self,std::map<std::string,std::string> destinations);
    explicit ManagedStateJournal(std::shared_ptr<SqliteStateStore>);
    ManagedStateJournal(ManagedStateJournal &&) noexcept = default;
    ManagedStateJournal &operator=(ManagedStateJournal &&) noexcept = default;
    bool database_backed() const noexcept { return bool(database_); }
    std::optional<Observation> inspect(const std::string &) const;
    void prepare(const std::vector<Image> &,const std::optional<nlohmann::json> &replace_rollback={});
    void commit();
    bool recover(bool retain_prepared=false);
    std::optional<nlohmann::json> decision();
    void verify_prepared(const nlohmann::json &);
    void write_committed_ini(const std::string &);
private:
    std::unique_ptr<PiManagementJournal> files_;
    std::shared_ptr<SqliteStateStore> database_;
    std::optional<StateSnapshot> before_;
    std::vector<StateChange> changes_;
    std::optional<nlohmann::json> decision_;
    static StateRecord record_kind(const std::string &);
};
} // namespace wsprrypi
