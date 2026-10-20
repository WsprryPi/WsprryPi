#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

using SupportBundleEntropySource =
    std::function<bool(unsigned char *, std::size_t)>;

std::optional<std::string> generate_support_bundle_case_id(
    const SupportBundleEntropySource &entropy);
std::optional<std::string> generate_support_bundle_artifact_id(
    const SupportBundleEntropySource &entropy);
bool valid_support_bundle_case_id(const std::string &value);
bool valid_support_bundle_artifact_id(const std::string &value);

enum class SupportBundleContextKind {
    existing_github_issue,
    new_github_issue,
    no_github,
};

struct SupportBundleContext {
    SupportBundleContextKind kind = SupportBundleContextKind::no_github;
    std::string issue_url;
    std::string problem_description;
    std::string contact;
};

bool valid_support_bundle_context(const SupportBundleContext &context);

enum class SupportBundleFinalizationFailure {
    none,
    unsafe_path,
    invalid_digest,
    open_failed,
    unsafe_file,
    empty,
    oversized,
    read_failed,
    digest_mismatch,
    changed,
    permission_failed,
};

struct SupportBundleFinalizationResult;

class FinalizedSupportBundle {
public:
    FinalizedSupportBundle() = default;
    ~FinalizedSupportBundle();
    FinalizedSupportBundle(const FinalizedSupportBundle &) = delete;
    FinalizedSupportBundle &operator=(const FinalizedSupportBundle &) = delete;
    FinalizedSupportBundle(FinalizedSupportBundle &&other) noexcept;
    FinalizedSupportBundle &operator=(FinalizedSupportBundle &&other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return descriptor_ >= 0; }
    [[nodiscard]] int descriptor() const noexcept { return descriptor_; }
    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
    [[nodiscard]] const std::string &basename() const noexcept { return basename_; }
    [[nodiscard]] const std::string &sha256() const noexcept { return sha256_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }

private:
    friend struct SupportBundleFinalizationResult;
    friend SupportBundleFinalizationResult finalize_support_bundle(
        const std::filesystem::path &, const std::string &, std::uint64_t);
    FinalizedSupportBundle(int descriptor,
                           std::filesystem::path path,
                           std::string basename,
                           std::string sha256,
                           std::uint64_t size);
    int descriptor_ = -1;
    std::filesystem::path path_;
    std::string basename_;
    std::string sha256_;
    std::uint64_t size_ = 0;
};

struct SupportBundleFinalizationResult {
    SupportBundleFinalizationFailure failure =
        SupportBundleFinalizationFailure::open_failed;
    FinalizedSupportBundle bundle;
    [[nodiscard]] bool finalized() const noexcept {
        return failure == SupportBundleFinalizationFailure::none && bundle.valid();
    }
};

SupportBundleFinalizationResult finalize_support_bundle(
    const std::filesystem::path &archive,
    const std::string &expected_sha256,
    std::uint64_t maximum_bytes);

enum class SupportBundleEncryptionFailure {
    none,
    invalid_request,
    unsafe_directory,
    executable_unavailable,
    output_collision,
    launch_failed,
    timed_out,
    process_failed,
    unsafe_output,
    empty_output,
    oversized_output,
    digest_failed,
    publish_failed,
};

struct SupportBundleEncryptionRequest {
    const FinalizedSupportBundle *bundle = nullptr;
    std::filesystem::path job_directory;
    std::string case_id;
    std::string artifact_id;
    std::string recipient;
    std::string key_id;
    std::filesystem::path executable = "/usr/bin/age";
    std::uint64_t maximum_encrypted_bytes = 128ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds timeout = std::chrono::minutes(5);
};

struct SupportBundleEncryptedArtifact {
    std::filesystem::path path;
    std::string basename;
    std::string artifact_id;
    std::string key_id;
    std::string sha256;
    std::uint64_t size = 0;
};

struct SupportBundleEncryptionResult {
    SupportBundleEncryptionFailure failure =
        SupportBundleEncryptionFailure::invalid_request;
    SupportBundleEncryptedArtifact artifact;
    [[nodiscard]] bool encrypted() const noexcept {
        return failure == SupportBundleEncryptionFailure::none;
    }
};

SupportBundleEncryptionResult encrypt_support_bundle(
    const SupportBundleEncryptionRequest &request);

struct SupportBundleReceipt {
    std::string project_id = "wsprrypi";
    std::string case_id;
    std::string artifact_id;
    std::string created_at_utc;
    std::string archive_filename;
    std::uint64_t archive_size = 0;
    std::string archive_sha256;
    std::string encrypted_filename;
    std::uint64_t encrypted_size = 0;
    std::string encrypted_sha256;
    std::string bundle_encryption_key_id;
    std::optional<std::string> issue_url;
    std::string upload_state = "encrypted_artifact_downloaded";
};

enum class SupportBundleReceiptFailure {
    none,
    invalid_receipt,
    unsafe_directory,
    output_collision,
    write_failed,
    publish_failed,
};

struct SupportBundleReceiptResult {
    SupportBundleReceiptFailure failure = SupportBundleReceiptFailure::invalid_receipt;
    std::filesystem::path path;
    std::string basename;
    [[nodiscard]] bool written() const noexcept {
        return failure == SupportBundleReceiptFailure::none;
    }
};

SupportBundleReceiptResult write_support_bundle_receipt(
    const std::filesystem::path &job_directory,
    const SupportBundleReceipt &receipt);
