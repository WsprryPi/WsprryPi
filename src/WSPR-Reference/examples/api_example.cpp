#include "wspr/wspr_ref_api.hpp"

#include <iostream>
#include <string>

int main()
{
    const auto encoded = wspr::encode_message("AA0NT", "EM18", 20);
    if (!encoded.ok)
    {
        std::cerr << "Encode failed: " << encoded.error << "\n";
        return 1;
    }

    std::cout << "Encoded type:     " << encoded.type << "\n";
    std::cout << "Encoded callsign: " << encoded.callsign << "\n";
    std::cout << "Encoded locator:  " << encoded.locator << "\n";
    std::cout << "Encoded power:    " << encoded.power_dbm << "\n";
    std::cout << "Encoded symbols:  " << encoded.symbols << "\n\n";

    wspr::WsprDecodedMessage decoded;
    std::string error;

    if (!wspr::decode_symbols(encoded.symbols, decoded, error))
    {
        std::cerr << "Decode failed: " << error << "\n";
        return 1;
    }

    std::cout << "Decoded callsign: " << decoded.callsign << "\n";
    std::cout << "Decoded locator:  " << decoded.locator << "\n";
    std::cout << "Decoded power:    " << decoded.power_dbm << "\n\n";

    const auto type2 = wspr::encode_message("AA0NT/12", "EM18", 20);
    if (!type2.ok)
    {
        std::cerr << "Type 2 encode failed: " << type2.error << "\n";
        return 1;
    }

    const auto type3 = wspr::encode_message("<AA0NT>", "EM18IG", 20);
    if (!type3.ok)
    {
        std::cerr << "Type 3 encode failed: " << type3.error << "\n";
        return 1;
    }

    const auto correlated =
        wspr::correlate_symbol_streams(type2.symbols, type3.symbols);

    if (!correlated.ok)
    {
        std::cerr << "Correlation setup failed: " << correlated.error << "\n";
        return 1;
    }

    if (!correlated.correlated)
    {
        std::cerr << "Messages did not correlate.\n";
        return 1;
    }

    std::cout << "Correlation status: "
              << (correlated.correlated ? "CORRELATED" : "NOT CORRELATED")
              << "\n";

    std::cout
        << "Correlated callsign: " << correlated.resolved.callsign << "\n";
    std::cout << "Correlated locator:  " << correlated.resolved.locator << "\n";
    std::cout << "Correlated power:    " << correlated.resolved.power_dbm << "\n";

    if (correlated.resolved.has_ambiguity)
    {
        std::cout << "Alternate extra:     "
                  << correlated.resolved.alternate_extra << "\n";
    }

    return 0;
}
