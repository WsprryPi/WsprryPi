# Calibration authority and scope

Optional direct PPS RF calibration is implemented for Raspberry Pi 5/RP1.
Its independent conducted acceptance is tracked in the release punchlist.
Direct capture for Pi 1–4 remains outside the selected implementation.
The bounded alternative RP1 clock-parent review is closed: retain nominal
200 MHz `pll_sys`, with no alternative-parent hardware testing selected.
The [release punchlist](Release%20Punchlist.md#saved-items) retains unfinished
work; the decisions and reopening criteria below govern these scopes.
Existing correction units, intrinsic composition, and backend ownership remain unchanged.

## Current capability and correction authority

| Path or authority | Implemented behavior | Limit or ownership boundary |
| --- | --- | --- |
| UTC and slots | Chrony disciplines host time; the application schedules against host wall time. | Keep Chrony as UTC authority. Slot alignment is distinct from RF-frequency accuracy and backend launch latency. |
| Chrony adapter | [PPMManager](../src/PPM-Manager/src/ppm_manager.cpp), `get_chrony_snapshot` / `parseChronyReports`, reads `chronyc -c tracking`, `sources -n`, and `sourcestats -n`; signed tracking frequency passes through unchanged. | Source signatures and heuristic provenance describe reports, not authenticated GNSS lock or a verified RF reference path. The optional RP1 adapter separately counts the RF clock domain against PPS. |
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

## Direct PPS disposition and acceptance criteria

**RP1 direct capture is selected; legacy direct capture remains unimplemented.**
The optional [RP1 adapter](../docs/direct-pps-calibration.md) uses one input-only
PIO state machine to count ten complete GPIO17 PPS periods. It verifies that
nominal 200 MHz `clk_sys` derives directly from `pll_sys`. It replaces the
host-frequency estimate, subtracts the existing intrinsic term before passing
additional correction to the backend, and leaves Chrony as UTC authority.
Three distinct stable observations qualify a candidate; capture errors, changed
reference identity, and expiry retire it. The public PIO UAPI is pinned and
vendored with its upstream licenses. This is a measurement of a verified RF
parent derivative, not software timing of PPS callbacks.

Pi 1–4 transmit from a main-SoC clock parent. Their host and RF clocks can share
an oscillator, but a portable raw-host-counter/PLLD or oscillator ratio has not
been established here. The existing host estimate remains available there.
No Pi 1–4 direct capture implementation or hardware acceptance is implied.

The following requirements govern acceptance and any later capture adapter.
The independent RF campaign must satisfy its declared cases before the
punchlist can mark this feature complete:

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
3. **Declared acceptance:** the selected wspr5 finite-TONE cases use measured
   absolute residual <=0.1 ppm and conditional receiver/reference engineering
   allowance <=0.05 ppm, with baseline improvement exceeding the comparison
   allowances. The [validation record](../docs/direct-pps-validation.md) states
   the assumptions, repeated observations, stopped attempts, and limitations.
   It does not claim traceable expanded metrological uncertainty, all-board
   equivalence, or an accuracy certificate. Any broader slice must declare its
   cases, measurement budget, and comparison target before testing.
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

The capture path has input-only device evidence and independent conducted
accuracy, repeatability, improvement, and cleanup evidence for the selected
wspr5 cases. Those results do not qualify another board, wiring, reference,
operating duration, or installation.

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
| `clk_i2s` | **Review closed; not selected.** | A mux/divider with XOSC, audio-PLL and GP inputs; `CLK_SET_RATE_PARENT` can affect ancestors. No new nominal rate selected. |
| `clk_adc` | **Review closed; not selected.** | A mux/divider with XOSC/GP inputs in the pinned source; not an independent oscillator. No new nominal rate selected. |
| `clk_sys` | **Review closed; not selected.** | A critical mux/divider which can derive from `pll_sys` or XOSC; a different path can share the same physical source. No new nominal rate selected. |

### Bounded feasibility finding

Retain nominal 200 MHz `pll_sys`. The source and numerical review found no
alternative parent with a materially better divider plan at the operating
frequencies considered. No alternative-parent hardware testing is selected.
This closes the review, not physical qualification of any alternative.

Raspberry Pi's reviewed stock
[`rpi-6.12.y` clock driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/clk/clk-rp1.c)
limits `clk_i2s` and `clk_adc` to 50 MHz and critical `clk_sys` to 200 MHz.
The direct audio-PLL parent entry for GPCLK0 is disabled in that source;
the audio VCO rate does not establish a selectable GHz GPCLK0 parent.
The companion [device tree](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/rp1.dtsi)
assigns nominal XOSC/audio output rates of 50, 61.44, and 153.6 MHz.
These branch links describe the source reviewed, not an immutable source pin
or an inspection of an installed kernel. Additional GP source entries do not
establish an independent reference, usable rate, or exclusive ownership.

The production numerical planner at WsprryPi
`5d72dd57465251f63ac937f9876900d38e46cee4` was compiled in a temporary,
hardware-free comparison harness. It used Q16 dividers, WSPR spacing
`12000/8192` Hz, and the production 66,792-entry dither period. The 17 centers
covered established WSPR presets from 2200 m through 4 m, including both 60 m
presets, plus representative 42.5 MHz and 61 MHz centers for 8 m and 5 m.
WSPR presets included the default 1,500 Hz dial-to-RF offset; representative
centers were direct RF inputs, not new presets or qualification claims.

At zero modeled correction, representative maximum adjacent-word separations
and absolute dither-average errors across each four-tone plan were:

| RF center | 200 MHz step | 50 MHz step | 200 MHz average error | 50 MHz average error |
| --- | --- | --- | --- | --- |
| 7.040100 MHz | 3.781 Hz | 15.125 Hz | 0.000028 Hz | 0.000102 Hz |
| 14.097100 MHz | 15.162 Hz | 60.647 Hz | 0.000096 Hz | 0.000389 Hz |
| 28.126100 MHz | 60.354 Hz | 241.419 Hz | 0.000204 Hz | 0.001583 Hz |
| 50.294500 MHz | 192.988 Hz | Not representable | 0.000257 Hz | Not representable |
| 70.092500 MHz | 374.829 Hz | Not representable | 0.002212 Hz | Not representable |

All 17 nominal 200 MHz plans passed. A further 20,451 cases per parent used
those centers, offsets of -100, 0, and +100 Hz, and 401 total-correction samples
from -246.245 through +153.755 ppm in 1 ppm increments. The 200 MHz parent
passed every case; its largest numerical average error was below 0.002806 Hz,
within the planner's 0.01 Hz limit. Neither 50 MHz nor 12.288 MHz rescued a
failed 200 MHz plan, and both lost upper-frequency coverage. Equal fractional
corrections were numerical sensitivity inputs only; the measured RP1 intrinsic
correction is not transferable to another parent.

A separate optimistic screen considered 710 distinct rates obtained by
integer division (divisors 1 through 255) of the nominal 50, 61.44, and
153.6 MHz sources, retaining rates at or below 50 MHz. This screen did not
establish live availability or ownership. Some rates improved individual
nearest-word errors: 30.72 MHz reduced the worst nearest-word error at the
10 m preset from 29.27 Hz to 6.64 Hz. However, its dither-average error was
worse, its adjacent-word excursion was larger, and it resolved no existing
acceptance failure. Every valid candidate plan had wider adjacent-word steps
than the 200 MHz plan. These isolated coincidences do not justify replacing
the current parent or initiating a hardware comparison.

Reopen only for a specific measured spectral problem, a reproducible planning
rejection, or a newly supported higher-rate parent that provides a concrete
predicted benefit. The sampled comparisons are not an exhaustive proof for
every custom frequency, spacing, or correction. Phase noise, spurs, absolute
accuracy, live ownership, and restoration of alternative parents remain
unmeasured. No parent selector, fallback, PLL tuning, provider contract change,
or alternative-parent hardware campaign is authorized by this decision.

Any separately approved reopened evaluation must first resolve the exact
provider IDs, aliases, available ancestry and nominal rates on the proposed
kernel/firmware, excluding cycles and feedback. Then evaluate
ownership/conflicts with other consumers, achievable
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
The optional RP1 direct PPS control is documented separately. No alternative
clock-parent selector or automatic custom-value migration is provided.

The independent [operator manual](https://wsprry-pi.readthedocs.io/en/stable/)
covers frequency calibration in `Advanced_Operations/timing_calibration.md`
and RP1 operation in `Advanced_Operations/rp1_gpclk.md`.
Its calibration guidance distinguishes intrinsic/additional accounting, bounded
prior-qualified reuse, unknown age and candidate/committed values. The component
and parent contracts above retain implementation detail and qualification limits.
