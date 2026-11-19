// SPDX-License-Identifier: MIT
#include "wtp_retirement.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_server_settings_json.hpp"
#include <iostream>
#include <stdexcept>
using wsprrypi::wtp_retirement_configuration_refusal;

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)

int main() {
    try {
        ArgParserConfig configuration;
        configuration.wtp_server = parse_wtp_server_settings(
            {{"Enabled",false},{"Port",32456},{"Interface","eth1"}});
        configuration.wtp = parse_wtp_settings({{"Transport","network_plain"},
            {"Hostname","pico-custom.local"},{"TCP Port",31499},
            {"Start Uncertainty ns",123456789}},false);
        const auto saved = wtp_settings_json(configuration.wtp);
        const auto listener = wtp_server_settings_json(configuration.wtp_server);
        for (auto backend : {TransmitBackendKind::GPIO, TransmitBackendKind::RP1_GPCLK,
                             TransmitBackendKind::SI5351, TransmitBackendKind::SIMULATED}) {
            configuration.transmit_backend = backend;
            CHECK(wtp_retirement_configuration_refusal(configuration).empty());
            configuration.wtp_server.enabled = true;
            CHECK(wtp_retirement_configuration_refusal(configuration).find("inbound WTP listener") != std::string::npos);
            configuration.wtp_server.enabled = false;
        }
        configuration.transmit_backend = TransmitBackendKind::WTP;
        for (bool enabled : {false,true}) {
            configuration.transmit = enabled;
            CHECK(wtp_retirement_configuration_refusal(configuration).find("WTP transmitter backend") != std::string::npos);
        }
        configuration.transmit_backend = TransmitBackendKind::SIMULATED;
        configuration.stored_wtp_backend = true;
        configuration.simulated_backend_override = true;
        CHECK(!wtp_retirement_configuration_refusal(configuration).empty());
        configuration.stored_wtp_backend = false;
        configuration.wtp_server_port_override = 32500;
        CHECK(!wtp_retirement_configuration_refusal(configuration).empty());
        configuration.wtp_server_port_override.reset();
        CHECK(wtp_retirement_configuration_refusal(configuration).empty());
        CHECK(wtp_settings_json(configuration.wtp) == saved);
        CHECK(wtp_server_settings_json(configuration.wtp_server) == listener);
        std::cout << "PASS: active legacy backend/listener refusal; inactive custom settings preserved\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
