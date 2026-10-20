#include "wspr/wspr_ref_api.hpp"
#include "wspr/wspr_ref_plan.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct EncodeFailureCase
    {
        std::string callsign;
        std::string locator;
        int power_dbm = 0;
    };
}

int main()
{
    const wspr::TransmissionPlanResult plan =
        wspr::plan_transmission(
            "W0/AA0NT",
            "EM18IG",
            20,
            wspr::TransmissionPlanPreference::RequirePaired);

    if (!plan.ok)
    {
        std::cerr << "Planner failed: " << plan.message << "\n";
        return 1;
    }

    if (plan.plan != wspr::TransmissionPlanType::Type2Type3Paired)
    {
        std::cerr << "Planner did not return Type2Type3Paired.\n";
        return 1;
    }

    const wspr::WsprEncodeResult result =
        wspr::encode_message(
            "W0/AA0NT",
            "EM18IG",
            20,
            wspr::TransmissionPlanPreference::RequirePaired);

    if (!result.ok)
    {
        std::cerr << "Encode failed: " << result.error << "\n";
        return 1;
    }

    if (result.type != "Type2Type3Paired")
    {
        std::cerr << "Unexpected encode result type: " << result.type << "\n";
        return 1;
    }

    if (result.symbols_list.size() != 2)
    {
        std::cerr << "Expected 2 symbol streams, got "
                  << result.symbols_list.size() << "\n";
        return 1;
    }

    if (result.symbols_list[0].size() != 162 ||
        result.symbols_list[1].size() != 162)
    {
        std::cerr << "One or both symbol streams are not 162 symbols long.\n";
        return 1;
    }

    const std::vector<EncodeFailureCase> failure_cases = {
        {"<AA0NT>", "EM18IG", 20},
        {"AA0NT", "EM18IG", 20},
        {"W0/AA0NT", "EM18", 20},
    };

    for (const auto &tc : failure_cases)
    {
        const wspr::WsprEncodeResult failed =
            wspr::encode_message(
                tc.callsign,
                tc.locator,
                tc.power_dbm,
                wspr::TransmissionPlanPreference::RequirePaired);

        if (failed.ok)
        {
            std::cerr << "RequirePaired unexpectedly succeeded for "
                      << tc.callsign << " " << tc.locator << "\n";
            return 1;
        }
    }

    std::cout << "Type: " << result.type << "\n";
    std::cout << "Message count: " << result.symbols_list.size() << "\n";
    std::cout << "Symbols 1 length: " << result.symbols_list[0].size() << "\n";
    std::cout << "Symbols 2 length: " << result.symbols_list[1].size() << "\n";
    std::cout << "Summary: PASS\n";

    return 0;
}
