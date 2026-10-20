#include "wspr/wspr_ref_encoder.hpp"

#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
std::string encode_symbols(const std::string& callsign,
                           const std::string& locator,
                           int power_dbm)
{
    wspr::WsprRefEncoder encoder;
    uint8_t symbols[wspr::WSPR_SYMBOL_COUNT] = {};

    encoder.wspr_encode(
        callsign.c_str(),
        locator.c_str(),
        static_cast<int8_t>(power_dbm),
        symbols);

    std::string out;
    out.reserve(wspr::WSPR_SYMBOL_COUNT);

    for (std::size_t i = 0; i < wspr::WSPR_SYMBOL_COUNT; ++i)
        out.push_back(static_cast<char>('0' + symbols[i]));

    return out;
}

std::string encode_symbols_after_longer_callsign(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm)
{
    wspr::WsprRefEncoder encoder;
    uint8_t discarded[wspr::WSPR_SYMBOL_COUNT] = {};
    uint8_t symbols[wspr::WSPR_SYMBOL_COUNT] = {};

    encoder.wspr_encode("AA0NTX/1", "EM18", 20, discarded);
    encoder.wspr_encode(
        callsign.c_str(),
        locator.c_str(),
        static_cast<int8_t>(power_dbm),
        symbols);

    std::string out;
    out.reserve(wspr::WSPR_SYMBOL_COUNT);

    for (std::size_t i = 0; i < wspr::WSPR_SYMBOL_COUNT; ++i)
        out.push_back(static_cast<char>('0' + symbols[i]));

    return out;
}
} // namespace

int main()
{
    std::ifstream in("test_vectors/wspr_golden_vectors.json");
    if (!in)
    {
        std::cerr
            << "Failed to open test_vectors/wspr_golden_vectors.json\n";
        return 1;
    }

    json vectors;
    in >> vectors;

    int pass_count = 0;
    int fail_count = 0;

    for (const auto& entry : vectors)
    {
        const std::string type = entry.at("type").get<std::string>();
        const std::string callsign = entry.at("callsign").get<std::string>();
        const std::string locator = entry.at("locator").get<std::string>();
        const int power_dbm = entry.at("power_dbm").get<int>();
        const std::string expected = entry.at("symbols").get<std::string>();

        const std::string actual = encode_symbols(callsign, locator, power_dbm);

        if (actual == expected)
        {
            ++pass_count;
            std::cout
                << "PASS: "
                << type << " "
                << callsign << " "
                << locator << " "
                << power_dbm
                << "\n";
        }
        else
        {
            ++fail_count;
            std::cout
                << "FAIL: "
                << type << " "
                << callsign << " "
                << locator << " "
                << power_dbm
                << "\n";
            std::cout << "Expected: " << expected << "\n";
            std::cout << "Actual:   " << actual << "\n";
        }

        if (type == "TYPE1" && callsign == "AA0NT" && power_dbm == 37)
        {
            const std::string after_longer =
                encode_symbols_after_longer_callsign(callsign, locator, power_dbm);

            if (after_longer == expected)
            {
                ++pass_count;
                std::cout << "PASS: TYPE1 short callsign after longer callsign\n";
            }
            else
            {
                ++fail_count;
                std::cout << "FAIL: TYPE1 short callsign after longer callsign\n";
                std::cout << "Expected: " << expected << "\n";
                std::cout << "Actual:   " << after_longer << "\n";
            }
        }
    }

    std::cout
        << "Summary: PASS=" << pass_count
        << " FAIL=" << fail_count
        << "\n";

    return (fail_count == 0) ? 0 : 1;
}
