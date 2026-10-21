#include "rp1_gpclk_event_program.hpp"
#include "chipset_offsets.hpp"

#include "rp1_gpclk_development_policy.hpp"
#include "rp1_gpclk_planner.hpp"
#include "rp1_gpclk_uapi.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wsprrypi
{
namespace
{
constexpr double kWsprSpacingHz = 12000.0 / 8192.0;

Rp1GpclkEventCompileResult reject(std::string error)
{
    Rp1GpclkEventCompileResult result;
    result.error = std::move(error);
    return result;
}

bool noFade(const EnvelopeSettings& envelope)
{
    return envelope.fade_shape == FadeShape::NONE &&
        envelope.fade_in <= std::chrono::nanoseconds::zero() &&
        envelope.fade_out <= std::chrono::nanoseconds::zero();
}

bool physicalTone(const Rp1GpclkTonePlan& numerical, double parent_hz,
    double maximum_error_hz, Rp1GpclkProviderSymbol& tone,
    double& realized_hz)
{
    constexpr std::uint64_t maximum_word = 0xffffffffULL;
    const std::uint64_t period = std::uint64_t{numerical.lower_word_count} +
        numerical.upper_word_count;
    if (period < 2 || period > RP1_GPCLK_DITHER_PERIOD_MAX)
        return false;

    auto lower = numerical.lower_divider_word;
    auto upper = numerical.upper_divider_word;
    // Numerical exact-word plans need a physical adjacent pair. Keep both
    // words inside the same integer field, including its upper boundary.
    if (lower == upper) {
        if ((lower & 0xffffULL) == 0xffffULL) --lower;
        upper = lower + 1;
    }
    if (lower < 65536 || upper > maximum_word || upper != lower + 1 ||
        (lower >> 16) != (upper >> 16))
        return false;
    if (numerical.lower_word_count && numerical.upper_word_count) {
        // Preserve the numerical planner's long-double rounding for ordinary
        // profiles; only endpoint representations require conversion.
        tone = {lower, upper, numerical.lower_word_count, numerical.upper_word_count};
        realized_hz = numerical.average_frequency_hz;
        return true;
    }

    const long double low_hz = static_cast<long double>(parent_hz) * 65536 / lower;
    const long double high_div_hz = static_cast<long double>(parent_hz) * 65536 / upper;
    const long double ratio = std::clamp(
        (numerical.requested_frequency_hz - high_div_hz) / (low_hz - high_div_hz),
        0.0L, 1.0L);
    // The provider requires both counts to be positive. Rounding near an
    // endpoint can otherwise turn a valid numerical plan into an invalid UAPI.
    const auto lower_count = std::clamp<std::uint64_t>(
        static_cast<std::uint64_t>(std::llround(ratio * period)), 1, period - 1);
    const auto upper_count = period - lower_count;
    const long double mean =
        (low_hz * lower_count + high_div_hz * upper_count) / period;
    if (std::fabs(mean - numerical.requested_frequency_hz) > maximum_error_hz)
        return false;
    tone = {lower, upper, static_cast<std::uint32_t>(lower_count),
        static_cast<std::uint32_t>(upper_count)};
    realized_hz = static_cast<double>(mean);
    return true;
}
}

bool validateRp1GpclkEventProgram(
    const Rp1GpclkProviderEventProgram& program,
    std::string& error) noexcept
{
    if (program.fractional_bits != 16 ||
        program.tick_divider != RP1_GPCLK_TICK_DIVIDER)
    {
        error = "RP1 GPCLK event program has invalid clock parameters.";
        return false;
    }
    if (program.tones.empty() ||
        program.tones.size() > RP1_GPCLK_MAX_TONES ||
        program.events.empty() ||
        program.events.size() > RP1_GPCLK_MAX_EVENTS)
    {
        error = "RP1 GPCLK event program exceeds its bounded table contract.";
        return false;
    }
    const auto integer = program.tones.front().lower_divider_word >> 16;
    for (const auto& tone : program.tones)
    {
        if (!integer || tone.upper_divider_word > 0xffffffffULL ||
            tone.lower_divider_word >= tone.upper_divider_word ||
            tone.upper_divider_word != tone.lower_divider_word + 1 ||
            (tone.lower_divider_word >> 16) != integer ||
            (tone.upper_divider_word >> 16) != integer ||
            !tone.lower_count || !tone.upper_count ||
            std::uint64_t{tone.lower_count} + tone.upper_count >
                RP1_GPCLK_DITHER_PERIOD_MAX)
        {
            error = "RP1 GPCLK event program contains an invalid tone profile.";
            return false;
        }
    }
    std::uint64_t total = 0;
    for (const auto& event : program.events)
    {
        if (event.duration_ns < RP1_GPCLK_EVENT_DURATION_NS_MIN ||
            event.duration_ns > RP1_GPCLK_EVENT_DURATION_NS_MAX ||
            (event.rf_on ? event.tone_index >= program.tones.size() : event.tone_index != 0) ||
            event.duration_ns > std::numeric_limits<std::uint64_t>::max() - total)
        {
            error = "RP1 GPCLK event program contains an invalid event.";
            return false;
        }
        total += event.duration_ns;
    }
    if (total != program.total_duration_ns ||
        total > RP1_GPCLK_REQUEST_DURATION_NS_MAX)
    {
        error = "RP1 GPCLK event program total duration is inconsistent.";
        return false;
    }
    return true;
}

Rp1GpclkEventCompileResult compileRp1GpclkEventProgram(
    const ExecutionPlan& plan, bool scheduled_events)
{
    if (plan.backend != BackendKind::RP1_GPCLK)
        return reject("Execution plan is not targeted for RP1 GPCLK.");
    if (plan.mode != TransmissionMode::WSPR &&
        plan.mode != TransmissionMode::TONE &&
        plan.mode != TransmissionMode::QRSS &&
        plan.mode != TransmissionMode::FSKCW &&
        plan.mode != TransmissionMode::DFCW)
        return reject("RP1 GPCLK generic events do not support this product mode.");
    const bool terminal_off = !plan.events.empty() && !plan.events.back().rf_on &&
        plan.events.back().duration == std::chrono::nanoseconds{1};
    const std::size_t event_count = plan.mode == TransmissionMode::TONE && !scheduled_events
        ? std::size_t{1} : plan.events.size() - (terminal_off ? 1 : 0);
    if (plan.events.empty() || !event_count || event_count > RP1_GPCLK_MAX_EVENTS)
        return reject("RP1 GPCLK event count is outside the supported bound.");
    if (plan.mode == TransmissionMode::WSPR && event_count != 162)
        return reject("RP1 GPCLK requires exactly one 162-symbol WSPR frame.");
    if (plan.mode == TransmissionMode::TONE &&
        (!plan.events.front().rf_on ||
         plan.events.front().frequency_hz != plan.reference_frequency_hz))
        return reject("RP1 GPCLK TONE requires one valid RF-on frequency.");
    for (std::size_t i = 0; i < event_count; ++i)
    {
        const auto& event = plan.events[i];
        if (!noFade(event.envelope))
            return reject("RP1 GPCLK events do not support envelope fades.");
    }
    if (scheduled_events) {
        std::chrono::nanoseconds offset{};
        for (const auto& event : plan.events) {
            if (event.offset_from_start != offset || event.duration.count() <= 0 ||
                event.duration > std::chrono::nanoseconds::max() - offset)
                return reject("Scheduled RP1 events must form one contiguous finite plan.");
            offset += event.duration;
        }
        if (offset != plan.summary.total_duration ||
            (plan.mode == TransmissionMode::TONE && !plan.duration_was_explicit))
            return reject("Scheduled RP1 duration must be explicit and consistent.");
    }

    double spacing = kWsprSpacingHz;
    if (plan.mode == TransmissionMode::FSKCW || plan.mode == TransmissionMode::DFCW)
        spacing = plan.summary.max_frequency_hz - plan.summary.min_frequency_hz;
    if (!std::isfinite(spacing) || spacing <= 0.0)
        return reject("RP1 GPCLK finite event plan has invalid tone spacing.");

    Rp1GpclkPlannerInput input;
    input.center_frequency_hz = plan.mode == TransmissionMode::TONE
        ? plan.reference_frequency_hz + 1.5 * spacing
        : plan.reference_frequency_hz;
    input.tone_spacing_hz = spacing;
    input.parent_frequency_hz =
        kRp1GpclkNominalParentFrequencyHz;
    input.source_rate_ppm = plan.calibration.ppm;
    input.intrinsic_source_rate_ppm = chipsetIntrinsicOffsetPpm(ClockChipset::Rp1);
    input.maximum_output_hz = kRp1GpclkMaximumDirectOutputHz;
    input.dither_sequence_length = Rp1GpclkBackend::kWritesPerSymbol;
    const auto planned = planRp1GpclkWspr(input);
    if (!planned.ok)
        return reject(planned.error);

    Rp1GpclkProviderEventProgram program;
    program.fractional_bits = planned.plan.fractional_bits;
    program.tick_divider = Rp1GpclkBackend::kTickDivider;

    auto toneForFrequency = [&](double frequency) -> std::size_t {
        std::size_t best = 0;
        double error = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < planned.plan.tones.size(); ++i)
        {
            const double candidate = std::fabs(
                planned.plan.tones[i].requested_frequency_hz - frequency);
            if (candidate < error) { error = candidate; best = i; }
        }
        return best;
    };

    std::vector<std::size_t> planned_indexes;
    std::vector<double> physical_frequencies;
    std::vector<double> realized;
    for (std::size_t i = 0; i < event_count; ++i)
    {
        const auto& event = plan.events[i];
        if (!event.rf_on) {
            realized.push_back(0.0);
            continue;
        }
        const std::size_t planned_index = toneForFrequency(event.frequency_hz);
        if (std::fabs(planned.plan.tones[planned_index].requested_frequency_hz -
                event.frequency_hz) > 0.001)
            return reject("RP1 GPCLK event frequency does not map to the planned tone table.");
        auto found = std::find(planned_indexes.begin(), planned_indexes.end(), planned_index);
        if (found == planned_indexes.end())
        {
            if (planned_indexes.size() == RP1_GPCLK_MAX_TONES)
                return reject("RP1 GPCLK finite event plan contains too many tones.");
            Rp1GpclkProviderSymbol tone;
            double frequency = 0.0;
            if (!physicalTone(planned.plan.tones[planned_index],
                    planned.plan.corrected_parent_frequency_hz,
                    input.maximum_average_error_hz, tone, frequency))
                return reject("RP1 GPCLK physical dither plan cannot meet the divider/count and average-error contract.");
            planned_indexes.push_back(planned_index);
            program.tones.push_back(tone);
            physical_frequencies.push_back(frequency);
            found = planned_indexes.end() - 1;
        }
        realized.push_back(physical_frequencies[found - planned_indexes.begin()]);
    }

    std::uint64_t total = 0;
    for (std::size_t i = 0; i < event_count; ++i)
    {
        const auto& event = plan.events[i];
        const std::uint64_t duration = plan.mode == TransmissionMode::TONE &&
                !plan.duration_was_explicit
            ? RP1_GPCLK_EVENT_DURATION_NS_MAX
            : event.duration.count() > 0
                ? static_cast<std::uint64_t>(event.duration.count()) : 0;
        if (duration < RP1_GPCLK_EVENT_DURATION_NS_MIN ||
            duration > RP1_GPCLK_EVENT_DURATION_NS_MAX || duration >
                std::numeric_limits<std::uint64_t>::max() - total)
            return reject("RP1 GPCLK event has an invalid duration.");
        std::uint16_t tone_index = 0;
        if (event.rf_on)
        {
            const auto found = std::find(planned_indexes.begin(), planned_indexes.end(),
                toneForFrequency(event.frequency_hz));
            tone_index = static_cast<std::uint16_t>(found - planned_indexes.begin());
        }
        program.events.push_back({duration, tone_index, event.rf_on});
        total += duration;
    }
    program.total_duration_ns = total;
    std::string error;
    if (!validateRp1GpclkEventProgram(program, error))
        return reject(error);
    Rp1GpclkEventCompileResult result;
    result.ok = true;
    result.program = std::move(program);
    if (terminal_off && event_count + 1 == plan.events.size()) realized.push_back(0.0);
    result.realized_frequencies = std::move(realized);
    return result;
}

} // namespace wsprrypi
