#include "support_bundle_job_directory_remover.hpp"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace {
using Failure = SupportBundleJobDirectoryRemovalFailure;

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

bool is_private_directory(const struct stat &info) {
    return S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
           (info.st_mode & 07777) == 0700;
}

bool same_object(const struct stat &left, const struct stat &right) {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

Failure remove_contents(int directory_fd, dev_t storage_device) {
    const int listing_fd = dup(directory_fd);
    if (listing_fd < 0) {
        return Failure::removal_failed;
    }
    DIR *directory = fdopendir(listing_fd);
    if (directory == nullptr) {
        (void)close(listing_fd);
        return Failure::removal_failed;
    }

    Failure outcome = Failure::none;
    while (true) {
        errno = 0;
        const dirent *entry = readdir(directory);
        if (entry == nullptr) {
            if (errno != 0) {
                outcome = Failure::removal_failed;
            }
            break;
        }
        const std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }

        struct stat entry_info {};
        if (fstatat(directory_fd, name.c_str(), &entry_info, AT_SYMLINK_NOFOLLOW) != 0) {
            outcome = Failure::removal_failed;
            break;
        }
        if (entry_info.st_uid != geteuid()) {
            outcome = Failure::unsafe_target;
            break;
        }

        if (S_ISDIR(entry_info.st_mode)) {
            if (entry_info.st_dev != storage_device ||
                (entry_info.st_mode & 07777) != 0700) {
                outcome = Failure::unsafe_target;
                break;
            }
            const int child_fd = openat(directory_fd, name.c_str(),
                                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child_fd < 0) {
                outcome = Failure::unsafe_target;
                break;
            }
            FileDescriptor child(child_fd);
            struct stat opened_info {};
            if (fstat(child.get(), &opened_info) != 0 ||
                !is_private_directory(opened_info) ||
                opened_info.st_dev != storage_device ||
                !same_object(entry_info, opened_info)) {
                outcome = Failure::unsafe_target;
                break;
            }
            outcome = remove_contents(child.get(), storage_device);
            if (outcome != Failure::none) {
                break;
            }
            if (unlinkat(directory_fd, name.c_str(), AT_REMOVEDIR) != 0) {
                outcome = Failure::removal_failed;
                break;
            }
            continue;
        }

        if (S_ISREG(entry_info.st_mode) && entry_info.st_nlink != 1) {
            outcome = Failure::unsupported_entry;
            break;
        }
        if (!S_ISREG(entry_info.st_mode) && !S_ISLNK(entry_info.st_mode) &&
            !S_ISFIFO(entry_info.st_mode) && !S_ISSOCK(entry_info.st_mode)) {
            outcome = Failure::unsupported_entry;
            break;
        }
        if (unlinkat(directory_fd, name.c_str(), 0) != 0) {
            outcome = Failure::removal_failed;
            break;
        }
    }
    if (closedir(directory) != 0 && outcome == Failure::none) {
        outcome = Failure::removal_failed;
    }
    return outcome;
}

SupportBundleJobDirectoryRemovalResult result(Failure failure) {
    return {failure};
}
}  // namespace

SupportBundleJobDirectoryRemovalResult remove_support_bundle_job_directory(
    const std::filesystem::path &storage_root,
    const std::string &job_id) {
    if (!storage_root.is_absolute()) {
        return result(Failure::invalid_root);
    }
    if (!valid_job_id(job_id)) {
        return result(Failure::invalid_job_id);
    }

    struct stat root_path_info {};
    if (lstat(storage_root.c_str(), &root_path_info) != 0) {
        return result(errno == ENOENT ? Failure::root_unavailable : Failure::unsafe_root);
    }
    std::error_code canonical_error;
    const std::filesystem::path canonical_root =
        std::filesystem::canonical(storage_root, canonical_error);
    if (canonical_error || canonical_root != storage_root.lexically_normal()) {
        return result(Failure::unsafe_root);
    }
    if (S_ISLNK(root_path_info.st_mode) || !is_private_directory(root_path_info)) {
        return result(Failure::unsafe_root);
    }

    const int root_fd =
        open(canonical_root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root_fd < 0) {
        return result(Failure::unsafe_root);
    }
    FileDescriptor root(root_fd);
    struct stat root_info {};
    if (fstat(root.get(), &root_info) != 0 || !is_private_directory(root_info) ||
        !same_object(root_path_info, root_info)) {
        return result(Failure::unsafe_root);
    }

    struct stat target_path_info {};
    if (fstatat(root.get(), job_id.c_str(), &target_path_info, AT_SYMLINK_NOFOLLOW) != 0) {
        return result(errno == ENOENT ? Failure::none : Failure::removal_failed);
    }
    if (S_ISLNK(target_path_info.st_mode) || !is_private_directory(target_path_info) ||
        target_path_info.st_dev != root_info.st_dev) {
        return result(Failure::unsafe_target);
    }

    const int target_fd = openat(root.get(), job_id.c_str(),
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (target_fd < 0) {
        return result(Failure::unsafe_target);
    }
    FileDescriptor target(target_fd);
    struct stat target_info {};
    if (fstat(target.get(), &target_info) != 0 || !is_private_directory(target_info) ||
        target_info.st_dev != root_info.st_dev || !same_object(target_path_info, target_info)) {
        return result(Failure::unsafe_target);
    }

    const Failure contents_result = remove_contents(target.get(), root_info.st_dev);
    if (contents_result != Failure::none) {
        return result(contents_result);
    }
    if (unlinkat(root.get(), job_id.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
        return result(Failure::removal_failed);
    }
    return result(Failure::none);
}
