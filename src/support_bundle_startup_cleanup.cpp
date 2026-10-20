#include "support_bundle_startup_cleanup.hpp"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class FileDescriptor {
public:
    explicit FileDescriptor(int descriptor = -1) : descriptor_(descriptor) {}
    ~FileDescriptor() {
        if (descriptor_ >= 0) {
            (void)close(descriptor_);
        }
    }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
    int descriptor_;
};

bool valid_job_id(const std::string &id) {
    if (id.size() != 32) {
        return false;
    }
    for (const unsigned char character : id) {
        const bool ascii_alphanumeric =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
        if (!ascii_alphanumeric && character != '-' && character != '_') {
            return false;
        }
    }
    return true;
}

bool private_root(const struct stat &info) {
    return S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
           (info.st_mode & 07777) == 0700;
}

bool same_object(const struct stat &left, const struct stat &right) {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}
}  // namespace

SupportBundleStartupCleanupResult cleanup_stale_support_bundle_jobs(
    const std::filesystem::path &storage_root,
    SupportBundleStartupJobRemover remover) {
    if (!storage_root.is_absolute()) {
        return {SupportBundleStartupCleanupStatus::unsafe_root, 0, 0};
    }

    struct stat path_info {};
    if (lstat(storage_root.c_str(), &path_info) != 0) {
        return {errno == ENOENT ? SupportBundleStartupCleanupStatus::unavailable
                                : SupportBundleStartupCleanupStatus::unsafe_root,
                0, 0};
    }
    std::error_code canonical_error;
    const std::filesystem::path canonical_root =
        std::filesystem::canonical(storage_root, canonical_error);
    if (canonical_error || canonical_root != storage_root.lexically_normal() ||
        S_ISLNK(path_info.st_mode) || !private_root(path_info)) {
        return {SupportBundleStartupCleanupStatus::unsafe_root, 0, 0};
    }

    const int root_fd =
        open(canonical_root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root_fd < 0) {
        return {SupportBundleStartupCleanupStatus::unsafe_root, 0, 0};
    }
    FileDescriptor root(root_fd);
    struct stat opened_info {};
    if (fstat(root.get(), &opened_info) != 0 || !private_root(opened_info) ||
        !same_object(path_info, opened_info)) {
        return {SupportBundleStartupCleanupStatus::unsafe_root, 0, 0};
    }

    const int listing_fd = dup(root.get());
    if (listing_fd < 0) {
        return {SupportBundleStartupCleanupStatus::partial_failure, 0, 1};
    }
    DIR *directory = fdopendir(listing_fd);
    if (directory == nullptr) {
        (void)close(listing_fd);
        return {SupportBundleStartupCleanupStatus::partial_failure, 0, 1};
    }

    SupportBundleStartupCleanupResult result{SupportBundleStartupCleanupStatus::completed, 0, 0};
    while (true) {
        errno = 0;
        const dirent *entry = readdir(directory);
        if (entry == nullptr) {
            if (errno != 0) {
                ++result.failed_count;
            }
            break;
        }

        const std::string name(entry->d_name);
        if (!valid_job_id(name)) {
            continue;
        }
        try {
            if (remover && remover(canonical_root, name).removed()) {
                ++result.removed_count;
            } else {
                ++result.failed_count;
            }
        } catch (...) {
            ++result.failed_count;
        }
    }
    if (closedir(directory) != 0) {
        ++result.failed_count;
    }
    if (result.failed_count != 0) {
        result.status = SupportBundleStartupCleanupStatus::partial_failure;
    }
    return result;
}
