#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"

namespace wsprrypi {

// Conservative host UTC/monotonic pair. Linux kernel NTP maximum error is
// included; a host without that evidence reports unsynchronized.
class WtpPiSystemClock final : public wsprrypico::wtp::Clock {
public:
    wsprrypico::wtp::ClockSnapshot snapshot() const override;
};

} // namespace wsprrypi
