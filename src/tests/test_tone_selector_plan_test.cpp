#include "../test_tone_selector_plan.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static void require_rejected(
    const TestToneSelectorPlanResult &result,
    const char *reason,
    const char *message)
{
    require(!result && !result.error.empty() &&
                result.error.find(reason) != std::string::npos,
            message);
}

int main()
{
    const HamBand band = HamBand::BAND_20M;
    std::array<BandGPIOConfig, HAM_BAND_COUNT> fallback{};
    fallback[ham_band_index(band)] = {19, true, true};

    std::vector<WsprFrequencyEntry> entries{
        {"40m", 7038600.0, 5, true, false},
        {"20m", 14095600.0, 17, false, false},
    };
    const auto matching = plan_test_tone_selector(band, entries, fallback);
    require(matching && matching.plan.enabled &&
                matching.plan.config.gpio == 17 &&
                !matching.plan.config.active_high &&
                matching.plan.band == band &&
                matching.plan.source == TestToneSelectorSource::FrequencyEntry,
            "same-band selector must win over a different-band selector and fallback");

    entries.push_back({"20m", 14095600.0, 17, false, false});
    require(static_cast<bool>(plan_test_tone_selector(band, entries, fallback)),
            "identical same-band selectors must be equivalent");

    entries.push_back({"20m", 14095600.0, 18, false, false});
    require_rejected(plan_test_tone_selector(band, entries, fallback),
                     "conflicting",
                     "conflicting same-band GPIOs must reject");

    const std::vector<WsprFrequencyEntry> opposite_polarity{
        {"20m", 14095600.0, 17, false, false},
        {"20m", 14095600.0, 17, true, false},
    };
    require_rejected(plan_test_tone_selector(band, opposite_polarity, fallback),
                     "conflicting",
                     "same GPIO with opposite polarity must reject");

    const std::vector<WsprFrequencyEntry> invalid_explicit{
        {"20m", 14095600.0, 28, false, false},
    };
    require_rejected(plan_test_tone_selector(band, invalid_explicit, fallback),
                     "invalid selector GPIO",
                     "invalid matching selector must reject instead of falling back");

    const std::vector<WsprFrequencyEntry> different_band_only{
        {"40m", 7038600.0, 5, true, false},
    };
    const auto fallback_plan = plan_test_tone_selector(
        band, different_band_only, fallback);
    require(fallback_plan && fallback_plan.plan.enabled &&
                fallback_plan.plan.source == TestToneSelectorSource::BandConfiguration &&
                fallback_plan.plan.config.gpio == 19 &&
                fallback_plan.plan.config.active_high,
            "selected-band fallback must apply without a matching entry");

    auto invalid_fallback = fallback;
    invalid_fallback[ham_band_index(band)] = {28, true, false};
    require_rejected(plan_test_tone_selector(band, different_band_only, invalid_fallback),
                     "invalid band selector GPIO",
                     "invalid enabled selected-band fallback must reject");

    const std::vector<WsprFrequencyEntry> skip_entry{
        {"skip", 0.0, 22, true, false},
    };
    const auto skip_fallback = plan_test_tone_selector(band, skip_entry, fallback);
    require(skip_fallback && skip_fallback.plan.enabled &&
                skip_fallback.plan.source == TestToneSelectorSource::BandConfiguration &&
                skip_fallback.plan.config.gpio == 19,
            "zero-frequency selector metadata must not suppress selected-band fallback");

    fallback[ham_band_index(band)] = {};
    const auto no_selector = plan_test_tone_selector(band, different_band_only, fallback);
    require(no_selector && !no_selector.plan.enabled &&
                no_selector.plan.source == TestToneSelectorSource::None,
            "missing selected-band fallback must yield a safe no-selector plan");
    const auto skip_no_selector = plan_test_tone_selector(band, skip_entry, fallback);
    require(skip_no_selector && !skip_no_selector.plan.enabled &&
                skip_no_selector.plan.source == TestToneSelectorSource::None,
            "zero-frequency selector metadata must not create a same-band selector");

    std::cout << "test tone selector plan passed\n";
}
