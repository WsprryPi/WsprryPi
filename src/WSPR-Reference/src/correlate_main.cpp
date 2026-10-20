#include "wspr/wspr_ref_api.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace
{
    using json = nlohmann::json;

    void print_message(const wspr::WsprDecodedMessage &message)
    {
        std::cout << "  Type:     ";

        switch (message.type)
        {
        case wspr::WsprMessageType::Type1:
            std::cout << "Type1\n";
            break;
        case wspr::WsprMessageType::Type2:
            std::cout << "Type2\n";
            break;
        case wspr::WsprMessageType::Type3:
            std::cout << "Type3\n";
            break;
        default:
            std::cout << "Unknown\n";
            break;
        }

        std::cout << "  Callsign: " << message.callsign << "\n";

        if (!message.extra.empty())
            std::cout << "  Extra:    " << message.extra << "\n";

        if (message.has_ambiguity)
            std::cout << "  Ambiguous with: "
                      << message.alternate_extra << "\n";

        if (message.has_hash)
            std::cout << "  Hash:     " << message.callsign_hash << "\n";

        if (!message.locator.empty())
            std::cout << "  Locator:  " << message.locator << "\n";

        std::cout << "  Power:    " << message.power_dbm << "\n";
        std::cout << "  Partial:  "
                  << (message.is_partial ? "true" : "false") << "\n";

        std::cout << "\n";
    }

    void print_quiet_correlated(const wspr::WsprDecodedMessage &message)
    {
        std::cout << "CORRELATED ";

        switch (message.type)
        {
        case wspr::WsprMessageType::Type1:
            std::cout << "TYPE1 ";
            break;
        case wspr::WsprMessageType::Type2:
            std::cout << "TYPE2 ";
            break;
        case wspr::WsprMessageType::Type3:
            std::cout << "TYPE3 ";
            break;
        default:
            std::cout << "UNKNOWN ";
            break;
        }

        std::cout << message.callsign << " ";

        if (message.has_hash)
            std::cout << message.callsign_hash << " ";
        else
            std::cout << "- ";

        if (!message.locator.empty())
            std::cout << message.locator << " ";
        else
            std::cout << "- ";

        std::cout << message.power_dbm;

        if (message.has_ambiguity)
            std::cout << " ALT " << message.alternate_extra;

        std::cout << "\n";
    }

    void print_quiet_uncorrelated(
        const std::string &prefix,
        const wspr::WsprDecodedMessage &message)
    {
        std::cout << prefix << " ";

        switch (message.type)
        {
        case wspr::WsprMessageType::Type1:
            std::cout
                << "TYPE1 "
                << message.callsign << " "
                << message.locator << " "
                << message.power_dbm << "\n";
            break;

        case wspr::WsprMessageType::Type2:
            std::cout
                << "TYPE2 "
                << message.callsign << " "
                << message.power_dbm;

            if (message.has_ambiguity)
                std::cout << " ALT " << message.alternate_extra;

            std::cout << "\n";
            break;

        case wspr::WsprMessageType::Type3:
            std::cout
                << "TYPE3 "
                << message.callsign << " "
                << message.callsign_hash << " "
                << message.locator << " "
                << message.power_dbm << "\n";
            break;

        default:
            std::cout << "UNKNOWN\n";
            break;
        }
    }

    std::string type_to_string(wspr::WsprMessageType type)
    {
        switch (type)
        {
        case wspr::WsprMessageType::Type1:
            return "TYPE1";
        case wspr::WsprMessageType::Type2:
            return "TYPE2";
        case wspr::WsprMessageType::Type3:
            return "TYPE3";
        default:
            return "UNKNOWN";
        }
    }

    json message_to_json(const wspr::WsprDecodedMessage &message)
    {
        json j;

        j["type"] = type_to_string(message.type);
        j["callsign"] = message.callsign;
        j["power_dbm"] = message.power_dbm;
        j["is_partial"] = message.is_partial;
        j["has_hash"] = message.has_hash;
        j["has_ambiguity"] = message.has_ambiguity;

        if (!message.locator.empty())
            j["locator"] = message.locator;

        if (!message.extra.empty())
            j["extra"] = message.extra;

        if (message.has_hash)
            j["hash"] = message.callsign_hash;

        if (message.has_ambiguity)
            j["alternate_extra"] = message.alternate_extra;

        return j;
    }
} // namespace

int main(int argc, char **argv)
{
    bool quiet = false;
    bool json_mode = false;
    int argi = 1;

    while (argi < argc)
    {
        const std::string arg = argv[argi];

        if (arg == "--quiet")
        {
            quiet = true;
            ++argi;
            continue;
        }

        if (arg == "--json")
        {
            json_mode = true;
            ++argi;
            continue;
        }

        break;
    }

    if (quiet && json_mode)
    {
        std::cerr << "Use either --quiet or --json, not both.\n";
        return 1;
    }

    if ((argc - argi) != 2)
    {
        std::cerr
            << "Usage: wspr-correlate [--quiet|--json] <symbols1> <symbols2>\n";
        return 1;
    }

    const wspr::WsprCorrelateResult result =
        wspr::correlate_symbol_streams(argv[argi], argv[argi + 1]);

    if (!result.ok)
    {
        std::cerr << result.error << "\n";
        return 1;
    }

    if (json_mode)
    {
        json j;

        if (result.correlated)
        {
            j = message_to_json(result.resolved);
            j["status"] = "CORRELATED";
        }
        else
        {
            j["status"] = "UNCORRELATED";
            j["message1"] = message_to_json(result.message1);
            j["message2"] = message_to_json(result.message2);
        }

        std::cout << j.dump(2) << "\n";
        return 0;
    }

    if (quiet)
    {
        if (result.correlated)
        {
            print_quiet_correlated(result.resolved);
            return 0;
        }

        print_quiet_uncorrelated("UNCORRELATED1", result.message1);
        print_quiet_uncorrelated("UNCORRELATED2", result.message2);
        return 0;
    }

    std::cout << "Decoded input 1\n";
    std::cout << "===============\n";
    print_message(result.message1);

    std::cout << "Decoded input 2\n";
    std::cout << "===============\n";
    print_message(result.message2);

    if (result.correlated)
    {
        std::cout << "Correlated result\n";
        std::cout << "=================\n";
        print_message(result.resolved);
        return 0;
    }

    std::cout << "No correlated result available.\n";
    return 0;
}
