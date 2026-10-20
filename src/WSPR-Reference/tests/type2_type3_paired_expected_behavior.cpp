#include "wspr/wspr_ref_api.hpp"
#include "wspr/wspr_ref_plan.hpp"

#include <iostream>
#include <string>
#include <vector>

int main()
{
    const std::string callsign = "AA0NT/12";
    const std::string locator = "EM18IG";
    const int power_dbm = 30;

    const wspr::TransmissionPlanResult plan =
        wspr::plan_transmission(callsign, locator, power_dbm);
    const wspr::WsprEncodeResult encoded =
        wspr::encode_message(callsign, locator, power_dbm);

    bool pass =
        plan.ok &&
        plan.plan == wspr::TransmissionPlanType::Type2Type3Paired &&
        plan.status == wspr::TransmissionPlanStatus::Ok &&
        encoded.ok;

    std::vector<std::string> frames = encoded.symbols_list;
    if (frames.empty() && !encoded.symbols.empty())
        frames.push_back(encoded.symbols);

    pass = pass && (frames.size() == 2U);

    wspr::WsprDecodedMessage frame1;
    wspr::WsprDecodedMessage frame2;
    std::string error1;
    std::string error2;

    const bool frame1_ok =
        (frames.size() >= 1U) &&
        wspr::decode_symbols(frames[0], frame1, error1);
    const bool frame2_ok =
        (frames.size() >= 2U) &&
        wspr::decode_symbols(frames[1], frame2, error2);

    pass =
        pass &&
        frame1_ok &&
        frame2_ok &&
        frame1.valid &&
        frame2.valid &&
        frame1.type == wspr::WsprMessageType::Type2 &&
        frame2.type == wspr::WsprMessageType::Type3 &&
        frame1.has_hash &&
        frame2.has_hash &&
        frame1.callsign_hash != frame2.callsign_hash;

    std::cout << "Paired compound case:\n";
    std::cout << "  Planned mode: " << wspr::to_string(plan.plan)
              << " (" << wspr::to_string(plan.status) << ")\n";
    std::cout << "  Frame count:  " << frames.size() << "\n";
    std::cout << "  Frame 1 type: " << (frame1_ok ? "Type2" : "decode-fail") << "\n";
    std::cout << "  Frame 1 call: " << frame1.callsign << "\n";
    std::cout << "  Frame 1 hash: "
              << (frame1.has_hash ? std::to_string(frame1.callsign_hash) : "-") << "\n";
    std::cout << "  Frame 1 extra:" << (frame1.extra.empty() ? " -" : " " + frame1.extra) << "\n";
    std::cout << "  Frame 1 pwr:  " << frame1.power_dbm << "\n";
    std::cout << "  Frame 2 type: " << (frame2_ok ? "Type3" : "decode-fail") << "\n";
    std::cout << "  Frame 2 call: " << frame2.callsign << "\n";
    std::cout << "  Frame 2 hash: "
              << (frame2.has_hash ? std::to_string(frame2.callsign_hash) : "-") << "\n";
    std::cout << "  Frame 2 loc:  " << frame2.locator << "\n";
    std::cout << "  Frame 2 pwr:  " << frame2.power_dbm << "\n";

    if (!error1.empty())
        std::cout << "  Frame 1 err:  " << error1 << "\n";

    if (!error2.empty())
        std::cout << "  Frame 2 err:  " << error2 << "\n";

    std::cout << "  Note:       differing decoded hashes are expected behavior for this paired representation.\n\n";

    std::cout << "Summary: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}
