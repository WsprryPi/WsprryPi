#include "wtp_endpoint/si5351_bridge.hpp"

#include <stdexcept>

namespace wsprrypi {

WsprSi5351Backend::Config wtp_pi_si5351_backend_config(
    int i2c_bus, int i2c_address, std::uint32_t reference_hz,
    bool crystal_reference, int crystal_load_pf, int tx_output,
    int power_level, bool dry_run) {
    if (i2c_bus < 0 || i2c_address < 0x60 || i2c_address > 0x6f ||
        reference_hz == 0 || tx_output < 0 || tx_output > 2 ||
        power_level < 1 || power_level > 4)
        throw std::invalid_argument("Invalid Si5351 WTP output selection");
    WsprSi5351Backend::Config config;
    config.device.i2c_bus = i2c_bus;
    config.device.i2c_address = static_cast<std::uint8_t>(i2c_address);
    config.device.reference_hz = reference_hz;
    config.device.reference_source = crystal_reference
        ? Si5351Device::ReferenceSource::CRYSTAL
        : Si5351Device::ReferenceSource::EXTERNAL_TCXO;
    config.device.crystal_load_capacitance_pf = crystal_load_pf;
    config.planner.reference_hz = reference_hz;
    config.planner.tx_output = static_cast<Si5351Device::Output>(tx_output);
    config.planner.park_unused_outputs = true;
    config.power_level = power_level;
    config.dry_run = dry_run;
    return config;
}
} // namespace wsprrypi
