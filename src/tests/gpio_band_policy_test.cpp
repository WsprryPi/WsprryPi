#include "execution_plan_compiler.hpp"
#include "gpio_band_policy.hpp"
#include "transmission_controller.hpp"

#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

namespace
{
void require(bool condition, const std::string& message)
{
    if (condition)
        return;

    std::cerr << "FAIL: " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

class ProbeBackend final : public wsprrypi::ITransmissionBackend
{
public:
    explicit ProbeBackend(wsprrypi::BackendKind kind)
        : kind_(kind)
    {
    }

    wsprrypi::BackendInfo info() const override
    {
        return {kind_, "probe", "policy test backend"};
    }

    wsprrypi::BackendCapabilities capabilities() const override
    {
        wsprrypi::BackendCapabilities capabilities;
        capabilities.output_class =
            kind_ == wsprrypi::BackendKind::SI5351
                ? wsprrypi::BackendOutputClass::EXTERNAL_CLOCK_RF
                : wsprrypi::BackendOutputClass::PHYSICAL_GPIO_RF;
        capabilities.supported_modes = 0xffffffffu;
        return capabilities;
    }

    wsprrypi::BackendCompileResult configure(
        const wsprrypi::ExecutionPlan&,
        const wsprrypi::BackendExecutionInputs&) override
    {
        ++configure_calls;
        return {true, {}, {}};
    }

    wsprrypi::ExecutionResult execute(
        const wsprrypi::ExecutionPlan&) override
    {
        return {true, false, false, {}};
    }

    wsprrypi::StartupQuiesceResult quiesceForStartup() override
    {
        return {true, {}};
    }

    void stop() noexcept override {}
    wsprrypi::CleanupResult cleanup() noexcept override { return {true, {}}; }

    int configure_calls{0};

private:
    wsprrypi::BackendKind kind_;
};

wsprrypi::MorseTiming short_timing()
{
    wsprrypi::MorseTiming timing;
    timing.dot = std::chrono::milliseconds(1);
    timing.dash = std::chrono::milliseconds(3);
    timing.intra_element_gap = std::chrono::milliseconds(1);
    timing.inter_character_gap = std::chrono::milliseconds(3);
    timing.inter_word_gap = std::chrono::milliseconds(7);
    return timing;
}

wsprrypi::TransmissionRequest request_for_mode(
    wsprrypi::BackendKind backend,
    wsprrypi::TransmissionMode mode,
    double frequency_hz)
{
    wsprrypi::TransmissionRequest request;
    request.mode = mode;
    request.output.backend = backend;
    request.output.output = backend == wsprrypi::BackendKind::SI5351
        ? wsprrypi::ClockSource::SI5351_CLK0
        : wsprrypi::ClockSource::GPIO_CLK;

    switch (mode)
    {
    case wsprrypi::TransmissionMode::WSPR:
    {
        PreparedWsprTransmission prepared;
        prepared.frames.resize(1);
        prepared.frames.front().symbols = {0, 1, 2, 3};
        prepared.current_frame = 1;
        wsprrypi::WsprPayload payload;
        payload.prepared = prepared;
        payload.base_frequency_hz = frequency_hz;
        request.payload = payload;
        break;
    }
    case wsprrypi::TransmissionMode::QRSS:
    {
        wsprrypi::QrssPayload payload;
        payload.message = "E";
        payload.frequency_hz = frequency_hz;
        payload.timing = short_timing();
        request.payload = payload;
        break;
    }
    case wsprrypi::TransmissionMode::FSKCW:
    {
        wsprrypi::FskcwPayload payload;
        payload.message = "E";
        payload.space_frequency_hz = frequency_hz;
        payload.mark_frequency_hz = frequency_hz + 5.0;
        payload.timing = short_timing();
        request.payload = payload;
        break;
    }
    case wsprrypi::TransmissionMode::DFCW:
    {
        wsprrypi::DfcwPayload payload;
        payload.message = "E";
        payload.dot_frequency_hz = frequency_hz;
        payload.dash_frequency_hz = frequency_hz + 5.0;
        payload.timing = short_timing();
        request.payload = payload;
        break;
    }
    case wsprrypi::TransmissionMode::TONE:
    {
        wsprrypi::TonePayload payload;
        payload.frequency_hz = frequency_hz;
        payload.duration = std::chrono::milliseconds(1);
        request.payload = payload;
        break;
    }
    case wsprrypi::TransmissionMode::CW:
        require(false, "generic CW compilation is not implemented");
        break;
    }

    return request;
}

void require_controller_policy(
    wsprrypi::BackendKind backend_kind,
    wsprrypi::TransmissionMode mode,
    double frequency_hz,
    bool expected_allowed,
    const std::string& context,
    bool allow_unqualified = false,
    bool allow_non_amateur = false,
    wsprrypi::HardwareProfile profile = wsprrypi::HardwareProfile::UNSPECIFIED)
{
    wsprrypi::ExecutionPlanCompiler compiler;
    ProbeBackend backend(backend_kind);
    wsprrypi::TransmissionController controller(compiler, backend);
    auto request = request_for_mode(backend_kind, mode, frequency_hz);
    request.policy.allow_unqualified_frequency = allow_unqualified;
    request.policy.allow_non_amateur_frequency = allow_non_amateur;
    request.policy.hardware_profile = profile;
    const auto direct_policy = wsprrypi::evaluate_frequency_policy(
        backend_kind, mode, frequency_hz, allow_unqualified,
        allow_non_amateur, profile);
    const auto result = controller.prepare(request);

    require(
        result.ok == expected_allowed,
        context + " policy result must match expectation at " +
            std::to_string(frequency_hz) + " Hz in mode " +
            std::to_string(static_cast<int>(mode)) + "; direct policy was " +
            wsprrypi::qualification_state_name(direct_policy.qualification) +
            " and controller error was '" + result.error + "'");
    require(
        backend.configure_calls == (expected_allowed ? 1 : 0),
        context + " must reject before backend configuration");
    if (!expected_allowed)
    {
        require(
            result.error.find("Transmission") != std::string::npos,
            context + " must provide an actionable policy error");
        require(
            controller.prepared_plan() == nullptr,
            context + " must not retain a rejected execution plan");
    }
}
} // namespace

int main()
{
    const auto twelve_meter_policy = wsprrypi::evaluate_frequency_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::TONE,
        24950000.0);
    require(
        twelve_meter_policy.band == "12m" &&
            twelve_meter_policy.error.find("12 m band") != std::string::npos,
        "policy must retain canonical band identity and readable diagnostics");

    const std::initializer_list<wsprrypi::TransmissionMode> exercised_modes{
        wsprrypi::TransmissionMode::WSPR,
        wsprrypi::TransmissionMode::QRSS,
        wsprrypi::TransmissionMode::FSKCW,
        wsprrypi::TransmissionMode::DFCW,
        wsprrypi::TransmissionMode::TONE};

    for (const auto mode : exercised_modes)
    {
        for (const double blocked_band_frequency :
             {24950000.0, 51000000.0, 70500000.0, 145000000.0,
              223500000.0, 435000000.0})
        {
            require_controller_policy(
                wsprrypi::BackendKind::RPI_CLOCK_GPIO,
                mode,
                blocked_band_frequency,
                false,
                "disqualified GPIO mode");
        }
    }

    for (const double blocked_frequency :
         {24890000.0, 24924600.0, 24950000.0, 24990000.0,
          50000000.0, 50293000.0, 51000000.0, 52000000.0,
          70000000.0, 70091000.0, 70500000.0, 71000000.0,
          144000000.0, 144489000.0, 145000000.0, 148000000.0,
          222000000.0, 223500000.0, 225000000.0,
          420000000.0, 435000000.0, 450000000.0})
    {
        require_controller_policy(
            wsprrypi::BackendKind::RPI_CLOCK_GPIO,
            wsprrypi::TransmissionMode::TONE,
            blocked_frequency,
            false,
            "disqualified GPIO frequency");
    }

    for (const auto mode : exercised_modes)
    {
        for (const double qualified_frequency :
             {3568600.0, 14095600.0, 21094600.0, 28124600.0})
        {
            require_controller_policy(
                wsprrypi::BackendKind::RPI_CLOCK_GPIO,
                mode,
                qualified_frequency,
                true,
                "qualified GPIO frequency");
        }
    }

    for (const double adjacent_frequency :
         {24889999.0, 24990001.0, 49999999.0, 52000001.0,
          69999999.0, 71000001.0, 143999999.0, 148000001.0,
          221999999.0, 225000001.0, 419999999.0, 450000001.0})
    {
        require_controller_policy(
            wsprrypi::BackendKind::RPI_CLOCK_GPIO,
            wsprrypi::TransmissionMode::TONE,
            adjacent_frequency,
            false,
            "frequency adjacent to a disqualified band");
    }

    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::QRSS,
        50293000.0, true, "unqualified override", true, false);
    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::TONE,
        30000000.0, false, "outside-band single override", true, false);
    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::TONE,
        30000000.0, true, "outside-band dual override", true, true);
    require_controller_policy(
        wsprrypi::BackendKind::SI5351,
        wsprrypi::TransmissionMode::TONE,
        223500000.0, false, "unavailable cannot be overridden", true, true);
    require_controller_policy(
        wsprrypi::BackendKind::SI5351,
        wsprrypi::TransmissionMode::TONE,
        435000000.0, false, "Si5351 70cm remains unavailable", true, true);
    for (const double nationally_allocated_frequency : {40000000.0, 60000000.0})
    {
        require_controller_policy(
            wsprrypi::BackendKind::SI5351,
            wsprrypi::TransmissionMode::TONE,
            nationally_allocated_frequency, false,
            "8m and 5m remain untested without override");
        require_controller_policy(
            wsprrypi::BackendKind::SI5351,
            wsprrypi::TransmissionMode::TONE,
            nationally_allocated_frequency, true,
            "8m and 5m experimental qualification override", true, false);
    }
    require_controller_policy(
        wsprrypi::BackendKind::SI5351,
        wsprrypi::TransmissionMode::TONE,
        902000000.0, false, "bands above 70cm require both overrides", true, false);
    for (const auto mode : exercised_modes)
    {
        require_controller_policy(
            wsprrypi::BackendKind::SI5351,
            mode,
            137500.0, true, "qualified Si5351 2200 m mode");
    }
    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::WSPR,
        137500.0, false, "legacy 2200 m profile", false, false,
        wsprrypi::HardwareProfile::BCM2836_BCM2837);
    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::TONE,
        137500.0, true, "legacy 2200 m TONE profile", false, false,
        wsprrypi::HardwareProfile::BCM2836_BCM2837);
    for (const auto qualified_mode : {
             wsprrypi::TransmissionMode::QRSS,
             wsprrypi::TransmissionMode::FSKCW,
             wsprrypi::TransmissionMode::DFCW})
    {
        const auto decision = wsprrypi::evaluate_frequency_policy(
            wsprrypi::BackendKind::RPI_CLOCK_GPIO,
            qualified_mode,
            137500.0,
            false,
            false,
            wsprrypi::HardwareProfile::BCM2836_BCM2837);
        require(
            decision.qualification == wsprrypi::QualificationState::QUALIFIED,
            "legacy 2200 m qualified CW mode must retain qualified state");
        require_controller_policy(
            wsprrypi::BackendKind::RPI_CLOCK_GPIO,
            qualified_mode,
            137500.0, true, "legacy 2200 m qualified CW profile", false, false,
            wsprrypi::HardwareProfile::BCM2836_BCM2837);
    }
    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::WSPR,
        137500.0, true, "BCM2711 2200 m profile", false, false,
        wsprrypi::HardwareProfile::BCM2711);

    for (const auto mode : {
             wsprrypi::TransmissionMode::TONE,
             wsprrypi::TransmissionMode::QRSS,
             wsprrypi::TransmissionMode::FSKCW,
             wsprrypi::TransmissionMode::DFCW})
    {
        require_controller_policy(
            wsprrypi::BackendKind::RPI_CLOCK_GPIO,
            mode,
            50294500.0, true, "BCM2711 6 m qualified CW profile", false, false,
            wsprrypi::HardwareProfile::BCM2711);
    }
    require_controller_policy(
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::TransmissionMode::WSPR,
        50294500.0, false, "BCM2711 6 m WSPR profile", false, false,
        wsprrypi::HardwareProfile::BCM2711);
    for (const double unavailable_frequency : {222101500.0, 432301500.0})
    {
        require_controller_policy(
            wsprrypi::BackendKind::RPI_CLOCK_GPIO,
            wsprrypi::TransmissionMode::TONE,
            unavailable_frequency, false, "BCM2711 unavailable profile", true, true,
            wsprrypi::HardwareProfile::BCM2711);
    }

    for (const auto mode : exercised_modes)
    {
        for (const double si5351_frequency :
             {24924600.0, 50293000.0, 144489000.0})
        {
            require_controller_policy(
                wsprrypi::BackendKind::SI5351,
                mode,
                si5351_frequency,
                true,
                "Si5351 frequency");
        }
    }

    for (const auto mode : exercised_modes)
    for (const double frequency : {50294500.0,70294500.0})
    {
        const auto decision=wsprrypi::evaluate_frequency_policy(
            wsprrypi::BackendKind::RP1_GPCLK,mode,frequency,false,false,
            wsprrypi::HardwareProfile::RP1_GPCLK);
        require(!decision.allowed &&
            decision.qualification==wsprrypi::QualificationState::UNQUALIFIED,
            "RP1 6 m and 4 m retain the documented unqualified disposition");
        require_controller_policy(wsprrypi::BackendKind::RP1_GPCLK,mode,
            frequency,false,"RP1 untested VHF",false,false,
            wsprrypi::HardwareProfile::RP1_GPCLK);
        require_controller_policy(wsprrypi::BackendKind::RP1_GPCLK,mode,
            frequency,true,"RP1 VHF policy opt-in does not grant backend capability",
            true,false,wsprrypi::HardwareProfile::RP1_GPCLK);
    }

    for (const auto mode : exercised_modes)
    {
        require_controller_policy(wsprrypi::BackendKind::RP1_GPCLK, mode,
            24926100.0, true, "RP1 documented qualified 12 m", false, false,
            wsprrypi::HardwareProfile::RP1_GPCLK);
        for (const double frequency : {144490500.0, 223500000.0, 435000000.0})
        {
            const auto decision = wsprrypi::evaluate_frequency_policy(
                wsprrypi::BackendKind::RP1_GPCLK, mode, frequency, true, true,
                wsprrypi::HardwareProfile::RP1_GPCLK);
            require(!decision.allowed && decision.qualification == wsprrypi::QualificationState::UNAVAILABLE,
                "RP1 high-band Unavailable cannot be overridden");
        }
    }

    require(!wsprrypi::evaluate_frequency_policy(wsprrypi::BackendKind::RP1_GPCLK,
        wsprrypi::TransmissionMode::CW, 24926100.0, false, false,
        wsprrypi::HardwareProfile::RP1_GPCLK).allowed,
        "12 m table reconciliation must not qualify an unrecorded CW mode");
    require(!wsprrypi::evaluate_frequency_policy(wsprrypi::BackendKind::RP1_GPCLK,
        wsprrypi::TransmissionMode::TONE, 144490500.0, true, true,
        wsprrypi::HardwareProfile::UNSPECIFIED).allowed,
        "an unspecified hardware profile cannot override RP1 high-band Unavailable");

    std::cout << "gpio_band_policy_test passed" << std::endl;
    return EXIT_SUCCESS;
}
