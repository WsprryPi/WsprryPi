#include "support_bundle_checksum_file.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
namespace {
class ScopedFileDescriptor {
public:
    explicit ScopedFileDescriptor(int descriptor) : descriptor_(descriptor) {}

    ~ScopedFileDescriptor() {
        if (descriptor_ >= 0) {
            (void)close(descriptor_);
        }
    }

    ScopedFileDescriptor(const ScopedFileDescriptor &) = delete;
    ScopedFileDescriptor &operator=(const ScopedFileDescriptor &) = delete;

    [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
    int descriptor_ = -1;
};

SupportBundleChecksumFileValidation failure(SupportBundleChecksumFileFailure category) {
    return {category};
}

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

bool is_lowercase_hex_digest(const std::string &digest) {
    if (digest.size() != 64) {
        return false;
    }
    for (const unsigned char character : digest) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool valid_reference(const SupportBundleDownloadReference &reference) {
    if (reference.status != SupportBundleDownloadReferenceStatus::available ||
        !safe_basename(reference.archive_basename) ||
        !safe_basename(reference.checksum_basename) ||
        !is_lowercase_hex_digest(reference.expected_sha256)) {
        return false;
    }

    return reference.archive_path.filename() == reference.archive_basename &&
           reference.checksum_path.filename() == reference.checksum_basename &&
           reference.archive_path.parent_path() == reference.checksum_path.parent_path() &&
           reference.checksum_basename == reference.archive_basename + ".sha256";
}

bool read_exactly(int descriptor, char *buffer, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t read_count = read(descriptor, buffer + offset, size - offset);
        if (read_count > 0) {
            offset += static_cast<std::size_t>(read_count);
            continue;
        }
        if (read_count < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}
}  // namespace

SupportBundleChecksumFileValidation validate_support_bundle_checksum_file(
    const SupportBundleDownloadReference &reference,
    std::size_t maximum_bytes) {
    if (!valid_reference(reference)) {
        return failure(SupportBundleChecksumFileFailure::reference_unavailable);
    }

    struct stat path_info {};
    if (lstat(reference.checksum_path.c_str(), &path_info) != 0) {
        return failure(errno == ENOENT ? SupportBundleChecksumFileFailure::missing
                                       : SupportBundleChecksumFileFailure::open_failed);
    }
    if (!S_ISREG(path_info.st_mode) || S_ISLNK(path_info.st_mode)) {
        return failure(SupportBundleChecksumFileFailure::unsafe_file);
    }

    const int descriptor =
        open(reference.checksum_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return failure(errno == ENOENT ? SupportBundleChecksumFileFailure::missing
                                       : SupportBundleChecksumFileFailure::open_failed);
    }
    const ScopedFileDescriptor file(descriptor);

    struct stat opened_info {};
    if (fstat(file.get(), &opened_info) != 0) {
        return failure(SupportBundleChecksumFileFailure::open_failed);
    }
    if (!S_ISREG(opened_info.st_mode)) {
        return failure(SupportBundleChecksumFileFailure::unsafe_file);
    }
    if (opened_info.st_uid != geteuid()) {
        return failure(SupportBundleChecksumFileFailure::ownership_mismatch);
    }
    if ((opened_info.st_mode & 07777) != 0600) {
        return failure(SupportBundleChecksumFileFailure::mode_mismatch);
    }
    if (opened_info.st_size <= 0) {
        return failure(SupportBundleChecksumFileFailure::empty);
    }
    if (static_cast<std::uintmax_t>(opened_info.st_size) > maximum_bytes) {
        return failure(SupportBundleChecksumFileFailure::oversized);
    }

    std::string record(static_cast<std::size_t>(opened_info.st_size), '\0');
    if (!read_exactly(file.get(), record.data(), record.size())) {
        return failure(SupportBundleChecksumFileFailure::malformed);
    }

    char trailing_byte = '\0';
    while (true) {
        const ssize_t read_count = read(file.get(), &trailing_byte, 1);
        if (read_count == 0) {
            break;
        }
        if (read_count > 0) {
            return failure(SupportBundleChecksumFileFailure::malformed);
        }
        if (errno != EINTR) {
            return failure(SupportBundleChecksumFileFailure::malformed);
        }
    }

    const std::string expected = reference.expected_sha256 + "  " +
                                 reference.archive_basename + "\n";
    if (record != expected) {
        return failure(SupportBundleChecksumFileFailure::malformed);
    }

    return failure(SupportBundleChecksumFileFailure::none);
}
