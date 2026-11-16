// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee C. Bussy
#pragma once
#include "solar_geometry.hpp"
#include "config_types.hpp"
#include <array>
#include <functional>
#include <span>
namespace wsprrypi {
struct SolarWsprList {
    std::string frequencies,profile="existing_common";
    WsprBandPreferences preferences;
    std::vector<WsprFrequencyEntry> entries;
};
struct SolarPeriodPolicy {
    bool allow=true;
    std::optional<SolarWsprList> wspr_override;
    std::optional<std::uint64_t> cw_base_nhz;
};
struct SolarPolicyConfig {
    bool schedule_enabled=true,solar_enabled=false;
    std::optional<std::string> locator;
    SolarPeriodPolicy day,night,morning,evening;
    SolarWindow morning_window,evening_window;
};
enum class SolarClockState { Unavailable,Synchronized,Holdover,Alarm };
struct SolarClockObservation {
    SolarClockState state=SolarClockState::Unavailable;
    std::uint64_t utc_ns{},generation{},maximum_error_ns{},holdover_age_ns{};
    bool healthy_sample_this_boot=false;
};
enum class SolarLocationSource { Locator,UnsupportedGps };
struct SolarEvaluationContext {
    std::uint64_t intended_utc_ns{},configuration_generation{},location_generation{};
    SolarClockObservation clock;
    SolarLocationSource location_source=SolarLocationSource::Locator;
};
enum class SolarCursorScope { Ordinary,Day,Night,Morning,Evening };
struct SolarModeIntent {
    ModeType mode=ModeType::WSPR;
    SolarWsprList wspr;
    std::uint64_t cw_base_nhz{};
    std::int64_t cw_shift_nhz{};
    double wspr_audio_offset_hz=WSPR_AUDIO_OFFSET_HZ;
};
struct SolarFrozenIntent {
    ModeType mode;
    SolarCursorScope cursor_scope=SolarCursorScope::Ordinary;
    std::size_t cursor_index{};
    std::optional<WsprFrequencyEntry> wspr_entry;
    std::string wspr_profile;
    WsprBandPreferences wspr_preferences;
    // WSPR nominal tones before ordinary random placement/correction; CW uses
    // space/mark or dot/dash. Phase 6 must freeze placement and revalidate launch.
    std::vector<std::uint64_t> nominal_tones_nhz;
};
enum class SolarOutcome { Allowed,Skipped,Held,Disabled };
enum class SolarWait { None,Period,Configuration,Clock,Date,Location,UnsupportedLocation,Frequency,Mode };
struct SolarEvaluation {
    SolarOutcome outcome=SolarOutcome::Held;
    SolarWait waiting=SolarWait::Configuration;
    std::optional<SolarPeriod> selected_period;
    std::optional<SolarFrozenIntent> intent;
    std::optional<SolarPosition> position;
    bool prolonged=false;
    SolarEvaluationContext observation;
};
// A read-only backend policy predicate supplied by the owning Pi validator.
// No Pico-specific frequency ceiling is built into this evaluator.
using SolarFrequencyPredicate=std::function<bool(ModeType,std::span<const std::uint64_t>)>;
std::optional<std::uint64_t> solar_decimal_hz(std::string_view) noexcept;
bool solar_clock_ready(const SolarClockObservation&) noexcept;
// Cursors are immutable proposed selection inputs. Evaluation advances none of
// them, obtains no authority, and persists/prepares/dispatches nothing.
SolarEvaluation evaluate_solar_policy(const SolarPolicyConfig&,const SolarModeIntent&,
    const SolarEvaluationContext&,const std::array<std::size_t,5>& cursors,
    const SolarFrequencyPredicate&);
}
