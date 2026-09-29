#include "wtp_endpoint/identity.hpp"

#include "WTP-Server/include/wtp_server/sha256.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace wsprrypi {
namespace {
std::string hex16(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (std::size_t i = 0; i < 16; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 15]);
    }
    return result;
}
} // namespace

std::string wtp_pi_device_id(const std::string& machine_id_path) {
    std::ifstream file(machine_id_path);
    std::string machine_id;
    if (!(file >> machine_id) || machine_id.size() != 32)
        throw std::runtime_error("Machine ID unavailable for WTP endpoint");
    for (char c : machine_id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            throw std::runtime_error("Machine ID is not canonical lowercase hex");
    constexpr std::string_view domain = "wsprrypi-wtp-device-v1:";
    const std::string input = std::string(domain) + machine_id;
    const auto digest = wsprrypico::wtp::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
    return hex16(digest);
}

std::string WtpPiBootIdentity::new_boot_id() {
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Boot identity random source unavailable");
    std::array<std::uint8_t, 16> bytes{};
    std::size_t read_count = 0;
    while (read_count < bytes.size()) {
        const auto result = read(fd, bytes.data() + read_count, bytes.size() - read_count);
        if (result > 0) read_count += static_cast<std::size_t>(result);
        else if (result < 0 && errno == EINTR) continue;
        else { close(fd); throw std::runtime_error("Boot identity random read failed"); }
    }
    close(fd);
    return hex16(bytes);
}
} // namespace wsprrypi
