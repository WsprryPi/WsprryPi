#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"

#include <string>

namespace wsprrypi {

// The machine ID is domain-separated and truncated to WTP's 128-bit display
// identity. It is an endpoint label, not authentication for Plain LAN.
std::string wtp_pi_device_id(const std::string& machine_id_path = "/etc/machine-id");

class WtpPiBootIdentity final : public wsprrypico::wtp::IdentitySource {
public:
    std::string new_boot_id() override;
};

} // namespace wsprrypi
