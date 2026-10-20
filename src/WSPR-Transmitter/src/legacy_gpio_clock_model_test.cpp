#include "legacy_gpio_clock_model.hpp"
#include "transmission_request.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    int failures = 0;

    void expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    bool nearlyEqual(double actual, double expected, double tolerance)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    template <typename Callback>
    void expectInvalid(Callback callback, const std::string& message)
    {
        bool rejected = false;
        try
        {
            callback();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        expect(rejected, message);
    }

    void testExactClockModels()
    {
        using namespace wsprrypi;

        const auto bcm2835 = legacyGpioClockModel(
            LegacyGpioProcessorProfile::Bcm2835,
            LegacyGpioClockParent::PllD);
        expect(
            bcm2835.processor == LegacyGpioProcessorProfile::Bcm2835 &&
                bcm2835.parent == LegacyGpioClockParent::PllD &&
                bcm2835.nominal_rate_hz == 500000000.0 &&
                bcm2835.intrinsic_system_to_rf_difference_ppm == -2.5 &&
                bcm2835.intrinsic_evidence ==
                    LegacyGpioIntrinsicEvidence::HistoricalAuthoritative,
            "BCM2835 PLLD must preserve the authoritative 500 MHz, -2.5 PPM model");

        const auto later500 = legacyGpioClockModel(
            LegacyGpioProcessorProfile::Bcm2836Bcm2837,
            LegacyGpioClockParent::PllD);
        expect(
            later500.processor ==
                    LegacyGpioProcessorProfile::Bcm2836Bcm2837 &&
                later500.parent == LegacyGpioClockParent::PllD &&
                later500.nominal_rate_hz == 500000000.0 &&
                later500.intrinsic_system_to_rf_difference_ppm == 0.0 &&
                later500.intrinsic_evidence ==
                    LegacyGpioIntrinsicEvidence::DiscoveryBaseline,
            "BCM2836/BCM2837/BCM2837B0 PLLD must use the promoted conducted 500 MHz zero intrinsic residual");

        const auto bcm2711PllD = legacyGpioClockModel(
            LegacyGpioProcessorProfile::Bcm2711,
            LegacyGpioClockParent::PllD);
        const auto bcm2711Oscillator = legacyGpioClockModel(
            LegacyGpioProcessorProfile::Bcm2711,
            LegacyGpioClockParent::Oscillator);
        expect(
            bcm2711PllD.processor == LegacyGpioProcessorProfile::Bcm2711 &&
                bcm2711PllD.parent == LegacyGpioClockParent::PllD &&
                bcm2711PllD.nominal_rate_hz == 750000000.0 &&
                bcm2711PllD.intrinsic_system_to_rf_difference_ppm == 0.0 &&
                bcm2711PllD.intrinsic_evidence ==
                    LegacyGpioIntrinsicEvidence::DiscoveryBaseline,
            "BCM2711 PLLD must use the conservative 750 MHz zero baseline pending GPS-referenced qualification");
        expect(
            bcm2711Oscillator.processor ==
                    LegacyGpioProcessorProfile::Bcm2711 &&
                bcm2711Oscillator.parent ==
                    LegacyGpioClockParent::Oscillator &&
                bcm2711Oscillator.nominal_rate_hz == 54000000.0 &&
                bcm2711Oscillator.intrinsic_system_to_rf_difference_ppm ==
                    0.0 &&
                bcm2711Oscillator.intrinsic_evidence ==
                    LegacyGpioIntrinsicEvidence::DiscoveryBaseline,
            "BCM2711 oscillator must use the conservative 54 MHz zero baseline pending GPS-referenced qualification");

        const auto selectedOscillator =
            selectLegacyGpioClockForAdditionalCorrection(
                LegacyGpioProcessorProfile::Bcm2711,
                137500.0,
                137502.0,
                9.616);
        expect(
            selectedOscillator.model.parent ==
                    LegacyGpioClockParent::Oscillator &&
                selectedOscillator.correction.intrinsic_ppm == 0.0 &&
                selectedOscillator.correction.additional_ppm == 9.616 &&
                selectedOscillator.correction.effective_ppm == 9.616,
            "parent-aware composition must apply the oscillator intrinsic exactly once");

        expectInvalid(
            [] {
                (void)legacyGpioClockModel(
                    LegacyGpioProcessorProfile::Bcm2835,
                    LegacyGpioClockParent::Oscillator);
            },
            "BCM2835 oscillator pairing must fail closed");
        expectInvalid(
            [] {
                (void)legacyGpioClockModel(
                    LegacyGpioProcessorProfile::Bcm2836Bcm2837,
                    LegacyGpioClockParent::Oscillator);
            },
            "BCM2836/BCM2837 oscillator pairing must fail closed");
        expectInvalid(
            [] {
                (void)legacyGpioClockModel(
                    static_cast<LegacyGpioProcessorProfile>(-1),
                    LegacyGpioClockParent::PllD);
            },
            "unknown processor identity must fail closed");
        expectInvalid(
            [] {
                (void)legacyGpioClockModel(
                    LegacyGpioProcessorProfile::Bcm2711,
                    static_cast<LegacyGpioClockParent>(-1));
            },
            "unknown parent identity must fail closed");
    }

    void testIntrinsicDifferenceSignAndValidation()
    {
        using namespace wsprrypi;

        expect(
            deriveLegacyGpioIntrinsicDifferencePpm(4.25, 1.5) == 2.75,
            "D = P - S must preserve the positive-fast sign convention");
        expect(
            deriveLegacyGpioIntrinsicDifferencePpm(-4.25, -1.5) == -2.75,
            "negative-fast inputs must preserve D = P - S without inversion");

        for (const double invalid : {
                 std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN()})
        {
            expectInvalid(
                [invalid] {
                    (void)deriveLegacyGpioIntrinsicDifferencePpm(invalid, 0.0);
                },
                "non-finite P must fail closed");
            expectInvalid(
                [invalid] {
                    (void)deriveLegacyGpioIntrinsicDifferencePpm(0.0, invalid);
                },
                "non-finite S must fail closed");
        }
    }

    void testRevisionIdentityResolution()
    {
        using namespace wsprrypi;

        expect(
            !legacyGpioProcessorProfileFromRevision(0U).has_value(),
            "missing revision identity must fail closed");
        expect(
            legacyGpioProcessorProfileFromRevision(0x0002U) ==
                LegacyGpioProcessorProfile::Bcm2835,
            "old-style nonzero revisions must retain BCM2835 identity");
        expect(
            legacyGpioProcessorProfileFromRevision(0x800000U) ==
                LegacyGpioProcessorProfile::Bcm2835,
            "new-style processor code 0 must resolve BCM2835");
        expect(
            legacyGpioProcessorProfileFromRevision(0x801000U) ==
                LegacyGpioProcessorProfile::Bcm2836Bcm2837 &&
                legacyGpioProcessorProfileFromRevision(0x802000U) ==
                    LegacyGpioProcessorProfile::Bcm2836Bcm2837,
            "new-style processor codes 1 and 2 must resolve the later 500 MHz class");
        expect(
            legacyGpioProcessorProfileFromRevision(0x803000U) ==
                LegacyGpioProcessorProfile::Bcm2711,
            "new-style processor code 3 must resolve BCM2711");
        expect(
            !legacyGpioProcessorProfileFromRevision(0x804000U).has_value() &&
                !legacyGpioProcessorProfileFromRevision(0x80F000U).has_value(),
            "unknown new-style processor codes must fail closed");
        expect(
            legacyHardwareProfile(
                LegacyGpioProcessorProfile::Bcm2835) ==
                    HardwareProfile::BCM2835 &&
                legacyHardwareProfile(
                    LegacyGpioProcessorProfile::Bcm2836Bcm2837) ==
                    HardwareProfile::BCM2836_BCM2837 &&
                legacyHardwareProfile(
                    LegacyGpioProcessorProfile::Bcm2711) ==
                    HardwareProfile::BCM2711,
            "exact processor identities must map to exact committed profiles");
        expect(
            legacyHardwareProfileMatches(
                HardwareProfile::BCM2711,
                LegacyGpioProcessorProfile::Bcm2711) &&
                !legacyHardwareProfileMatches(
                    HardwareProfile::BCM2835,
                    LegacyGpioProcessorProfile::Bcm2711) &&
                !legacyHardwareProfileMatches(
                    HardwareProfile::UNSPECIFIED,
                    LegacyGpioProcessorProfile::Bcm2835),
            "profile agreement must reject wrong and unspecified identities");
    }

    void testParentSelectionAndCorrectionBoundaries()
    {
        using namespace wsprrypi;

        const auto bcm2835 = selectLegacyGpioClock(
            LegacyGpioProcessorProfile::Bcm2835,
            14097100.0,
            14097100.0,
            -1.5);
        expect(
            bcm2835.model.parent == LegacyGpioClockParent::PllD &&
                bcm2835.correction.intrinsic_ppm == -2.5 &&
                bcm2835.correction.additional_ppm == 1.0 &&
                bcm2835.correction.effective_ppm == -1.5 &&
                nearlyEqual(
                    bcm2835.corrected_rate_hz,
                    500000000.0 * (1.0 - 1.5e-6),
                    1.0e-6),
            "BCM2835 selection must apply its intrinsic correction exactly once");

        const auto later = selectLegacyGpioClock(
            LegacyGpioProcessorProfile::Bcm2836Bcm2837,
            14097100.0,
            14097100.0,
            1.0);
        expect(
            later.correction.intrinsic_ppm == 0.0 &&
                later.correction.additional_ppm == 1.0 &&
                later.correction.effective_ppm == 1.0,
            "later 500 MHz profiles must never inherit the BCM2835 intrinsic value");

        constexpr double maximum_divisor = 16777215.0 / 4096.0;
        for (const double effective_ppm : {-100.0, 0.0, 100.0})
        {
            const double corrected_plld_hz =
                750000000.0 * (1.0 + effective_ppm * 1.0e-6);
            const double transition_hz = corrected_plld_hz / maximum_divisor;
            const auto below = selectLegacyGpioClock(
                LegacyGpioProcessorProfile::Bcm2711,
                transition_hz - 100.0,
                transition_hz - 100.0,
                effective_ppm);
            const auto above = selectLegacyGpioClock(
                LegacyGpioProcessorProfile::Bcm2711,
                transition_hz + 100.0,
                transition_hz + 100.0,
                effective_ppm);
            expect(
                below.model.parent == LegacyGpioClockParent::Oscillator &&
                    above.model.parent == LegacyGpioClockParent::PllD,
                "BCM2711 parent transition must track zero and signed corrections");
            expect(
                below.model.nominal_rate_hz == 54000000.0 &&
                    above.model.nominal_rate_hz == 750000000.0,
                "BCM2711 parents must retain independent nominal rates across transitions");
        }
    }

    double rawDetectedForReference(double reference_hz)
    {
        return reference_hz + 0.4484 + reference_hz * 1.01012e-6;
    }

    void testExactSdrInverseAndRfError()
    {
        using namespace wsprrypi;

        constexpr double reference_hz = 10000000.0;
        const double raw_hz = rawDetectedForReference(reference_hz);
        const double calibrated_hz =
            calibrateIssue429SdrFrequencyHz(raw_hz);
        expect(
            nearlyEqual(
                calibrated_hz,
                reference_hz,
                5.0e-9),
            "exact SDR inverse must recover a synthetic reference frequency");
        expect(
            std::fabs(raw_hz - reference_hz) > 10.0,
            "omitting the SDR inverse must remain detectable in the fixture");
        expect(
            std::fabs(
                calibrateIssue429SdrFrequencyHz(calibrated_hz) -
                reference_hz) > 10.0,
            "applying the SDR inverse twice must remain detectable in the fixture");

        constexpr double requested_hz = 14097100.0;
        constexpr double physical_error_ppm = 3.25;
        const double actual_hz = requested_hz * (1.0 + physical_error_ppm * 1.0e-6);
        const auto measurement = analyzeIssue429SdrMeasurement(
            requested_hz,
            rawDetectedForReference(actual_hz),
            1);
        expect(
            nearlyEqual(measurement.calibrated_fundamental_hz, actual_hz, 1.0e-8) &&
                nearlyEqual(
                    measurement.calibrated_rf_error_hz,
                    actual_hz - requested_hz,
                    1.0e-8) &&
                nearlyEqual(
                    measurement.calibrated_rf_error_ppm,
                    physical_error_ppm,
                    1.0e-9),
            "measurement analysis must retain calibrated hertz and positive-fast PPM error");

        constexpr double negative_error_ppm = -4.5;
        const double slow_actual_hz =
            requested_hz * (1.0 + negative_error_ppm * 1.0e-6);
        const auto slow_measurement = analyzeIssue429SdrMeasurement(
            requested_hz,
            rawDetectedForReference(slow_actual_hz),
            1);
        expect(
            nearlyEqual(
                slow_measurement.calibrated_rf_error_ppm,
                negative_error_ppm,
                1.0e-9),
            "measurement analysis must preserve negative-fast RF error");
    }

    void testHarmonicCalibrationOrdering()
    {
        using namespace wsprrypi;

        constexpr double requested_hz = 1000000.0;
        constexpr std::uint32_t harmonic = 7;
        constexpr double actual_fundamental_hz = requested_hz + 0.125;
        const double actual_harmonic_hz = actual_fundamental_hz * harmonic;
        const double raw_harmonic_hz = rawDetectedForReference(actual_harmonic_hz);
        const auto measurement = analyzeIssue429SdrMeasurement(
            requested_hz,
            raw_harmonic_hz,
            harmonic);

        const double wrong_order =
            calibrateIssue429SdrFrequencyHz(raw_harmonic_hz / harmonic);
        expect(
            nearlyEqual(
                measurement.calibrated_fundamental_hz,
                actual_fundamental_hz,
                1.0e-9),
            "harmonic analysis must calibrate the raw harmonic before division");
        expect(
            std::fabs(wrong_order - actual_fundamental_hz) > 0.1,
            "the fixture must detect division before removal of the affine intercept");
    }

    void testMeasurementValidation()
    {
        using namespace wsprrypi;

        for (const double invalid : {
                 0.0,
                 -1.0,
                 std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN()})
        {
            expectInvalid(
                [invalid] { (void)calibrateIssue429SdrFrequencyHz(invalid); },
                "invalid raw detected frequency must fail closed");
            expectInvalid(
                [invalid] {
                    (void)analyzeIssue429SdrMeasurement(invalid, 1000000.0, 1);
                },
                "invalid requested fundamental must fail closed");
        }
        expectInvalid(
            [] {
                (void)analyzeIssue429SdrMeasurement(1000000.0, 1000000.0, 0);
            },
            "zero unauthenticated harmonic number must fail closed");
    }
}

int main()
{
    testExactClockModels();
    testIntrinsicDifferenceSignAndValidation();
    testRevisionIdentityResolution();
    testParentSelectionAndCorrectionBoundaries();
    testExactSdrInverseAndRfError();
    testHarmonicCalibrationOrdering();
    testMeasurementValidation();

    if (failures != 0)
    {
        std::cerr << failures << " legacy GPIO clock-model test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Legacy GPIO clock-model tests passed\n";
    return EXIT_SUCCESS;
}
