#include "wspr_ref_api.hpp"

#include "wspr_constants.hpp"
#include "wspr_ref_correlator.hpp"
#include "wspr_ref_decoder.hpp"
#include "wspr_ref_encoder.hpp"
#include "wspr_ref_encode_internal.hpp"
#include "wspr_ref_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
    bool validate_symbol_stream(const uint8_t *symbols, std::size_t count)
    {
        if (symbols == nullptr)
            return false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (symbols[i] > 3U)
                return false;
        }

        return true;
    }

    std::string symbols_to_string(const uint8_t *symbols, std::size_t count)
    {
        std::string out;
        out.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
            out.push_back(static_cast<char>('0' + symbols[i]));

        return out;
    }
} // namespace

namespace wspr
{
    WsprEncodeResult encode_message(
        const std::string &callsign,
        const std::string &locator,
        int power_dbm)
    {
        return encode_message(
            callsign,
            locator,
            power_dbm,
            TransmissionPlanPreference::Auto);
    }

    WsprEncodeResult encode_message(
        const std::string &callsign,
        const std::string &locator,
        int power_dbm,
        TransmissionPlanPreference preference)
    {
        return internal::encode_message_with_metadata(
                   callsign,
                   locator,
                   power_dbm,
                   preference)
            .encoded;
    }
} // namespace wspr

namespace wspr::internal
{
    WsprPreparedEncodeResult encode_message_with_metadata(
        const std::string &callsign,
        const std::string &locator,
        int power_dbm,
        TransmissionPlanPreference preference)
    {
        WsprPreparedEncodeResult prepared;
        WsprEncodeResult &result = prepared.encoded;
        WsprEncodeRuntimeMetadata &metadata = prepared.metadata;
        auto plan = plan_transmission(
            callsign,
            locator,
            power_dbm,
            preference);

        metadata.callsign_raw = callsign;
        metadata.locator_raw = locator;
        metadata.callsign_normalized = plan.normalized_callsign;
        metadata.locator_normalized = plan.normalized_locator;
        result.callsign = plan.normalized_callsign;
        result.locator = plan.normalized_locator;
        result.power_dbm = plan.power_dbm;

        if (!plan.ok)
        {
            result.error = plan.message;
            return prepared;
        }

        WsprRefEncoder encoder;
        uint8_t symbols[WSPR_SYMBOL_COUNT] = {};
        std::memset(symbols, 0, sizeof(symbols));

        switch (plan.plan)
        {
        case TransmissionPlanType::Type1Single:
        case TransmissionPlanType::Type2Single:
        case TransmissionPlanType::Type3Single:
        {
            metadata.frame_callsigns = {plan.normalized_callsign};
            metadata.frame_locators = {plan.normalized_locator};
            encoder.wspr_encode(
                plan.normalized_callsign.c_str(),
                plan.normalized_locator.c_str(),
                static_cast<int8_t>(plan.power_dbm),
                symbols);

            result.type = to_string(plan.plan);
            break;
        }

        case TransmissionPlanType::Type2Type3Paired:
        {
            WsprRefEncoder paired_encoder;

            uint8_t type2_symbols[WSPR_SYMBOL_COUNT] = {};
            uint8_t type3_symbols[WSPR_SYMBOL_COUNT] = {};

            metadata.frame_callsigns = {
                plan.type2_callsign,
                plan.type3_callsign};
            metadata.frame_locators = {
                plan.type2_locator,
                plan.type3_locator};

            paired_encoder.wspr_encode(
                plan.type2_callsign.c_str(),
                plan.type2_locator.c_str(),
                static_cast<int8_t>(plan.power_dbm),
                type2_symbols);

            if (!validate_symbol_stream(type2_symbols, WSPR_SYMBOL_COUNT))
            {
                result.error = "Generated Type 2 symbol stream is invalid.";
                return prepared;
            }

            paired_encoder.wspr_encode(
                plan.type3_callsign.c_str(),
                plan.type3_locator.c_str(),
                static_cast<int8_t>(plan.power_dbm),
                type3_symbols);

            if (!validate_symbol_stream(type3_symbols, WSPR_SYMBOL_COUNT))
            {
                result.error = "Generated Type 3 symbol stream is invalid.";
                return prepared;
            }

            result.type = to_string(plan.plan);
            result.symbols.clear();
            result.symbols_list.clear();
            result.symbols_list.push_back(
                symbols_to_string(type2_symbols, WSPR_SYMBOL_COUNT));
            result.symbols_list.push_back(
                symbols_to_string(type3_symbols, WSPR_SYMBOL_COUNT));

            metadata.total_frame_count = result.symbols_list.size();
            result.ok = true;
            return prepared;
        }

        default:
        {
            result.error = "Invalid transmission plan.";
            return prepared;
        }
        }

        if (!validate_symbol_stream(symbols, WSPR_SYMBOL_COUNT))
        {
            result.error = "Generated symbol stream is invalid.";
            return prepared;
        }

        result.symbols = symbols_to_string(symbols, WSPR_SYMBOL_COUNT);
        result.symbols_list.clear();
        result.symbols_list.push_back(result.symbols);
        metadata.total_frame_count = result.symbols_list.size();
        result.ok = true;
        return prepared;
    }

} // namespace wspr::internal

namespace wspr
{
    bool decode_symbols(
        const std::string &symbols,
        WsprDecodedMessage &message,
        std::string &error)
    {
        WsprRefDecoder decoder;
        WsprRefUnpacker unpacker;
        uint8_t payload_bits[WSPR_PAYLOAD_BIT_COUNT] = {};

        message = WsprDecodedMessage{};
        error.clear();

        if (!decoder.decode_payload_bits_from_symbols(symbols, payload_bits, error))
            return false;

        if (unpacker.unpack_type1(payload_bits, WSPR_PAYLOAD_BIT_COUNT, message))
            return true;

        if (unpacker.unpack_type2(payload_bits, WSPR_PAYLOAD_BIT_COUNT, message))
            return true;

        if (unpacker.unpack_type3(payload_bits, WSPR_PAYLOAD_BIT_COUNT, message))
            return true;

        error = "No unpacker recognized the payload.";
        return false;
    }

    bool correlate_messages(
        const WsprDecodedMessage &a,
        const WsprDecodedMessage &b,
        WsprDecodedMessage &resolved,
        std::string &error)
    {
        WsprRefCorrelator correlator;

        resolved = WsprDecodedMessage{};
        error.clear();

        correlator.add_message(a);
        correlator.add_message(b);

        if (correlator.try_resolve_last(resolved))
            return true;

        error = "No correlated result available.";
        return false;
    }

    WsprCorrelateResult correlate_symbol_streams(
        const std::string &symbols1,
        const std::string &symbols2)
    {
        WsprCorrelateResult result;
        std::string error;

        if (!decode_symbols(symbols1, result.message1, error))
        {
            result.error = "Failed to decode first message: " + error;
            return result;
        }

        if (!decode_symbols(symbols2, result.message2, error))
        {
            result.error = "Failed to decode second message: " + error;
            return result;
        }

        result.ok = true;
        result.correlated = correlate_messages(
            result.message1,
            result.message2,
            result.resolved,
            result.error);
        return result;
    }
} // namespace wspr
