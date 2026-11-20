#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"

#include <string>
#include <array>
#include <cstdint>

namespace wsprrypi {

// OS-owned identity, domain-separated and truncated to WTP's 128-bit display
// label, never authentication. Linux retains its existing machine-ID mapping;
// macOS uses its host UUID with a distinct domain and a bounded OS query.
std::string wtp_pi_device_id();
// Explicit file source for existing Linux software fixtures. No CLI, INI,
// environment, browser, or network request can select this source.
std::string wtp_pi_device_id(const std::string& machine_id_path);
// Pure Mac mapping used by the OS producer and cross-platform golden tests.
std::string wtp_pi_macos_device_id(const std::array<std::uint8_t,16>& host_uuid);
namespace testing {
// Actual-main, separate-installation software fixtures only. Must be selected
// before first identity use; never exposed by production arguments or input.
void select_wtp_pi_device_identity(const std::string &identity);
}

class WtpPiBootIdentity final : public wsprrypico::wtp::IdentitySource {
public:
    std::string new_boot_id() override;
};

} // namespace wsprrypi
