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
#include <algorithm>
#include <ctime>
#include <mutex>
#include <optional>

namespace wsprrypi {
namespace {
std::mutex identity_mutex;
std::optional<std::string> fixture_identity;
bool identity_selected{};
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
std::string derived_id(std::string_view domain,std::string_view seed) {
    const std::string input=std::string(domain)+std::string(seed);
    const auto digest=wsprrypico::wtp::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(input.data()),input.size()));
    return hex16(digest);
}
} // namespace

std::string wtp_pi_macos_device_id(const std::array<std::uint8_t,16>& host_uuid) {
    if(std::all_of(host_uuid.begin(),host_uuid.end(),[](auto byte){return byte==0;})||
       std::all_of(host_uuid.begin(),host_uuid.end(),[](auto byte){return byte==255;}))
        throw std::runtime_error("Mac host identity is unavailable");
    return derived_id("wsprrypi-wtp-macos-device-v1:",hex16(host_uuid));
}

std::string wtp_pi_device_id() {
    std::lock_guard lock(identity_mutex);identity_selected=true;
    if(fixture_identity)return *fixture_identity;
#ifdef __APPLE__
    std::array<std::uint8_t,16> host_uuid{};
    const timespec timeout{2,0}; // Zero would mean an unbounded OS wait.
    if(::gethostuuid(host_uuid.data(),&timeout)!=0)
        throw std::runtime_error("Mac host identity is unavailable");
    return wtp_pi_macos_device_id(host_uuid);
#else
    return wtp_pi_device_id("/etc/machine-id");
#endif
}

void testing::select_wtp_pi_device_identity(const std::string &identity){
    std::lock_guard lock(identity_mutex);
    if(identity_selected||fixture_identity||identity.size()!=32||identity.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::logic_error("Software installation identity must be selected once before use");
    fixture_identity=identity;
}

std::string wtp_pi_device_id(const std::string& machine_id_path) {
    std::ifstream file(machine_id_path);
    std::string machine_id;
    if (!(file >> machine_id) || machine_id.size() != 32)
        throw std::runtime_error("Machine ID unavailable for WTP endpoint");
    for (char c : machine_id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            throw std::runtime_error("Machine ID is not canonical lowercase hex");
    return derived_id("wsprrypi-wtp-device-v1:",machine_id);
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
