#include "wspr/wspr_ref_encoder.hpp"
#include "wspr/wspr_ref_decoder.hpp"
#include "wspr/wspr_ref_unpack.hpp"
#include "wspr/wspr_constants.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

struct Type2Case
{
    std::string callsign;
    std::string locator;
    int8_t power_dbm;
    std::string expected_callsign;
    std::string expected_extra;
};

int main()
{
    const std::vector<Type2Case> cases = {
        {"AA0NT/1", "EM18", 20, "<hashed>/1", "/1"},
        {"AA0NT/12", "EM18", 20, "<hashed>/12", "/12"},
        {"W0/AA0NT", "EM18", 20, "W0/<hashed>", "W0/"}};

    wspr::WsprRefEncoder encoder;
    wspr::WsprRefDecoder decoder;
    wspr::WsprRefUnpacker unpacker;

    bool all_pass = true;

    for (const auto &tc : cases)
    {
        uint8_t symbols[wspr::WSPR_SYMBOL_COUNT] = {};
        uint8_t payload_bits[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};

        encoder.wspr_encode(
            tc.callsign.c_str(),
            tc.locator.c_str(),
            tc.power_dbm,
            symbols);

        std::string symbol_text;
        symbol_text.reserve(wspr::WSPR_SYMBOL_COUNT);

        for (std::size_t i = 0; i < wspr::WSPR_SYMBOL_COUNT; ++i)
            symbol_text.push_back(static_cast<char>('0' + symbols[i]));

        std::string error;
        if (!decoder.decode_payload_bits_from_symbols(
                symbol_text,
                payload_bits,
                error))
        {
            std::cerr << "Payload decode failed for " << tc.callsign
                      << ": " << error << "\n";
            return 1;
        }

        wspr::WsprDecodedMessage msg;
        if (!unpacker.unpack_type2(
                payload_bits,
                wspr::WSPR_PAYLOAD_BIT_COUNT,
                msg))
        {
            std::cerr << "Type 2 unpack failed for " << tc.callsign
                      << ": " << msg.error << "\n";
            return 1;
        }

        const bool pass =
            msg.valid &&
            msg.type == wspr::WsprMessageType::Type2 &&
            msg.callsign == tc.expected_callsign &&
            msg.extra == tc.expected_extra &&
            msg.power_dbm == tc.power_dbm;

        std::cout << "Case: " << tc.callsign << "\n";
        std::cout << "  Decoded callsign: " << msg.callsign << "\n";
        std::cout << "  Decoded extra:    " << msg.extra << "\n";
        std::cout << "  Decoded power:    " << msg.power_dbm << "\n";
        std::cout << "  Result:           " << (pass ? "PASS" : "FAIL") << "\n\n";

        if (!pass)
            all_pass = false;
    }

    std::cout << "Summary: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass ? 0 : 1;
}
