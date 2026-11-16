// SPDX-License-Identifier: MIT
#pragma once
#include "pi_management_anchor.hpp"
#include "pi_management_journal.hpp"
#include <functional>
#include <cstdint>
#include <mutex>
namespace wsprrypi {
// Parent application owner of the complete INI/settings/receipt transaction.
// Authorization, binding, capability and runtime admission remain the service's
// responsibility. This class never starts work or changes execution ownership.
class PiManagedStore {
public:
    struct Operation {
        std::string principal,authorization_generation,route;
        nlohmann::json request;
    };
    PiManagedStore(std::string ini,std::string self,PiManagementAnchor);
    // Trusted production owner selects the fixed device-owned namespace before
    // any recovery. Standalone fixtures retain their isolated default paths.
    void bind_recurrence_storage(const std::string &path,bool previously_initialized);
    void validate_initial_source();
    // Before parsing an existing INI: recover the single durable decision and
    // validate all existing authority. An absent history stays uninitialized.
    void recover();
    // Explicit pre-parser boundary for absent INI or untouched installed stock.
    // Existing managed/legacy evidence is never converted into a clean install.
    void prepare_installation(bool prior_output_history=false);
    bool installation_pending();
    bool migration_pending() const;
    // Explicit startup after the owning configuration validator succeeds.
    // Never call from a GET or a failed physical-backend fallback.
    void initialize(const nlohmann::json &internal);
    // First explicit recovery/migration write: create history and publish its
    // complete after-image in the same decision, never against the old INI.
    void initialize_persisted(const nlohmann::json &internal,const std::function<void()> &writer);
    void initialize_stock(const nlohmann::json &internal,const std::string &ini_bytes);
    bool initialized() const;
    const std::string &device_id() const noexcept;
    std::uint64_t context_epoch() const;
    nlohmann::json settings();
    // Private target-owned record. Reads never infer initialization from absence.
    nlohmann::json recurrence_authority();
    // Caller holds native configuration/admission reservation and has verified
    // complete controller inventory and output/pair reconciliation. No RF here.
    nlohmann::json rollback_configuration();
    void persist_migration(const nlohmann::json &internal,const std::function<void()> &writer,const Operation &,bool rollback,
        const std::optional<nlohmann::json> &pending_job = {});
    // Exact retained intent, after the live owner verifies all recorded work
    // reconciled. Does not initialize an operation or write the unchanged INI.
    void complete_migration();
    // Monitor reload of our own verified INI is an observation, not a save.
    void validate_snapshot(const nlohmann::json &internal);
    std::optional<nlohmann::json> replay(const Operation &);
    std::optional<nlohmann::json> receipt(const std::string &principal,
        const std::string &authorization_generation,const std::string &operation);
    // Called under the application's configuration/admission lock, after its
    // semantic validator, before runtime publication. Invokes its INI writer
    // once, inside the retained committed WAL, and publishes only after recovery.
    void persist(const nlohmann::json &internal,const std::function<void()> &writer,
        const std::optional<Operation> &operation = {});
    // Exact Reset/Repair after-image, validated by the owning parent parser.
    // The journal performs the final replacement under its complete decision.
    void persist_stock(const nlohmann::json &internal,const std::string &ini_bytes);
    // Trusted private-setting writer: parent validates the sole permitted byte
    // change, and public configuration/scheduling must stay exactly unchanged.
    void persist_private(const nlohmann::json &internal,const std::string &before,const std::string &after);
    // Durable acceptance under the service's exact observed-output reservation.
    // Changes only the existing saved Transmit permission; schedule enablement
    // stays intact. Receipt acceptance does not prove output inactivity.
    void persist_stop(const nlohmann::json &internal,const std::function<void()> &writer,const Operation &);
    // Component publication sink; valid only for this store's live committed
    // transaction and its exact bounded INI after-image. No standalone save.
    void write_committed_ini(const std::string &bytes);
    // Strong recovery precondition includes the fixed INI observation (including
    // absence and equal-byte replacement), managed generation, and context epoch.
    // No private path, contents, or metadata is returned to the browser.
    std::string recovery_revision();
    // The caller owns authorization, semantic validation, and preservation of
    // private references. Save/action receipt and the full after-image share one
    // decision, including the first write from an invalid or absent initial INI.
    void persist_recovery(const nlohmann::json &internal,const std::function<void()> &writer,
        const Operation &,const std::optional<std::string> &stock_bytes = {});
private:
    void check();
    void check_initial_rollback();
    void load();
    void initialize_transaction(const nlohmann::json &,const std::string &,const std::function<void()> &,
        const std::optional<Operation> &operation = {},const std::optional<std::string> &source_revision = {});
    void publish(const nlohmann::json &candidate,const std::string &ini,
        const std::function<void()> &writer,const std::optional<Operation> &,bool authority_transition=false,
        bool pending=false,bool write_ini=true,const std::string &complete_operation={});
    std::string ini_,self_,directory_,history_,source_binding_;
    PiManagementAnchor anchor_;
    PiManagementJournal journal_;
    mutable std::recursive_mutex mutex_;
    bool initialized_{},held_{true};
    bool recurrence_bound_{},require_recurrence_{};
    std::uint64_t context_epoch_{1};
    nlohmann::json settings_,operations_,recurrence_,legacy_,bootstrap_;
    std::optional<nlohmann::json> initial_rollback_;
    std::map<std::string,std::optional<PiManagementJournal::Observation>> initial_observations_;
    std::map<std::string,PiManagementJournal::Observation> observations_;
};
} // namespace wsprrypi
