#include "wspr/wspr_ref_api.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <string>

namespace
{
    using json = nlohmann::json;

    std::string cli_type_from_plan(const std::string &plan_type)
    {
        if (plan_type == "Type1Single")
            return "TYPE1";
        if (plan_type == "Type2Single")
            return "TYPE2";
        if (plan_type == "Type3Single")
            return "TYPE3";
        if (plan_type == "Type2Type3Paired")
            return "TYPE2+TYPE3";
        return "UNKNOWN";
    }
} // namespace

int main(int argc, char **argv)
{
    bool quiet = false;
    bool json_mode = false;
    bool require_paired = false;
    int argi = 1;

    while (argi < argc)
    {
        const std::string arg = argv[argi];

        if (arg == "--quiet" || arg == "--symbols-only")
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

        if (arg == "--paired")
        {
            require_paired = true;
            ++argi;
            continue;
        }

        break;
    }

    if (quiet && json_mode)
    {
        std::cerr << "Use either --quiet/--symbols-only or --json, not both.\n";
        return 1;
    }

    if ((argc - argi) != 3)
    {
        std::cerr
            << "Usage: wspr-encode [--quiet|--symbols-only|--json|--paired] "
            << "<callsign> <locator> <power_dbm>\n";
        return 1;
    }

    const std::string callsign = argv[argi];
    const std::string locator = argv[argi + 1];

    int power_dbm = 0;
    try
    {
        power_dbm = std::stoi(argv[argi + 2]);
    }
    catch (const std::exception &)
    {
        std::cerr << "Invalid power value.\n";
        return 1;
    }

    const wspr::WsprEncodeResult result =
        require_paired
            ? wspr::encode_message(
                  callsign,
                  locator,
                  power_dbm,
                  wspr::TransmissionPlanPreference::RequirePaired)
            : wspr::encode_message(
                  callsign,
                  locator,
                  power_dbm);

    if (!result.ok)
    {
        std::cerr << result.error << "\n";
        return 1;
    }

    const std::string cli_type = cli_type_from_plan(result.type);
    const bool is_paired = result.symbols_list.size() > 1;

    if (json_mode)
    {
        json j;
        j["type"] = cli_type;
        j["plan_type"] = result.type;
        j["callsign"] = result.callsign;
        j["locator"] = result.locator;
        j["power_dbm"] = result.power_dbm;
        j["message_count"] = result.symbols_list.size();

        if (is_paired)
        {
            j["symbols_list"] = result.symbols_list;
        }
        else
        {
            j["symbols"] = result.symbols;
        }

        std::cout << j.dump(2) << "\n";
        return 0;
    }

    if (quiet)
    {
        if (is_paired)
        {
            for (const auto &symbols : result.symbols_list)
                std::cout << symbols << "\n";
        }
        else
        {
            std::cout << result.symbols << "\n";
        }

        return 0;
    }

    std::cout << "Type: " << cli_type << "\n";
    std::cout << "Plan type: " << result.type << "\n";
    std::cout << "Callsign: " << result.callsign << "\n";
    std::cout << "Locator: " << result.locator << "\n";
    std::cout << "Power: " << result.power_dbm << " dBm\n";
    std::cout << "Preference: "
              << (require_paired ? "RequirePaired" : "Auto")
              << "\n";

    if (is_paired)
    {
        std::cout << "Message count: " << result.symbols_list.size() << "\n";
        for (std::size_t i = 0; i < result.symbols_list.size(); ++i)
        {
            std::cout << "Symbols " << (i + 1) << ": "
                      << result.symbols_list[i] << "\n";
        }
    }
    else
    {
        std::cout << "Symbols: " << result.symbols << "\n";
    }

    return 0;
}
