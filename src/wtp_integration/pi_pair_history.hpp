// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include "pi_pair_catalog_store.hpp"
#include "pi_pair_initialization.hpp"
#include "owned_state_directory.hpp"
#include "pi_pair_tls.hpp"
#include "pi_pair_retirement.hpp"
#include "pi_pair_mutation_guard.hpp"
#include <memory>
#include <atomic>
namespace wsprrypi {
class LocalBrowserPrincipal;
class LocalPairingReadPrincipal;
class ManagementBrowserSessions;
struct BrowserRequest;
struct PicoHttpResponse;
class PiPairCommitApproval;
class PiPairConnect;
class PiPairUsage;
class PiPairRenewal;
class PiPairServiceLifetime;
class PiPairOwner;
class PiManagedStore;
class PiPairMachineBackend;
class PiPairRemoteController;
struct PiPairCommitResult;
// Authoritative device-owned selection of a single private pair namespace.
// Missing history is not initialized by opening it. Only a positive trusted
// provisioning event can create first history. Access recovery is a separate
// protected operation; it must never scan old namespaces back into authority.
class PiPairHistory {
public:
    PiPairHistory(std::string directory,std::string self,std::function<void(PiPairCatalogWritePoint)> checkpoint = {});
    ~PiPairHistory();
    PiPairHistory(const PiPairHistory &)=delete;
    PiPairHistory &operator=(const PiPairHistory &)=delete;
    using InitializationCheckpoint=std::function<void(bool namespace_stage,OwnedStatePublicationPoint)>;
    static std::unique_ptr<PiPairHistory> initialize(std::string directory,PiPairInitializationProof,
        const InitializationCheckpoint &checkpoint = {});
    bool available() noexcept;
    // Read-only UI precondition hint, never authorization or initialization.
    // A verifiable selector retains its counter even when its catalog is lost;
    // irrecoverable safe records use zero. Unsafe/replaced paths return none.
    std::optional<std::string> recovery_generation() noexcept;
    PiPairCatalogStore::Snapshot snapshot();
    // Trusted recovery capture only. Pins the selected key/history namespace;
    // absent first-use history stays absent, and unavailable history refuses.
    void with_backup_files(const std::function<void(const std::map<std::string,std::string> &)> &);
    void publish(const PiPairCatalogStore::Snapshot &,const nlohmann::json &);
    void publish(const PiPairCatalogStore::Snapshot &,const nlohmann::json &,const nlohmann::json &);
    void publish(const PiPairCatalogStore::Snapshot &,const nlohmann::json &,const nlohmann::json &,const nlohmann::json &peers);
    // Only an actual protected local recover request can replace lost pairing
    // storage. The complete new namespace precedes the atomic selector switch;
    // old namespaces/WALs remain evidence and are never imported as authority.
    // Sessions are retired on publication or an uncertain publication result.
    void recover_access(const BrowserRequest &,const LocalBrowserPrincipal &,ManagementBrowserSessions &,
        const std::function<void(PiPairCatalogWritePoint)> &checkpoint = {});
    PicoHttpResponse remove_access(const BrowserRequest &,const LocalBrowserPrincipal &,ManagementBrowserSessions &,const std::string &boot);
    PicoHttpResponse local_pairing_receipt(const BrowserRequest &,const LocalPairingReadPrincipal &);
    // Incoming management lookup only. Retirement callbacks resolve against
    // the separate outgoing verifier owner. Lookup is strictly read-only: an
    // unauthenticated ClientHello must never acknowledge or erase an epoch.
    std::shared_ptr<const PiPairTlsCredential> management_credential(const PiPairTlsIdentity &);
    // Separate read-only fixed-purpose lookups. Controllers resolve outgoing
    // profiles; targets resolve their incoming active/retained manager. Neither
    // restores ordinary management or acknowledges/erases retained evidence.
    std::shared_ptr<const PiPairTlsCredential> retirement_server_credential(const PiPairTlsIdentity &);
    std::shared_ptr<const PiPairTlsCredential> retirement_client_credential(const PiPairTlsIdentity &);
    bool retirement_finished(const PiPairPrincipal &,bool controller_server);
    std::optional<PiPairRetirementPrincipal> authorize_retirement(const PiPairPrincipal &,const nlohmann::json &);
    std::optional<PiPairMutationGuard> admit_retirement(const PiPairRetirementPrincipal &);
    std::optional<PiPairMutationGuard> admit_retirement_request(const PiPairPrincipal &,const nlohmann::json &);
    std::optional<nlohmann::json> retirement_request(const nlohmann::json &unsigned_request);
    std::optional<nlohmann::json> retirement_response(const nlohmann::json &request,const nlohmann::json &durable_retired_receipt);
    std::optional<PiPairRetiredProof> retired_proof(const PiPairPrincipal &,const nlohmann::json &original_request,const nlohmann::json &response);
    // Sign only an owner's actual retained terminal outcome. No wire handler
    // accepts an arbitrary acknowledgement for signing or grants consumption.
    std::optional<nlohmann::json> resume_acknowledgement(const nlohmann::json &unsigned_ack);
    std::optional<PiPairResumeOutcomeProof> resume_outcome_proof(const PiPairPrincipal &,const nlohmann::json &);
    std::optional<PiPairMutationGuard> admit_resume_outcome(const PiPairResumeOutcomeProof &);
    std::optional<PiPairResumeGrantProof> outgoing_resume_grant(const std::string &profile_id,const PiPairPrincipal &,
        const nlohmann::json &grant,const nlohmann::json &migration,const nlohmann::json &settings);
    std::optional<PiPairMutationGuard> admit_resume_grant(const PiPairResumeGrantProof &);
    std::optional<PiPairResumeTargetProof> incoming_resume_target(const BrowserRequest &,const LocalBrowserPrincipal &);
    std::optional<PiPairResumeTargetProof> incoming_resume_target(const BrowserRequest &,const PiPairPrincipal &);
    std::optional<PiPairMutationGuard> admit_resume_target(const PiPairResumeTargetProof &);
    PiPairCommitResult commit_pair(const PiPairCommitApproval &);
    std::optional<nlohmann::json> pair_operation(const PiPairPrincipal &,const std::string &operation_id);
    // Actual target-side two-phase rotation. Only an authenticated current
    // principal can prepare; a previous epoch can finish its one exact commit.
    // The caller reserves shared mutation admission before invoking this owner.
    std::optional<nlohmann::json> rotate(const PiPairPrincipal &,const nlohmann::json &,ManagementBrowserSessions &);
    // Private controller-side recovery owner. Public requests are projections
    // of durable intent; no code or key is returned by these methods.
    std::shared_ptr<const PiPairTlsCredential> outgoing_credential(const std::string &profile_id,const PiPairTlsIdentity &);
    std::optional<nlohmann::json> prepare_outgoing_rotation(const std::string &profile_id,const std::string &operation_id);
    std::optional<nlohmann::json> outgoing_rotation_request(const std::string &profile_id);
    bool accept_outgoing_rotation(const std::string &profile_id,const PiPairPrincipal &,const nlohmann::json &);
    bool acknowledge_outgoing_rotation(const std::string &profile_id,const PiPairPrincipal &,ManagementBrowserSessions &);
    enum class ManagementAccess {Denied,Current,RotationRecovery};
    // Call with the real channel principal only after successful Finished.
    // This is not mutation admission: dispatch must recheck the current pair,
    // generation, epoch and exact resource under its own commit lock.
    ManagementAccess accept_finished(const PiPairPrincipal &,ManagementBrowserSessions *local_sessions = nullptr);
    // Explicit lifetime checkpoints, never reads/polls or per-connection I/O.
    // Finish only after all management workers have stopped. Destruction alone
    // cannot attest an orderly stop; a dirty restart forces renewal admission.
    // False means an existing owner already holds this accounting lifetime.
    bool begin_service();
    void finish_service();
    // Real Finished observations update RAM only. Outgoing callers invoke this
    // once per exchange, separately from repeated final-admission checks.
    bool record_outgoing_finished(const std::string &profile_id,const PiPairPrincipal &);
    bool renewal_due(const PiPairPrincipal &);
    bool outgoing_renewal_due(const std::string &profile_id);
    std::optional<nlohmann::json> management_authorization(const PiPairPrincipal &);
    // Final current-principal/policy/renewal admission. Hold only through the
    // local transaction or first actual carrier handoff, never a network wait.
    std::optional<PiPairMutationGuard> admit_mutation(const PiPairPrincipal &,bool checkpoint_counters=true);
    std::optional<PiPairMutationGuard> admit_outgoing_mutation(const std::string &profile_id,
        const std::string &profile_generation,const PiPairPrincipal &,bool checkpoint_counters=true);
    // Bounded local maintenance using the actual native transaction owner.
    // Only positively closed revoked incoming scopes qualify; private unknown
    // operations, outgoing recovery records and all recurrence proofs survive.
    std::size_t reclaim_native_retired(PiManagedStore &);
    bool unclean_start() const noexcept{return unclean_start_;}
private:
    friend class PiPairConnect;
    friend class PiPairRenewal;
    friend class PiPairServiceLifetime;
    friend class PiPairOwner;
    friend class PiPairMachineBackend;
    friend class PiPairRemoteController;
    // Bounded private enrollment bookkeeping after a verified read-only USB
    // observation. Never settings/output admission; due keys may still observe.
    bool observe_management(const PiPairPrincipal &,const std::function<void()> &);
    bool observe_outgoing_management(const std::string &profile,const std::string &generation,const PiPairPrincipal &,const std::function<void()> &);
    bool reopen_service_history() noexcept;
    // Caller holds mutex_. A bound, closed/abandoned owner denies new work.
    bool service_permits() const noexcept;
    // Called only by the actual controller exchange owner. The verified
    // exchange, protected request and fresh discovery binding precede staging.
    nlohmann::json stage_outgoing_initial(const BrowserRequest &,const LocalBrowserPrincipal &,
        ManagementBrowserSessions &,const nlohmann::json &discovery,PiPairExchange &);
    std::size_t reclaim_outgoing_retired(const std::string &profile,const PiPairPrincipal &,const nlohmann::json &migration);
    bool outgoing_operation_known(const std::string &operation_id);
    std::optional<nlohmann::json> outgoing_initial(const std::string &operation_id);
    std::optional<nlohmann::json> outgoing_authority_registration(const std::string &profile);
    std::optional<nlohmann::json> stage_authority_registration(const std::string &profile,const nlohmann::json &request);
    std::optional<PiPairMutationGuard> admit_authority_registration(const std::string &profile,const std::string &generation,
        const PiPairPrincipal &,const std::string &operation,bool authenticated_absence,bool checkpoint_counters=true);
    bool accept_authority_registration(const std::string &profile,const std::string &generation,const PiPairPrincipal &,const nlohmann::json &reply,const std::function<void()> &retire_contexts = {});
    std::shared_ptr<const PiPairTlsCredential> initial_credential(const std::string &operation_id,const PiPairTlsIdentity &);
    std::optional<PiPairMutationGuard> admit_initial_commit(const std::string &operation_id);
    std::optional<PiPairMutationGuard> admit_rotation_handoff(const std::string &profile_id,const std::string &profile_generation,
        const std::string &operation_id,const PiPairPrincipal &);
    bool accept_outgoing_initial(const std::string &operation_id,const PiPairPrincipal &,const nlohmann::json &,ManagementBrowserSessions &);
    // Caller owns mutex_. Exact ID/generation lookup only; never credential
    // search/fallback, normal management authority, or an exported verifier.
    std::optional<PiPairSecret> retirement_verifier(const PiPairTlsIdentity &,bool controller_server,const std::string &peer);
    void verify_selection() const;
    void open_directory();
    void load_selection(std::function<void(PiPairCatalogWritePoint)> = {});
    void checkpoint(PiPairCatalogStore::Checkpoint);
    // Caller holds mutex_; recovers only this writer's exact prepared decision.
    bool publish_outgoing(const PiPairCatalogStore::Snapshot &,nlohmann::json catalog,
        const nlohmann::json &operations,nlohmann::json peers);
    void prepare_usage(const PiPairCatalogStore::Snapshot &,nlohmann::json &catalog,nlohmann::json &peers);
    void reconcile_usage(const PiPairCatalogStore::Snapshot &);
    struct Lock;
    std::unique_ptr<Lock> lock_;
    std::unique_ptr<PiPairCatalogStore> store_;
    std::unique_ptr<PiPairUsage> usage_;
    std::string directory_,self_;
    std::optional<PiManagementJournal::Observation> selected_;
    std::mutex mutex_;
    bool unclean_start_{},running_marked_{};
    // RAM-only exclusive service lease, including unavailable-history recovery.
    std::weak_ptr<void> service_owner_;
    std::weak_ptr<std::atomic_bool> service_closed_;
    bool service_bound_{};
};
} // namespace wsprrypi
