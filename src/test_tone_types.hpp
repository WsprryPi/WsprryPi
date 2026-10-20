#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

enum class TestToneRequestSource { LegacyDefault, LegacyExactRf, WsprBand, CustomRf };

struct ParsedTestToneRequest
{
    TestToneRequestSource source = TestToneRequestSource::LegacyDefault;
    std::string band;
    std::optional<std::uint64_t> frequency_hz;
    std::optional<std::chrono::nanoseconds> duration;
    struct Rp1DevelopmentConfirmation
    {
        bool enabled = false;
        int route_gpio = 0;
        bool physical_connection = false;
        bool attenuation_and_load = false;
        bool bounded_operation = false;
        bool non_radiating_topology = false;
        bool experimental_acknowledged = false;
        std::string operation_id;
    } rp1_development;
};
