#include "support_bundle_archive_digest.hpp"

#include <cassert>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
SupportBundleDownloadReference reference_for(const fs::path &archive) {
    return {SupportBundleDownloadReferenceStatus::available, archive};
}

void write_file(const fs::path &path, const std::string &contents) {
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(descriptor >= 0);
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = write(descriptor, contents.data() + offset, contents.size() - offset);
        assert(written > 0);
        offset += static_cast<std::size_t>(written);
    }
    assert(close(descriptor) == 0);
    assert(chmod(path.c_str(), 0600) == 0);
}

SupportBundleDownloadFile open_archive(const fs::path &archive, std::uint64_t limit) {
    auto opened = open_support_bundle_download_file(reference_for(archive), limit);
    assert(opened.available());
    return std::move(opened.file);
}

void assert_at_start(const SupportBundleDownloadFile &archive) {
    assert(lseek(archive.descriptor(), 0, SEEK_CUR) == 0);
}
}  // namespace

int main() {
    char template_path[] = "/tmp/wsprrypi-archive-digest-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const fs::path root(template_path);
    assert(chmod(root.c_str(), 0700) == 0);

    const fs::path empty = root / "empty.tar.gz";
    write_file(empty, "");
    const auto empty_result = open_support_bundle_download_file(reference_for(empty), 1);
    assert(!empty_result.available());
    assert(empty_result.failure == SupportBundleDownloadFileFailure::empty);

    const fs::path small = root / "small.tar.gz";
    write_file(small, "abc");
    auto small_archive = open_archive(small, 3);
    assert(lseek(small_archive.descriptor(), 2, SEEK_SET) == 2);
    assert(verify_support_bundle_archive_digest(
               small_archive,
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
               .valid());
    assert_at_start(small_archive);

    assert(lseek(small_archive.descriptor(), 1, SEEK_SET) == 1);
    const auto mismatch = verify_support_bundle_archive_digest(
        small_archive,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(!mismatch.valid());
    assert(mismatch.failure == SupportBundleArchiveDigestFailure::digest_mismatch);
    assert_at_start(small_archive);

    assert(lseek(small_archive.descriptor(), 1, SEEK_SET) == 1);
    const auto malformed = verify_support_bundle_archive_digest(small_archive, "not-a-digest");
    assert(!malformed.valid());
    assert(malformed.failure == SupportBundleArchiveDigestFailure::invalid_expected_digest);
    assert_at_start(small_archive);

    const fs::path large = root / "large.tar.gz";
    write_file(large, std::string(1000000, 'a'));
    auto large_archive = open_archive(large, 1000000);
    const auto multi_buffer = verify_support_bundle_archive_digest(
        large_archive,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    assert(multi_buffer.valid());
    assert_at_start(large_archive);

    const fs::path truncated = root / "truncated.tar.gz";
    write_file(truncated, "abc");
    auto truncated_archive = open_archive(truncated, 3);
    assert(truncate(truncated.c_str(), 1) == 0);
    const auto truncated_result = verify_support_bundle_archive_digest(
        truncated_archive,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(!truncated_result.valid());
    assert(truncated_result.failure == SupportBundleArchiveDigestFailure::size_mismatch ||
           truncated_result.failure == SupportBundleArchiveDigestFailure::truncated);
    assert_at_start(truncated_archive);

    fs::remove_all(root);
    std::cout << "support_bundle_archive_digest_test: PASS\n";
}
