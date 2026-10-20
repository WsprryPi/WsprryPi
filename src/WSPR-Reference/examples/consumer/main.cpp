#include "wspr/wspr_ref_api.hpp"

#include <iostream>
#include <string>

namespace
{
    bool is_valid_symbol_stream(const std::string& symbols, std::string& error)
    {
        if (symbols.size() != 162)
        {
            error = "Expected 162 numeric WSPR symbols, got " +
                    std::to_string(symbols.size());
            return false;
        }

        for (char c : symbols)
        {
            if (c < '0' || c > '3')
            {
                error = std::string(
                            "Refusing to decode non-numeric symbol stream: ") +
                        c;
                return false;
            }
        }

        return true;
    }
} // namespace

int main()
{
    const auto encoded = wspr::encode_message("AA0NT", "EM18", 20);
    if (!encoded.ok)
    {
        std::cerr << encoded.error << "\n";
        return 1;
    }

    std::cout << "Encoded symbols: " << encoded.symbols << "\n";

    wspr::WsprDecodedMessage decoded;
    std::string error;

    if (!is_valid_symbol_stream(encoded.symbols, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    if (!wspr::decode_symbols(encoded.symbols, decoded, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    std::cout << "Decoded callsign: " << decoded.callsign << "\n";

    return 0;
}
