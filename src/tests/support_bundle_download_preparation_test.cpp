#include "support_bundle_download_preparation.hpp"

#include <cassert>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
constexpr const char *kArchiveBasename = "WsprryPi-support-test.tar.gz";
constexpr const char *kDigest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

SupportBundleDownloadReference reference_for(const fs::path &root) {
    const fs::path archive = root / kArchiveBasename;
    return {SupportBundleDownloadReferenceStatus::available,
            archive,
            kArchiveBasename,
            root / (std::string(kArchiveBasename) + ".sha256"),
            std::string(kArchiveBasename) + ".sha256",
            kDigest};
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

void write_valid_fixture(const SupportBundleDownloadReference &reference) {
    write_file(reference.archive_path, "abc");
    write_file(reference.checksum_path,
               reference.expected_sha256 + "  " + reference.archive_basename + "\n");
}

void assert_no_download(const SupportBundleDownloadPreparationResult &result,
                        SupportBundleDownloadPreparationFailure expected) {
    assert(!result.available());
    assert(result.failure == expected);
    assert(!result.download.valid());
    assert(result.download.descriptor() == -1);
    assert(result.download.size() == 0);
    assert(result.download.basename().empty());
}

void reset_fixture(const SupportBundleDownloadReference &reference) {
    std::error_code error;
    fs::remove(reference.archive_path, error);
    fs::remove(reference.checksum_path, error);
    write_valid_fixture(reference);
}
}  // namespace

int main() {
    char template_path[] = "/tmp/wsprrypi-download-preparation-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const fs::path root(template_path);
    assert(chmod(root.c_str(), 0700) == 0);
    const auto reference = reference_for(root);

    write_valid_fixture(reference);
    int prepared_descriptor = -1;
    {
        auto prepared = prepare_support_bundle_download(reference);
        assert(prepared.available());
        assert(prepared.download.size() == 3);
        assert(prepared.download.basename() == kArchiveBasename);
        assert(lseek(prepared.download.descriptor(), 0, SEEK_CUR) == 0);
        char buffer[4] = {};
        assert(read(prepared.download.descriptor(), buffer, 3) == 3);
        assert(std::string(buffer, 3) == "abc");
        prepared_descriptor = prepared.download.descriptor();
    }
    errno = 0;
    assert(fcntl(prepared_descriptor, F_GETFD) == -1 && errno == EBADF);

    reset_fixture(reference);
    write_file(reference.checksum_path,
               std::string(64, 'b') + "  " + reference.archive_basename + "\n");
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::corrupt);

    reset_fixture(reference);
    write_file(reference.checksum_path, reference.expected_sha256 + "  other.tar.gz\n");
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::corrupt);

    reset_fixture(reference);
    write_file(reference.archive_path, "def");
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::corrupt);

    reset_fixture(reference);
    assert(unlink(reference.checksum_path.c_str()) == 0);
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::unavailable);

    reset_fixture(reference);
    assert(unlink(reference.checksum_path.c_str()) == 0);
    assert(symlink("/tmp", reference.checksum_path.c_str()) == 0);
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::unsafe);
    assert(unlink(reference.checksum_path.c_str()) == 0);

    reset_fixture(reference);
    assert(unlink(reference.checksum_path.c_str()) == 0);
    assert(mkdir(reference.checksum_path.c_str(), 0700) == 0);
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::unsafe);
    assert(rmdir(reference.checksum_path.c_str()) == 0);

    reset_fixture(reference);
    assert(unlink(reference.archive_path.c_str()) == 0);
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::unavailable);

    reset_fixture(reference);
    assert(unlink(reference.archive_path.c_str()) == 0);
    assert(symlink("/tmp", reference.archive_path.c_str()) == 0);
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::unsafe);
    assert(unlink(reference.archive_path.c_str()) == 0);

    reset_fixture(reference);
    assert(unlink(reference.archive_path.c_str()) == 0);
    assert(mkdir(reference.archive_path.c_str(), 0700) == 0);
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::unsafe);
    assert(rmdir(reference.archive_path.c_str()) == 0);

    reset_fixture(reference);
    write_file(reference.archive_path, "");
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::corrupt);

    reset_fixture(reference);
    write_file(reference.archive_path, "abcd");
    assert_no_download(prepare_support_bundle_download(reference, 3),
                       SupportBundleDownloadPreparationFailure::corrupt);

    reset_fixture(reference);
    write_file(reference.archive_path, "def");
    assert_no_download(prepare_support_bundle_download(reference),
                       SupportBundleDownloadPreparationFailure::corrupt);

    fs::remove_all(root);
    std::cout << "support_bundle_download_preparation_test: PASS\n";
}
