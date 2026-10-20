#include "system_clock_frequency_estimate.hpp"
#include "WSPR-Transmitter/src/transmission_request.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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

    SystemClockFrequencyEstimate provider(
        double frequency_ppm,
        FrequencyEstimateQualification qualification =
            FrequencyEstimateQualification::Qualified)
    {
        SystemClockFrequencyEstimate value;
        value.provider_name = "chrony";
        value.qualification = qualification;
        value.frequency_ppm = frequency_ppm;
        value.age_seconds = 2.0;
        value.synchronized = true;
        value.selected_source = true;
        value.leap_normal = true;
        value.skew_ppm = 0.2;
        value.residual_frequency_ppm = 0.05;
        value.source_signature = "ntp.example";
        value.snapshot_time = std::chrono::system_clock::time_point{
            std::chrono::seconds{1700000000}};
        value.retained_source_samples = 8;
        value.source_stability_span_seconds = 512.0;
        return value;
    }

    void testFinalSelectionBounds()
    {
        const auto positive_boundary = select_gpio_frequency_correction(
            true, 1.0, 0.0, provider(199.0));
        const auto negative_boundary = select_gpio_frequency_correction(
            true, -1.0, 0.0, provider(-199.0));
        expect(
            positive_boundary.valid && positive_boundary.effective_ppm == 200.0,
            "qualified provider composition must accept the exact +200 PPM boundary");
        expect(
            negative_boundary.valid && negative_boundary.effective_ppm == -200.0,
            "qualified provider composition must accept the exact -200 PPM boundary");

        const auto positive_overflow = select_gpio_frequency_correction(
            true, 2.0, 0.0, provider(199.0));
        const auto negative_overflow = select_gpio_frequency_correction(
            true, -2.0, 0.0, provider(-199.0));
        expect(
            !positive_overflow.valid && positive_overflow.effective_ppm == 0.0,
            "199 + 2 PPM must fail before producing a committed correction");
        expect(
            !negative_overflow.valid && negative_overflow.effective_ppm == 0.0,
            "-199 - 2 PPM must fail before producing a committed correction");

        for (const double invalid : {
                 200.000001,
                 -200.000001,
                 std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN()})
        {
            expect(
                !select_gpio_frequency_correction(
                     true, invalid, 0.0, provider(0.0)).valid,
                "invalid conducted residual must fail explicitly");
            expect(
                !select_gpio_frequency_correction(
                     false, 0.0, invalid, provider(0.0)).valid,
                "invalid manual correction must fail explicitly");
        }

        expect(
            !select_gpio_frequency_correction(
                 true, 0.0, 0.0, provider(200.000001)).valid,
            "out-of-range qualified provider data must fail explicitly");
        auto invalid_stale = provider(
            -200.000001,
            FrequencyEstimateQualification::Stale);
        invalid_stale.age_seconds = 600.0;
        expect(
            !select_gpio_frequency_correction(
                 true, 0.0, 0.0, invalid_stale).valid,
            "out-of-range stale provider data must fail explicitly");
    }

    void testProviderMetadataValidation()
    {
        auto evaluate_once = [](SystemClockFrequencyEstimate sample) {
            FrequencyEstimateQualifier qualifier;
            return qualifier.evaluate(std::move(sample));
        };

        for (const double invalid : {
                 -1.0,
                 std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN()})
        {
            auto bad_age = provider(1.0);
            bad_age.age_seconds = invalid;
            const auto age_result = evaluate_once(bad_age);
            expect(
                age_result.qualification ==
                        FrequencyEstimateQualification::Unavailable &&
                    age_result.reason.find("age is malformed") !=
                        std::string::npos,
                "negative or non-finite provider age must be rejected as malformed");

            auto bad_span = provider(1.0);
            bad_span.source_stability_span_seconds = invalid;
            const auto span_result = evaluate_once(bad_span);
            expect(
                span_result.qualification ==
                        FrequencyEstimateQualification::Unavailable &&
                    span_result.reason.find("stability span is malformed") !=
                        std::string::npos,
                "negative or non-finite stability span must be rejected as malformed");
        }

        auto negative_skew = provider(1.0);
        negative_skew.skew_ppm = -0.01;
        const auto negative_skew_result = evaluate_once(negative_skew);
        expect(
            negative_skew_result.qualification ==
                    FrequencyEstimateQualification::Unavailable &&
                negative_skew_result.reason.find("skew metadata is malformed") !=
                    std::string::npos,
            "negative skew must not pass provider qualification");

        negative_skew.age_seconds = 600.0;
        const auto stale_negative_skew_result = evaluate_once(negative_skew);
        expect(
            stale_negative_skew_result.qualification ==
                    FrequencyEstimateQualification::Unavailable &&
                stale_negative_skew_result.reason.find(
                    "skew metadata is malformed") != std::string::npos,
            "stale age must not bypass malformed skew validation");

        auto high_skew = provider(1.0);
        high_skew.skew_ppm = 1.01;
        const auto high_skew_result = evaluate_once(high_skew);
        expect(
            high_skew_result.qualification ==
                FrequencyEstimateQualification::Converging,
            "finite positive skew above the limit must remain a convergence condition");
        high_skew.age_seconds = 600.0;
        expect(
            evaluate_once(high_skew).qualification ==
                FrequencyEstimateQualification::Converging,
            "stale age must not bypass the maximum skew quality gate");

        auto nonfinite_residual = provider(1.0);
        nonfinite_residual.residual_frequency_ppm =
            std::numeric_limits<double>::quiet_NaN();
        expect(
            evaluate_once(nonfinite_residual).qualification ==
                FrequencyEstimateQualification::Unavailable,
            "non-finite provider residual metadata must be rejected");
        nonfinite_residual.age_seconds = 600.0;
        expect(
            evaluate_once(nonfinite_residual).qualification ==
                FrequencyEstimateQualification::Unavailable,
            "stale age must not bypass malformed residual validation");

        auto high_residual = provider(1.0);
        high_residual.residual_frequency_ppm = 0.51;
        high_residual.age_seconds = 600.0;
        expect(
            evaluate_once(high_residual).qualification ==
                FrequencyEstimateQualification::Converging,
            "stale age must not bypass the residual-frequency quality gate");

        auto missing_signature = provider(1.0);
        missing_signature.source_signature.clear();
        expect(
            evaluate_once(missing_signature).qualification ==
                FrequencyEstimateQualification::Unavailable,
            "missing source identity must be rejected explicitly");
    }

    void testFallbackExclusivityAndStaleValidation()
    {
        const auto qualified = select_gpio_frequency_correction(
            true, 0.25, 8.0, provider(1.0));
        expect(
            qualified.valid &&
                qualified.mode == GpioCorrectionMode::QualifiedEstimate &&
                qualified.effective_ppm == 1.25 &&
                qualified.source_signature == "ntp.example" &&
                qualified.snapshot_time.time_since_epoch() ==
                    std::chrono::seconds{1700000000},
            "qualified provider selection must exclude manual fallback and retain snapshot identity");

        SystemClockFrequencyEstimate unavailable;
        unavailable.qualification = FrequencyEstimateQualification::Unavailable;
        unavailable.reason = "provider unavailable";
        const auto manual = select_gpio_frequency_correction(
            true, 0.25, -3.0, unavailable);
        expect(
            manual.valid && manual.mode == GpioCorrectionMode::FixedManual &&
                manual.effective_ppm == -3.0 && manual.residual_ppm == 0.25,
            "manual correction must remain exclusive while retaining the configured residual as unapplied provenance");

        auto stale = provider(1.0, FrequencyEstimateQualification::Stale);
        stale.age_seconds = 600.0;
        const auto stale_selected = select_gpio_frequency_correction(
            true, -0.25, 8.0, stale);
        expect(
            stale_selected.valid &&
                stale_selected.mode == GpioCorrectionMode::StaleEstimate &&
                stale_selected.effective_ppm == 0.75,
            "usable stale provider data must exclude the manual fallback");

        stale.age_seconds = -1.0;
        expect(
            !select_gpio_frequency_correction(true, 0.0, 8.0, stale).valid,
            "malformed stale age must fail rather than silently selecting manual or zero");
    }

    void testStaleFallbackRetainsQualifiedSnapshotIdentity()
    {
        FrequencyEstimateQualifier fresh;
        auto never_qualified = provider(2.0);
        never_qualified.age_seconds = 600.0;
        const auto rejected = fresh.evaluate(never_qualified);
        expect(!rejected.last_qualified_frequency_ppm.has_value() &&
                rejected.qualification == FrequencyEstimateQualification::Unavailable,
            "an old first observation must not bypass the qualification stability window");
        FrequencyEstimateQualifier qualifier;
        auto original = provider(1.0);
        original.source_provenance = "original source";
        SystemClockFrequencyEstimate qualified;
        for (int i = 0; i < 3; ++i)
            qualified = qualifier.evaluate(original);
        expect(
            qualified.qualification == FrequencyEstimateQualification::Qualified,
            "stable provider fixture must qualify before fallback identity testing");

        auto changed = provider(2.0);
        changed.source_signature = "replacement.example";
        changed.source_provenance = "replacement source";
        changed.snapshot_time += std::chrono::hours{1};
        changed.synchronized = false;
        const auto fallback = qualifier.evaluate(changed);
        const auto selected = select_gpio_frequency_correction(
            true, 0.0, 0.0, fallback);
        expect(
            selected.mode == GpioCorrectionMode::StaleEstimate &&
                selected.effective_ppm == 1.0 &&
                selected.source_signature == "ntp.example" &&
                selected.source_provenance == "original source" &&
                selected.snapshot_time == original.snapshot_time,
            "stale numeric fallback must retain the same qualified source identity and snapshot time");
    }

    void testIntrinsicCompositionAndFreezing()
    {
        const auto qualified = select_gpio_frequency_correction(
            true, 0.25, 8.0, provider(1.0));

        SystemClockFrequencyEstimate unavailable;
        unavailable.qualification = FrequencyEstimateQualification::Unavailable;
        const auto manual = select_gpio_frequency_correction(
            true, 0.0, 3.0, unavailable);
        const auto zero = select_gpio_frequency_correction(
            true, 0.0, 0.0, unavailable);

        const auto with_provider =
            compose_gpio_frequency_correction(-2.5, qualified);
        const auto with_manual =
            compose_gpio_frequency_correction(-2.5, manual);
        const auto with_zero =
            compose_gpio_frequency_correction(-2.5, zero);
        expect(
            with_provider.valid && with_provider.effective_ppm == -1.25,
            "BCM2835 intrinsic correction must remain active with provider correction");
        expect(
            with_manual.valid && with_manual.effective_ppm == 0.5,
            "BCM2835 intrinsic correction must remain active with manual correction");
        expect(
            with_zero.valid && with_zero.effective_ppm == -2.5,
            "BCM2835 intrinsic correction must remain active with zero additional correction");

        GpioFrequencyCorrection upper;
        upper.effective_ppm = 199.0;
        expect(
            compose_gpio_frequency_correction(2.0, upper).valid &&
                compose_gpio_frequency_correction(2.0, upper).effective_ppm == 201.0,
            "intrinsic composition must preserve the independent caller range");
        GpioFrequencyCorrection lower;
        lower.effective_ppm = -200.0;
        const auto rp1 = compose_gpio_frequency_correction(-46.245, lower);
        expect(rp1.valid && std::abs(rp1.effective_ppm + 246.245) < 1e-9,
               "RP1 intrinsic must not shrink the caller's -200 ppm boundary");
        expect(
            !compose_gpio_frequency_correction(
                 std::numeric_limits<double>::infinity(), zero).valid,
            "non-finite intrinsic correction must fail explicitly");
        expect(
            compose_gpio_frequency_correction(0.0, upper).valid,
            "a bounded additional correction must remain valid without an intrinsic component");

        wsprrypi::TransmissionRequest committed;
        committed.calibration.ppm = with_provider.additional_ppm;
        GpioFrequencyCorrection refreshed = qualified;
        refreshed.effective_ppm = 9.0;
        expect(
            committed.calibration.ppm == 1.25 &&
                refreshed.effective_ppm == 9.0,
            "a committed request must freeze the additional correction across provider refresh");
    }
}

int main()
{
    testFinalSelectionBounds();
    testProviderMetadataValidation();
    testFallbackExclusivityAndStaleValidation();
    testStaleFallbackRetainsQualifiedSnapshotIdentity();
    testIntrinsicCompositionAndFreezing();

    if (failures != 0)
    {
        std::cerr << failures << " GPIO frequency-correction test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "GPIO frequency-correction tests passed\n";
    return EXIT_SUCCESS;
}
