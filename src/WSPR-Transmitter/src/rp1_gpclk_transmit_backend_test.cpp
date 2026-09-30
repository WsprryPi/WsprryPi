#include "rp1_gpclk_transmit_backend.hpp"
#include "rp1_gpclk_planner.hpp"
#include "rp1_gpclk_uapi.h"
#include "wspr_transmit.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
int failures;
void expect(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

class Owner final : public IControllerBridge
{
public:
    WsprTransmitState backendStateValue() const noexcept override { return WsprTransmitState::ENABLED; }
    void backendSetStateValue(WsprTransmitState) noexcept override {}
    bool backendShouldStop() const noexcept override { return stop; }
    void backendSignalStopRequest() noexcept override { stop = true; }
    void backendRequestStopTxNoJoin() noexcept override { stop = true; }
    bool backendWaitInterruptableFor(std::chrono::nanoseconds) override { return !stop; }
    void backendThrowIfStopRequested(const char*) override {}
    void backendReportExecutionProgress(std::size_t value) noexcept override { progress.push_back(value); }
    void backendFireTransmitCallback(WsprTransmissionCallbackEvent, WsprTransmitLogLevel, const std::string&, double) override {}
    bool backendRestartCurrentConfiguration() override { return false; }
    bool stop{false};
    std::vector<std::size_t> progress;
};

class Provider final : public wsprrypi::Rp1GpclkProvider
{
public:
    bool query(std::uint32_t route, std::uint64_t capabilities, bool,
        wsprrypi::Rp1GpclkProviderIdentity& identity, std::string&) override {
        identity.route=route;
        identity.compatibility_state=RP1_GPCLK_COMPAT_EXPERIMENTAL;
        identity.capabilities=capabilities; identity.module_id="rp1-gpclk-dkms";
        identity.build_id="1.1.2";
        identity.compatibility_id=route==RP1_GPCLK_ROUTE_GPIO20
            ? "external-provider-gpio20" : "external-provider-gpio4";
        return true;
    }
    bool acquire(std::uint32_t route, std::uint64_t capabilities,
        std::string& error) override {
        if (fail_acquire) { error=acquire_error; endpoint_closed=true; lease_id=0; return false; }
        acquired_route=route; required_capabilities=capabilities; acquired=true;
        endpoint_closed=false; lease_id=41; return true;
    }
    bool submitEvents(wsprrypi::Rp1GpclkProviderEventProgram& value, std::string& error) override {
        if (fail_submit) { error=submit_error; return false; }
        value.generation=1;
        event_program=value; submitted=true; state_value=completion_after_submit; return true;
    }
    bool requestFiniteStop(std::uint64_t, std::string&) override { stopped=true; return true; }
    wsprrypi::Rp1GpclkCompletionState state(std::uint64_t) const noexcept override { return state_value; }
    wsprrypi::Rp1GpclkProviderEventState eventState(std::uint64_t) const noexcept override {
        return {state_value,current_event,terminal_reason,cleanup_fault,elapsed_ns,remaining_ns};
    }
    std::uint32_t terminal_reason{RP1_GPCLK_REASON_COMPLETE};
    std::uint32_t cleanup_fault{};
    std::uint64_t elapsed_ns{};
    std::uint64_t remaining_ns{};
    wsprrypi::Rp1GpclkCompletionState completion_after_submit{
        wsprrypi::Rp1GpclkCompletionState::complete};
    bool release(std::string& error) noexcept override {
        released=true; endpoint_closed=true; lease_id=0;
        if (!release_ok) error=release_error;
        return release_ok;
    }
    std::uint64_t leaseId() const noexcept override { return lease_id; }
    bool endpointClosed() const noexcept override { return endpoint_closed; }
    std::uint32_t acquired_route{}; std::uint64_t required_capabilities{};
    bool acquired{},submitted{},stopped{},released{};
    bool release_ok{true},endpoint_closed{true};
    std::uint64_t lease_id{};
    std::string release_error{"injected release failure"};
    bool fail_acquire{},fail_submit{};
    std::string acquire_error{"injected acquire failure"};
    std::string submit_error{"injected submit ENOTTY"};
    wsprrypi::Rp1GpclkProviderEventProgram event_program{};
    std::uint32_t current_event{};
    wsprrypi::Rp1GpclkCompletionState state_value{wsprrypi::Rp1GpclkCompletionState::idle};
};

wsprrypi::ExecutionPlan framePlan(std::size_t count=162)
{
    constexpr double spacing=12000.0/8192.0;
    constexpr auto duration=std::chrono::nanoseconds{682666667};
    wsprrypi::ExecutionPlan plan;
    plan.id.value=7; plan.backend=wsprrypi::BackendKind::RP1_GPCLK;
    plan.mode=wsprrypi::TransmissionMode::WSPR;
    plan.reference_frequency_hz=14097100.0;
    plan.calibration.ppm=3.802;
    for (std::size_t i=0;i<count;++i) {
        wsprrypi::RfEvent event; event.rf_on=true;
        event.offset_from_start=duration*i; event.duration=duration;
        event.frequency_hz=14097100.0-1.5*spacing+(i%4)*spacing;
        plan.events.push_back(event);
    }
    return plan;
}

wsprrypi::BackendExecutionInputs developmentInputs(int drive=2, int gpio=4)
{
    wsprrypi::BackendExecutionInputs inputs;
    inputs.power_level=drive; inputs.tx_gpio=inputs.configured_tx_gpio=gpio;
    auto& d=inputs.rp1_development;
    d.enabled=true; d.persisted_gpio=d.active_gpio=d.module_gpio=d.confirmation_gpio=gpio;
    d.active_route_count=1; d.route_transaction_resolved=true;
    d.scheduler_idle=d.application_owns_operation=true;
    d.endpoint_available=d.endpoint_closed=d.endpoint_exclusively_acquirable=true;
    d.physical_connection_confirmed=true;
    d.attenuation_and_load_confirmed=d.bounded_operation_confirmed=true;
    d.non_radiating_topology_confirmed=d.experimental_status_acknowledged=true;
    d.confirmation_current=true; d.operation_id=d.confirmation_operation_id="test-operation";
    d.route_transaction_generation=d.confirmation_route_transaction_generation=3;
    wsprrypi::Rp1GpclkProviderIdentity identity;
    identity.route=gpio==20 ? RP1_GPCLK_ROUTE_GPIO20 : RP1_GPCLK_ROUTE_GPIO4;
    identity.compatibility_state=RP1_GPCLK_COMPAT_EXPERIMENTAL;
    identity.capabilities=RP1_GPCLK_CAP_SUBMIT_EVENTS |
        RP1_GPCLK_CAP_STOP_DRAIN | RP1_GPCLK_CAP_STABLE_STATE |
        RP1_GPCLK_CAP_ROUTE_IDENTITY | RP1_GPCLK_CAP_COMPAT_IDENTITY |
        RP1_GPCLK_CAP_CLEANUP_FAULT_LATCH | RP1_GPCLK_CAP_OUTPUT_INHIBIT |
        RP1_GPCLK_CAP_PASSIVE_SNAPSHOT | RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS;
    identity.module_id="rp1-gpclk-dkms"; identity.build_id="1.1.2";
    identity.compatibility_id=gpio==20
        ? "external-provider-gpio20" : "external-provider-gpio4";
    return inputs;
}

wsprrypi::ExecutionPlan qrssPlan()
{
    wsprrypi::ExecutionPlan plan; plan.id.value=8;
    plan.backend=wsprrypi::BackendKind::RP1_GPCLK;
    plan.mode=wsprrypi::TransmissionMode::QRSS;
    plan.reference_frequency_hz=14097100.0+1.5*(12000.0/8192.0);
    plan.calibration.ppm=-4.25;
    wsprrypi::RfEvent on; on.rf_on=true; on.duration=std::chrono::seconds(1); on.frequency_hz=14097100.0;
    wsprrypi::RfEvent off; off.rf_on=false; off.offset_from_start=on.duration; off.duration=std::chrono::seconds(1); off.frequency_hz=14097100.0;
    plan.events={on,off}; plan.summary.event_count=2; plan.summary.total_duration=std::chrono::seconds(2); plan.summary.min_frequency_hz=plan.summary.max_frequency_hz=14097100.0;
    return plan;
}

wsprrypi::ExecutionPlan twoTonePlan(wsprrypi::TransmissionMode mode, bool gated)
{
    wsprrypi::ExecutionPlan plan; plan.id.value=9;
    plan.backend=wsprrypi::BackendKind::RP1_GPCLK; plan.mode=mode;
    const double low=14097100.0, high=14097105.0;
    plan.reference_frequency_hz=low+1.5*(high-low);
    wsprrypi::RfEvent first; first.rf_on=true; first.duration=std::chrono::seconds(1); first.frequency_hz=low;
    wsprrypi::RfEvent second; second.rf_on=true; second.offset_from_start=first.duration; second.duration=std::chrono::seconds(1); second.frequency_hz=high;
    plan.events={first,second};
    if (gated) { wsprrypi::RfEvent gap; gap.rf_on=false; gap.offset_from_start=std::chrono::seconds(2); gap.duration=std::chrono::seconds(1); plan.events.push_back(gap); }
    plan.summary.event_count=plan.events.size(); plan.summary.total_duration=gated ? std::chrono::seconds(3) : std::chrono::seconds(2); plan.summary.min_frequency_hz=low; plan.summary.max_frequency_hz=high;
    return plan;
}

wsprrypi::ExecutionPlan tonePlan(bool explicit_duration)
{
    auto plan=qrssPlan(); plan.id.value=10; plan.mode=wsprrypi::TransmissionMode::TONE;
    plan.reference_frequency_hz=14097100.0; plan.duration_was_explicit=explicit_duration;
    return plan;
}

wsprrypi::Rp1GpclkPlan expectedPlan(
    double center_frequency_hz, double spacing_hz, double source_rate_ppm=0.0)
{
    wsprrypi::Rp1GpclkPlannerInput input;
    input.center_frequency_hz=center_frequency_hz;
    input.tone_spacing_hz=spacing_hz;
    input.parent_frequency_hz=
        wsprrypi::kRp1GpclkNominalParentFrequencyHz;
    input.source_rate_ppm=source_rate_ppm;
    input.intrinsic_source_rate_ppm=-46.245;
    input.maximum_output_hz=100000000.0;
    input.dither_sequence_length=wsprrypi::Rp1GpclkBackend::kWritesPerSymbol;
    const auto result=wsprrypi::planRp1GpclkWspr(input);
    expect(result.ok,"200 MHz compatibility parent must produce a valid plan");
    expect(result.plan.nominal_parent_frequency_hz==200000000.0,
        "compatibility parent must remain explicitly fixed at 200 MHz");
    const double required_corrected_parent=
        200000000.0*(1.0+(source_rate_ppm-46.245)/1000000.0);
    expect(std::fabs(result.plan.corrected_parent_frequency_hz-
            required_corrected_parent)<0.001,
        "corrected parent must apply source-rate PPM to the 200 MHz nominal rate");
    return result.plan;
}

void test_direct_band_range()
{
    using Mode=wsprrypi::TransmissionMode;
    expect(wsprrypi::kRp1GpclkMaximumDirectOutputHz==100000000.0,
        "direct-output contract must remain 100 MHz, not the parent rate");
    for (int gpio : {4,20})
    for (double frequency : {137500.0,475700.0,1838100.0,3570100.0,
        5288700.0,7040100.0,10140200.0,14097100.0,18106100.0,21096100.0,
        24926100.0,28126100.0,50294500.0,70092500.0,144490500.0})
    for (double ppm : {-200.0,0.0,200.0})
    for (Mode mode : {Mode::TONE,Mode::WSPR,Mode::QRSS,Mode::FSKCW,Mode::DFCW})
    {
        auto plan=mode==Mode::TONE ? tonePlan(true) :
            mode==Mode::WSPR ? framePlan() :
            mode==Mode::QRSS ? qrssPlan() : twoTonePlan(mode,mode==Mode::DFCW);
        const double shift=frequency-14097100.0;
        plan.reference_frequency_hz+=shift;
        plan.summary.min_frequency_hz+=shift;
        plan.summary.max_frequency_hz+=shift;
        plan.calibration.ppm=ppm;
        for (auto& event : plan.events) event.frequency_hz+=shift;
        Owner owner;
        auto provider=std::make_unique<Provider>();
        Provider* observed=provider.get();
        WsprRp1GpclkBackend backend(owner,std::move(provider));
        const auto configured=backend.configure(plan,developmentInputs(2,gpio));
        if (frequency>100000000.0)
        {
            expect(!configured.ok && configured.error.find("output range")!=std::string::npos,
                "2 m must remain rejected without harmonic planning");
            expect(!observed->acquired && !observed->submitted,
                "out-of-range plans must not acquire or submit to the provider");
            continue;
        }
        expect(configured.ok,"12 m, 6 m and 4 m must configure for every implemented mode");
        if (!configured.ok) continue;
        const auto executed=backend.execute(plan);
        expect(executed.ok,"direct-band mock provider execution must complete");
        if (!executed.ok) continue;
        expect(observed->acquired && observed->submitted && observed->released,
            "direct-band execution must retain acquire/submit/release lifecycle");
        expect(observed->acquired_route==(gpio==20 ? RP1_GPCLK_ROUTE_GPIO20 : RP1_GPCLK_ROUTE_GPIO4),
            "direct-band execution must retain the selected route");
        expect(!observed->event_program.tones.empty(),
            "every product mode must submit at least one generic tone");
        if (observed->event_program.tones.empty()) continue;
        const auto tone=observed->event_program.tones[0];
        const double requested=plan.events[0].frequency_hz;
        // Independent literal/formula: catches missing, doubled, wrong-sign,
        // or multiplicatively applied intrinsic offsets in every adapter path.
        const double parent=200000000.0*(1.0+(ppm-46.245)*1e-6);
        const double average=parent*65536.0*
            (double(tone.lower_count)/tone.lower_divider_word+
             double(tone.upper_count)/tone.upper_divider_word)/
            (tone.lower_count+tone.upper_count);
        expect(std::fabs(average-requested)<=0.01,
            "submitted divider counts must synthesize direct RF within the unchanged tolerance");
        if (mode==Mode::TONE)
            expect(observed->event_program.events.size()==1 &&
                observed->event_program.events[0].duration_ns==1000000000ULL,
                "finite tones must preserve their duration as generic events");
    }
}
}

int main()
{
    test_direct_band_range();
    {
        Owner scheduled_owner;
        auto scheduled_provider = std::make_unique<Provider>();
        auto* physical = scheduled_provider.get();
        WsprRp1GpclkBackend scheduled(scheduled_owner, std::move(scheduled_provider));
        bool allow_launch = false;
        unsigned admissions = 0, launches = 0;
        expect(scheduled.setScheduledExecutionHooks({[&] { ++admissions; return allow_launch; },
            [&] { ++launches; }, [] { return false; }, true}),
            "RP1 accepts native scheduled hooks");
        auto finite = framePlan();
        finite.summary.total_duration = finite.events.back().offset_from_start +
            finite.events.back().duration;
        wsprrypi::RfEvent tail;
        tail.offset_from_start = finite.summary.total_duration;
        tail.duration = std::chrono::nanoseconds{1};
        finite.events.push_back(tail);
        finite.summary.total_duration += tail.duration;
        expect(scheduled.configure(finite, developmentInputs()).ok,
            "WTP terminal RF-off is accepted without a short provider event");
        const auto realized = scheduled.realizedEventFrequencies();
        expect(realized.size() == finite.events.size() && realized.back() == 0 &&
            std::fabs(realized.front() - finite.events.front().frequency_hz) <= 0.01,
            "RP1 realization comes from the configured divider/count plan");
        expect(!scheduled.execute(finite).ok && admissions == 1 && launches == 0 &&
            !physical->submitted && physical->released,
            "declined launch never submits output and releases the provider");
        allow_launch = true;
        expect(scheduled.configure(finite, developmentInputs()).ok &&
            scheduled.execute(finite).ok && launches == 1 && physical->submitted &&
            physical->event_program.events.size() == 162,
            "admitted scheduled WSPR submits one complete frame and observes launch");
        auto bad = finite;
        bad.events.back().offset_from_start += std::chrono::nanoseconds{1};
        expect(!scheduled.configure(bad, developmentInputs()).ok,
            "inconsistent terminal-off offset is rejected");
        auto gated = tonePlan(true);
        expect(scheduled.configure(gated, developmentInputs()).ok &&
            scheduled.realizedEventFrequencies().size() == 2,
            "scheduled TONE preserves its finite RF-off interval");
        expect(scheduled.cleanup().ok, "scheduled provider cleanup remains explicit");
    }
    Owner owner;
    auto provider=std::make_unique<Provider>();
    Provider* observed=provider.get();
    WsprRp1GpclkBackend backend(owner,std::move(provider));
    auto short_plan=framePlan(161);
    expect(!backend.configure(short_plan,developmentInputs()).ok,"short frame must be rejected");
    auto plan=framePlan();
    expect(!backend.configure(plan,developmentInputs(6)).ok,"invalid drive must be rejected");
    auto default_denied_provider=std::make_unique<Provider>();
    Provider* default_denied_observed=default_denied_provider.get();
    WsprRp1GpclkBackend default_denied_backend(owner,std::move(default_denied_provider));
    expect(default_denied_backend.configure(plan,{2,4}).ok,
        "ordinary RP1 selection may compile without granting development authorization");
    const auto default_denied=default_denied_backend.execute(plan);
    expect(!default_denied.ok && !default_denied_observed->acquired,
        "ordinary and direct-adapter execution must be denied before endpoint acquisition");
    expect(backend.configure(plan,developmentInputs()).ok,"valid frame must configure");
    const auto result=backend.execute(plan);
    expect(result.ok && !result.stopped,"valid frame must execute");
    expect(observed->acquired && observed->submitted && observed->released,"provider lifecycle must complete");
    expect(observed->event_program.drive_ma==2,"minimum drive must be carried in submission");
    expect(observed->acquired_route==RP1_GPCLK_ROUTE_GPIO4,
        "GPIO4 route must be carried independently into acquisition");
    expect(observed->event_program.events[0].tone_index==0 &&
        observed->event_program.events[1].tone_index==1,
        "symbol order must preserve tone indexes");
    expect(observed->event_program.tones[0].lower_divider_word !=
            observed->event_program.tones[1].lower_divider_word ||
        observed->event_program.tones[0].upper_divider_word !=
            observed->event_program.tones[1].upper_divider_word ||
        observed->event_program.tones[0].lower_count !=
            observed->event_program.tones[1].lower_count ||
        observed->event_program.tones[0].upper_count !=
            observed->event_program.tones[1].upper_count,
        "frame must carry distinct complete tone plans");
    const auto expected_wspr=expectedPlan(
        plan.reference_frequency_hz,12000.0/8192.0,plan.calibration.ppm);
    expect(observed->event_program.tones[0].lower_divider_word==
            expected_wspr.tones[0].lower_divider_word &&
        observed->event_program.tones[0].upper_divider_word==
            expected_wspr.tones[0].upper_divider_word,
        "WSPR must plan divider words from the 200 MHz compatibility parent");
    const auto operation_record=wsprrypi::rp1GpclkOperationRecordSnapshot();
    expect(operation_record.schema_version==2, "operation record schema must expose failure detail");
    expect(operation_record.operation_id=="test-operation", "operation record must preserve operation identity");
    expect(operation_record.module_version=="1.1.2", "operation record must preserve module version");
    expect(operation_record.route==RP1_GPCLK_ROUTE_GPIO4, "operation record must preserve route");
    expect(operation_record.generation==1, "operation record must preserve generation");
    expect(operation_record.state=="complete", "operation record must preserve terminal completion");
    expect(operation_record.cleanup_attempted && operation_record.cleanup_complete,
        "operation record must preserve cleanup completion");
    expect(operation_record.endpoint_closed && operation_record.lease==0,
        "operation record must preserve endpoint closure and lease release");
    expect(!operation_record.qualification_claim,
        "operation record must remain explicitly nonqualifying");

    auto event_provider=std::make_unique<Provider>();
    Provider* event_observed=event_provider.get();
    WsprRp1GpclkBackend event_backend(owner,std::move(event_provider));
    auto qrss=qrssPlan();
    expect(event_backend.configure(qrss,developmentInputs()).ok,"QRSS finite events must configure");
    const auto event_result=event_backend.execute(qrss);
    expect(event_result.ok && event_observed->event_program.events.size()==2,
        "QRSS finite event program must execute through provider contract");
    expect(event_observed->event_program.events[0].rf_on && !event_observed->event_program.events[1].rf_on,
        "QRSS RF gating must be preserved");
    const auto expected_qrss=expectedPlan(
        qrss.reference_frequency_hz,12000.0/8192.0,qrss.calibration.ppm);
    expect(event_observed->event_program.tones[0].lower_divider_word==
            expected_qrss.tones[0].lower_divider_word &&
        event_observed->event_program.tones[0].upper_divider_word==
            expected_qrss.tones[0].upper_divider_word,
        "finite events must plan divider words from the 200 MHz compatibility parent");

    auto fsk_provider=std::make_unique<Provider>(); Provider* fsk_observed=fsk_provider.get();
    WsprRp1GpclkBackend fsk_backend(owner,std::move(fsk_provider)); auto fsk=twoTonePlan(wsprrypi::TransmissionMode::FSKCW,false);
    expect(fsk_backend.configure(fsk,developmentInputs()).ok && fsk_backend.execute(fsk).ok,
        "FSKCW finite events must execute");
    expect(fsk_observed->event_program.tones.size()==2 && fsk_observed->event_program.events[0].rf_on && fsk_observed->event_program.events[1].rf_on,
        "FSKCW must preserve two continuous-RF tones");

    auto dfcw_provider=std::make_unique<Provider>(); Provider* dfcw_observed=dfcw_provider.get();
    WsprRp1GpclkBackend dfcw_backend(owner,std::move(dfcw_provider)); auto dfcw=twoTonePlan(wsprrypi::TransmissionMode::DFCW,true);
    expect(dfcw_backend.configure(dfcw,developmentInputs()).ok && dfcw_backend.execute(dfcw).ok,
        "DFCW finite events must execute");
    expect(!dfcw_observed->event_program.events[2].rf_on,
        "DFCW must preserve RF-off gaps");

    auto tone_provider=std::make_unique<Provider>(); Provider* tone_observed=tone_provider.get();
    WsprRp1GpclkBackend tone_backend(owner,std::move(tone_provider));
    auto implicit_tone=tonePlan(false);
    expect(tone_backend.configure(implicit_tone,developmentInputs()).ok && tone_backend.execute(implicit_tone).ok,
        "implicit-duration TONE must use the continuous operation");
    expect(tone_observed->event_program.events.size()==1 &&
        tone_observed->event_program.events[0].duration_ns==
            RP1_GPCLK_EVENT_DURATION_NS_MAX &&
        (tone_observed->required_capabilities &
            RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS)!=0,
        "continuous TONE must use the maximum finite generic event and bounded chunks");
    const auto expected_tone=expectedPlan(
        implicit_tone.reference_frequency_hz+1.5*(12000.0/8192.0),
        12000.0/8192.0,implicit_tone.calibration.ppm);
    expect(tone_observed->event_program.tones[0].lower_divider_word==
            expected_tone.tones[0].lower_divider_word &&
        tone_observed->event_program.tones[0].upper_divider_word==
            expected_tone.tones[0].upper_divider_word,
        "TONE must plan divider words from the 200 MHz compatibility parent");
    auto finite_provider=std::make_unique<Provider>(); Provider* finite_observed=finite_provider.get();
    finite_observed->terminal_reason = RP1_GPCLK_REASON_COMPLETE;
    WsprRp1GpclkBackend finite_backend(owner,std::move(finite_provider));
    auto explicit_tone=tonePlan(true);
    expect(finite_backend.configure(explicit_tone,developmentInputs()).ok && finite_backend.execute(explicit_tone).ok,
        "explicit-duration TONE must use the finite operation");
    expect(wsprrypi::rp1GpclkOperationRecordSnapshot().terminal_reason == RP1_GPCLK_REASON_COMPLETE,
        "TONE must preserve the provider terminal reason in its operation record");
    expect(finite_observed->event_program.events.size()==1 &&
        finite_observed->event_program.events[0].duration_ns==1000000000ULL,
        "finite TONE must preserve the kernel-owned one-second generic event");

    auto faded=qrssPlan(); faded.events[0].envelope.fade_shape=wsprrypi::FadeShape::LINEAR;
    auto fade_provider=std::make_unique<Provider>(); WsprRp1GpclkBackend fade_backend(owner,std::move(fade_provider));
    expect(!fade_backend.configure(faded,developmentInputs()).ok,"RP1 finite-event fades must be rejected, not approximated");

    auto cw=qrssPlan(); cw.mode=wsprrypi::TransmissionMode::CW;
    auto cw_provider=std::make_unique<Provider>(); WsprRp1GpclkBackend cw_backend(owner,std::move(cw_provider));
    expect(!cw_backend.configure(cw,developmentInputs()).ok,"unimplemented canonical CW must remain rejected");

    auto acquire_failure_provider=std::make_unique<Provider>();
    Provider* acquire_failure_observed=acquire_failure_provider.get();
    acquire_failure_observed->fail_acquire=true;
    WsprRp1GpclkBackend acquire_failure_backend(owner,std::move(acquire_failure_provider));
    expect(acquire_failure_backend.configure(plan,developmentInputs()).ok,"acquire-failure frame must configure");
    const auto acquire_failure=acquire_failure_backend.execute(plan);
    expect(!acquire_failure.ok && !acquire_failure.faulted,
        "acquire failure must remain an unsuccessful non-terminal-fault result");
    expect(acquire_failure.error=="injected acquire failure",
        "acquire failure must preserve provider error text");
    expect(!acquire_failure_observed->released,
        "failed acquire must not release ownership that was never acquired");
    const auto acquire_failure_record=wsprrypi::rp1GpclkOperationRecordSnapshot();
    expect(acquire_failure_record.state=="acquire-failed" &&
        acquire_failure_record.cleanup_attempted && acquire_failure_record.cleanup_complete &&
        acquire_failure_record.endpoint_closed && acquire_failure_record.lease==0,
        "acquisition failure must leave a terminal closed-endpoint record");

    auto submit_failure_provider=std::make_unique<Provider>();
    Provider* submit_failure_observed=submit_failure_provider.get();
    submit_failure_observed->fail_submit=true;
    WsprRp1GpclkBackend submit_failure_backend(owner,std::move(submit_failure_provider));
    expect(submit_failure_backend.configure(plan,developmentInputs()).ok,"submit-failure frame must configure");
    const auto submit_failure=submit_failure_backend.execute(plan);
    expect(!submit_failure.ok && !submit_failure.faulted,
        "submit failure must remain an unsuccessful non-terminal-fault result");
    expect(submit_failure.error=="injected submit ENOTTY",
        "submit failure must preserve provider error text");
    expect(submit_failure_observed->acquired && submit_failure_observed->released,
        "submit failure must release acquired provider ownership");
    const auto submit_failure_record=wsprrypi::rp1GpclkOperationRecordSnapshot();
    expect(submit_failure_record.state=="submit-failed" &&
        submit_failure_record.cleanup_attempted && submit_failure_record.cleanup_complete &&
        submit_failure_record.endpoint_closed && submit_failure_record.lease==0,
        "submission failure must leave a terminal closed-endpoint record");

    auto provider_failure_provider=std::make_unique<Provider>();
    Provider* provider_failure_observed=provider_failure_provider.get();
    provider_failure_observed->completion_after_submit=
        wsprrypi::Rp1GpclkCompletionState::failed;
    provider_failure_observed->terminal_reason=RP1_GPCLK_REASON_PINCTRL_FAILED;
    provider_failure_observed->cleanup_fault=1;
    provider_failure_observed->elapsed_ns=250000000ULL;
    provider_failure_observed->remaining_ns=750000000ULL;
    WsprRp1GpclkBackend provider_failure_backend(
        owner,std::move(provider_failure_provider));
    expect(provider_failure_backend.configure(plan,developmentInputs()).ok,
        "provider-failure frame must configure");
    const auto provider_failure=provider_failure_backend.execute(plan);
    expect(!provider_failure.ok && provider_failure.faulted &&
        provider_failure.cleanup_attempted && provider_failure.cleanup.ok &&
        provider_failure.error.find("pinctrl-failed (11)")!=std::string::npos &&
        provider_failure.error.find("cleanup fault")!=std::string::npos,
        "provider failure must preserve terminal reason and cleanup state");
    const auto provider_failure_record=
        wsprrypi::rp1GpclkOperationRecordSnapshot();
    expect(provider_failure_record.schema_version==2 &&
        provider_failure_record.terminal_reason_name=="pinctrl-failed" &&
        provider_failure_record.cleanup_fault &&
        provider_failure_record.elapsed_ns==250000000ULL &&
        provider_failure_record.remaining_ns==750000000ULL,
        "operation record must expose structured provider failure detail");

    auto cleanup_failure_provider=std::make_unique<Provider>();
    Provider* cleanup_failure_observed=cleanup_failure_provider.get();
    cleanup_failure_observed->completion_after_submit=
        wsprrypi::Rp1GpclkCompletionState::failed;
    cleanup_failure_observed->terminal_reason=RP1_GPCLK_REASON_CLEANUP_FAILED;
    cleanup_failure_observed->cleanup_fault=1;
    cleanup_failure_observed->release_ok=false;
    WsprRp1GpclkBackend cleanup_failure_backend(
        owner,std::move(cleanup_failure_provider));
    expect(cleanup_failure_backend.configure(plan,developmentInputs()).ok,
        "cleanup-failure frame must configure");
    const auto cleanup_failure=cleanup_failure_backend.execute(plan);
    expect(!cleanup_failure.ok && cleanup_failure.faulted &&
        cleanup_failure.error.find("cleanup-failed (13)")!=std::string::npos &&
        cleanup_failure.error.find("injected release failure")!=std::string::npos,
        "cleanup failure must preserve both terminal reason and release diagnostic");
    const auto cleanup_failure_record=
        wsprrypi::rp1GpclkOperationRecordSnapshot();
    expect(cleanup_failure_record.state=="cleanup-fault" &&
        cleanup_failure_record.cleanup_fault &&
        !cleanup_failure_record.cleanup_complete &&
        cleanup_failure_record.endpoint_closed &&
        cleanup_failure_record.lease==0,
        "cleanup failure must distinguish a latched provider fault from local endpoint ownership");
    if (failures) return 1;
    std::cout << "RP1 GPCLK scheduler backend tests passed\n";
}
