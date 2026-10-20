#ifndef WSPR_REF_API_HPP
#define WSPR_REF_API_HPP

/// \file wspr_ref_api.hpp
/// \brief High-level public API for WSPR encoding, decoding, and correlation.

#include "wspr_ref_unpack.hpp"
#include "wspr_ref_plan.hpp"

#include <string>
#include <vector>

namespace wspr
{
/// \brief Result returned by \ref encode_message.
struct WsprEncodeResult
{
    /// \brief True when encoding completed successfully.
    bool ok = false;
    /// \brief Detected message type string such as `TYPE1`, `TYPE2`, or `TYPE3`.
    std::string type;
    /// \brief Input callsign copied into the result.
    std::string callsign;
    /// \brief Input locator copied into the result.
    std::string locator;
    /// \brief Input power in dBm copied into the result.
    int power_dbm = 0;
    /// \brief Encoded 162-symbol WSPR symbol stream.
    std::string symbols;
    /// @brief List of symbol stream variants that decode to the same message. This is empty when \ref ok is false.
    std::vector<std::string> symbols_list;
    /// \brief Error text when \ref ok is false.
    std::string error;
};

/// \brief Result returned by \ref correlate_symbol_streams.
struct WsprCorrelateResult
{
    /// \brief True when both symbol streams decoded successfully.
    bool ok = false;
    /// \brief True when the decoded messages could be correlated.
    bool correlated = false;
    /// \brief Decoded form of the first symbol stream.
    WsprDecodedMessage message1;
    /// \brief Decoded form of the second symbol stream.
    WsprDecodedMessage message2;
    /// \brief Resolved correlated message when \ref correlated is true.
    WsprDecodedMessage resolved;
    /// \brief Error text when setup or correlation failed.
    std::string error;
};

/// \brief Encode a WSPR message into its 162-symbol channel representation.
/// \param callsign Callsign text. A slash indicates Type 2, and angle brackets indicate Type 3.
/// \param locator Maidenhead locator. A 6-character locator also implies Type 3.
/// \param power_dbm Transmit power in dBm.
/// \return An encoding result containing the symbol stream or an error.
WsprEncodeResult encode_message(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm);

/// \brief Encode a WSPR message into its 162-symbol channel representation.
/// \param callsign Callsign text. A slash indicates Type 2, and angle brackets indicate Type 3.
/// \param locator Maidenhead locator. A 6-character locator also implies Type 3.
/// \param power_dbm Transmit power in dBm.
/// \param preference Transmission plan preference. This is only a hint and does not guarantee a specific plan type.
/// \return An encoding result containing the symbol stream or an error.
WsprEncodeResult encode_message(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm,
    TransmissionPlanPreference preference);

/// \brief Decode a 162-symbol WSPR stream into a structured message.
/// \param symbols Symbol text containing digits in the range 0-3.
/// \param message Receives the decoded message on success.
/// \param error Receives the failure reason on error.
/// \return True if any supported message type unpacked successfully.
bool decode_symbols(
    const std::string& symbols,
    WsprDecodedMessage& message,
    std::string& error);

/// \brief Attempt to correlate two previously decoded WSPR messages.
/// \param a First decoded message.
/// \param b Second decoded message.
/// \param resolved Receives the resolved message when correlation succeeds.
/// \param error Receives the failure reason when correlation fails.
/// \return True if the messages form a valid correlated pair.
bool correlate_messages(
    const WsprDecodedMessage& a,
    const WsprDecodedMessage& b,
    WsprDecodedMessage& resolved,
    std::string& error);

/// \brief Decode and correlate two WSPR symbol streams in one call.
/// \param symbols1 First 162-symbol stream.
/// \param symbols2 Second 162-symbol stream.
/// \return A correlation result containing decoded inputs and any resolved message.
WsprCorrelateResult correlate_symbol_streams(
    const std::string& symbols1,
    const std::string& symbols2);
} // namespace wspr

#endif // WSPR_REF_API_HPP
