#include "wtp_endpoint/revocation_journal.hpp"

#include <array>
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <limits>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace wsprrypi {
namespace {
bool valid_id(std::string_view value) {
    if (value.size() != 32) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

bool write_all(int fd, std::string_view text) {
    while (!text.empty()) {
        const auto written = write(fd, text.data(), text.size());
        if (written > 0) text.remove_prefix(static_cast<std::size_t>(written));
        else if (written < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}
} // namespace

WtpRevocationJournal::WtpRevocationJournal(std::string path, std::string device_id)
    : path_(std::move(path)), device_id_(std::move(device_id)) {
    if (path_.empty() || !valid_id(device_id_))
        error_ = "Invalid revocation journal path or device ID";
}

bool WtpRevocationJournal::load() {
    if (!error_.empty()) return false;
    const int fd = open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) { generation_ = 0; return true; }
        error_ = "Unable to open revocation journal";
        return false;
    }
    struct stat info{};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 0077) != 0) {
        close(fd);
        error_ = "Revocation journal is not a private regular file";
        return false;
    }
    std::array<char, 128> bytes{};
    const auto count = read(fd, bytes.data(), bytes.size());
    close(fd);
    if (count <= 0 || count >= static_cast<ssize_t>(bytes.size())) {
        error_ = "Invalid revocation journal length";
        return false;
    }
    const std::string_view contents(bytes.data(), static_cast<std::size_t>(count));
    const auto first = contents.find(' ');
    const auto second = contents.find(' ', first == std::string_view::npos ? 0 : first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        contents.substr(0, first) != "wtp-pi-revocation-v1" ||
        contents.substr(first + 1, second - first - 1) != device_id_ ||
        contents.back() != '\n') {
        error_ = "Revocation journal identity or format mismatch";
        return false;
    }
    const auto number = contents.substr(second + 1, contents.size() - second - 2);
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(number.data(), number.data() + number.size(), parsed);
    if (number.empty() || result.ec != std::errc{} ||
        result.ptr != number.data() + number.size()) {
        error_ = "Invalid revocation generation";
        return false;
    }
    generation_ = parsed;
    return true;
}

bool WtpRevocationJournal::advance() {
    if (!error_.empty() || generation_ == std::numeric_limits<std::uint64_t>::max()) {
        error_ = "Revocation generation unavailable";
        return false;
    }
    const auto next = generation_ + 1;
    const std::filesystem::path path(path_);
    const auto directory = path.parent_path();
    if (directory.empty()) { error_ = "Revocation journal needs a directory"; return false; }
    const std::string pattern = path_ + ".tmp.XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int fd = mkstemp(temporary.data());
    if (fd < 0) { error_ = "Unable to create revocation journal temporary file"; return false; }
    const std::string value = "wtp-pi-revocation-v1 " + device_id_ + " " +
                              std::to_string(next) + "\n";
    const bool written = write_all(fd, value) && fsync(fd) == 0;
    const int close_result = close(fd);
    if (!written || close_result != 0 || rename(temporary.data(), path_.c_str()) != 0) {
        unlink(temporary.data());
        error_ = "Unable to persist revocation generation";
        return false;
    }
    const int dir_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir_fd < 0) { error_ = "Unable to sync revocation directory"; return false; }
    const bool synced = fsync(dir_fd) == 0;
    close(dir_fd);
    if (!synced) { error_ = "Unable to sync revocation directory"; return false; }
    generation_ = next;
    return true;
}
} // namespace wsprrypi
