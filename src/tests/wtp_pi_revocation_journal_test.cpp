#include "wtp_endpoint/revocation_journal.hpp"

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
} // namespace

int main() {
    std::array<char, 64> directory{};
    const std::string pattern = "/tmp/wsprrypi-revocation-test.XXXXXX";
    check(pattern.size() + 1 <= directory.size(), "test path too long");
    std::copy(pattern.begin(), pattern.end(), directory.begin());
    char* root = mkdtemp(directory.data());
    check(root != nullptr, "cannot create test directory");
    const std::string path = std::string(root) + "/revocation.state";
    const std::string identity(32, 'a');
    wsprrypi::WtpRevocationJournal journal(path, identity);
    check(journal.load() && journal.generation() == 0, "missing journal starts at zero");
    check(journal.advance() && journal.generation() == 1, "first revocation must persist");
    struct stat file{};
    check(stat(path.c_str(), &file) == 0 && (file.st_mode & 0777) == 0600,
          "journal must be private");
    wsprrypi::WtpRevocationJournal restored(path, identity);
    check(restored.load() && restored.generation() == 1,
          "revocation must survive journal reconstruction");
    check(restored.advance() && restored.generation() == 2,
          "generation must advance monotonically");
    check(chmod(path.c_str(), 0644) == 0, "cannot change test permissions");
    wsprrypi::WtpRevocationJournal public_file(path, identity);
    check(!public_file.load(), "public journal must fail closed");
    check(chmod(path.c_str(), 0600) == 0, "cannot restore test permissions");
    wsprrypi::WtpRevocationJournal wrong_identity(path, std::string(32, 'b'));
    check(!wrong_identity.load(), "another device identity must not adopt journal");
    const int fd = open(path.c_str(), O_WRONLY | O_TRUNC);
    check(fd >= 0, "cannot corrupt test journal");
    const std::string bad = "invalid\n";
    check(write(fd, bad.data(), bad.size()) == static_cast<ssize_t>(bad.size()),
          "cannot write corrupt test journal");
    close(fd);
    wsprrypi::WtpRevocationJournal corrupt(path, identity);
    check(!corrupt.load() && !corrupt.advance(), "corrupt journal must fail closed");
    check(unlink(path.c_str()) == 0 && mkfifo(path.c_str(), 0600) == 0,
          "cannot create nonregular test file");
    wsprrypi::WtpRevocationJournal fifo(path, identity);
    check(!fifo.load(), "nonregular journal must fail closed without blocking");
    check(unlink(path.c_str()) == 0 && rmdir(root) == 0,
          "cannot remove owned test files");
}
