#include "support_bundle_job_manager.hpp"

#include <ctime>
#include "support_bundle_result_validator.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool safe_direct_child(const std::filesystem::path &job_directory,
                       const std::string &basename,
                       std::filesystem::path &path) {
    if (basename.empty() || basename.find_first_of("/\\") != std::string::npos ||
        basename.find("..") != std::string::npos) {
        return false;
    }
    path = (job_directory / basename).lexically_normal();
    return path.parent_path() == job_directory.lexically_normal() &&
           path.filename() == basename;
}

bool safe_download_references(const std::filesystem::path &storage_root,
                              const std::filesystem::path &job_directory,
                              const std::string &job_id,
                              const std::string &archive_filename,
                              const std::string &checksum_filename,
                              std::filesystem::path &archive_path,
                              std::filesystem::path &checksum_path) {
    if (job_directory.lexically_normal().parent_path() != storage_root ||
        job_directory.filename() != job_id) {
        return false;
    }
    return safe_direct_child(job_directory, archive_filename, archive_path) &&
           safe_direct_child(job_directory, checksum_filename, checksum_path);
}

}

SupportBundleJobManager::SupportBundleJobManager(std::shared_ptr<SupportBundleJobExecutor> executor,
                                                 IdGenerator ids,
                                                 std::filesystem::path storage_root,
                                                 JobDirectoryRemover remover,
                                                 std::chrono::milliseconds retention,
                                                 std::chrono::milliseconds retry_delay,
                                                 CaseIdGenerator case_ids)
    : executor_(std::move(executor)), ids_(std::move(ids)), case_ids_(std::move(case_ids)),
      remover_(std::move(remover)),
      retention_(std::max(retention, std::chrono::milliseconds::zero())),
      retry_delay_(std::max(retry_delay, std::chrono::milliseconds(1))) {
    if (!remover_) {
        remover_ = remove_support_bundle_job_directory;
    }
    struct stat info{}; if (!storage_root.is_absolute() || lstat(storage_root.c_str(), &info) != 0 || S_ISLNK(info.st_mode) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0777) != 0700) return;
    std::error_code error; storage_root_ = std::filesystem::canonical(storage_root, error); storage_ready_ = !error;
    expiration_worker_ = std::thread(&SupportBundleJobManager::expiration_loop, this);
}
SupportBundleJobManager::~SupportBundleJobManager() { shutdown(); }
bool SupportBundleJobManager::valid_id(const std::string &id) { return id.size() == 32 && std::all_of(id.begin(), id.end(), [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }); }
std::optional<SupportBundleJobSnapshot> SupportBundleJobManager::create(SupportBundleJobRequest request, std::string &error) {
    std::lock_guard lock(mutex_);
    error.clear();
    if (shutting_down_) { error = "shutting_down"; return std::nullopt; }
    if (!storage_ready_) { error = "storage_unavailable"; return std::nullopt; }
    issue_url_.reset();
    if (request.private_request && request.private_request->context.kind == SupportBundleContextKind::existing_github_issue)
        issue_url_ = request.private_request->context.issue_url;
    if (job_ && (job_->state == SupportBundleJobState::queued || job_->state == SupportBundleJobState::running)) { error = "job_active"; return std::nullopt; }
    std::string case_id;
    if (request.private_request) {
        if (!valid_support_bundle_context(request.private_request->context)) {
            error = "invalid_support_context"; return std::nullopt;
        }
        if (!case_ids_) { error = "case_id_generation_failed"; return std::nullopt; }
        try {
            const auto generated = case_ids_();
            if (!generated || !valid_support_bundle_case_id(*generated)) {
                error = "case_id_generation_failed"; return std::nullopt;
            }
            case_id = *generated;
        } catch (...) { error = "case_id_generation_failed"; return std::nullopt; }
    }
    std::string id;
    try { id = ids_(); } catch (...) { error = "id_generation_failed"; return std::nullopt; }
    if (!valid_id(id)) { error = "invalid_job_id"; return std::nullopt; }
    const auto directory = storage_root_ / id;
    struct stat existing{}; if (lstat(directory.c_str(), &existing) == 0 || mkdir(directory.c_str(), 0700) != 0) { error = "job_setup_failed"; return std::nullopt; }
    if (lstat(directory.c_str(), &existing) != 0 || !S_ISDIR(existing.st_mode) || S_ISLNK(existing.st_mode) || (existing.st_mode & 0777) != 0700) {
        remove_unsuccessful_job_directory(id, directory);
        error = "job_setup_failed";
        return std::nullopt;
    }
    if (worker_.joinable()) worker_.join();
    job_ = SupportBundleJobSnapshot{id, SupportBundleJobState::queued, request.probe_i2c,
                                    "", "", "", false, case_id,
                                    request.private_request
                                        ? SupportBundlePrivateLifecycle::collecting
                                        : SupportBundlePrivateLifecycle::none};
    job_directory_ = directory;
    validated_archive_filename_.clear();
    validated_checksum_filename_.clear();
    validated_sha256_.clear();
    downloaded_archive_size_.reset();
    download_removed_ = false;
    finalized_bundle_.reset();
    const auto support_context = request.private_request
                                     ? std::optional(request.private_request->context)
                                     : std::nullopt;
    try { worker_ = std::thread(&SupportBundleJobManager::run, this, id,
                                request.probe_i2c, directory, case_id,
                                support_context); }
    catch (const std::system_error &) {
        job_.reset();
        job_directory_.clear();
        remove_unsuccessful_job_directory(id, directory);
        error = "worker_launch_failed";
        return std::nullopt;
    }
    return job_;
}
std::optional<SupportBundleJobSnapshot> SupportBundleJobManager::lookup(const std::string &id) const { std::lock_guard lock(mutex_); if (!valid_id(id) || !job_ || job_->id != id) return std::nullopt; return job_; }
SupportBundleDownloadReference SupportBundleJobManager::download_reference(const std::string &id) const {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id) {
        return {SupportBundleDownloadReferenceStatus::malformed_or_unknown_id, {}};
    }
    if (job_->state == SupportBundleJobState::queued || job_->state == SupportBundleJobState::running) {
        return {SupportBundleDownloadReferenceStatus::not_ready, {}};
    }
    std::filesystem::path archive_path;
    std::filesystem::path checksum_path;
    if (job_->state != SupportBundleJobState::succeeded || !job_->download_available ||
        !safe_download_references(storage_root_, job_directory_, id,
                                  validated_archive_filename_, validated_checksum_filename_,
                                  archive_path, checksum_path)) {
        return {SupportBundleDownloadReferenceStatus::no_download, {}, {}, {}, {}, {}};
    }
    return {SupportBundleDownloadReferenceStatus::available,
            std::move(archive_path),
            validated_archive_filename_,
            std::move(checksum_path),
            validated_checksum_filename_,
            validated_sha256_};
}

SupportBundleDownloadDeletionResult SupportBundleJobManager::delete_download(
    const std::string &id) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id) {
        return {SupportBundleDownloadDeletionStatus::malformed_or_unknown_id};
    }
    if (job_->state == SupportBundleJobState::queued || job_->state == SupportBundleJobState::running) {
        return {SupportBundleDownloadDeletionStatus::not_terminal};
    }
    if (job_->state != SupportBundleJobState::succeeded) {
        return {SupportBundleDownloadDeletionStatus::no_retained_download};
    }
    if (download_removed_) {
        return {SupportBundleDownloadDeletionStatus::already_removed};
    }
    if (!job_->download_available ||
        validated_archive_filename_.empty() || validated_checksum_filename_.empty() ||
        validated_sha256_.empty()) {
        return {SupportBundleDownloadDeletionStatus::no_retained_download};
    }
    std::filesystem::path archive_path;
    std::filesystem::path checksum_path;
    if (!safe_download_references(storage_root_, job_directory_, id,
                                  validated_archive_filename_, validated_checksum_filename_,
                                  archive_path, checksum_path)) {
        return {SupportBundleDownloadDeletionStatus::no_retained_download};
    }

    SupportBundleJobDirectoryRemovalResult cleanup_result;
    try {
        cleanup_result = remover_(storage_root_, id);
    } catch (...) {
        return {SupportBundleDownloadDeletionStatus::cleanup_failed};
    }
    if (!cleanup_result.removed()) {
        return {SupportBundleDownloadDeletionStatus::cleanup_failed};
    }

    clear_current_download_locked(id);
    cancel_expiration_locked(id);
    expiration_cv_.notify_all();
    return {SupportBundleDownloadDeletionStatus::removed};
}
void SupportBundleJobManager::run(std::string id, bool probe_i2c,
                                  std::filesystem::path job_directory,
                                  std::string expected_case_id,
                                  std::optional<SupportBundleContext> support_context) {
    { std::lock_guard lock(mutex_); if (!job_ || job_->id != id) return; job_->state = SupportBundleJobState::running; }
    SupportBundleExecutionResult result;
    try { result = executor_->run({probe_i2c, job_directory, expected_case_id,
                                   std::move(support_context)}); }
    catch (...) { result = {false, "executor_exception", "Support collection failed."}; }
    SupportBundleResultValidation validation;
    if (result.succeeded) validation = validate_support_bundle_result(job_directory, probe_i2c);
    bool remove_job_directory = false;
    bool schedule_expiration = false;
    {
        std::lock_guard lock(mutex_); if (!job_ || job_->id != id || job_->state == SupportBundleJobState::failed) return;
        if (shutting_down_) result = {false, "shutting_down", "Support collection stopped."};
        if (result.succeeded && !validation.valid) {
            result.succeeded = false;
            switch (validation.failure) {
            case SupportBundleResultFailure::missing: result.failure_category = "result_missing"; break;
            case SupportBundleResultFailure::ambiguous: result.failure_category = "result_ambiguous"; break;
            case SupportBundleResultFailure::unsafe_file: result.failure_category = "result_unsafe"; break;
            case SupportBundleResultFailure::oversized: result.failure_category = "result_oversized"; break;
            case SupportBundleResultFailure::inconsistent: result.failure_category = "result_inconsistent"; break;
            default: result.failure_category = "result_invalid"; break;
            }
        }
        if (result.succeeded &&
            ((expected_case_id.empty() && !validation.case_id.empty()) ||
             (!expected_case_id.empty() &&
              (validation.case_id != expected_case_id || !validation.manifest_included)))) {
            result.succeeded = false;
            result.failure_category = "result_inconsistent";
        }
        std::filesystem::path archive_path;
        std::filesystem::path checksum_path;
        if (result.succeeded &&
            !safe_download_references(storage_root_, job_directory, id,
                                      validation.archive_filename, validation.checksum_filename,
                                      archive_path, checksum_path)) {
            result.succeeded = false;
            result.failure_category = "result_invalid";
        }
        job_->state = result.succeeded ? SupportBundleJobState::succeeded : SupportBundleJobState::failed;
        job_->i2c_probe_status = result.succeeded ? validation.i2c_probe_status : "";
        job_->download_available = result.succeeded;
        job_->case_id = result.succeeded ? validation.case_id : "";
        job_->private_lifecycle = result.succeeded && validation.manifest_included
                                      ? SupportBundlePrivateLifecycle::candidate_ready
                                      : SupportBundlePrivateLifecycle::none;
        validated_archive_filename_ = result.succeeded ? validation.archive_filename : "";
        validated_checksum_filename_ = result.succeeded ? validation.checksum_filename : "";
        validated_sha256_ = result.succeeded ? validation.sha256 : "";
        download_removed_ = false;
        job_->failure_category = result.succeeded ? "" : (result.failure_category.starts_with("result_") || result.failure_category == "shutting_down" || result.failure_category == "executor_exception" ? result.failure_category : "collector_failed");
        job_->failure_message = result.succeeded ? "" : (job_->failure_category == "shutting_down" ? "Support collection stopped." : "Support collection failed.");
        remove_job_directory = !result.succeeded;
        if (result.succeeded && !shutting_down_) {
            expirations_.push_back({id, job_directory,
                                    std::chrono::steady_clock::now() + retention_});
            schedule_expiration = true;
        }
    }
    if (schedule_expiration) {
        expiration_cv_.notify_all();
    }
    if (remove_job_directory) {
        remove_unsuccessful_job_directory(id, job_directory);
    }
}

SupportBundleCandidateDownloadStatus SupportBundleJobManager::mark_candidate_downloaded(
    const std::string &id, std::uint64_t size) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id) {
        return SupportBundleCandidateDownloadStatus::malformed_or_unknown_id;
    }
    if (job_->private_lifecycle == SupportBundlePrivateLifecycle::candidate_downloaded ||
        job_->private_lifecycle == SupportBundlePrivateLifecycle::finalized) {
        return SupportBundleCandidateDownloadStatus::already_marked;
    }
    if (job_->state != SupportBundleJobState::succeeded ||
        job_->private_lifecycle != SupportBundlePrivateLifecycle::candidate_ready ||
        !job_->download_available || size == 0) {
        return SupportBundleCandidateDownloadStatus::unavailable;
    }
    downloaded_archive_size_ = size;
    job_->private_lifecycle = SupportBundlePrivateLifecycle::candidate_downloaded;
    return SupportBundleCandidateDownloadStatus::marked;
}

SupportBundleFinalizationOutcome SupportBundleJobManager::finalize_candidate(
    const std::string &id) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id) {
        return {SupportBundleFinalizationStatus::malformed_or_unknown_id, std::nullopt};
    }
    if (finalized_bundle_ && finalized_bundle_->valid() &&
        (job_->private_lifecycle == SupportBundlePrivateLifecycle::finalized ||
         job_->private_lifecycle == SupportBundlePrivateLifecycle::encrypted_downloaded ||
         job_->private_lifecycle == SupportBundlePrivateLifecycle::upload_page_opened ||
         job_->private_lifecycle == SupportBundlePrivateLifecycle::upload_reported_complete)) {
        return {SupportBundleFinalizationStatus::already_finalized, job_};
    }
    if (job_->case_id.empty() || job_->private_lifecycle == SupportBundlePrivateLifecycle::none) {
        return {SupportBundleFinalizationStatus::not_private, job_};
    }
    if (job_->state != SupportBundleJobState::succeeded || !job_->download_available) {
        return {SupportBundleFinalizationStatus::not_ready, job_};
    }
    if (job_->private_lifecycle != SupportBundlePrivateLifecycle::candidate_downloaded) {
        return {SupportBundleFinalizationStatus::download_required, job_};
    }
    if (!downloaded_archive_size_) {
        return {SupportBundleFinalizationStatus::artifact_invalid, job_};
    }
    std::filesystem::path archive_path;
    std::filesystem::path checksum_path;
    if (!safe_download_references(storage_root_, job_directory_, id,
                                  validated_archive_filename_,
                                  validated_checksum_filename_, archive_path,
                                  checksum_path)) {
        return {SupportBundleFinalizationStatus::artifact_invalid, job_};
    }
    struct stat archive_info {};
    if (lstat(archive_path.c_str(), &archive_info) != 0 ||
        !S_ISREG(archive_info.st_mode) || S_ISLNK(archive_info.st_mode) ||
        archive_info.st_uid != geteuid() || (archive_info.st_mode & 0777) != 0600 ||
        archive_info.st_size < 0 ||
        static_cast<std::uint64_t>(archive_info.st_size) != *downloaded_archive_size_) {
        return {SupportBundleFinalizationStatus::artifact_invalid, job_};
    }
    auto finalized = finalize_support_bundle(archive_path, validated_sha256_,
                                              128ULL * 1024ULL * 1024ULL);
    if (!finalized.finalized()) {
        return {SupportBundleFinalizationStatus::artifact_invalid, job_};
    }
    finalized_bundle_ = std::move(finalized.bundle);
    job_->private_lifecycle = SupportBundlePrivateLifecycle::finalized;
    return {SupportBundleFinalizationStatus::finalized, job_};
}

namespace {
std::string receipt_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (gmtime_r(&now, &utc) == nullptr) return {};
    char value[21]{};
    return std::strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", &utc) == 20
        ? std::string(value) : std::string{};
}
}

SupportBundleEncryptionOutcome SupportBundleJobManager::encrypt_candidate(
    const std::string &id, const std::string &key_id, const std::string &recipient,
    const std::filesystem::path &executable) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id)
        return {SupportBundleEncryptionStatus::malformed_or_unknown_id, std::nullopt};
    if (encrypted_artifact_)
        return {encrypted_artifact_->key_id == key_id ? SupportBundleEncryptionStatus::already_encrypted
                                                       : SupportBundleEncryptionStatus::key_mismatch, job_};
    if (!finalized_bundle_ || !finalized_bundle_->valid() ||
        job_->private_lifecycle != SupportBundlePrivateLifecycle::finalized)
        return {SupportBundleEncryptionStatus::not_finalized, job_};
    SupportBundleEncryptionRequest request{&*finalized_bundle_, job_directory_, job_->case_id,
                                            id, recipient, key_id};
    request.executable = executable;
    auto result = encrypt_support_bundle(request);
    if (!result.encrypted())
        return {SupportBundleEncryptionStatus::encryption_failed, job_, result.failure};
    encrypted_artifact_ = std::move(result.artifact);
    return {SupportBundleEncryptionStatus::encrypted, job_};
}

SupportBundleDownloadReference SupportBundleJobManager::encrypted_reference(const std::string &id) const {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id)
        return {SupportBundleDownloadReferenceStatus::malformed_or_unknown_id};
    if (!encrypted_artifact_) return {SupportBundleDownloadReferenceStatus::not_ready};
    return {SupportBundleDownloadReferenceStatus::available, encrypted_artifact_->path,
            encrypted_artifact_->basename, {}, {}, encrypted_artifact_->sha256};
}

SupportBundleCandidateDownloadStatus SupportBundleJobManager::mark_encrypted_downloaded(
    const std::string &id, std::uint64_t size) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id)
        return SupportBundleCandidateDownloadStatus::malformed_or_unknown_id;
    if (receipt_artifact_ && receipt_artifact_->written())
        return SupportBundleCandidateDownloadStatus::already_marked;
    if (!encrypted_artifact_ || encrypted_artifact_->size != size || !finalized_bundle_)
        return SupportBundleCandidateDownloadStatus::unavailable;
    auto downloaded = finalize_support_bundle(encrypted_artifact_->path,
                                               encrypted_artifact_->sha256,
                                               encrypted_artifact_->size);
    if (!downloaded.finalized() || downloaded.bundle.size() != encrypted_artifact_->size)
        return SupportBundleCandidateDownloadStatus::unavailable;
    const std::string created = receipt_timestamp();
    if (created.empty()) return SupportBundleCandidateDownloadStatus::unavailable;
    SupportBundleReceipt receipt;
    receipt.case_id = job_->case_id; receipt.artifact_id = id; receipt.created_at_utc = created;
    receipt.archive_filename = finalized_bundle_->basename();
    receipt.archive_size = finalized_bundle_->size(); receipt.archive_sha256 = finalized_bundle_->sha256();
    receipt.encrypted_filename = encrypted_artifact_->basename;
    receipt.encrypted_size = encrypted_artifact_->size; receipt.encrypted_sha256 = encrypted_artifact_->sha256;
    receipt.bundle_encryption_key_id = encrypted_artifact_->key_id; receipt.issue_url = issue_url_;
    auto result = write_support_bundle_receipt(job_directory_, receipt);
    if (!result.written()) return SupportBundleCandidateDownloadStatus::unavailable;
    receipt_artifact_ = std::move(result);
    job_->private_lifecycle = SupportBundlePrivateLifecycle::encrypted_downloaded;
    return SupportBundleCandidateDownloadStatus::marked;
}

SupportBundleDownloadReference SupportBundleJobManager::receipt_reference(const std::string &id) const {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id)
        return {SupportBundleDownloadReferenceStatus::malformed_or_unknown_id};
    if (!receipt_artifact_ || !receipt_artifact_->written())
        return {SupportBundleDownloadReferenceStatus::not_ready};
    return {SupportBundleDownloadReferenceStatus::available, receipt_artifact_->path,
            receipt_artifact_->basename, {}, {}, {}};
}

SupportBundleUploadTransitionStatus SupportBundleJobManager::mark_upload_page_opened(
    const std::string &id) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id)
        return SupportBundleUploadTransitionStatus::malformed_or_unknown_id;
    if (job_->private_lifecycle == SupportBundlePrivateLifecycle::upload_page_opened ||
        job_->private_lifecycle == SupportBundlePrivateLifecycle::upload_reported_complete)
        return SupportBundleUploadTransitionStatus::already_transitioned;
    if (job_->private_lifecycle != SupportBundlePrivateLifecycle::encrypted_downloaded ||
        !receipt_artifact_ || !receipt_artifact_->written())
        return SupportBundleUploadTransitionStatus::unavailable;
    job_->private_lifecycle = SupportBundlePrivateLifecycle::upload_page_opened;
    return SupportBundleUploadTransitionStatus::transitioned;
}

SupportBundleUploadTransitionStatus SupportBundleJobManager::report_upload_complete(
    const std::string &id) {
    std::lock_guard lock(mutex_);
    if (!valid_id(id) || !job_ || job_->id != id)
        return SupportBundleUploadTransitionStatus::malformed_or_unknown_id;
    if (job_->private_lifecycle == SupportBundlePrivateLifecycle::upload_reported_complete)
        return SupportBundleUploadTransitionStatus::already_transitioned;
    if (job_->private_lifecycle != SupportBundlePrivateLifecycle::upload_page_opened)
        return SupportBundleUploadTransitionStatus::unavailable;
    job_->private_lifecycle = SupportBundlePrivateLifecycle::upload_reported_complete;
    return SupportBundleUploadTransitionStatus::transitioned;
}

void SupportBundleJobManager::clear_current_download_locked(const std::string &id) {
    if (!job_ || job_->id != id || job_->state != SupportBundleJobState::succeeded) {
        return;
    }
    job_directory_.clear();
    validated_archive_filename_.clear();
    validated_checksum_filename_.clear();
    validated_sha256_.clear();
    downloaded_archive_size_.reset();
    download_removed_ = true;
    job_->download_available = false;
    job_->case_id.clear();
    job_->private_lifecycle = SupportBundlePrivateLifecycle::none;
    finalized_bundle_.reset();
    encrypted_artifact_.reset();
    receipt_artifact_.reset();
    issue_url_.reset();
}

void SupportBundleJobManager::cancel_expiration_locked(const std::string &id) {
    std::erase_if(expirations_, [&id](const ExpirationEntry &entry) {
        return entry.id == id;
    });
}

void SupportBundleJobManager::expiration_loop() {
    std::unique_lock lock(mutex_);
    while (!shutting_down_) {
        if (expirations_.empty()) {
            expiration_cv_.wait(lock, [this] { return shutting_down_ || !expirations_.empty(); });
            continue;
        }
        const auto earliest = std::min_element(
            expirations_.begin(), expirations_.end(),
            [](const ExpirationEntry &left, const ExpirationEntry &right) {
                return left.deadline < right.deadline;
            });
        const auto deadline = earliest->deadline;
        if (std::chrono::steady_clock::now() < deadline) {
            expiration_cv_.wait_until(lock, deadline);
            continue;
        }

        const ExpirationEntry entry = *earliest;
        SupportBundleJobDirectoryRemovalResult cleanup_result;
        try {
            cleanup_result = remover_(storage_root_, entry.id);
        } catch (...) {
            cleanup_result = {SupportBundleJobDirectoryRemovalFailure::removal_failed};
        }
        if (cleanup_result.removed()) {
            expirations_.erase(earliest);
            clear_current_download_locked(entry.id);
        } else {
            earliest->deadline = std::chrono::steady_clock::now() + retry_delay_;
        }
    }
}

void SupportBundleJobManager::remove_unsuccessful_job_directory(
    const std::string &id,
    const std::filesystem::path &job_directory) {
    if (job_directory.lexically_normal().parent_path() != storage_root_ ||
        job_directory.filename() != id) {
        return;
    }
    try {
        (void)remover_(storage_root_, id);
    } catch (...) {
        // Cleanup details are intentionally not exposed through job status.
    }
}
void SupportBundleJobManager::shutdown() {
    { std::lock_guard lock(mutex_); shutting_down_ = true; }
    expiration_cv_.notify_all();
    executor_->request_stop();
    if (worker_.joinable()) worker_.join();
    if (expiration_worker_.joinable()) expiration_worker_.join();
}
