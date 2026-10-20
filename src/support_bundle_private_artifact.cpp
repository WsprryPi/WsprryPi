#include "support_bundle_private_artifact.hpp"

#include "json.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
#include <string_view>

namespace fs = std::filesystem;

namespace {
constexpr char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr char kHex[] = "0123456789abcdef";
constexpr std::size_t kReadBuffer = 64 * 1024;

bool no_controls(const std::string &value, std::size_t maximum, bool require_text) {
    if (value.size() > maximum || (require_text && value.empty())) return false;
    bool visible = false;
    for (unsigned char c : value) {
        if (c < 32 || c == 127) return false;
        if (c != ' ' && c != '\t') visible = true;
    }
    return !require_text || visible;
}

bool safe_basename(const std::string &value) {
    return no_controls(value, 255, true) && value != "." && value != ".." &&
           value.find('/') == value.npos && value.find('\\') == value.npos;
}

bool lower_digest(const std::string &value) {
    if (value.size() != 64) return false;
    for (unsigned char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool valid_issue_url(const std::string &value) {
    constexpr std::string_view prefix =
        "https://github.com/WsprryPi/WsprryPi/issues/";
    if (!value.starts_with(prefix) || value.size() == prefix.size()) return false;
    const auto number = std::string_view(value).substr(prefix.size());
    if (number.front() == '0') return false;
    for (unsigned char c : number) if (c < '0' || c > '9') return false;
    return number.size() <= 10;
}

bool safe_private_directory(const fs::path &directory, fs::path &canonical) {
    struct stat info {};
    if (!directory.is_absolute() || lstat(directory.c_str(), &info) != 0 ||
        S_ISLNK(info.st_mode) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0777) != 0700) return false;
    std::error_code error;
    canonical = fs::canonical(directory, error);
    return !error;
}

bool sha256_descriptor(int descriptor, std::uint64_t expected_size, std::string &output) {
    if (lseek(descriptor, 0, SEEK_SET) < 0) return false;
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) return false;
    std::array<unsigned char, kReadBuffer> buffer {};
    std::uint64_t remaining = expected_size;
    while (remaining) {
        const auto request = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        const ssize_t count = read(descriptor, buffer.data(), request);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0 || EVP_DigestUpdate(context.get(), buffer.data(), count) != 1) return false;
        remaining -= static_cast<std::uint64_t>(count);
    }
    unsigned char extra;
    for (;;) {
        const ssize_t count = read(descriptor, &extra, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count != 0) return false;
        break;
    }
    std::array<unsigned char, 32> digest {};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 || length != 32) return false;
    output.resize(64);
    for (std::size_t index = 0; index < digest.size(); ++index) {
        output[index * 2] = kHex[digest[index] >> 4];
        output[index * 2 + 1] = kHex[digest[index] & 15];
    }
    return lseek(descriptor, 0, SEEK_SET) == 0;
}

bool os_entropy(unsigned char *bytes, std::size_t size) {
    return size <= static_cast<std::size_t>(INT_MAX) &&
           RAND_bytes(bytes, static_cast<int>(size)) == 1;
}

std::string descriptor_path(int descriptor) {
    const fs::path proc = fs::path("/proc/self/fd") / std::to_string(descriptor);
    std::error_code error;
    if (fs::exists(proc, error) && !error) return proc.string();
    return (fs::path("/dev/fd") / std::to_string(descriptor)).string();
}

bool wait_for_child(pid_t pid, std::chrono::milliseconds timeout, int &status) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) return true;
        if (result < 0 && errno != EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    (void)kill(-pid, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) return false;
        if (result < 0 && errno != EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    (void)kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return false;
}

bool valid_key_or_recipient(const std::string &value, std::size_t maximum) {
    return no_controls(value, maximum, true) && value.find_first_of(" \t") == value.npos;
}

bool valid_timestamp(const std::string &value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 ||
            index == 16 || index == 19) continue;
        if (value[index] < '0' || value[index] > '9') return false;
    }
    return value.substr(5, 2) >= "01" && value.substr(5, 2) <= "12" &&
           value.substr(8, 2) >= "01" && value.substr(8, 2) <= "31" &&
           value.substr(11, 2) <= "23" && value.substr(14, 2) <= "59" &&
           value.substr(17, 2) <= "60";
}

bool write_all(int descriptor, const std::string &contents) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t count = write(descriptor, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return fsync(descriptor) == 0;
}
}  // namespace

std::optional<std::string> generate_support_bundle_case_id(
    const SupportBundleEntropySource &source) {
    std::array<unsigned char, 8> bytes {};
    const auto entropy = source ? source : SupportBundleEntropySource(os_entropy);
    if (!entropy(bytes.data(), bytes.size())) return std::nullopt;
    std::uint64_t value = 0;
    for (unsigned char byte : bytes) value = (value << 8) | byte;
    value >>= 4;
    std::string output;
    for (int index = 11; index >= 0; --index) {
        if (index == 7 || index == 3) output.push_back('-');
        output.push_back(kCrockford[(value >> (index * 5)) & 31]);
    }
    return output;
}

std::optional<std::string> generate_support_bundle_artifact_id(
    const SupportBundleEntropySource &source) {
    std::array<unsigned char, 16> bytes {};
    const auto entropy = source ? source : SupportBundleEntropySource(os_entropy);
    if (!entropy(bytes.data(), bytes.size())) return std::nullopt;
    std::string output(32, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        output[index * 2] = kHex[bytes[index] >> 4];
        output[index * 2 + 1] = kHex[bytes[index] & 15];
    }
    return output;
}

bool valid_support_bundle_case_id(const std::string &value) {
    if (value.size() != 14 || value[4] != '-' || value[9] != '-') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 9) continue;
        if (std::string_view(kCrockford).find(value[index]) == std::string_view::npos) return false;
    }
    return true;
}

bool valid_support_bundle_artifact_id(const std::string &value) {
    if (value.size() != 32) return false;
    for (unsigned char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool valid_support_bundle_context(const SupportBundleContext &context) {
    switch (context.kind) {
    case SupportBundleContextKind::existing_github_issue:
        return valid_issue_url(context.issue_url) && context.problem_description.empty() &&
               context.contact.empty();
    case SupportBundleContextKind::new_github_issue:
    case SupportBundleContextKind::no_github:
        return context.issue_url.empty() &&
               no_controls(context.problem_description, 4096, true) &&
               no_controls(context.contact, 512, true);
    }
    return false;
}

FinalizedSupportBundle::FinalizedSupportBundle(int descriptor, fs::path path,
                                               std::string basename,
                                               std::string sha256,
                                               std::uint64_t size)
    : descriptor_(descriptor), path_(std::move(path)), basename_(std::move(basename)),
      sha256_(std::move(sha256)), size_(size) {}
FinalizedSupportBundle::~FinalizedSupportBundle() { if (descriptor_ >= 0) close(descriptor_); }
FinalizedSupportBundle::FinalizedSupportBundle(FinalizedSupportBundle &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)), path_(std::move(other.path_)),
      basename_(std::move(other.basename_)), sha256_(std::move(other.sha256_)), size_(other.size_) {}
FinalizedSupportBundle &FinalizedSupportBundle::operator=(FinalizedSupportBundle &&other) noexcept {
    if (this != &other) {
        if (descriptor_ >= 0) close(descriptor_);
        descriptor_ = std::exchange(other.descriptor_, -1);
        path_ = std::move(other.path_); basename_ = std::move(other.basename_);
        sha256_ = std::move(other.sha256_); size_ = other.size_;
    }
    return *this;
}

SupportBundleFinalizationResult finalize_support_bundle(
    const fs::path &archive, const std::string &expected_sha256,
    std::uint64_t maximum_bytes) {
    if (!archive.is_absolute() || !safe_basename(archive.filename().string()))
        return {SupportBundleFinalizationFailure::unsafe_path, {}};
    if (!lower_digest(expected_sha256))
        return {SupportBundleFinalizationFailure::invalid_digest, {}};
    const int descriptor = open(archive.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return {SupportBundleFinalizationFailure::open_failed, {}};
    struct stat before {};
    if (fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_uid != geteuid() || (before.st_mode & 0777) != 0600) {
        close(descriptor); return {SupportBundleFinalizationFailure::unsafe_file, {}};
    }
    if (before.st_size <= 0) { close(descriptor); return {SupportBundleFinalizationFailure::empty, {}}; }
    if (static_cast<std::uint64_t>(before.st_size) > maximum_bytes) {
        close(descriptor); return {SupportBundleFinalizationFailure::oversized, {}};
    }
    std::string digest;
    if (!sha256_descriptor(descriptor, before.st_size, digest)) {
        close(descriptor); return {SupportBundleFinalizationFailure::read_failed, {}};
    }
    struct stat after {};
    if (fstat(descriptor, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime) {
        close(descriptor); return {SupportBundleFinalizationFailure::changed, {}};
    }
    if (digest != expected_sha256) {
        close(descriptor); return {SupportBundleFinalizationFailure::digest_mismatch, {}};
    }
    if (fchmod(descriptor, 0400) != 0) {
        close(descriptor); return {SupportBundleFinalizationFailure::permission_failed, {}};
    }
    std::error_code canonical_error;
    const fs::path canonical_archive = fs::canonical(archive, canonical_error);
    if (canonical_error || canonical_archive.filename() != archive.filename()) {
        close(descriptor); return {SupportBundleFinalizationFailure::unsafe_path, {}};
    }
    return {SupportBundleFinalizationFailure::none,
            FinalizedSupportBundle(descriptor, canonical_archive, archive.filename().string(),
                                   digest, before.st_size)};
}

SupportBundleEncryptionResult encrypt_support_bundle(
    const SupportBundleEncryptionRequest &request) {
    if (!request.bundle || !request.bundle->valid() ||
        !valid_support_bundle_case_id(request.case_id) ||
        !valid_support_bundle_artifact_id(request.artifact_id) ||
        !valid_key_or_recipient(request.recipient, 1024) ||
        !request.recipient.starts_with("age1") ||
        !valid_key_or_recipient(request.key_id, 128) ||
        !request.key_id.starts_with("wsprrypi-bundle-") ||
        request.maximum_encrypted_bytes == 0 ||
        request.timeout <= std::chrono::milliseconds::zero())
        return {SupportBundleEncryptionFailure::invalid_request, {}};
    fs::path directory;
    if (!safe_private_directory(request.job_directory, directory) ||
        request.bundle->path().parent_path() != directory)
        return {SupportBundleEncryptionFailure::unsafe_directory, {}};
    struct stat executable_info {};
    if (!request.executable.is_absolute() || lstat(request.executable.c_str(), &executable_info) != 0 ||
        S_ISLNK(executable_info.st_mode) || !S_ISREG(executable_info.st_mode) ||
        (executable_info.st_mode & 0022) != 0 ||
        access(request.executable.c_str(), X_OK) != 0)
        return {SupportBundleEncryptionFailure::executable_unavailable, {}};
    struct stat archive_info {};
    std::string current_archive_digest;
    if (fstat(request.bundle->descriptor(), &archive_info) != 0 ||
        !S_ISREG(archive_info.st_mode) || archive_info.st_uid != geteuid() ||
        (archive_info.st_mode & 0777) != 0400 || archive_info.st_size <= 0 ||
        static_cast<std::uint64_t>(archive_info.st_size) != request.bundle->size() ||
        !sha256_descriptor(request.bundle->descriptor(), request.bundle->size(),
                           current_archive_digest) ||
        current_archive_digest != request.bundle->sha256())
        return {SupportBundleEncryptionFailure::invalid_request, {}};
    const std::string basename = "wsprrypi-support-" + request.case_id + "-" +
                                 request.artifact_id + ".tar.gz.age";
    const fs::path output = directory / basename;
    const fs::path temporary = directory / ("." + basename + ".partial");
    struct stat collision {};
    if (lstat(output.c_str(), &collision) == 0 || errno != ENOENT ||
        lstat(temporary.c_str(), &collision) == 0 || errno != ENOENT)
        return {SupportBundleEncryptionFailure::output_collision, {}};

    const int flags = fcntl(request.bundle->descriptor(), F_GETFD);
    if (flags < 0) return {SupportBundleEncryptionFailure::launch_failed, {}};
    const std::string input = descriptor_path(request.bundle->descriptor());
    std::string executable = request.executable.string();
    std::string temporary_string = temporary.string();
    std::vector<char *> argv = {executable.data(), const_cast<char *>("--encrypt"),
        const_cast<char *>("--recipient"), const_cast<char *>(request.recipient.c_str()),
        const_cast<char *>("--output"), temporary_string.data(),
        const_cast<char *>(input.c_str()), nullptr};
    const pid_t pid = fork();
    if (pid < 0) return {SupportBundleEncryptionFailure::launch_failed, {}};
    if (pid == 0) {
        if (setpgid(0, 0) != 0) _exit(127);
        umask(0077);
        if (fcntl(request.bundle->descriptor(), F_SETFD, flags & ~FD_CLOEXEC) != 0) _exit(127);
        const int null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0 || dup2(null_fd, STDERR_FILENO) < 0)
            _exit(127);
        execv(request.executable.c_str(), argv.data());
        _exit(127);
    }
    if (setpgid(pid, pid) != 0 &&
        !(errno == EACCES && getpgid(pid) == pid) && errno != ESRCH) {
        (void)kill(pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::launch_failed, {}};
    }
    int status = 0;
    if (!wait_for_child(pid, request.timeout, status)) {
        unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::timed_out, {}};
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::process_failed, {}};
    }
    const int encrypted_fd = open(temporary.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat encrypted_info {};
    if (encrypted_fd < 0 || fstat(encrypted_fd, &encrypted_info) != 0 ||
        !S_ISREG(encrypted_info.st_mode) || encrypted_info.st_uid != geteuid() ||
        (encrypted_info.st_mode & 0777) != 0600) {
        if (encrypted_fd >= 0) close(encrypted_fd);
        unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::unsafe_output, {}};
    }
    if (encrypted_info.st_size <= 0) {
        close(encrypted_fd); unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::empty_output, {}};
    }
    if (static_cast<std::uint64_t>(encrypted_info.st_size) > request.maximum_encrypted_bytes) {
        close(encrypted_fd); unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::oversized_output, {}};
    }
    std::string digest;
    if (!sha256_descriptor(encrypted_fd, encrypted_info.st_size, digest)) {
        close(encrypted_fd); unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::digest_failed, {}};
    }
    close(encrypted_fd);
    if (link(temporary.c_str(), output.c_str()) != 0) {
        unlink(temporary.c_str());
        return {SupportBundleEncryptionFailure::publish_failed, {}};
    }
    if (unlink(temporary.c_str()) != 0) {
        unlink(output.c_str());
        return {SupportBundleEncryptionFailure::publish_failed, {}};
    }
    return {SupportBundleEncryptionFailure::none,
            {output, basename, request.artifact_id, request.key_id, digest,
             static_cast<std::uint64_t>(encrypted_info.st_size)}};
}

SupportBundleReceiptResult write_support_bundle_receipt(
    const fs::path &job_directory, const SupportBundleReceipt &receipt) {
    if (receipt.project_id != "wsprrypi" || !valid_support_bundle_case_id(receipt.case_id) ||
        !valid_support_bundle_artifact_id(receipt.artifact_id) ||
        !valid_timestamp(receipt.created_at_utc) ||
        !safe_basename(receipt.archive_filename) || receipt.archive_size == 0 ||
        !lower_digest(receipt.archive_sha256) || !safe_basename(receipt.encrypted_filename) ||
        receipt.encrypted_size == 0 || !lower_digest(receipt.encrypted_sha256) ||
        !valid_key_or_recipient(receipt.bundle_encryption_key_id, 128) ||
        !receipt.bundle_encryption_key_id.starts_with("wsprrypi-bundle-") ||
        (receipt.issue_url && !valid_issue_url(*receipt.issue_url)) ||
        receipt.upload_state != "encrypted_artifact_downloaded" ||
        !receipt.archive_filename.ends_with(".tar.gz") ||
        receipt.encrypted_filename != "wsprrypi-support-" + receipt.case_id + "-" +
            receipt.artifact_id + ".tar.gz.age")
        return {SupportBundleReceiptFailure::invalid_receipt, {}, {}};
    fs::path directory;
    if (!safe_private_directory(job_directory, directory))
        return {SupportBundleReceiptFailure::unsafe_directory, {}, {}};
    const std::string basename = "wsprrypi-support-" + receipt.case_id + "-" +
                                 receipt.artifact_id + ".receipt.json";
    const fs::path output = directory / basename;
    const fs::path temporary = directory / ("." + basename + ".partial");
    struct stat collision {};
    if (lstat(output.c_str(), &collision) == 0 || errno != ENOENT ||
        lstat(temporary.c_str(), &collision) == 0 || errno != ENOENT)
        return {SupportBundleReceiptFailure::output_collision, {}, {}};
    const int descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0)
        return {errno == EEXIST ? SupportBundleReceiptFailure::output_collision
                               : SupportBundleReceiptFailure::write_failed, {}, {}};
    nlohmann::ordered_json json = {
        {"schema_version", 1}, {"project_id", receipt.project_id},
        {"case_id", receipt.case_id}, {"artifact_id", receipt.artifact_id},
        {"created_at_utc", receipt.created_at_utc},
        {"archive_filename", receipt.archive_filename}, {"archive_size", receipt.archive_size},
        {"archive_sha256", receipt.archive_sha256},
        {"encrypted_filename", receipt.encrypted_filename},
        {"encrypted_size", receipt.encrypted_size},
        {"encrypted_sha256", receipt.encrypted_sha256},
        {"bundle_encryption_key_id", receipt.bundle_encryption_key_id},
        {"issue_url", receipt.issue_url ? nlohmann::ordered_json(*receipt.issue_url)
                                         : nlohmann::ordered_json(nullptr)},
        {"upload_state", receipt.upload_state},
    };
    const std::string contents = json.dump(2) + "\n";
    const bool wrote = contents.size() <= 16 * 1024 && write_all(descriptor, contents) &&
                       close(descriptor) == 0;
    if (!wrote) {
        close(descriptor); unlink(temporary.c_str());
        return {SupportBundleReceiptFailure::write_failed, {}, {}};
    }
    if (link(temporary.c_str(), output.c_str()) != 0) {
        unlink(temporary.c_str());
        return {SupportBundleReceiptFailure::publish_failed, {}, {}};
    }
    if (unlink(temporary.c_str()) != 0) {
        unlink(output.c_str());
        return {SupportBundleReceiptFailure::publish_failed, {}, {}};
    }
    return {SupportBundleReceiptFailure::none, output, basename};
}
