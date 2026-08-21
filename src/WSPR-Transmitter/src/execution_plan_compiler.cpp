#include "execution_plan_compiler.hpp"
#include "standard_feld.hpp"
#include "standard_feld_asset.hpp"

#include <chrono>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace wsprrypi
{
namespace
{

constexpr double kWsprSymbolPeriodSeconds = 8192.0 / 12000.0;
constexpr double kWsprToneSpacingHz = 1.0 / kWsprSymbolPeriodSeconds;
constexpr auto kDefaultToneDuration = std::chrono::hours(24);

std::string_view morse_code_for(char ch)
{
    switch (std::toupper(static_cast<unsigned char>(ch)))
    {
    case 'A': return ".-";
    case 'B': return "-...";
    case 'C': return "-.-.";
    case 'D': return "-..";
    case 'E': return ".";
    case 'F': return "..-.";
    case 'G': return "--.";
    case 'H': return "....";
    case 'I': return "..";
    case 'J': return ".---";
    case 'K': return "-.-";
    case 'L': return ".-..";
    case 'M': return "--";
    case 'N': return "-.";
    case 'O': return "---";
    case 'P': return ".--.";
    case 'Q': return "--.-";
    case 'R': return ".-.";
    case 'S': return "...";
    case 'T': return "-";
    case 'U': return "..-";
    case 'V': return "...-";
    case 'W': return ".--";
    case 'X': return "-..-";
    case 'Y': return "-.--";
    case 'Z': return "--..";
    case '0': return "-----";
    case '1': return ".----";
    case '2': return "..---";
    case '3': return "...--";
    case '4': return "....-";
    case '5': return ".....";
    case '6': return "-....";
    case '7': return "--...";
    case '8': return "---..";
    case '9': return "----.";
    case '/': return "-..-.";
    case '?': return "..--..";
    case '.': return ".-.-.-";
    case ',': return "--..--";
    case '-': return "-....-";
    case '+': return ".-.-.";
    case '=': return "-...-";
    default: return {};
    }
}

bool is_space_like(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

void validate_positive_duration(
    std::chrono::nanoseconds duration,
    const char* field_name)
{
    if (duration <= std::chrono::nanoseconds::zero())
    {
        throw std::runtime_error(
            std::string("Morse timing field is invalid: ") + field_name);
    }
}

void append_event(
    ExecutionPlan& plan,
    std::chrono::nanoseconds& offset,
    RfEventType type,
    bool rf_on,
    double frequency_hz,
    std::chrono::nanoseconds duration,
    const EnvelopeSettings& envelope,
    int message_char_index = -1)
{
    if (duration <= std::chrono::nanoseconds::zero())
        return;

    RfEvent event;
    event.offset_from_start = offset;
    event.duration = duration;
    event.type = type;
    event.frequency_hz = frequency_hz;
    event.rf_on = rf_on;
    event.message_char_index = message_char_index;
    event.envelope = envelope;
    plan.events.push_back(event);
    offset += duration;
}

template <typename MarkFn, typename GapFn>
void expand_morse_message(
    const std::string& message,
    MarkFn&& emit_mark,
    GapFn&& emit_gap)
{
    enum class GapKind
    {
        IntraElement,
        InterCharacter,
        InterWord
    };

    for (std::size_t i = 0; i < message.size(); ++i)
    {
        const char ch = message[i];

        if (is_space_like(ch))
            continue;

        const std::string_view morse = morse_code_for(ch);
        if (morse.empty())
            throw std::runtime_error("Payload contains unsupported character.");

        for (std::size_t j = 0; j < morse.size(); ++j)
        {
            emit_mark(morse[j], static_cast<int>(i));

            if (j + 1U < morse.size())
                emit_gap(GapKind::IntraElement, static_cast<int>(i));
        }

        std::size_t next = i + 1U;
        while (next < message.size() && is_space_like(message[next]))
            ++next;

        if (next >= message.size())
            continue;

        const int gap_char_index =
            next > (i + 1U)
                ? static_cast<int>(i + 1U)
                : static_cast<int>(i);

        emit_gap(next > (i + 1U) ? GapKind::InterWord
                                 : GapKind::InterCharacter,
                 gap_char_index);
    }
}

std::size_t resolve_wspr_frame_index(const PreparedWsprTransmission& prepared)
{
    if (prepared.frames.empty())
        throw std::runtime_error("WSPR payload has no prepared frames.");

    // PreparedWsprTransmission.current_frame is 1-based within the payload
    // committed for this execution request.
    if (prepared.current_frame == 0U)
        return 0U;

    const std::size_t frame_index = prepared.current_frame - 1U;
    if (frame_index >= prepared.frames.size())
        throw std::runtime_error("WSPR payload current frame index is out of range.");

    return frame_index;
}

double wspr_symbol_frequency(
    double base_frequency_hz,
    double tone_spacing_hz,
    std::uint8_t symbol)
{
    if (symbol > 3U)
        throw std::runtime_error("Invalid WSPR symbol.");

    return base_frequency_hz
        - 1.5 * tone_spacing_hz
        + static_cast<double>(symbol) * tone_spacing_hz;
}

PlanSummary build_summary(const std::vector<RfEvent>& events)
{
    PlanSummary summary{};

    summary.event_count = events.size();
    if (events.empty())
        return summary;

    summary.total_duration =
        events.back().offset_from_start + events.back().duration;

    bool first = true;
    for (const auto& event : events)
    {
        if (!event.rf_on)
            continue;

        if (first)
        {
            summary.min_frequency_hz = event.frequency_hz;
            summary.max_frequency_hz = event.frequency_hz;
            first = false;
        }
        else
        {
            if (event.frequency_hz < summary.min_frequency_hz)
                summary.min_frequency_hz = event.frequency_hz;
            if (event.frequency_hz > summary.max_frequency_hz)
                summary.max_frequency_hz = event.frequency_hz;
        }
    }

    return summary;
}

} // namespace

ExecutionPlan ExecutionPlanCompiler::compile(
    const TransmissionRequest& request) const
{
    return std::visit(
        [this, &request](const auto& payload) -> ExecutionPlan
        {
            using PayloadT = std::decay_t<decltype(payload)>;

            if constexpr (std::is_same_v<PayloadT, WsprPayload>)
                return compile_wspr(request, payload);
            else if constexpr (std::is_same_v<PayloadT, QrssPayload>)
                return compile_qrss(request, payload);
            else if constexpr (std::is_same_v<PayloadT, FskcwPayload>)
                return compile_fskcw(request, payload);
            else if constexpr (std::is_same_v<PayloadT, DfcwPayload>)
                return compile_dfcw(request, payload);
            else if constexpr (std::is_same_v<PayloadT, CwPayload>)
                return compile_cw(request, payload);
            else if constexpr (std::is_same_v<PayloadT, TonePayload>)
                return compile_tone(request, payload);
            else
                return compile_standard_feld(request, payload);
        },
        request.payload);
}

ExecutionPlan ExecutionPlanCompiler::compile_wspr(
    const TransmissionRequest& request,
    const WsprPayload& payload) const
{
    const std::size_t frame_index = resolve_wspr_frame_index(payload.prepared);

    if (payload.base_frequency_hz <= 0.0)
        throw std::runtime_error("WSPR payload base frequency is invalid.");

    const auto symbol_duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(kWsprSymbolPeriodSeconds));

    const auto& frame = payload.prepared.frames[frame_index];

    ExecutionPlan plan;
    plan.request_id = request.id;
    plan.mode = request.mode;
    plan.backend = request.output.backend;
    plan.reference_frequency_hz = payload.base_frequency_hz;
    plan.calibration = request.calibration;
    plan.policy = request.policy;
    plan.events.reserve(frame.symbols.size());

    std::chrono::nanoseconds offset{0};

    for (std::uint8_t symbol : frame.symbols)
    {
        RfEvent event;
        event.offset_from_start = offset;
        event.duration = symbol_duration;
        event.type = RfEventType::HOLD;
        event.frequency_hz = wspr_symbol_frequency(
            payload.base_frequency_hz,
            kWsprToneSpacingHz,
            symbol);
        event.rf_on = true;
        event.envelope = payload.envelope;

        plan.events.push_back(event);
        offset += symbol_duration;
    }

    plan.summary = build_summary(plan.events);
    return plan;
}

ExecutionPlan ExecutionPlanCompiler::compile_qrss(
    const TransmissionRequest& request,
    const QrssPayload& payload) const
{
    if (payload.message.empty())
        throw std::runtime_error("QRSS payload message is empty.");

    if (payload.frequency_hz <= 0.0)
        throw std::runtime_error("QRSS payload frequency is invalid.");

    validate_positive_duration(payload.timing.dot, "dot");
    validate_positive_duration(payload.timing.dash, "dash");
    validate_positive_duration(payload.timing.intra_element_gap, "intra_element_gap");
    validate_positive_duration(payload.timing.inter_character_gap, "inter_character_gap");
    validate_positive_duration(payload.timing.inter_word_gap, "inter_word_gap");

    ExecutionPlan plan;
    plan.request_id = request.id;
    plan.mode = request.mode;
    plan.backend = request.output.backend;
    // The current Raspberry Pi backend compatibility path still configures a
    // WSPR-style 4-tone table. QRSS execution uses symbol 0 from that table,
    // so the backend reference stays offset by 1.5 tone spacings while the
    // emitted events carry the fixed user-requested frequency.
    plan.reference_frequency_hz = payload.frequency_hz + 1.5 * kWsprToneSpacingHz;
    plan.calibration = request.calibration;
    plan.policy = request.policy;

    std::chrono::nanoseconds offset{0};

    try
    {
        expand_morse_message(
            payload.message,
            [&](char element, int message_char_index)
            {
                const auto duration =
                    (element == '.') ? payload.timing.dot : payload.timing.dash;
                append_event(
                    plan,
                    offset,
                    RfEventType::RF_ON,
                    true,
                    payload.frequency_hz,
                    duration,
                    payload.envelope,
                    message_char_index);
            },
            [&](auto gap_kind, int message_char_index)
            {
                append_event(
                    plan,
                    offset,
                    RfEventType::RF_OFF,
                    false,
                    payload.frequency_hz,
                    gap_kind == decltype(gap_kind)::IntraElement
                        ? payload.timing.intra_element_gap
                        : (gap_kind == decltype(gap_kind)::InterWord
                               ? payload.timing.inter_word_gap
                               : payload.timing.inter_character_gap),
                    payload.envelope,
                    message_char_index);
            });
    }
    catch (const std::runtime_error& e)
    {
        if (std::string_view(e.what()) == "Payload contains unsupported character.")
            throw std::runtime_error("QRSS payload contains unsupported character.");
        throw;
    }

    if (plan.events.empty())
        throw std::runtime_error("QRSS payload produced no execution events.");

    plan.summary = build_summary(plan.events);
    return plan;
}

ExecutionPlan ExecutionPlanCompiler::compile_fskcw(
    const TransmissionRequest& request,
    const FskcwPayload& payload) const
{
    if (payload.message.empty())
        throw std::runtime_error("FSKCW payload message is empty.");

    if (payload.mark_frequency_hz <= 0.0)
        throw std::runtime_error("FSKCW payload mark frequency is invalid.");

    if (payload.space_frequency_hz <= 0.0)
        throw std::runtime_error("FSKCW payload space frequency is invalid.");

    if (payload.mark_frequency_hz <= payload.space_frequency_hz)
        throw std::runtime_error("FSKCW mark frequency must be greater than space frequency.");

    validate_positive_duration(payload.timing.dot, "dot");
    validate_positive_duration(payload.timing.dash, "dash");
    validate_positive_duration(payload.timing.intra_element_gap, "intra_element_gap");
    validate_positive_duration(payload.timing.inter_character_gap, "inter_character_gap");
    validate_positive_duration(payload.timing.inter_word_gap, "inter_word_gap");

    ExecutionPlan plan;
    plan.request_id = request.id;
    plan.mode = request.mode;
    plan.backend = request.output.backend;
    const double tone_spacing_hz =
        payload.mark_frequency_hz - payload.space_frequency_hz;
    plan.reference_frequency_hz =
        payload.space_frequency_hz + 1.5 * tone_spacing_hz;
    plan.calibration = request.calibration;
    plan.policy = request.policy;

    std::chrono::nanoseconds offset{0};

    try
    {
        expand_morse_message(
            payload.message,
            [&](char element, int message_char_index)
            {
                const auto duration =
                    (element == '.') ? payload.timing.dot : payload.timing.dash;
                append_event(
                    plan,
                    offset,
                    RfEventType::HOLD,
                    true,
                    payload.mark_frequency_hz,
                    duration,
                    payload.envelope,
                    message_char_index);
            },
            [&](auto gap_kind, int message_char_index)
            {
                append_event(
                    plan,
                    offset,
                    RfEventType::HOLD,
                    true,
                    payload.space_frequency_hz,
                    gap_kind == decltype(gap_kind)::IntraElement
                        ? payload.timing.intra_element_gap
                        : (gap_kind == decltype(gap_kind)::InterWord
                               ? payload.timing.inter_word_gap
                               : payload.timing.inter_character_gap),
                    payload.envelope,
                    message_char_index);
            });
    }
    catch (const std::runtime_error& e)
    {
        if (std::string_view(e.what()) == "Payload contains unsupported character.")
            throw std::runtime_error("FSKCW payload contains unsupported character.");
        throw;
    }

    if (plan.events.empty())
        throw std::runtime_error("FSKCW payload produced no execution events.");

    plan.summary = build_summary(plan.events);
    plan.summary.min_frequency_hz = payload.space_frequency_hz;
    plan.summary.max_frequency_hz = payload.mark_frequency_hz;
    return plan;
}

ExecutionPlan ExecutionPlanCompiler::compile_dfcw(
    const TransmissionRequest& request,
    const DfcwPayload& payload) const
{
    if (payload.message.empty())
        throw std::runtime_error("DFCW payload message is empty.");

    if (payload.dot_frequency_hz <= 0.0)
        throw std::runtime_error("DFCW payload dot frequency is invalid.");

    if (payload.dash_frequency_hz <= 0.0)
        throw std::runtime_error("DFCW payload dash frequency is invalid.");

    if (payload.dot_frequency_hz == payload.dash_frequency_hz)
        throw std::runtime_error("DFCW dot and dash frequencies must differ.");

    validate_positive_duration(payload.timing.dot, "dot");
    validate_positive_duration(payload.timing.dash, "dash");
    validate_positive_duration(payload.timing.intra_element_gap, "intra_element_gap");
    validate_positive_duration(payload.timing.inter_character_gap, "inter_character_gap");
    validate_positive_duration(payload.timing.inter_word_gap, "inter_word_gap");

    ExecutionPlan plan;
    plan.request_id = request.id;
    plan.mode = request.mode;
    plan.backend = request.output.backend;
    plan.reference_frequency_hz =
        std::min(payload.dot_frequency_hz, payload.dash_frequency_hz) +
        1.5 * std::abs(payload.dash_frequency_hz - payload.dot_frequency_hz);
    plan.calibration = request.calibration;
    plan.policy = request.policy;

    std::chrono::nanoseconds offset{0};

    try
    {
        expand_morse_message(
            payload.message,
            [&](char element, int message_char_index)
            {
                append_event(
                    plan,
                    offset,
                    RfEventType::HOLD,
                    true,
                    element == '.'
                        ? payload.dot_frequency_hz
                        : payload.dash_frequency_hz,
                    payload.timing.dot,
                    payload.envelope,
                    message_char_index);
            },
            [&](auto gap_kind, int message_char_index)
            {
                append_event(
                    plan,
                    offset,
                    RfEventType::RF_OFF,
                    false,
                    0.0,
                    gap_kind == decltype(gap_kind)::IntraElement
                        ? payload.timing.intra_element_gap
                        : (gap_kind == decltype(gap_kind)::InterWord
                               ? payload.timing.inter_word_gap
                               : payload.timing.inter_character_gap),
                    payload.envelope,
                    message_char_index);
            });
    }
    catch (const std::runtime_error& e)
    {
        if (std::string_view(e.what()) == "Payload contains unsupported character.")
            throw std::runtime_error("DFCW payload contains unsupported character.");
        throw;
    }

    if (plan.events.empty())
        throw std::runtime_error("DFCW payload produced no execution events.");

    plan.summary = build_summary(plan.events);
    plan.summary.min_frequency_hz =
        std::min(payload.dot_frequency_hz, payload.dash_frequency_hz);
    plan.summary.max_frequency_hz =
        std::max(payload.dot_frequency_hz, payload.dash_frequency_hz);
    return plan;
}

ExecutionPlan ExecutionPlanCompiler::compile_cw(
    const TransmissionRequest&,
    const CwPayload&) const
{
    throw std::runtime_error("CW execution-plan compilation is not implemented.");
}

ExecutionPlan ExecutionPlanCompiler::compile_tone(
    const TransmissionRequest& request,
    const TonePayload& payload) const
{
    if (payload.frequency_hz <= 0.0)
        throw std::runtime_error("Tone payload frequency is invalid.");

    const std::chrono::nanoseconds duration =
        payload.duration.value_or(kDefaultToneDuration);
    validate_positive_duration(duration, "tone duration");

    ExecutionPlan plan;
    plan.request_id = request.id;
    plan.mode = request.mode;
    plan.backend = request.output.backend;
    plan.reference_frequency_hz = payload.frequency_hz;
    plan.calibration = request.calibration;
    plan.policy = request.policy;
    plan.duration_was_explicit = payload.duration.has_value();

    RfEvent on_event;
    on_event.offset_from_start = std::chrono::nanoseconds::zero();
    on_event.duration = duration;
    on_event.type = RfEventType::RF_ON;
    on_event.frequency_hz = payload.frequency_hz;
    on_event.rf_on = true;
    on_event.envelope = payload.envelope;
    plan.events.push_back(on_event);

    RfEvent off_event;
    off_event.offset_from_start = duration;
    off_event.duration = std::chrono::nanoseconds{1};
    off_event.type = RfEventType::RF_OFF;
    off_event.frequency_hz = payload.frequency_hz;
    off_event.rf_on = false;
    off_event.envelope = payload.envelope;
    plan.events.push_back(off_event);

    plan.summary = build_summary(plan.events);
    return plan;
}

ExecutionPlan ExecutionPlanCompiler::compile_standard_feld(
    const TransmissionRequest& request,
    const StandardFeldPayload& payload) const
{
    if (payload.frequency_hz <= 0.0)
        throw std::runtime_error("Standard Feld payload frequency is invalid.");

    if (payload.profile_id != standard_feld::kProfileId)
        throw std::runtime_error("Standard Feld payload profile is unsupported.");

    // Validate and normalize the complete input before constructing any plan.
    const std::string normalized =
        standard_feld::normalize_message(payload.message);

    constexpr std::uint64_t max_message_size =
        std::numeric_limits<std::uint64_t>::max() /
            standard_feld::kPositionsPerCell -
        2U;
    if (normalized.size() > max_message_size)
        throw std::runtime_error("Standard Feld payload message is too large.");

    ExecutionPlan plan;
    plan.request_id = request.id;
    plan.mode = request.mode;
    plan.backend = request.output.backend;
    plan.reference_frequency_hz = payload.frequency_hz;
    plan.calibration = request.calibration;
    plan.policy = request.policy;

    const std::uint64_t cell_count = normalized.size() + 2U;
    const std::uint64_t total_positions =
        cell_count * standard_feld::kPositionsPerCell;
    plan.events.reserve(static_cast<std::size_t>(total_positions));

    const auto boundary_ns = [](std::uint64_t position)
    {
        // Exact integer round-half-up of n * 1,000,000,000 / 245.
        constexpr std::uint64_t numerator = 1'000'000'000ULL;
        constexpr std::uint64_t denominator =
            standard_feld::kPositionsPerSecond;
        return std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                (position * numerator + denominator / 2U) / denominator)};
    };

    for (std::uint64_t absolute = 0; absolute < total_positions; ++absolute)
    {
        const std::uint64_t cell =
            absolute / standard_feld::kPositionsPerCell;
        const std::uint64_t within_cell =
            absolute % standard_feld::kPositionsPerCell;
        const auto column = static_cast<std::uint8_t>(
            within_cell / standard_feld::kPhysicalPositionsPerColumn);
        const auto physical_position = static_cast<std::uint8_t>(
            within_cell % standard_feld::kPhysicalPositionsPerColumn);

        RfEvent::RasterProgress progress;
        if (cell == 0U)
        {
            progress.cell_kind = RfEvent::RasterProgress::CellKind::LEADER;
        }
        else if (cell + 1U == cell_count)
        {
            progress.cell_kind = RfEvent::RasterProgress::CellKind::TRAILER;
        }
        else
        {
            progress.cell_kind = RfEvent::RasterProgress::CellKind::MESSAGE;
            progress.normalized_char_index = static_cast<int>(cell - 1U);
        }
        progress.cell_column = column;
        progress.physical_position = physical_position;
        progress.absolute_position = absolute;

        bool rf_on = false;
        if (progress.cell_kind ==
            RfEvent::RasterProgress::CellKind::MESSAGE)
        {
            rf_on = standard_feld::physical_pixel(
                static_cast<unsigned char>(
                    normalized[static_cast<std::size_t>(cell - 1U)]),
                column,
                physical_position);
        }

        RfEvent event;
        event.offset_from_start = boundary_ns(absolute);
        event.duration = boundary_ns(absolute + 1U) - event.offset_from_start;
        event.type = rf_on ? RfEventType::RF_ON : RfEventType::RF_OFF;
        event.frequency_hz = payload.frequency_hz;
        event.rf_on = rf_on;
        event.message_char_index = progress.normalized_char_index;
        event.raster_progress = progress;
        plan.events.push_back(event);
    }

    plan.summary = build_summary(plan.events);
    return plan;
}

} // namespace wsprrypi
