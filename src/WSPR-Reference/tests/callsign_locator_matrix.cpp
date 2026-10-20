#include "wspr/wspr_ref_api.hpp"
#include "wspr/wspr_ref_plan.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct MatrixCase
    {
        std::string label;
        std::string callsign;
        std::string locator;
        int power_dbm = 0;
        bool expect_encode_ok = false;
        wspr::TransmissionPlanType expected_plan =
            wspr::TransmissionPlanType::Invalid;
        wspr::TransmissionPlanStatus expected_status =
            wspr::TransmissionPlanStatus::InternalError;
        std::size_t expected_frame_count = 0;
        std::vector<wspr::WsprMessageType> expected_types;
        std::string expected_type2_extra;
        std::string expected_alternate_extra;
        bool expect_ambiguity = false;
    };

    const char *message_type_name(wspr::WsprMessageType type)
    {
        switch (type)
        {
        case wspr::WsprMessageType::Type1:
            return "Type1";
        case wspr::WsprMessageType::Type2:
            return "Type2";
        case wspr::WsprMessageType::Type3:
            return "Type3";
        case wspr::WsprMessageType::Unknown:
        default:
            return "Unknown";
        }
    }

    bool type2_callsign_matches(
        const std::string &callsign,
        const std::string &extra)
    {
        if (extra.empty())
            return false;

        if (extra.back() == '/')
            return callsign == extra + "<hashed>";

        if (extra.front() == '/')
            return callsign == "<hashed>" + extra;

        return false;
    }

    bool verify_decoded_frame(
        const MatrixCase &tc,
        std::size_t frame_index,
        const wspr::WsprDecodedMessage &decoded)
    {
        if (!decoded.valid)
            return false;

        if (frame_index >= tc.expected_types.size())
            return false;

        if (decoded.type != tc.expected_types[frame_index])
            return false;

        if (decoded.power_dbm != tc.power_dbm)
            return false;

        switch (decoded.type)
        {
        case wspr::WsprMessageType::Type1:
            return
                !decoded.is_partial &&
                !decoded.has_hash &&
                !decoded.has_ambiguity &&
                decoded.alternate_extra.empty() &&
                decoded.callsign == tc.callsign &&
                decoded.locator == tc.locator;

        case wspr::WsprMessageType::Type2:
            return
                decoded.is_partial &&
                decoded.has_hash &&
                type2_callsign_matches(decoded.callsign, tc.expected_type2_extra) &&
                decoded.extra == tc.expected_type2_extra &&
                decoded.locator.empty() &&
                decoded.has_ambiguity == tc.expect_ambiguity &&
                decoded.alternate_extra == tc.expected_alternate_extra;

        case wspr::WsprMessageType::Type3:
            return
                decoded.is_partial &&
                decoded.has_hash &&
                !decoded.has_ambiguity &&
                decoded.alternate_extra.empty() &&
                decoded.callsign == "<hashed>" &&
                decoded.locator == tc.locator;

        case wspr::WsprMessageType::Unknown:
        default:
            return false;
        }
    }
} // namespace

int main()
{
    const std::vector<MatrixCase> cases = {
        {"Type 1 K1ABC", "K1ABC", "FN20", 30, true,
         wspr::TransmissionPlanType::Type1Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type1}, "", "", false},
        {"Type 1 W9XYZ", "W9XYZ", "EM18", 37, true,
         wspr::TransmissionPlanType::Type1Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type1}, "", "", false},
        {"Type 1 N0CAL", "N0CAL", "DM79", 10, true,
         wspr::TransmissionPlanType::Type1Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type1}, "", "", false},
        {"Type 2 suffix /1", "AA0NT/1", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "/1", "", false},
        {"Type 2 suffix /12", "AA0NT/12", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "/12", "", false},
        {"Type 2 prefix W1/", "W1/AA0NT", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "W1/", "", false},
        {"Type 2 prefix K/", "K/AA0NT", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "K/", "", false},
        {"Type 2 suffix /P", "AA0NT/P", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "/P", "", false},
        {"Type 2 suffix /M", "AA0NT/M", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "/M", "", false},
        {"Type 3 <AA0NT>", "<AA0NT>", "EM18IG", 20, true,
         wspr::TransmissionPlanType::Type3Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type3}, "", "", false},
        {"Type 3 <K1ABC>", "<K1ABC>", "FN20AB", 30, true,
         wspr::TransmissionPlanType::Type3Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type3}, "", "", false},
        {"Type 2 overlap /Z", "AA0NT/Z", "EM18", 30, true,
         wspr::TransmissionPlanType::Type2Single,
         wspr::TransmissionPlanStatus::Ok, 1,
         {wspr::WsprMessageType::Type2}, "/Z", "/09", true},
        {"Paired 6-char locator", "AA0NT/12", "EM18IG", 30, true,
         wspr::TransmissionPlanType::Type2Type3Paired,
         wspr::TransmissionPlanStatus::Ok, 2,
         {wspr::WsprMessageType::Type2, wspr::WsprMessageType::Type3},
         "/12", "", false},
        {"Invalid explicit Type 3 locator", "<AA0NT>", "EM18", 20, false,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::Type3RequiresSixCharLocator, 0,
         {}, "", "", false},
        {"Invalid locator length", "AA0NT/12", "EM18I", 30, false,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::InvalidLocator, 0,
         {}, "", "", false},
        {"Invalid callsign characters", "AA0NT?", "EM18", 30, false,
         wspr::TransmissionPlanType::Invalid,
         wspr::TransmissionPlanStatus::InvalidCallsign, 0,
         {}, "", "", false}};

    bool all_pass = true;

    for (const auto &tc : cases)
    {
        const wspr::TransmissionPlanResult plan =
            wspr::plan_transmission(tc.callsign, tc.locator, tc.power_dbm);
        const wspr::WsprEncodeResult encoded =
            wspr::encode_message(tc.callsign, tc.locator, tc.power_dbm);

        bool pass = true;

        std::cout << "Case: " << tc.label << "\n";
        std::cout << "  Input:    " << tc.callsign
                  << " " << tc.locator
                  << " " << tc.power_dbm << "\n";
        std::cout << "  Planned:  " << wspr::to_string(plan.plan)
                  << " (" << wspr::to_string(plan.status) << ")\n";

        if (tc.expect_encode_ok)
        {
            pass =
                plan.ok &&
                plan.plan == tc.expected_plan &&
                plan.status == tc.expected_status &&
                encoded.ok;

            if (!encoded.ok)
            {
                std::cout << "  Encode:   FAIL " << encoded.error << "\n";
            }
            else
            {
                std::vector<std::string> frames = encoded.symbols_list;
                if (frames.empty() && !encoded.symbols.empty())
                    frames.push_back(encoded.symbols);

                pass = pass && (frames.size() == tc.expected_frame_count);

                std::cout << "  Encode:   OK " << encoded.type
                          << " (" << frames.size() << " frame(s))\n";

                for (std::size_t i = 0; i < frames.size(); ++i)
                {
                    wspr::WsprDecodedMessage decoded;
                    std::string error;
                    const bool decoded_ok =
                        wspr::decode_symbols(frames[i], decoded, error);

                    std::cout << "  Frame " << (i + 1) << ":  "
                              << (decoded_ok ? "decoded" : "decode-fail")
                              << "\n";

                    if (!decoded_ok)
                    {
                        std::cout << "    Error:    " << error << "\n";
                        pass = false;
                        continue;
                    }

                    std::cout << "    Type:     "
                              << message_type_name(decoded.type) << "\n";
                    std::cout << "    Callsign: " << decoded.callsign << "\n";
                    std::cout << "    Locator:  " << decoded.locator << "\n";
                    std::cout << "    Power:    " << decoded.power_dbm << "\n";
                    std::cout << "    Hash:     "
                              << (decoded.has_hash ? std::to_string(decoded.callsign_hash) : "-")
                              << "\n";
                    std::cout << "    Partial:  "
                              << (decoded.is_partial ? "true" : "false") << "\n";
                    std::cout << "    Ambig:    "
                              << (decoded.has_ambiguity ? "true" : "false") << "\n";

                    if (decoded.has_ambiguity)
                        std::cout << "    Alt:      "
                                  << decoded.alternate_extra << "\n";

                    pass = pass && verify_decoded_frame(tc, i, decoded);
                }
            }
        }
        else
        {
            pass =
                !plan.ok &&
                plan.plan == tc.expected_plan &&
                plan.status == tc.expected_status &&
                !encoded.ok;

            std::cout << "  Encode:   "
                      << (encoded.ok ? "UNEXPECTED SUCCESS" : "expected failure")
                      << "\n";
            std::cout << "  Error:    " << encoded.error << "\n";
        }

        std::cout << "  Result:   " << (pass ? "PASS" : "FAIL") << "\n\n";

        if (!pass)
            all_pass = false;
    }

    std::cout << "Summary: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass ? 0 : 1;
}
