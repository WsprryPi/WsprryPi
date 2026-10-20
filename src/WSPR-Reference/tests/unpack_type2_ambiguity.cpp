#include "wspr/wspr_ref_encoder.hpp"
#include "wspr/wspr_ref_decoder.hpp"
#include "wspr/wspr_ref_unpack.hpp"
#include "wspr/wspr_constants.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    const std::string callsign = "AA0NT/Z";
    const std::string locator = "EM18";
    const int8_t power_dbm = 20;

    wspr::WsprRefEncoder encoder;
    wspr::WsprRefDecoder decoder;
    wspr::WsprRefUnpacker unpacker;

    uint8_t symbols[wspr::WSPR_SYMBOL_COUNT] = {};
    uint8_t payload_bits[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};

    encoder.wspr_encode(
        callsign.c_str(),
        locator.c_str(),
        power_dbm,
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
        std::cerr << "Payload decode failed: " << error << "\n";
        return 1;
    }

    wspr::WsprDecodedMessage message;
    if (!unpacker.unpack_type2(
            payload_bits,
            wspr::WSPR_PAYLOAD_BIT_COUNT,
            message))
    {
        std::cerr << "Type 2 unpack failed: " << message.error << "\n";
        return 1;
    }

    std::cout << "Decoded callsign: " << message.callsign << "\n";
    std::cout << "Decoded extra:    " << message.extra << "\n";
    std::cout << "Decoded power:    " << message.power_dbm << "\n";
    std::cout << "Has ambiguity:    "
              << (message.has_ambiguity ? "true" : "false") << "\n";
    std::cout << "Alternate extra:  " << message.alternate_extra << "\n\n";

    const bool pass =
        message.valid &&
        message.type == wspr::WsprMessageType::Type2 &&
        message.callsign == "<hashed>/Z" &&
        message.extra == "/Z" &&
        message.power_dbm == power_dbm &&
        message.has_ambiguity &&
        message.alternate_extra == "/09";

    std::cout << "Summary: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}
