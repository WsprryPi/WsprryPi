#include "support_bundle_download_file.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

namespace {
bool safe_basename(const std::string &value) {
    if (value.empty() || value == "." || value == ".." ||
        value.find("..") != std::string::npos ||
        value.find_first_of("/\\") != std::string::npos) {
        return false;
    }
    for (const unsigned char character : value) {
        if (character < 0x20 || character == 0x7f) {
            return false;
        }
    }
    return true;
}

SupportBundleDownloadFileResult failure(SupportBundleDownloadFileFailure category) {
    return {category, {}};
}
}  // namespace

SupportBundleDownloadFile::SupportBundleDownloadFile(int descriptor,
                                                     std::uint64_t size,
                                                     std::string basename)
    : descriptor_(descriptor), size_(size), basename_(std::move(basename)) {}

SupportBundleDownloadFile::~SupportBundleDownloadFile() {
    if (descriptor_ >= 0) {
        close(descriptor_);
    }
}

SupportBundleDownloadFile::SupportBundleDownloadFile(SupportBundleDownloadFile &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)),
      size_(std::exchange(other.size_, 0)),
      basename_(std::move(other.basename_)) {}

SupportBundleDownloadFile &SupportBundleDownloadFile::operator=(
    SupportBundleDownloadFile &&other) noexcept {
    if (this != &other) {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
        descriptor_ = std::exchange(other.descriptor_, -1);
        size_ = std::exchange(other.size_, 0);
        basename_ = std::move(other.basename_);
    }
    return *this;
}

bool SupportBundleDownloadFile::valid() const noexcept {
    return descriptor_ >= 0;
}

int SupportBundleDownloadFile::descriptor() const noexcept {
    return descriptor_;
}

std::uint64_t SupportBundleDownloadFile::size() const noexcept {
    return size_;
}

const std::string &SupportBundleDownloadFile::basename() const noexcept {
    return basename_;
}

SupportBundleDownloadFileResult open_support_bundle_download_file(
    const SupportBundleDownloadReference &reference,
    std::uint64_t maximum_bytes) {
    if (reference.status != SupportBundleDownloadReferenceStatus::available) {
        return failure(SupportBundleDownloadFileFailure::reference_unavailable);
    }

    const std::string basename = reference.archive_path.filename().string();
    if (!safe_basename(basename)) {
        return failure(SupportBundleDownloadFileFailure::unsafe_file);
    }

    struct stat path_info {};
    if (lstat(reference.archive_path.c_str(), &path_info) != 0) {
        return failure(errno == ENOENT ? SupportBundleDownloadFileFailure::missing
                                       : SupportBundleDownloadFileFailure::open_failed);
    }
    if (!S_ISREG(path_info.st_mode) || S_ISLNK(path_info.st_mode)) {
        return failure(SupportBundleDownloadFileFailure::unsafe_file);
    }

    const int descriptor = open(reference.archive_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return failure(errno == ENOENT ? SupportBundleDownloadFileFailure::missing
                                       : SupportBundleDownloadFileFailure::open_failed);
    }

    const auto fail_after_open = [descriptor](SupportBundleDownloadFileFailure category) {
        (void)close(descriptor);
        return failure(category);
    };

    struct stat opened_info {};
    if (fstat(descriptor, &opened_info) != 0) {
        return fail_after_open(SupportBundleDownloadFileFailure::open_failed);
    }
    if (!S_ISREG(opened_info.st_mode)) {
        return fail_after_open(SupportBundleDownloadFileFailure::unsafe_file);
    }
    if (opened_info.st_uid != geteuid()) {
        return fail_after_open(SupportBundleDownloadFileFailure::ownership_mismatch);
    }
    const mode_t mode = opened_info.st_mode & 07777;
    if (mode != 0600 && mode != 0400) {
        return fail_after_open(SupportBundleDownloadFileFailure::mode_mismatch);
    }
    if (opened_info.st_size <= 0) {
        return fail_after_open(SupportBundleDownloadFileFailure::empty);
    }
    const auto size = static_cast<std::uint64_t>(opened_info.st_size);
    if (size > maximum_bytes) {
        return fail_after_open(SupportBundleDownloadFileFailure::oversized);
    }

    return {SupportBundleDownloadFileFailure::none,
            SupportBundleDownloadFile(descriptor, size, basename)};
}
