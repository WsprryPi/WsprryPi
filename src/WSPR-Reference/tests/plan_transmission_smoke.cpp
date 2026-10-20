#include "wspr/wspr_ref_plan.hpp"

#include <iostream>
#include <string>
#include <vector>

struct PlanCase
{
    std::string callsign;
    std::string locator;
    int power_dbm = 0;
    wspr::TransmissionPlanPreference preference =
        wspr::TransmissionPlanPreference::Auto;

    wspr::TransmissionPlanType expected_plan =
        wspr::TransmissionPlanType::Invalid;
    wspr::TransmissionPlanStatus expected_status =
        wspr::TransmissionPlanStatus::InternalError;
    bool expected_ok = false;
};

int main()
{
    const std::vector<PlanCase> cases = {
        {"AA0NT",
         "EM18",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Type1Single,
         wspr::TransmissionPlanStatus::Ok,
         true},
        {"W0/AA0NT",
         "EM18",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok,
         true},
        {"W0/AA0NT",
         "EM18IG",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Type2Type3Paired,
         wspr::TransmissionPlanStatus::Ok,
         true},
        {"AA0NT/1",
         "EM18",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok,
         true},
        {"<AA0NT>",
         "EM18IG",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Type3Single,
         wspr::TransmissionPlanStatus::Ok,
         true},
        {"AA0NT",
         "EM18IG",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::BareLongCallsignRequiresExplicitType3,
         false},
        {"<AA0NT>",
         "EM18",
         20,
         wspr::TransmissionPlanPreference::Auto,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::Type3RequiresSixCharLocator,
         false},
        {"AA0NT",
         "EM18",
         20,
         wspr::TransmissionPlanPreference::RequirePaired,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::PairedTransmissionUnavailable,
         false},
        {"W0/AA0NT",
         "EM18IG",
         20,
         wspr::TransmissionPlanPreference::RequirePaired,
         wspr::TransmissionPlanType::Type2Type3Paired,
         wspr::TransmissionPlanStatus::Ok,
         true},
        {"<AA0NT>",
         "EM18IG",
         20,
         wspr::TransmissionPlanPreference::RequirePaired,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::PairedTransmissionUnavailable,
         false},
        {"AA0NT",
         "EM18IG",
         20,
         wspr::TransmissionPlanPreference::RequirePaired,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::PairedTransmissionUnavailable,
         false},
        {"W0/AA0NT",
         "EM18",
         20,
         wspr::TransmissionPlanPreference::RequirePaired,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::PairedTransmissionUnavailable,
         false},
    };

    bool all_pass = true;

    for (const auto &tc : cases)
    {
        const wspr::TransmissionPlanResult result =
            wspr::plan_transmission(
                tc.callsign,
                tc.locator,
                tc.power_dbm,
                tc.preference);

        const bool pass =
            result.ok == tc.expected_ok &&
            result.plan == tc.expected_plan &&
            result.status == tc.expected_status;

        std::cout << "Case: " << tc.callsign
                  << " " << tc.locator
                  << " " << tc.power_dbm
                  << "\n";
        std::cout << "  Preference:        "
                  << wspr::to_string(tc.preference)
                  << "\n";
        std::cout << "  Result ok:        "
                  << (result.ok ? "true" : "false")
                  << "\n";
        std::cout << "  Result plan:      "
                  << wspr::to_string(result.plan)
                  << "\n";
        std::cout << "  Result status:    "
                  << wspr::to_string(result.status)
                  << "\n";
        std::cout << "  Message:          "
                  << result.message
                  << "\n";
        std::cout << "  Test result:      "
                  << (pass ? "PASS" : "FAIL")
                  << "\n\n";

        if (pass &&
            tc.expected_plan == wspr::TransmissionPlanType::Type2Type3Paired)
        {
            if (result.type2_callsign.empty() ||
                result.type2_locator.empty() ||
                result.type3_callsign.empty() ||
                result.type3_locator.empty())
            {
                std::cout << "  Missing paired planning fields.\n";
                all_pass = false;
            }
        }

        if (!pass)
            all_pass = false;
    }

    std::cout << "Summary: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass ? 0 : 1;
}
