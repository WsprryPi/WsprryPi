# Calibration Scope Decisions for Wsprry Pi 4.0

Decision: October 15, 2026. **Native/direct PPS RF calibration is deferred
beyond 4.0. RP1's nominal 200 MHz `pll_sys` parent is already implemented;
evaluation of other parents is deferred.** This completes the calibration
scope review, not PPS implementation or backend/RF qualification. No evidence
reviewed establishes a 4.0 requirement for a new calibration mechanism.

## Evidence boundary

The application review uses `devel`
`6a39398bb23d69ba0628e1b82171717040115ec8` (equal to refreshed `origin/devel`).
Provider inspection is read-only at RP1-GPCLK-DKMS `devel`
`924c7e546ab82a40497339c4baeb813f95f3408e`; operator documentation is read-only
at WsprryPi-Docs `devel` `01d9bef6f8d7a05ac2b3223f02270483f33caac4`.
Relative application source links below refer to the reviewed application
revision. Unrelated working-tree WTP/UI edits are excluded from this decision
and its commit. Sibling revisions are local observations, not remote refreshes.

Relevant history, reviewed against current source:

- [`b2b6a40`](https://github.com/WsprryPi/WsprryPi/commit/b2b6a40032597d688eae948524b5737b81e3fb9b)
  centralized intrinsic offsets; [`93c8002`](https://github.com/WsprryPi/WsprryPi/commit/93c8002ebef91d6d73ec456a4ec825616d3cfe9f)
  reconciled additional-only requests, legacy models and correction provenance.
- [`81309e9`](https://github.com/WsprryPi/WsprryPi/commit/81309e9f14b724a0ae2088c2ec9515304caef60d)
  adopted 200 MHz RP1 planning. Later canonical-UAPI and native WTP work
  (`f3314e355468c3d7394e653bccc2e04220701a92`,
  `c01cd5c8d08fecf5478ec0d9024c215258ad7525`) retains that parent and composition.
- `15e677e1c3b4435edff802b50f58a36cd747b603` hardened unusably small manual
  values. Accepted #448 commits `a6c867fafc88bd1ed092552d159662c23be4f000`
  and `91d7cdc614c7f0883ce62922668b1e4cbd4f1294` concern negotiated Pico
  configuration updates/type validation, not a new calibration authority.

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
estimate, intrinsic term or composed RF total. No migration/default/save path
changes in this task.

[Qualification constants](../src/system_clock_frequency_estimate.hpp) require
three stable evaluations with spread <=0.1 ppm, at least three retained provider
samples, skew <=1 ppm, absolute Chrony residual frequency <=0.5 ppm, and current
age <=300 seconds. A changed source signature resets the evaluation history.
These are evaluations, not proof of three distinct new PPS observations or a
fixed observation duration. Reuse requires a previously qualified snapshot,
with its reported age plus monotonic elapsed time since that qualification
evaluation <=900 seconds; an old first observation cannot qualify itself.
This is the implemented age arithmetic, not independently verified reference
freshness: cached provider reports do not accrue age at consumption (F1 below).
A current unavailable/converging sample can
therefore still select the bounded stale correction; qualification and selected
correction mode are different fields.

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

No capture-path demonstration or new measurement was performed here. A demand
to implement this before 4.0 would be a scope change, not an unfinished part of
this decision task.

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

| Historical item | Disposition | Identity constraint for any future evaluation |
| --- | --- | --- |
| `pll_sys` | **Implemented**, nominal 200 MHz; remove the stale future-feature classification. | Keep current provider identity, correction composition, ownership and set/restore contract. |
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

## Source findings requiring separate runtime scope

These remain open follow-ups; this documentation task does not repair them or
infer a failure in a normally formed Chrony report.

| Finding / severity | Source evidence and impact | Smallest next step and required verification |
| --- | --- | --- |
| F1 / P2: reported freshness can be overstated | [Parser lines 291-297](https://github.com/WsprryPi/WsprryPi/blob/6a39398bb23d69ba0628e1b82171717040115ec8/src/PPM-Manager/src/ppm_manager.cpp#L291-L297) leave age zero for missing/malformed selected-source age. Also, reports are cached between the 120-second provider updates; [consumption](../src/scheduling.cpp) reconstructs snapshot time as now minus reported age, and repeated successful evaluations reset the qualifier's last-qualified time without aging that cached report. Thus missing/invalid metadata can pass age gates and cached-report reuse can overstate reference freshness. | Separately approve preserving unknown/invalid age and carrying acquisition time so cached observations accrue age. Add parser-to-qualifier fixtures for absent/malformed/valid age, repeated cached consumption, elapsed expiry and bounded prior-qualified fallback; run on a safe Linux surface. |
| F2 / P2: provenance can over-credit a local reference | [Parser lines 322-335](https://github.com/WsprryPi/WsprryPi/blob/6a39398bb23d69ba0628e1b82171717040115ec8/src/PPM-Manager/src/ppm_manager.cpp#L322-L335) search the entire `sources_csv` for PPS/GPS/GNSS/PHC substrings, even unselected rows; a selected network source plus an unselected PPS row can be labeled local. This affects attribution, not proof of physical lock. | Separately approve provenance derived from selected/combined source records with explicit unknowns. Verify selected-network/unselected-PPS, mixed and unknown fixtures; never infer lock/routing from text. |

Neither finding establishes a requirement for native PPS. Their correction-path
impact needs separate triage; completion of this scope decision does not close
them or declare them repaired.

## Documentation Impact

- **Updated:** this maintainer decision, the two calibration entries in
  [Release Punchlist](Release%20Punchlist.md), and the discovery link in
  [release tools README](README.md). No application/component source changes.
- **Considered unchanged:** WsprryPi-Docs
  `docs/Advanced_Operations/ini_configuration/runtime.md` adds no new setting;
  existing Chrony UTC, Si5351 independence and WTP no-host-correction guidance
  remain applicable. `Experimental/rp1_gpio` is historical, not provider authority.
- **Still required under separate cross-repository scope:** in
  `docs/Advanced_Operations/timing_calibration.md`, distinguish additional from
  intrinsic RF compensation, document residual/manual accounting and bounded
  stale fallback even when current qualification is unavailable/converging, and
  state that PPS through Chrony is host timing/estimate support, not direct RF
  measurement. In `docs/Advanced_Operations/rp1_gpclk.md`, document the current
  nominal 200 MHz parent and correction/default versus qualification boundary.
  In `docs/User_Interface/Setup/Transmitter/index.md`, explain existing
  candidate versus committed additional-only correction/provenance and link the
  calibration guidance. Keep intrinsic/total internal; add no operator parent
  selector or PPS control. Review any resulting rendered UI documentation with
  Impeccable; replace screenshots only if materially inaccurate.

## Validation and decision-review boundary

Documentation-only validation runs on the local macOS host: source/history and
link review, complete scoped diff review, and Git whitespace checks. No C++
compilation, standalone component tests, integration suites, live Chrony query,
sibling build/render or hardware operation is needed or claimed. The focused
Makefile recipes and PPM-Manager fixed parser fixtures were inspected, not run;
no available noncompeting Linux validation surface was assumed. Source findings
F1/F2 are source assessments, not reproduced test failures.

Executed documentation checks (macOS, no C++ semantics profile):

```sh
python3 /tmp/verify_calibration_scope.py
git diff --check
git diff --cached --check
```

The temporary checker validates local file/heading links, pinned repository
objects and the intended two-row punchlist patch. All passed after repairs;
the final staged diff must contain only the three named maintainer paths.
`git fetch origin devel` passed with `git rev-list --left-right --count
HEAD...origin/devel` reporting `0 0` before documentation work; no fast-forward
was needed. Upstream ancestry evidence was pinned by `git ls-remote
https://github.com/raspberrypi/linux.git refs/heads/rpi-6.12.y` and reading that
exact revision's source. An initial sandbox DNS failure required a read-only
network retry. A later Git refresh could not write restricted `.git/FETCH_HEAD`;
its authorized retry passed. No hardware or system operation followed.

### Adversarial assessment and repairs

| Assessment finding | Required correction and verification |
| --- | --- |
| R1 / P2: saved `pll_sys` wording classified implemented behavior as future work. | Corrected only the two authorized saved rows; checked provider overlay, setup/execution and userspace planner at the pinned revisions. |
| R2 / P2: first-draft stale-window wording could be read as a physical freshness guarantee. | Clarified reported-age/evaluation arithmetic and recorded the cached-report limitation in open F1; rechecked provider caching, scheduler consumption and qualifier reset paths. |
| R3 / P3: scheduler provenance wording could imply inbound WTP uses those same status fields. | Limited that wording to local scheduling and distinguished inbound prepare-time freezing; rechecked receiving runtime and tone-engine preparation. |
| R4 / P3: first draft lacked exact validation commands/results and line-specific parser evidence. | Added commands, honest skips and pinned parser links; reran link/scope and whitespace checks. |

Reassessment after repairs found no actionable in-scope finding. It challenged
UTC versus RF measurement, reference presence versus verified routing, XOSC
history versus current 200 MHz, aliases/ancestry versus independent oscillators,
nominal versus measured rate, product defaults versus board calibration,
correction signs/ranges/double counting, candidate versus committed values,
Si5351/WTP isolation, persistence/default behavior, optional scope, reopening
criteria, missing evidence and unrelated changes. F1/F2 remain explicitly open
runtime follow-ups. No software test or hardware success is inferred from this
assessment.

Runtime behavior, defaults, correction algorithms/signs, persistence, clock
parents, provider lifecycle and accepted #448 work are unchanged by this task.
The [RP1 publication dependency order](Release%20Punchlist.md#rp1-dependency-order),
macOS deferral and Standard Feld post-4.0 scope remain unchanged. Independent
route, mode, timing, installation/service, frequency, spectral and RF
qualification and existing release gates remain outstanding. Settling scope
does not establish release readiness.
