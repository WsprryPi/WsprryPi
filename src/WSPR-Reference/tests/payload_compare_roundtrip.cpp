#include "wspr/wspr_ref_encoder.hpp"
#include "wspr/wspr_ref_decoder.hpp"
#include "wspr/wspr_constants.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    bool compare_bits(const uint8_t *a, const uint8_t *b, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (a[i] != b[i])
                return false;
        }

        return true;
    }

    void print_bits(const std::string &label, const uint8_t *bits, std::size_t count)
    {
        std::cout << label << "\n";
        for (std::size_t i = 0; i < count; ++i)
            std::cout << static_cast<unsigned>(bits[i]);
        std::cout << "\n\n";
    }
} // namespace

int main()
{
    const std::string callsign = "AA0NT";
    const std::string locator = "EM18";
    const int8_t power_dbm = 20;

    wspr::WsprRefEncoder encoder;
    wspr::WsprRefDecoder decoder;

    uint8_t symbols[wspr::WSPR_SYMBOL_COUNT] = {};
    uint8_t encoder_payload[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};
    uint8_t decoder_payload[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};

    encoder.wspr_encode(
        callsign.c_str(),
        locator.c_str(),
        power_dbm,
        symbols);

    if (!encoder.debug_get_payload_bits(
            callsign.c_str(),
            locator.c_str(),
            power_dbm,
            encoder_payload))
    {
        std::cerr << "Failed to extract encoder payload bits.\n";
        return 1;
    }

    std::string symbol_text;
    symbol_text.reserve(wspr::WSPR_SYMBOL_COUNT);

    for (std::size_t i = 0; i < wspr::WSPR_SYMBOL_COUNT; ++i)
        symbol_text.push_back(static_cast<char>('0' + symbols[i]));

    std::string error;
    if (!decoder.decode_payload_bits_from_symbols(
            symbol_text,
            decoder_payload,
            error))
    {
        std::cerr << "Failed to decode payload bits: " << error << "\n";
        return 1;
    }

    print_bits("Encoder payload bits:", encoder_payload, wspr::WSPR_PAYLOAD_BIT_COUNT);
    print_bits("Decoder payload bits:", decoder_payload, wspr::WSPR_PAYLOAD_BIT_COUNT);

    const bool match = compare_bits(
        encoder_payload,
        decoder_payload,
        wspr::WSPR_PAYLOAD_BIT_COUNT);

    std::cout << "Summary: " << (match ? "PASS" : "FAIL") << "\n";
    return match ? 0 : 1;
}
