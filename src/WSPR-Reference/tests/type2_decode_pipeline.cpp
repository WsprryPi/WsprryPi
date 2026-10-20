#include "wspr/wspr_ref_api.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct DecodeCase
    {
        std::string callsign;
        std::string locator;
        int power_dbm = 0;
        std::string expected_callsign;
        std::string expected_extra;
    };
}

int main()
{
    const std::vector<DecodeCase> cases = {
        {"AA0NT/12", "EM18", 20, "<hashed>/12", "/12"},
        {"W0/AA0NT", "EM18", 20, "W0/<hashed>", "W0/"}};

    bool all_pass = true;

    for (const auto &tc : cases)
    {
        const wspr::WsprEncodeResult encoded =
            wspr::encode_message(tc.callsign, tc.locator, tc.power_dbm);

        if (!encoded.ok)
        {
            std::cerr << "Encode failed for " << tc.callsign
                      << ": " << encoded.error << "\n";
            return 1;
        }

        wspr::WsprDecodedMessage decoded;
        std::string error;
        if (!wspr::decode_symbols(encoded.symbols, decoded, error))
        {
            std::cerr << "Decode failed for " << tc.callsign
                      << ": " << error << "\n";
            return 1;
        }

        const bool pass =
            decoded.valid &&
            decoded.type == wspr::WsprMessageType::Type2 &&
            decoded.callsign == tc.expected_callsign &&
            decoded.extra == tc.expected_extra &&
            decoded.power_dbm == tc.power_dbm &&
            decoded.has_hash &&
            decoded.is_partial;

        std::cout << "Decode case: " << tc.callsign << "\n";
        std::cout << "  Callsign: " << decoded.callsign << "\n";
        std::cout << "  Extra:    " << decoded.extra << "\n";
        std::cout << "  Result:   " << (pass ? "PASS" : "FAIL") << "\n\n";

        if (!pass)
            all_pass = false;
    }

    const wspr::WsprEncodeResult type2 =
        wspr::encode_message("AA0NT/12", "EM18", 20);
    const wspr::WsprEncodeResult type3 =
        wspr::encode_message("<AA0NT>", "EM18IG", 20);

    if (!type2.ok || !type3.ok)
    {
        std::cerr << "Failed to encode correlation inputs.\n";
        return 1;
    }

    const wspr::WsprCorrelateResult correlated =
        wspr::correlate_symbol_streams(type2.symbols, type3.symbols);

    const bool correlate_pass =
        correlated.ok &&
        correlated.correlated &&
        correlated.message1.callsign == "<hashed>/12" &&
        correlated.message1.extra == "/12" &&
        correlated.message1.has_hash &&
        correlated.message2.has_hash &&
        correlated.message1.callsign_hash == correlated.message2.callsign_hash &&
        correlated.resolved.callsign == "<callsign>/12" &&
        correlated.resolved.locator == "EM18IG" &&
        correlated.resolved.power_dbm == 20 &&
        correlated.resolved.has_hash &&
        correlated.resolved.callsign_hash == correlated.message2.callsign_hash &&
        !correlated.resolved.is_partial;

    std::cout << "Correlate case:\n";
    std::cout << "  Decoded Type 2 callsign: " << correlated.message1.callsign << "\n";
    std::cout << "  Resolved callsign:       " << correlated.resolved.callsign << "\n";
    std::cout << "  Result:                  " << (correlate_pass ? "PASS" : "FAIL") << "\n\n";

    all_pass = all_pass && correlate_pass;

    const wspr::WsprEncodeResult ambiguous_type2 =
        wspr::encode_message("AA0NT/Z", "EM18", 20);

    if (!ambiguous_type2.ok)
    {
        std::cerr << "Failed to encode ambiguous Type 2 input.\n";
        return 1;
    }

    wspr::WsprDecodedMessage ambiguous_decoded;
    std::string ambiguous_error;
    if (!wspr::decode_symbols(
            ambiguous_type2.symbols,
            ambiguous_decoded,
            ambiguous_error))
    {
        std::cerr << "Decode failed for ambiguous Type 2 input: "
                  << ambiguous_error << "\n";
        return 1;
    }

    const bool ambiguous_decode_pass =
        ambiguous_decoded.valid &&
        ambiguous_decoded.type == wspr::WsprMessageType::Type2 &&
        ambiguous_decoded.callsign == "<hashed>/Z" &&
        ambiguous_decoded.extra == "/Z" &&
        ambiguous_decoded.has_ambiguity &&
        ambiguous_decoded.alternate_extra == "/09" &&
        ambiguous_decoded.is_partial;

    std::cout << "Ambiguous decode case:\n";
    std::cout << "  Callsign: " << ambiguous_decoded.callsign << "\n";
    std::cout << "  Extra:    " << ambiguous_decoded.extra << "\n";
    std::cout << "  Alt:      " << ambiguous_decoded.alternate_extra << "\n";
    std::cout << "  Result:   "
              << (ambiguous_decode_pass ? "PASS" : "FAIL") << "\n\n";

    all_pass = all_pass && ambiguous_decode_pass;

    const wspr::WsprCorrelateResult ambiguous_correlated =
        wspr::correlate_symbol_streams(ambiguous_type2.symbols, type3.symbols);

    const bool ambiguous_correlate_pass =
        ambiguous_correlated.ok &&
        ambiguous_correlated.correlated &&
        ambiguous_correlated.message1.callsign == "<hashed>/Z" &&
        ambiguous_correlated.message1.extra == "/Z" &&
        ambiguous_correlated.message1.has_ambiguity &&
        ambiguous_correlated.message1.alternate_extra == "/09" &&
        ambiguous_correlated.resolved.callsign == "<callsign>/Z" &&
        ambiguous_correlated.resolved.extra == "/Z" &&
        ambiguous_correlated.resolved.locator == "EM18IG" &&
        ambiguous_correlated.resolved.power_dbm == 20 &&
        !ambiguous_correlated.resolved.is_partial &&
        !ambiguous_correlated.resolved.has_ambiguity &&
        ambiguous_correlated.resolved.alternate_extra.empty();

    std::cout << "Ambiguous correlate case:\n";
    std::cout << "  Decoded Type 2 callsign: " << ambiguous_correlated.message1.callsign << "\n";
    std::cout << "  Resolved callsign:       " << ambiguous_correlated.resolved.callsign << "\n";
    std::cout << "  Result:                  "
              << (ambiguous_correlate_pass ? "PASS" : "FAIL") << "\n\n";

    all_pass = all_pass && ambiguous_correlate_pass;

    const wspr::WsprEncodeResult mismatched_type3 =
        wspr::encode_message("<K1ABC>", "EM18IG", 20);

    if (!mismatched_type3.ok)
    {
        std::cerr << "Failed to encode mismatched correlation inputs.\n";
        return 1;
    }

    const wspr::WsprCorrelateResult mismatched =
        wspr::correlate_symbol_streams(type2.symbols, mismatched_type3.symbols);

    const bool mismatch_pass =
        mismatched.ok &&
        !mismatched.correlated &&
        mismatched.message1.valid &&
        mismatched.message2.valid &&
        mismatched.message1.has_hash &&
        mismatched.message2.has_hash &&
        mismatched.message1.callsign_hash != mismatched.message2.callsign_hash &&
        !mismatched.resolved.valid;

    std::cout << "Mismatched correlate case:\n";
    std::cout << "  Type 2 hash: " << mismatched.message1.callsign_hash << "\n";
    std::cout << "  Type 3 hash: " << mismatched.message2.callsign_hash << "\n";
    std::cout << "  Result:      " << (mismatch_pass ? "PASS" : "FAIL") << "\n\n";

    all_pass = all_pass && mismatch_pass;

    std::cout << "Summary: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass ? 0 : 1;
}
