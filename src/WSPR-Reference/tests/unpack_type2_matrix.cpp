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
    std::string expected_primary_extra;
    bool expect_ambiguity;
    std::string expected_alternate_extra;
};

int main()
{
    const std::vector<Type2Case> cases = {
        // One-character suffix
        {"AA0NT/0", "EM18", 20, "/0", false, ""},
        {"AA0NT/1", "EM18", 20, "/1", false, ""},
        {"AA0NT/P", "EM18", 20, "/P", false, ""},
        {"AA0NT/Z", "EM18", 20, "/Z", true, "/09"},

        // Two-digit numeric suffix
        {"AA0NT/00", "EM18", 20, "/Q", true, "/00"},
        {"AA0NT/12", "EM18", 20, "/12", false, ""},
        {"AA0NT/59", "EM18", 20, "/59", false, ""},
        {"AA0NT/99", "EM18", 20, "/99", false, ""},

        // Prefix
        {"W0/AA0NT", "EM18", 20, "W0/", false, ""},
        {"DL/AA0NT", "EM18", 20, "DL/", false, ""},
        {"F4/AA0NT", "EM18", 20, "F4/", false, ""},
        {"AB1/AA0NT", "EM18", 20, "AB1/", false, ""}};

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

        const bool base_pass =
            msg.valid &&
            msg.type == wspr::WsprMessageType::Type2 &&
            ((!tc.expected_primary_extra.empty() && tc.expected_primary_extra.back() == '/' &&
              msg.callsign == tc.expected_primary_extra + "<hashed>") ||
             (!tc.expected_primary_extra.empty() && tc.expected_primary_extra.front() == '/' &&
              msg.callsign == "<hashed>" + tc.expected_primary_extra)) &&
            msg.extra == tc.expected_primary_extra &&
            msg.power_dbm == tc.power_dbm &&
            msg.is_partial;

        const bool ambiguity_pass =
            !tc.expect_ambiguity ||
            (msg.has_ambiguity &&
             msg.alternate_extra == tc.expected_alternate_extra);

        const bool unambiguous_pass =
            tc.expect_ambiguity ||
            (!msg.has_ambiguity &&
             msg.alternate_extra.empty());

        const bool pass = base_pass && ambiguity_pass && unambiguous_pass;

        std::string result_label;
        if (pass && tc.expect_ambiguity)
            result_label = "AMBIGUOUS (expected overlap)";
        else if (pass)
            result_label = "PASS";
        else
            result_label = "FAIL";

        std::cout << "Case: " << tc.callsign << "\n";
        std::cout << "  Decoded callsign: " << msg.callsign << "\n";
        std::cout << "  Decoded extra:    " << msg.extra << "\n";

        if (msg.has_ambiguity)
            std::cout << "  Alternate extra:  " << msg.alternate_extra << "\n";

        std::cout << "  Decoded power:    " << msg.power_dbm << "\n";
        std::cout << "  Result:           " << result_label << "\n\n";

        if (!pass)
            all_pass = false;
    }

    std::cout << "Summary: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass ? 0 : 1;
}
