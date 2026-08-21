#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "transmission_payloads.hpp"
#include "transmission_request.hpp"

namespace wsprrypi
{

enum class RfEventType
{
    SET_FREQUENCY,
    RF_ON,
    RF_OFF,
    HOLD
};

struct PlanId
{
    std::uint64_t value{0};
};

struct RfEvent
{
    std::chrono::nanoseconds offset_from_start{};
    std::chrono::nanoseconds duration{};

    RfEventType type{RfEventType::HOLD};

    double frequency_hz{0.0};
    bool rf_on{false};
    int message_char_index{-1};

    struct RasterProgress
    {
        enum class CellKind
        {
            LEADER,
            MESSAGE,
            TRAILER
        };

        CellKind cell_kind{CellKind::MESSAGE};
        int normalized_char_index{-1};
        std::uint8_t cell_column{0};
        std::uint8_t physical_position{0};
        std::uint64_t absolute_position{0};
    };

    std::optional<RasterProgress> raster_progress{};

    EnvelopeSettings envelope{};
};

struct PlanSummary
{
    std::chrono::nanoseconds total_duration{};
    std::size_t event_count{0};
    double min_frequency_hz{0.0};
    double max_frequency_hz{0.0};
};

struct ExecutionPlan
{
    PlanId id{};
    RequestId request_id{};

    TransmissionMode mode{TransmissionMode::WSPR};
    BackendKind backend{BackendKind::RPI_CLOCK_GPIO};
    double reference_frequency_hz{0.0};

    CalibrationSnapshot calibration{};
    ExecutionPolicy policy{};

    // Relevant only to finite TONE plans.  Backends that cannot safely accept
    // an implicit long-running tone must reject false.
    bool duration_was_explicit{false};

    std::vector<RfEvent> events{};
    PlanSummary summary{};
};

} // namespace wsprrypi
