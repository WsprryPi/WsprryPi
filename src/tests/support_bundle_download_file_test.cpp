#include "support_bundle_download_file.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
SupportBundleDownloadReference reference_for(const fs::path &path) {
    return {SupportBundleDownloadReferenceStatus::available, path};
}

void write_file(const fs::path &path, std::size_t bytes, mode_t mode = 0600) {
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    assert(descriptor >= 0);
    std::string content(bytes, 'a');
    assert(bytes == 0 || write(descriptor, content.data(), content.size()) ==
                             static_cast<ssize_t>(content.size()));
    assert(close(descriptor) == 0);
    assert(chmod(path.c_str(), mode) == 0);
}

void expect_failure(const SupportBundleDownloadReference &reference,
                    std::uint64_t limit,
                    SupportBundleDownloadFileFailure expected) {
    auto result = open_support_bundle_download_file(reference, limit);
    assert(!result.available());
    assert(result.failure == expected);
    assert(!result.file.valid());
    assert(result.file.basename().empty());
}
}  // namespace

int main() {
    char template_path[] = "/tmp/wsprrypi-download-file-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const fs::path root(template_path);
    assert(chmod(root.c_str(), 0700) == 0);

    const fs::path valid = root / "support bundle.tar.gz";
    write_file(valid, 4);
    int owned_descriptor = -1;
    {
        auto result = open_support_bundle_download_file(reference_for(valid), 4);
        assert(result.available());
        assert(result.file.size() == 4);
        assert(result.file.basename() == "support bundle.tar.gz");
        owned_descriptor = result.file.descriptor();
        assert((fcntl(owned_descriptor, F_GETFD) & FD_CLOEXEC) != 0);
    }
    errno = 0;
    assert(fcntl(owned_descriptor, F_GETFD) == -1 && errno == EBADF);

    expect_failure(reference_for(root / "missing.tar.gz"), 4,
                   SupportBundleDownloadFileFailure::missing);
    const fs::path link = root / "link.tar.gz";
    assert(symlink(valid.c_str(), link.c_str()) == 0);
    expect_failure(reference_for(link), 4, SupportBundleDownloadFileFailure::unsafe_file);
    expect_failure(reference_for(root), 4, SupportBundleDownloadFileFailure::unsafe_file);
    const fs::path fifo = root / "archive.fifo";
    assert(mkfifo(fifo.c_str(), 0600) == 0);
    expect_failure(reference_for(fifo), 4, SupportBundleDownloadFileFailure::unsafe_file);

    const fs::path wrong_mode = root / "wrong-mode.tar.gz";
    write_file(wrong_mode, 1, 0640);
    expect_failure(reference_for(wrong_mode), 4, SupportBundleDownloadFileFailure::mode_mismatch);
    const fs::path empty = root / "empty.tar.gz";
    write_file(empty, 0);
    expect_failure(reference_for(empty), 4, SupportBundleDownloadFileFailure::empty);
    const fs::path over_limit = root / "over-limit.tar.gz";
    write_file(over_limit, 5);
    expect_failure(reference_for(over_limit), 4, SupportBundleDownloadFileFailure::oversized);
    const auto unavailable = SupportBundleDownloadReference{
        SupportBundleDownloadReferenceStatus::not_ready, valid};
    expect_failure(unavailable, 4, SupportBundleDownloadFileFailure::reference_unavailable);

    fs::remove_all(root);
    std::cout << "support_bundle_download_file_test: PASS\n";
}
