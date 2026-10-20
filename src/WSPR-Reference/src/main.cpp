#include "wspr/wspr_ref_api.hpp"

#include <iostream>
#include <string>

namespace
{
const char *infer_wspr_type(const std::string &callsign)
{
    if (!callsign.empty() && callsign.front() == '<')
        return "TYPE3";

    if (callsign.find('/') != std::string::npos)
        return "TYPE2";

    return "TYPE1";
}
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::cerr
            << "Usage: jtencode-wspr <callsign> <locator> <power_dbm>\n"
            << "\n"
            << "Examples:\n"
            << "  jtencode-wspr AA0NT EM18 20\n"
            << "  jtencode-wspr AA0NT/1 EM18 20\n"
            << "  jtencode-wspr AA0NT/12 EM18 20\n"
            << "  jtencode-wspr W0/AA0NT EM18 20\n"
            << "  jtencode-wspr \"<AA0NT>\" EM18IG 20\n";
        return 1;
    }

    const std::string callsign = argv[1];
    const std::string locator = argv[2];
    const int power = std::stoi(argv[3]);

    const wspr::WsprEncodeResult result =
        wspr::encode_message(callsign, locator, power);

    if (!result.ok)
    {
        std::cerr << "Error: " << result.error << "\n";
        return 1;
    }

    std::cout << "Type: " << infer_wspr_type(callsign) << "\n";
    std::cout << "Callsign: " << result.callsign << "\n";
    std::cout << "Locator: " << result.locator << "\n";
    std::cout << "Power: " << result.power_dbm << " dBm\n";
    std::cout << "Symbols: " << result.symbols << "\n";

    return 0;
}
