// SPDX-License-Identifier: MIT
#include "PPM-Manager/src/ppm_manager.hpp"
#include "system_clock_frequency_estimate.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
using namespace std::chrono;
const auto acquired = steady_clock::time_point{seconds{1000}};
const auto acquired_utc = system_clock::time_point{seconds{1700000000}};
const std::string tracking = "REF,2,0,0,0,0,0,1.25,0.05,0.20,0,0,16,Normal\n";
const std::string stats = "ntp.example,8,5,512,1.25,0.20,0,0\n";
int failures = 0;

void expect(bool condition, const std::string &description)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

PPMProviderSnapshot report(const std::string &age = "5")
{
    return PPMManager::parseChronyReports(tracking,
        "^,*,ntp.example,2,6,377," + age + ",0,0,0\n", stats, acquired);
}

SystemClockFrequencyEstimate evaluate(FrequencyEstimateQualifier &qualifier,
    const PPMProviderSnapshot &provider, seconds elapsed)
{
    const auto now = acquired + elapsed;
    return qualifier.evaluate(system_clock_frequency_estimate_from_provider(
        provider, now, acquired_utc + elapsed), now);
}

GpioFrequencyCorrection select(const SystemClockFrequencyEstimate &sample, double manual = 8.0)
{
    return select_gpio_frequency_correction(true, -0.25, manual, sample);
}
}

int main()
{
    const auto cached = report();
    FrequencyEstimateQualifier qualifier;
    expect(evaluate(qualifier, cached, seconds{0}).qualification == FrequencyEstimateQualification::Converging,
           "the first good evaluation converges");
    expect(evaluate(qualifier, cached, seconds{1}).qualification == FrequencyEstimateQualification::Converging,
           "the second good evaluation converges");
    const auto qualified = evaluate(qualifier, cached, seconds{2});
    expect(qualified.qualification == FrequencyEstimateQualification::Qualified &&
               qualified.age_seconds == 7.0 && select(qualified).effective_ppm == 1.0,
           "a good estimate qualifies with its actual age and estimate-plus-residual correction");
    const auto refreshed = evaluate(qualifier, cached, seconds{90});
    expect(refreshed.age_seconds == 95.0 && refreshed.snapshot_time == qualified.snapshot_time,
           "cached consumption advances age while preserving the report's sample timestamp");

    const auto boundary = evaluate(qualifier, cached, seconds{295});
    expect(boundary.qualification == FrequencyEstimateQualification::Qualified && boundary.age_seconds == 300.0,
           "the exact current-age limit remains usable");
    const auto stale = evaluate(qualifier, cached, seconds{296});
    expect(stale.qualification != FrequencyEstimateQualification::Qualified &&
               select(stale).mode == GpioCorrectionMode::StaleEstimate &&
               select(stale).estimate_age_seconds == 301.0,
           "a cached report crosses into bounded stale fallback");
    const auto last_stale = evaluate(qualifier, cached, seconds{895});
    expect(select(last_stale).mode == GpioCorrectionMode::StaleEstimate &&
               select(last_stale).estimate_age_seconds == 900.0,
           "the exact stale-age limit remains usable");
    const auto expired = evaluate(qualifier, cached, seconds{896});
    expect(select(expired).mode == GpioCorrectionMode::FixedManual &&
               select(expired).effective_ppm == 8.0 &&
               select(expired, 0.0).mode == GpioCorrectionMode::Uncalibrated,
           "expired data falls back to manual or zero without adding residual");

    FrequencyEstimateQualifier first_old;
    expect(select(evaluate(first_old, cached, seconds{296})).mode == GpioCorrectionMode::FixedManual,
           "an old first report cannot manufacture a qualified stale snapshot");

    for (const std::string age : {"", "bad", "7junk", "nan", "inf", "-1", "1e999"})
    {
        const auto malformed = report(age);
        FrequencyEstimateQualifier never_qualified;
        const auto rejected = evaluate(never_qualified, malformed, seconds{0});
        expect(rejected.qualification == FrequencyEstimateQualification::Unavailable &&
                   select(rejected).mode == GpioCorrectionMode::FixedManual,
               "malformed age cannot qualify or bypass fallback: " + age);
    }

    FrequencyEstimateQualifier loss;
    evaluate(loss, cached, seconds{0});
    evaluate(loss, cached, seconds{1});
    evaluate(loss, cached, seconds{2});
    const auto missing = PPMManager::parseChronyReports("", "", "", acquired + seconds{10});
    const auto held = evaluate(loss, missing, seconds{10});
    expect(select(held).mode == GpioCorrectionMode::StaleEstimate &&
               select(held).estimate_age_seconds == 15.0,
           "source loss retains the previous qualified age and correction");
    expect(select(evaluate(loss, missing, seconds{896})).mode == GpioCorrectionMode::FixedManual,
           "repeated source-loss reports cannot extend holdover");

    FrequencyEstimateQualifier malformed_holdover;
    evaluate(malformed_holdover, cached, seconds{0});
    evaluate(malformed_holdover, cached, seconds{1});
    evaluate(malformed_holdover, cached, seconds{2});
    expect(select(evaluate(malformed_holdover, report("bad"), seconds{90})).estimate_age_seconds == 95.0 &&
               select(evaluate(malformed_holdover, report("bad"), seconds{896})).mode == GpioCorrectionMode::FixedManual,
           "malformed ages preserve only the bounded previous snapshot, never a renewed window");

    FrequencyEstimateQualifier replacement;
    evaluate(replacement, cached, seconds{0});
    evaluate(replacement, cached, seconds{1});
    evaluate(replacement, cached, seconds{2});
    const auto local = PPMManager::parseChronyReports(tracking,
        "#,*,REF0,0,4,377,1\n", "REF0,8,5,512,1.25,0.20,0,0\n", acquired + seconds{10});
    const auto reselected = evaluate(replacement, local, seconds{10});
    expect(reselected.qualification == FrequencyEstimateQualification::Converging &&
               select(reselected).mode == GpioCorrectionMode::StaleEstimate &&
               select(reselected).source_signature == qualified.source_signature,
           "source replacement resets convergence and retains the old held snapshot's identity");
    evaluate(replacement, local, seconds{11});
    expect(evaluate(replacement, local, seconds{12}).source_provenance ==
               "GNSS/PPS or other local reference clock",
           "the new source owns provenance after its own stability window");

    auto no_clock = cached;
    no_clock.age_reference_time.reset();
    FrequencyEstimateQualifier missing_clock;
    expect(evaluate(missing_clock, no_clock, seconds{10}).qualification == FrequencyEstimateQualification::Unavailable,
           "a numeric source age without its acquisition clock is unusable");

    const auto network = PPMManager::parseChronyReports(tracking,
        "^,*,ntp.example,2,6,377,5\n#,?,PPS0,0,4,0,1\n", stats, acquired);
    FrequencyEstimateQualifier network_qualifier;
    evaluate(network_qualifier, network, seconds{0});
    evaluate(network_qualifier, network, seconds{1});
    const auto network_correction = select(evaluate(network_qualifier, network, seconds{2}));
    expect(network_correction.source_provenance == "Network NTP" &&
               network_correction.effective_ppm == 1.0,
           "unused PPS cannot change correction attribution or its numeric value");

    if (failures) return 1;
    std::cout << "Chrony report-to-correction freshness, expiry, fallback and provenance tests passed.\n";
}
