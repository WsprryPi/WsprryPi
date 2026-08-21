#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "prepared_wspr_transmission.hpp"

namespace wsprrypi
{

enum class TransmissionMode
{
    WSPR,
    QRSS,
    FSKCW,
    DFCW,
    CW,
    TONE,
    STANDARD_FELD
};

enum class FadeShape
{
    NONE,
    LINEAR,
    RAISED_COSINE
};

struct MorseTiming
{
    std::chrono::nanoseconds dot{};
    std::chrono::nanoseconds dash{};
    std::chrono::nanoseconds intra_element_gap{};
    std::chrono::nanoseconds inter_character_gap{};
    std::chrono::nanoseconds inter_word_gap{};
};

struct EnvelopeSettings
{
    FadeShape fade_shape{FadeShape::NONE};
    std::chrono::nanoseconds fade_in{};
    std::chrono::nanoseconds fade_out{};
    std::chrono::nanoseconds fade_slice{std::chrono::milliseconds(5)};
};

struct WsprPreparedMessage
{
    std::string callsign;
    std::string locator;
    int power_dbm{0};

    std::vector<std::uint8_t> symbols;
    double symbol_period_seconds{0.0};
    double tone_spacing_hz{0.0};

    bool is_paired{false};
};

struct WsprPayload
{
    PreparedWsprTransmission prepared;
    double base_frequency_hz{0.0};
    EnvelopeSettings envelope{};
};

struct QrssPayload
{
    std::string message;
    double frequency_hz{0.0};
    MorseTiming timing{};
    EnvelopeSettings envelope{};
};

struct FskcwPayload
{
    std::string message;
    double mark_frequency_hz{0.0};
    double space_frequency_hz{0.0};
    MorseTiming timing{};
    EnvelopeSettings envelope{};
};

struct DfcwPayload
{
    std::string message;
    double dot_frequency_hz{0.0};
    double dash_frequency_hz{0.0};
    MorseTiming timing{};
    EnvelopeSettings envelope{};
};

struct CwPayload
{
    std::string message;
    double frequency_hz{0.0};
    MorseTiming timing{};
    EnvelopeSettings envelope{};
};

struct TonePayload
{
    double frequency_hz{0.0};
    std::optional<std::chrono::nanoseconds> duration{};
    EnvelopeSettings envelope{};
};

struct StandardFeldPayload
{
    std::string message;
    double frequency_hz{0.0};
    std::string profile_id{"standard-feld-wsprry-v1"};
};

using TransmissionPayload = std::variant<
    WsprPayload,
    QrssPayload,
    FskcwPayload,
    DfcwPayload,
    CwPayload,
    TonePayload,
    StandardFeldPayload>;

} // namespace wsprrypi
