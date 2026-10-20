/**
 * @file test.cpp
 * @brief Hardware-free PPM provider snapshot parser test.
 *
 * Licensed under the repository-root LICENSE.md.
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "ppm_manager.hpp"

#include <cmath>
#include <iostream>

namespace
{
bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 0.000001;
}

int failures = 0;
void expect(bool condition, const std::string &description)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}
}

int main()
{
    const std::string tracking =
        "REF,1,2,3,4,5,6,-1.250,0.125,0.500,10,11,12,Normal\n";
    const std::string sources =
        "#,*,GPS,3,4,5,7.5\n"
        "^,+,pool.example,3,4,5,8.0\n";
    const std::string stats =
        "GPS,12,2,3600\n"
        "pool.example,9,2,1800\n";

    const auto snapshot =
        PPMManager::parseChronyReports(tracking, sources, stats);
    const bool valid = snapshot.frequency_ppm.has_value() &&
                       near(*snapshot.frequency_ppm, -1.250) &&
                       snapshot.residual_frequency_ppm.has_value() &&
                       near(*snapshot.residual_frequency_ppm, 0.125) &&
                       snapshot.skew_ppm.has_value() &&
                       near(*snapshot.skew_ppm, 0.500) &&
                       snapshot.synchronized && snapshot.selected_source &&
                       snapshot.combined_sources && snapshot.leap_normal &&
                       near(snapshot.age_seconds, 7.5) &&
                       snapshot.source_signature == "*:GPS,+:pool.example" &&
                       snapshot.source_provenance == "Mixed" &&
                       snapshot.retained_source_samples == 9 &&
                       near(snapshot.source_stability_span_seconds, 1800.0) &&
                       snapshot.error_reason.empty();

    const auto unavailable = PPMManager::parseChronyReports("", "", "");
    const bool unavailableValid =
        !unavailable.frequency_ppm.has_value() &&
        !unavailable.synchronized &&
        unavailable.error_reason ==
            "chrony tracking data is unavailable or incomplete";

    expect(valid && unavailableValid, "valid and unavailable reports retain their contracts");

    const auto acquired = std::chrono::steady_clock::time_point{std::chrono::seconds{1000}};
    auto parse = [&](const std::string &rows) {
        return PPMManager::parseChronyReports(tracking, rows, stats, acquired);
    };
    for (const std::string age : {"", "broken", "7junk", "nan", "inf", "-1", "1e999"})
    {
        const auto report = parse("^,*,pool.example,3,4,5," + age + "\n");
        expect(!report.error_reason.empty(), "invalid source age reports an error: " + age);
        expect(!std::isfinite(report.withAgeAt(acquired).age_seconds),
               "invalid source age cannot become fresh: " + age);
    }
    expect(!std::isfinite(parse("^,*,pool.example\n").age_seconds),
           "a missing age remains unknown");
    const auto zero = parse("^,*,pool.example,3,4,5,0\n");
    expect(zero.age_seconds == 0.0 && zero.error_reason.empty(), "a real zero age remains valid");
    const auto cached = parse("^,*,pool.example,3,4,5,5\n");
    const auto later = cached.withAgeAt(acquired + std::chrono::seconds{90});
    expect(near(later.age_seconds, 95.0) && cached.age_seconds == 5.0,
           "consuming a cached report accrues elapsed age without changing the cache");
    expect(near(later.withAgeAt(acquired + std::chrono::seconds{100}).age_seconds, 105.0),
           "aging an already-aged copy does not count an interval twice");
    expect(!std::isfinite(cached.withAgeAt(acquired - std::chrono::seconds{1}).age_seconds),
           "a future acquisition timestamp cannot make data fresh");
    auto no_anchor = cached;
    no_anchor.age_reference_time.reset();
    expect(!std::isfinite(no_anchor.withAgeAt(acquired).age_seconds),
           "unknown acquisition time cannot establish freshness");

    const std::string network = "^,*,pool.example,3,4,5,5\n";
    for (const std::string state : {"?", "-", "x", "~"})
        expect(parse(network + "#," + state + ",PPS0,0,4,0,1\n").source_provenance == "Network NTP",
               "unselected local sources cannot change network provenance");
    expect(parse("^,*,GPS-server.example,3,4,5,5\n").source_provenance == "Network NTP",
           "network source names are not local-reference evidence");
    expect(parse("#,*,REF0,0,4,5,5\n").source_provenance ==
               "GNSS/PPS or other local reference clock",
           "an arbitrary local reference ID is classified by its mode");
    expect(parse(network + "=,+,peer.example,3,4,5,1\n").source_provenance == "Network NTP",
           "multiple network sources are still network provenance");
    expect(parse("#,*,REF0,0,4,5,5\n#,+,REF1,0,4,5,1\n").source_provenance ==
               "GNSS/PPS or other local reference clock",
           "multiple local sources are still local provenance");
    expect(parse(network + "#,+,REF0,0,4,5,1\n").source_provenance == "Mixed",
           "selected network and combined local sources report mixed provenance");
    expect(parse("?,*,PPS0,0,4,5,5\n").source_provenance == "Unknown" &&
               parse(network + "?,+,REF0,0,4,5,1\n").source_provenance == "Unknown",
           "unknown selected or combined source modes remain unknown");
    for (const std::string rows : {"#,+,PPS0,0,4,5,1\n",
             "^,*,pool.example\n#,*,PPS0,0,4,5,1\n", "^,*,,3,4,5,1\n"})
    {
        const auto ambiguous = parse(rows);
        expect(!ambiguous.selected_source && !ambiguous.synchronized &&
                   !std::isfinite(ambiguous.age_seconds) && ambiguous.source_provenance == "Unknown",
               "missing or ambiguous selected-source identity cannot establish freshness/provenance");
    }

    if (failures)
    {
        std::cerr << failures << " PPM provider snapshot test(s) failed.\n";
        return 1;
    }
    std::cout << "PPM report parsing, source provenance and cached age tests passed.\n";
    return 0;
}
