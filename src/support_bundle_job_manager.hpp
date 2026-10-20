#pragma once

#include "support_bundle_job_directory_remover.hpp"
#include "support_bundle_private_artifact.hpp"

#include <functional>
#include <filesystem>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

enum class SupportBundleJobState { queued, running, succeeded, failed };
enum class SupportBundlePrivateLifecycle {
    none,
    collecting,
    candidate_ready,
    candidate_downloaded,
    finalized,
    encrypted_downloaded,
    upload_page_opened,
    upload_reported_complete,
};
struct SupportBundlePrivateRequest { SupportBundleContext context; };
struct SupportBundleJobRequest {
    bool probe_i2c = false;
    std::optional<SupportBundlePrivateRequest> private_request;
};
struct SupportBundleJobSnapshot {
    std::string id;
    SupportBundleJobState state = SupportBundleJobState::queued;
    bool probe_i2c_requested = false;
    std::string i2c_probe_status;
    std::string failure_category;
    std::string failure_message;
    bool download_available = false;
    std::string case_id;
    SupportBundlePrivateLifecycle private_lifecycle = SupportBundlePrivateLifecycle::none;
};
enum class SupportBundleDownloadReferenceStatus {
    available,
    malformed_or_unknown_id,
    not_ready,
    no_download,
};
enum class SupportBundleDownloadDeletionStatus {
    removed,
    already_removed,
    malformed_or_unknown_id,
    not_terminal,
    no_retained_download,
    cleanup_failed,
};
struct SupportBundleDownloadDeletionResult {
    SupportBundleDownloadDeletionStatus status =
        SupportBundleDownloadDeletionStatus::malformed_or_unknown_id;

    [[nodiscard]] bool removed() const noexcept {
        return status == SupportBundleDownloadDeletionStatus::removed;
    }
};
struct SupportBundleDownloadReference {
    SupportBundleDownloadReferenceStatus status =
        SupportBundleDownloadReferenceStatus::malformed_or_unknown_id;
    std::filesystem::path archive_path;
    std::string archive_basename;
    std::filesystem::path checksum_path;
    std::string checksum_basename;
    std::string expected_sha256;
};
struct SupportBundleExecutionResult { bool succeeded = false; std::string failure_category; std::string failure_message; };
struct SupportBundleExecutionContext {
    bool probe_i2c = false;
    std::filesystem::path job_directory;
    std::string case_id;
    std::optional<SupportBundleContext> support_context;
};
enum class SupportBundleCandidateDownloadStatus {
    marked,
    already_marked,
    malformed_or_unknown_id,
    unavailable,
};
enum class SupportBundleUploadTransitionStatus {
    transitioned,
    already_transitioned,
    malformed_or_unknown_id,
    unavailable,
};
enum class SupportBundleFinalizationStatus {
    finalized,
    already_finalized,
    malformed_or_unknown_id,
    not_private,
    not_ready,
    download_required,
    artifact_invalid,
};
struct SupportBundleFinalizationOutcome {
    SupportBundleFinalizationStatus status =
        SupportBundleFinalizationStatus::malformed_or_unknown_id;
    std::optional<SupportBundleJobSnapshot> snapshot;
};
enum class SupportBundleEncryptionStatus {
    encrypted, already_encrypted, malformed_or_unknown_id, not_finalized,
    key_mismatch, encryption_failed, receipt_failed
};
struct SupportBundleEncryptionOutcome {
    SupportBundleEncryptionStatus status = SupportBundleEncryptionStatus::malformed_or_unknown_id;
    std::optional<SupportBundleJobSnapshot> snapshot;
    SupportBundleEncryptionFailure primitive_failure = SupportBundleEncryptionFailure::none;
};
class SupportBundleJobExecutor {
public:
    virtual ~SupportBundleJobExecutor() = default;
    virtual SupportBundleExecutionResult run(const SupportBundleExecutionContext &context) = 0;
    virtual void request_stop() noexcept = 0;
};
class SupportBundleJobManager {
public:
    inline static constexpr std::chrono::hours kProductionRetention = std::chrono::hours(24);
    inline static constexpr std::chrono::minutes kProductionRetryDelay = std::chrono::minutes(5);
    using IdGenerator = std::function<std::string()>;
    using CaseIdGenerator = std::function<std::optional<std::string>()>;
    using JobDirectoryRemover = std::function<SupportBundleJobDirectoryRemovalResult(
        const std::filesystem::path &, const std::string &)>;
    SupportBundleJobManager(std::shared_ptr<SupportBundleJobExecutor> executor,
                            IdGenerator ids,
                            std::filesystem::path storage_root,
                            JobDirectoryRemover remover = remove_support_bundle_job_directory,
                            std::chrono::milliseconds retention = kProductionRetention,
                            std::chrono::milliseconds retry_delay = kProductionRetryDelay,
                            CaseIdGenerator case_ids = {});
    ~SupportBundleJobManager();
    SupportBundleJobManager(const SupportBundleJobManager &) = delete;
    std::optional<SupportBundleJobSnapshot> create(SupportBundleJobRequest request, std::string &error);
    std::optional<SupportBundleJobSnapshot> lookup(const std::string &id) const;
    SupportBundleDownloadReference download_reference(const std::string &id) const;
    SupportBundleCandidateDownloadStatus mark_candidate_downloaded(const std::string &id,
                                                                    std::uint64_t size);
    SupportBundleFinalizationOutcome finalize_candidate(const std::string &id);
    SupportBundleEncryptionOutcome encrypt_candidate(const std::string &id,
                                                      const std::string &key_id,
                                                      const std::string &recipient,
                                                      const std::filesystem::path &executable = "/usr/bin/age");
    SupportBundleDownloadReference encrypted_reference(const std::string &id) const;
    SupportBundleCandidateDownloadStatus mark_encrypted_downloaded(const std::string &id,
                                                                    std::uint64_t size);
    SupportBundleDownloadReference receipt_reference(const std::string &id) const;
    SupportBundleUploadTransitionStatus mark_upload_page_opened(const std::string &id);
    SupportBundleUploadTransitionStatus report_upload_complete(const std::string &id);
    SupportBundleDownloadDeletionResult delete_download(const std::string &id);
    void shutdown();
    static bool valid_id(const std::string &id);
private:
    void run(std::string id, bool probe_i2c, std::filesystem::path job_directory,
             std::string expected_case_id,
             std::optional<SupportBundleContext> support_context);
    void expiration_loop();
    void clear_current_download_locked(const std::string &id);
    void cancel_expiration_locked(const std::string &id);
    void remove_unsuccessful_job_directory(const std::string &id,
                                           const std::filesystem::path &job_directory);
    std::shared_ptr<SupportBundleJobExecutor> executor_;
    IdGenerator ids_;
    CaseIdGenerator case_ids_;
    std::filesystem::path storage_root_;
    JobDirectoryRemover remover_;
    std::chrono::milliseconds retention_;
    std::chrono::milliseconds retry_delay_;
    bool storage_ready_ = false;
    mutable std::mutex mutex_;
    std::optional<SupportBundleJobSnapshot> job_;
    std::filesystem::path job_directory_;
    std::string validated_archive_filename_;
    std::string validated_checksum_filename_;
    std::string validated_sha256_;
    std::optional<std::uint64_t> downloaded_archive_size_;
    bool download_removed_ = false;
    std::optional<FinalizedSupportBundle> finalized_bundle_;
    std::optional<SupportBundleEncryptedArtifact> encrypted_artifact_;
    std::optional<SupportBundleReceiptResult> receipt_artifact_;
    std::optional<std::string> issue_url_;
    std::thread worker_;
    struct ExpirationEntry {
        std::string id;
        std::filesystem::path job_directory;
        std::chrono::steady_clock::time_point deadline;
    };
    std::vector<ExpirationEntry> expirations_;
    std::condition_variable expiration_cv_;
    std::thread expiration_worker_;
    bool shutting_down_ = false;
};
