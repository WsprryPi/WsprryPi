#include "support_bundle_checksum_file.hpp"

#include <cassert>
#include <cerrno>
#include <dirent.h>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
constexpr const char *kArchiveBasename = "WsprryPi-support-test bundle.tar.gz";

std::string digest() {
    return std::string(64, 'a');
}

SupportBundleDownloadReference reference_for(const fs::path &root) {
    const fs::path archive = root / kArchiveBasename;
    return {SupportBundleDownloadReferenceStatus::available,
            archive,
            kArchiveBasename,
            root / (std::string(kArchiveBasename) + ".sha256"),
            std::string(kArchiveBasename) + ".sha256",
            digest()};
}

void write_file(const fs::path &path, const std::string &contents, mode_t mode = 0600) {
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    assert(descriptor >= 0);
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = write(descriptor, contents.data() + offset, contents.size() - offset);
        assert(written > 0);
        offset += static_cast<std::size_t>(written);
    }
    assert(close(descriptor) == 0);
    assert(chmod(path.c_str(), mode) == 0);
}

std::string canonical_record(const SupportBundleDownloadReference &reference) {
    return reference.expected_sha256 + "  " + reference.archive_basename + "\n";
}

void expect_failure(const SupportBundleDownloadReference &reference,
                    SupportBundleChecksumFileFailure expected,
                    std::size_t maximum_bytes = kSupportBundleMaximumChecksumSidecarBytes) {
    const auto result = validate_support_bundle_checksum_file(reference, maximum_bytes);
    assert(!result.valid());
    assert(result.failure == expected);
}

std::size_t open_descriptor_count() {
    DIR *directory = opendir("/proc/self/fd");
    if (directory == nullptr) {
        directory = opendir("/dev/fd");
    }
    assert(directory != nullptr);
    std::size_t count = 0;
    while (readdir(directory) != nullptr) {
        ++count;
    }
    assert(closedir(directory) == 0);
    return count;
}
}  // namespace

int main() {
    char template_path[] = "/tmp/wsprrypi-checksum-file-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const fs::path root(template_path);
    assert(chmod(root.c_str(), 0700) == 0);

    const auto reference = reference_for(root);
    write_file(reference.checksum_path, canonical_record(reference));
    const std::size_t descriptors_before = open_descriptor_count();
    assert(validate_support_bundle_checksum_file(reference).valid());
    assert(open_descriptor_count() == descriptors_before);

    write_file(reference.checksum_path, std::string(64, 'b') + "  " + reference.archive_basename + "\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, reference.expected_sha256 + "  other.tar.gz\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, "abc  " + reference.archive_basename + "\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, std::string(65, 'a') + "  " + reference.archive_basename + "\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, std::string(64, 'g') + "  " + reference.archive_basename + "\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, reference.expected_sha256 + "  ../archive.tar.gz\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, canonical_record(reference) + "extra\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);
    write_file(reference.checksum_path, reference.expected_sha256 + "  " +
                                          std::string("WsprryPi\0-support", 17) + ".tar.gz\n");
    expect_failure(reference, SupportBundleChecksumFileFailure::malformed);

    write_file(reference.checksum_path, "");
    expect_failure(reference, SupportBundleChecksumFileFailure::empty);
    write_file(reference.checksum_path, std::string(513, 'x'));
    expect_failure(reference, SupportBundleChecksumFileFailure::oversized);
    for (const mode_t mode : {0640, 0400, 0660}) {
        assert(unlink(reference.checksum_path.c_str()) == 0);
        write_file(reference.checksum_path, canonical_record(reference), mode);
        expect_failure(reference, SupportBundleChecksumFileFailure::mode_mismatch);
    }
    assert(open_descriptor_count() == descriptors_before);

    assert(unlink(reference.checksum_path.c_str()) == 0);
    expect_failure(reference, SupportBundleChecksumFileFailure::missing);
    assert(symlink("/tmp", reference.checksum_path.c_str()) == 0);
    expect_failure(reference, SupportBundleChecksumFileFailure::unsafe_file);
    assert(unlink(reference.checksum_path.c_str()) == 0);
    assert(mkdir(reference.checksum_path.c_str(), 0700) == 0);
    expect_failure(reference, SupportBundleChecksumFileFailure::unsafe_file);
    assert(rmdir(reference.checksum_path.c_str()) == 0);
    assert(mkfifo(reference.checksum_path.c_str(), 0600) == 0);
    expect_failure(reference, SupportBundleChecksumFileFailure::unsafe_file);
    assert(unlink(reference.checksum_path.c_str()) == 0);

    write_file(reference.checksum_path, canonical_record(reference));
    const int before = open(reference.checksum_path.c_str(), O_RDONLY | O_CLOEXEC);
    assert(before >= 0);
    assert(close(before) == 0);
    assert(validate_support_bundle_checksum_file(reference).valid());

    fs::remove_all(root);
    std::cout << "support_bundle_checksum_file_test: PASS\n";
}
