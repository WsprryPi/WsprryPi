#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"
#include "WSPR-Transmitter/src/transmission_backend.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace wsprrypi {
inline constexpr std::uint64_t wtp_max_job_duration_ns = 86'400'000'000'000ULL;
inline constexpr std::array wtp_modes{
    std::pair{TransmissionMode::WSPR, std::string_view{"wspr"}},
    std::pair{TransmissionMode::TONE, std::string_view{"tone"}},
    std::pair{TransmissionMode::QRSS, std::string_view{"qrss"}},
    std::pair{TransmissionMode::FSKCW, std::string_view{"fskcw"}},
    std::pair{TransmissionMode::DFCW, std::string_view{"dfcw"}},
    std::pair{TransmissionMode::CW, std::string_view{"cw"}}};

inline std::optional<TransmissionMode> wtp_transmission_mode(std::string_view name) {
    for (const auto& [mode, wire] : wtp_modes)
        if (name == wire) return mode;
    return std::nullopt;
}

inline std::optional<std::uint64_t> wtp_frequency_nhz(double hz) {
    const long double value = static_cast<long double>(hz) * 1'000'000'000.0L;
    if (!std::isfinite(value) || value < 1 ||
        value >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        return std::nullopt;
    return static_cast<std::uint64_t>(std::round(value));
}

inline wsprrypico::wtp::ServiceConfig wtp_backend_caps(const ITransmissionBackend& backend) {
    const auto native = backend.capabilities();
    wsprrypico::wtp::ServiceConfig wire;
    wire.capability_engine = backend.info().name;
    wire.supported_modes.clear();
    for (const auto& [mode, name] : wtp_modes)
        if (supports_mode(native, mode)) wire.supported_modes.emplace_back(name);
    if (native.output_class != BackendOutputClass::NON_RF_SIMULATION) {
        const auto minimum = wtp_frequency_nhz(native.min_frequency_hz);
        const auto maximum = wtp_frequency_nhz(native.max_frequency_hz);
        if (!minimum || !maximum || *minimum > *maximum)
            throw std::runtime_error("Backend has no usable WTP frequency envelope");
        wire.minimum_frequency_nhz = *minimum;
        wire.maximum_frequency_nhz = *maximum;
    }
    if (wire.supported_modes.empty()) throw std::runtime_error("Backend has no WTP modes");
    // Bounded protocol resources, independent of RF backend qualification.
    wire.max_events = wsprrypico::wtp::EventList::maximum_events;
    wire.max_job_duration_ns = wtp_max_job_duration_ns;
    wire.minimum_arm_lead_ns = 2'000'000'000ULL;
    wire.maximum_arm_uncertainty_ns = 500'000'000ULL;
    wire.output_disable_timeout_ns = 5'000'000'000ULL;
    return wire;
}
} // namespace wsprrypi
