#pragma once

#include "wtp_endpoint/native_bridge.hpp"
#include "WSPR-Transmitter/src/wspr_transmit_backend_si5351.hpp"

namespace wsprrypi {
using WtpPiSi5351Bridge = WtpPiNativeBridge;

WsprSi5351Backend::Config wtp_pi_si5351_backend_config(
    int i2c_bus, int i2c_address, std::uint32_t reference_hz,
    bool crystal_reference, int crystal_load_pf, int tx_output,
    int power_level, bool dry_run = false);

} // namespace wsprrypi
