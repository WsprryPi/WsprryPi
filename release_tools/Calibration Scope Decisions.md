# Calibration authority and scope

Native/direct PPS RF calibration and alternative RP1 clock-parent evaluation
are deferred beyond WsprryPi 4.0. RP1 output uses nominal 200 MHz `pll_sys`.
The [release punchlist](Release%20Punchlist.md#saved-items) retains those scope
decisions and their reopening criteria. Existing correction settings and
backend ownership apply independently of those deferred features.

## Current capability and correction authority

| Path or authority | Implemented behavior | Limit or ownership boundary |
| --- | --- | --- |
| UTC and slots | Chrony disciplines host time; the application schedules against host wall time. | Keep Chrony as UTC authority. Slot alignment is distinct from RF-frequency accuracy and backend launch latency. |
| Chrony adapter | [PPMManager](../src/PPM-Manager/src/ppm_manager.cpp), `get_chrony_snapshot` / `parseChronyReports`, reads `chronyc -c tracking`, `sources -n`, and `sourcestats -n`; signed tracking frequency passes through unchanged. | Source signatures and heuristic provenance describe reports, not authenticated GNSS lock or a verified RF reference path. No native PPS device/capture API was found in application source. |
| GPIO estimate selection | [Qualifier and selector](../src/system_clock_frequency_estimate.cpp) use synchronization, selected source, normal leap status, identity, quality, stability and age, then select usable estimate plus conducted residual, fixed manual, or zero additional correction. | A host-clock estimate is a proxy for RF error; retained behavior does not establish cross-domain accuracy. Details and source limitations follow below. |
| Intrinsic RF defaults | [Shared selector](../src/Chipset-Offsets/src/chipset_offsets.hpp): BCM2835 **-2.5 ppm**; BCM2836/BCM2837 and BCM2711 **0 ppm**; RP1 **-46.245 ppm**. | Product defaults with different evidence histories, not certificates for individual boards. Physical GPIO backends add the intrinsic term once. |
| Legacy GPIO | [Clock models](../src/WSPR-Transmitter/src/legacy_gpio_clock_model.cpp) use PLLD 500 MHz for BCM2835/6/7, PLLD 750 MHz for BCM2711, and BCM2711 oscillator 54 MHz when PLLD cannot represent the tone range. | Processor, complete frequency range and corrected-divider representability select the parent; unsupported cases fail. RF correction and PWM/DMA timing are separate. |
| RP1 GPIO | [Planner](../src/WSPR-Transmitter/src/rp1_gpclk_planner.hpp) uses nominal 200 MHz; [event compiler](../src/WSPR-Transmitter/src/rp1_gpclk_event_program.cpp) supplies additional correction and the shared intrinsic separately. | Current provider owns parent selection/restoration. Numerical planning is not measurement of the physical parent or qualification of either route. |
| Si5351 | [Backend-specific config](../src/config_handler.cpp), `resolve_backend_specific_config`, selects `si5351_ppm`; [planner](../src/WSPR-Transmitter/src/si5351_planner.cpp), `effectiveReferenceHz`, uses its own reference calibration. | GPIO/Chrony/intrinsic terms do not apply. Its `reference_hz * (1 - calibration_ppm / 1e6)` convention must not be replaced by the GPIO sign convention. |
| Outbound WTP / receiving Pi | Backend selection clears host PPM/estimate use for outbound WTP; [converter](../src/wtp_integration/execution_plan.cpp) rejects nonzero host calibration or a reference override. A [receiving Pi](../src/wtp_endpoint/runtime.cpp) selects its own local correction before preparing its backend plan. | No Pi-host correction is transported to the remote transmitter. Receiving-device calibration remains local to that complete transmitter application. |

### Selection, persistence and committed work

[Defaults and migration](../src/config_handler.cpp) enable the GPIO estimate
with residual/manual zero. The zero-initialized [config structure](../src/config_types.hpp)
is not the loaded-default policy. Retired `GPIO.Use NTP` migrates to the
canonical estimate key, preserving an existing canonical value; absent manual
PPM inherits old `Calibration.PPM` only when the legacy estimate was disabled.
[Deserialization](../src/config_handler_deserialization.cpp) validates finite
GPIO inputs within +/-200 ppm; [serialization](../src/config_handler_serialization.cpp)
persists estimate enable, residual and manual separately. `Calibration.PPM`
persists Si5351 reference calibration. [Form population](../WsprryPi-UI/data/site.js)
uses these separate saved values; configuration saves do not persist a live
estimate, intrinsic term or composed RF total. Saved values retain these independent semantics.

[Qualification constants](../src/system_clock_frequency_estimate.hpp) require
three stable evaluations with spread <=0.1 ppm, at least three retained provider
samples, skew <=1 ppm, absolute Chrony residual frequency <=0.5 ppm, and current
age <=300 seconds. A changed source signature resets the evaluation history.
These are evaluations, not proof of three distinct new PPS observations or a
fixed observation duration. Reuse requires a previously qualified snapshot,
with its reported age plus monotonic elapsed time since that qualification
evaluation <=900 seconds; an old first observation cannot qualify itself.
These age gates are software policy, not independent verification of the
physical reference. Missing, malformed or ambiguous selected-source age remains
unknown. Snapshots carry a monotonic acquisition anchor and cached copies accrue
elapsed age at consumption, including copies already aged by the component.
The application rechecks age when selecting correction for future requests,
including inbound WTP preparation. A current unavailable/converging sample can
still select a previously qualified estimate within the bounded stale interval;
qualification and selected correction mode are different fields.

Provenance considers selected (`*`) and combined (`+`) source rows. Chrony mode
`#` identifies a local reference; `^` and `=` identify network sources. Only a
cross-kind combination is `Mixed`; unrecognized modes remain `Unknown`.
Unselected PPS/GNSS rows and source names do not establish local provenance,
reference lock or RF accuracy. See the
[PPM-Manager contract](../src/PPM-Manager/README.md) for acquisition and parsing.

Let `E` be the selected qualified/retained estimate, `R` the configured conducted
residual, `M` fixed manual, `I` intrinsic, and `A` additional correction:

```text
usable estimate enabled:     A = E + R
otherwise:                  A = M (estimate disabled, or nonzero fallback)
otherwise:                  A = 0 (uncalibrated additional correction)
RF divider model:           F_model = F_nominal * (1 + (I + A) / 1e6)
```

Residual is unused in manual/zero mode; manual is not added to an estimate.
Chrony's residual-frequency quality statistic is not `R` and is not another
correction term. Each configured input and selected `A` retains its independent
+/-200 ppm limit; intrinsic and additional are separately bounded, so their
finite sum can reach +/-400 ppm. In particular, RP1 `A=-200` produces total
`-246.245`, which is valid. Positive GPIO PPM means a fast modeled source:
increasing the modeled parent increases the divider and lowers physical RF.
The formula changes divider calculations, not the physical parent rate.

A conducted residual must measure what remains with the current intrinsic and
estimate already active. A full independently measured parent error `P` must
be reconciled as additional `P-I`, or residual `P-I-E`, before reuse; it must
not be stacked as another full correction. For example, old RP1 manual total
`-46.245` corresponds to additional zero under the current baseline. No saved
custom value is automatically rewritten. Receiver/SDR calibration corrects the
measurement once; it is not an extra production-transmitter term. These are the
existing additive ppm conventions, not a new higher-order algorithm.

[Local scheduling](../src/scheduling.cpp), `select_and_publish_gpio_correction` and
`freeze_gpio_correction_provenance`, retains internal components/total but puts
only `A` into committed requests. Legacy parent identity is resolved against
the execution frequency range; RP1 records `PLL_SYS`/200 MHz. Candidate refresh
does not rewrite the committed request or its execution identity.
[Reload handling](../src/scheduling_config.cpp) defers changes while execution
requires it. [WebSocket reporting](../src/web_socket_commands.cpp) exposes
additional-only `correction_ppm` and `effective_gpio_ppm`, with provenance;
intrinsic/composed totals remain internal. The [legacy backend](../src/WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp),
`setup_dma_freq_table`, adds intrinsic for RF once, while its PWM/DMA model uses
additional correction only. RP1 event durations/tick parameters do not acquire
the intrinsic RF correction. No new mid-transmission adjustment is selected.
The [RP1 adapter](../src/WSPR-Transmitter/src/rp1_gpclk_transmit_backend.cpp),
`configure`, retains the compiled divider/event program for execution.
Inbound WTP separately freezes the receiving Pi's correction into its plan at
`WtpPiToneEngine::prepare`; this does not assert that inbound jobs populate the
local scheduler's candidate/committed status fields.

## Direct PPS disposition and reopening criteria

**Defer native PPS RF calibration beyond 4.0; retain the existing capabilities.**
Chrony can already use administrator-configured PPS/GNSS for host synchronization
and its frequency estimate. Reading the same host timestamps directly offers
diagnostics or another host-clock estimator, but does not independently measure
an unobserved RF clock. Linux PPS supplies edge timestamps/sequence numbers;
the timestamp domain, capture latency and jitter still matter. See the
[Chrony tracking reference](https://chrony-project.org/doc/4.4/chronyc.html#tracking)
and [Linux PPS API](https://www.kernel.org/doc/html/latest/driver-api/pps.html).

| Backend | What must be measured for new RF-calibration value | Present evidence gap |
| --- | --- | --- |
| Legacy GPIO | Count the selected PLLD/oscillator or a rigorously identified divider derivative over reference-defined intervals. The host estimate can track correlated drift, but an exact host-timebase-to-RF-parent relationship cannot be assumed across processors/parents. | No reviewed application contract provides a PPS-latched RF counter/capture path; software timestamping alone measures the host domain. Capture facility, routing, ratio and uncertainty remain unproven. |
| RP1 | Measure RP1 `pll_sys`/GPCLK or a verified derivative against an independent reference. Host UTC/frequency estimates do not establish the error of the separate RP1 clock domain. | Current provider has no selected PPS-latched RF measurement interface. Stock-provider frequency counting uses `clk_slow_sys` (derived from XOSC), not a demonstrated independent PPS gate; see the pinned source below. |

A hardware edge counter latched at PPS boundaries could supply the missing
frequency observation for either backend, provided its input really is that
RF parent or a verified derivative and the gate/reference is independent.
No such facility, electrical path or supported capture interface is established
by this review; feasibility must precede estimator implementation.

Reopen only when a separately approved slice can demonstrate material RF
accuracy, drift tracking or repeatability benefit beyond the retained
Chrony/intrinsic/manual/conducted-residual path. Before implementation or live
work, require:

1. **Reference and capture identity:** exact receiver/GPSDO, lock/status evidence,
   PPS routing/level, RF parent/derivative and divider ratio, counter width,
   timestamp domain, capture mechanism and exclusive ownership. GPSDO presence,
   a PPS source label or a nominal rate query is insufficient.
2. **Measurement budget:** characterize reference uncertainty, PPS edge jitter,
   capture latency variation, quantization, overflow/missed pulses, temperature
   and observation duration. One-count resolution is `1e6/(f_count*T)` ppm:
   ideal 200 MHz counting for 1 second gives 0.005 ppm per count; a divided
   1 MHz input gives 1 ppm per count. These are arithmetic examples, not measured
   uncertainty. Both interval boundaries and capture errors need budgeting.
3. **Acceptance target proposed for that slice:** independently measured
   absolute residual <=0.1 ppm with expanded measurement uncertainty <=0.05 ppm
   on every predeclared board/parent/route/frequency/temperature case, with
   repeatability and improvement over the existing baseline greater than the
   combined comparison uncertainty. Approve or revise these engineering targets
   before the campaign; they are not current product limits or achieved results.
4. **One correction authority:** select whether the result replaces the host
   estimate or supplies a residual relative to it. Define positive/negative
   conversion and `I`, `E`, `R`, `M` accounting; never add two full estimates.
   Keep Chrony as UTC authority and preserve Si5351/WTP separation.
5. **Loss/holdover behavior:** declare sample count, observation window, maximum
   age and uncertainty before acceptance; test lock loss, stale input, source
   replacement, missed/duplicate edges and holdover expiry. Never allow
   unbounded reuse or promote a label into verified reference quality.
6. **Commit boundary:** deterministic tests must show zero changes to committed
   divider/event/provenance snapshots during refresh, loss and cancellation;
   any accepted new value applies only to future work. Test signs, range edges,
   fallback, persistence compatibility and invalid measurements without hardware.
7. **Independent validation:** use an independently calibrated counter or receiver
   with identified reference routing, including validation independent of the
   estimator under test. Record raw evidence and uncertainty across repeated
   sessions. Separately authorize hardware/RF operation and verify safe stopping,
   output-off and cleanup. Frequency acceptance does not qualify power/spurs.

A PPS-latched RF capture path and its measurement benefit remain unestablished.
Implementation before 4.0 would change the selected release scope.

## RP1 parent disposition

The [provider contract at the reviewed revision](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/924c7e546ab82a40497339c4baeb813f95f3408e/docs/contracts/rp1-gpclk-dkms-module-contract.md)
and route overlays identify `RP1_CLK_GP0` and `RP1_PLL_SYS` as distinct resources
named `gpclk` and `parent`. These names identify an output and its parent, not
two independent oscillators. [Clock setup/restoration](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/924c7e546ab82a40497339c4baeb813f95f3408e/src/rp1_gpclk_clock_setup.c)
and [execution](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/924c7e546ab82a40497339c4baeb813f95f3408e/src/rp1_gpclk_execution.c)
select and verify parent/rate, including after rate requests that can reparent;
bounded cleanup disables output, restores safe pinctrl and restores prior
parent/rate. Source implementation is not new proof of successful cleanup on
hardware. Userspace must retain nominal 200 MHz and the existing Q16 plan.

For ancestry context, inspect Raspberry Pi's stock
[`clk-rp1.c` at `5d39807272da852e10f3fc4467c33b57f59ecc61`](https://github.com/raspberrypi/linux/blob/5d39807272da852e10f3fc4467c33b57f59ecc61/drivers/clk/clk-rp1.c).
This is an upstream source snapshot, not an observation of an installed kernel.
PLL cores derive from XOSC; `pll_sys` is an output of `pll_sys_core`.

| Clock path | Disposition | Identity constraint for any future evaluation |
| --- | --- | --- |
| `pll_sys` | **Implemented**, nominal 200 MHz. | Keep current provider identity, correction composition, ownership and set/restore contract. |
| `clk_i2s` | **Deferred alternative-parent evaluation** beyond 4.0. | A mux/divider with XOSC, audio-PLL and GP inputs; `CLK_SET_RATE_PARENT` can affect ancestors. No new nominal rate selected. |
| `clk_adc` | **Deferred alternative-parent evaluation** beyond 4.0. | A mux/divider with XOSC/GP inputs in the pinned source; not an independent oscillator. No new nominal rate selected. |
| `clk_sys` | **Deferred alternative-path evaluation** beyond 4.0. | A critical mux/divider which can derive from `pll_sys` or XOSC; a different path can share the same physical source. No new nominal rate selected. |

Future evaluation must first resolve the exact provider IDs, aliases, available
ancestry and nominal rates on the proposed kernel/firmware, excluding cycles
and feedback. Then evaluate ownership/conflicts with other consumers, achievable
range, divider resolution, frequency accuracy/repeatability, phase noise/spurs,
cancellation, output-off and parent/rate restoration. Coordinate module/userspace
compatibility and maintain independent GPIO4/GPIO20 evidence. Finer arithmetic
resolution establishes neither physical accuracy nor spectral improvement.
No parent sweep, selection/fallback control, PLL tuning or new default is chosen.

The historical 50 MHz/XOSC planning assumption is obsolete for current output;
it is not an alternative current contract. Preserve older XOSC observations
under their original identities, without transferring a measured correction to
`pll_sys`. In contrast, the retained
[fourteen-point GPIO20 campaign at `b2b6a40`](https://github.com/WsprryPi/WsprryPi/blob/b2b6a40032597d688eae948524b5737b81e3fb9b/docs/research/issue-429-rp1-accuracy/README.md)
records active `PLL_SYS`/200 MHz and restoration to disabled XOSC/50 MHz afterward.
Its rounded equal-band mean became the shared -46.245 ppm policy default.
Those finite TONE observations do not establish all-board equivalence,
corrected-output closure, temporal stability, GPIO4 evidence or current
candidate qualification. History is retained by immutable links, not rewritten.

## Validation and documentation boundaries

The maintained software checks cover report parsing and aging, selected-source
provenance, current/stale expiry, source loss/replacement, correction composition,
fallback and persistence. From `src`, relevant entry points are:

```sh
make -C PPM-Manager/src test
make gpio-frequency-correction-test chipset-offsets-test SUDO=
make calibration-roundtrip-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
node ../WsprryPi-UI/tests/gpio_correction_provenance_test.js
```

Inspect the recipes and use a supported Linux build environment. These commands
exercise software contracts; they do not qualify host-clock correlation with RF,
reference routing/lock, target timing, services, installation, power or spectrum.
No direct PPS control, clock-parent selector or automatic custom-value migration
is provided by this scope decision.

The independent [operator manual](https://wsprry-pi.readthedocs.io/en/stable/)
covers frequency calibration in `Advanced_Operations/timing_calibration.md`
and RP1 operation in `Advanced_Operations/rp1_gpclk.md`.
Its calibration guidance distinguishes intrinsic/additional accounting, bounded
prior-qualified reuse, unknown age and candidate/committed values. The component
and parent contracts above retain implementation detail and qualification limits.
