# BCM direct PPS calibration

The optional Pi 1–4 GPIO provider measures the shared BCM oscillator against
an existing, operator-selected PPS GPIO input. Its additional correction replaces the host-rate
estimate at the existing request preparation boundary. RP1 retains its separate
PIO provider; Si5351, outbound WTP, and simulation retain their existing policy.

## Clock identity and measurement

The [BCM clock driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/clk/bcm/clk-bcm2835.c)
identifies `timer` as an oscillator-derived system-clocksource clock, with a
6.12 divider. `CM_FRAC` enables its fractional mode. PLLD and the timer share
the oscillator; Pi 4 can also select that oscillator directly for RF. This is
a measurement of their common reference, not a separate RF loopback counter.
The [BCM peripheral manual](https://datasheets.raspberrypi.com/bcm2835/bcm2835-peripherals.pdf)
describes the GPIO level registers and the 64-bit system timer.

The adapter requires an exact supported SoC compatibility token, Raspberry Pi
identity, and matching 32- or 64-bit device-tree peripheral ranges. A malformed
or missing range never selects a fallback physical address. Read-only mappings
use the actual OS page size and 64-bit file offsets, including 32-bit ARM
builds. Only timer, GPIO, and clock-manager register pages are mapped, with
`O_RDONLY` and `PROT_READ`; initialization failure unmaps all completed mappings.

The selected GPIO (default 17, configurable from 0 through 27) must already
be an input owned by the existing PPS configuration. Exactly one Linux
`ppsN` source must identify `pps@<GPIO in hexadecimal>.-1`; serial PPS sources
and ambiguous matches are rejected. wspr4 uses GPIO18 and `pps@12.-1`. The adapter checks PLLD lock, oscillator and
PLLD nominal rates/ancestry, timer parent, divider, enable/busy/fraction mode,
and GPIO input mux. It never changes GPIO mux/direction/pulls, clocks, IRQs,
DMA, Linux PPS parameters, Chrony, or services. No reusable component is modified.

Each sample brackets a GPIO level read with coherent high/low/high counter
reads. Architecture-specific full-system barriers order reads across peripheral
blocks; the ARMv6 CP15 DMB operation is accessible in user mode under the
[ARM architecture's barrier exception](https://documentation-service.arm.com/static/5f1067bc0daa596235e7eee5)
(section 3.1). Initial high and held-high levels cannot manufacture rising
edges. Backward counters and endpoint brackets wider than 20 microseconds are
rejected. Intermediate brackets can be at most 500 microseconds: these edges
only establish the consecutive period count and do not enter the frequency
equation. Every adjacent PPS interval must remain within 999,000–1,001,000
counter ticks. The worker sleeps in interruptible ten-millisecond increments
for about 970 milliseconds between predicted edges, then observes the next
low-to-high transition. A missed sampling window retires acquisition instead
of treating delayed delivery as an accurate edge.

For exact timer-divider word `D`, nominal oscillator `X`, intrinsic correction
`I`, residual `R`, and midpoint counter difference `C` over 120 PPS periods:

```text
nominal_timer_hz = X * 4096 / D
full_error E    = (C / (nominal_timer_hz * 120) - 1) * 1000000 ppm
provider term   = E - I
selected term   = (E - I) + R
RF model        = nominal_RF_parent * (1 + (I + selected_term) / 1000000)
```

Subtract counters before floating-point conversion to retain precision at
long uptime. A rounded 1 MHz assumption is insufficient: the 19.2 MHz case
with divider word 78,643 has a nominal timer rate of approximately
1,000,002.543 Hz. Nonzero fractional bits require fractional mode. Nominal PLLD reporting may differ from the RF model by at most 0.2 ppm;
its actual deviation plus one hertz of reporting quantization enters the
uncertainty budget. A changed report restarts acquisition.

The conditional endpoint budget includes half each measured bracket, two timer
ticks per endpoint for quantization/input synchronization allowance, and one
microsecond of externally qualified reference error per endpoint. Add the
RF-parent nominal-deviation/reporting allowance. Exact-nominal parents with
20-microsecond endpoint brackets remain below 0.220 ppm; every accepted sample
must remain within 0.250 ppm after all allowances. The reference-edge budget and common-clock relationship
are prerequisites; sample stability does not establish absolute RF accuracy.

## Qualification and integration

Three distinct 120-period observations with spread at most 0.1 ppm qualify the
latest result. Counter intervals and completion timestamps must advance;
replayed observations cannot qualify. A clock identity change, source loss,
invalid interval, uncertainty failure, or capture error clears the result and
history. Expiry is 150 monotonic seconds from endpoint observation, dated before
potentially delayed clock-tree verification. This allows the next long capture
without retaining an expired estimate. Backward time never grants freshness.

Existing local-reference health checks remain independent: Chrony must report
one synchronized, selected local source, normal leap status, no combined
sources, and report age at most 300 seconds. A Chrony source identity change
stops both direct workers. Backend changes disable the inactive worker. During
acquisition, instability, loss, or expiry, manual fallback applies without the
residual. Explicit CLI host/manual overrides and independent saved preferences
remain unchanged. Committed transmission requests retain their original
correction, divider programs, and provenance.

The existing `GPIO.Use Direct PPS Calibration` boolean covers both BCM GPIO
and RP1. Defaults, copying, and serialization retain the same key and schema.
The active backend enables reference polling without rewriting the saved host
preference. The `GPIO.PPS Input Pin` integer is copied, validated, persisted,
and serialized throughout configuration. Changing it releases capture and
clears qualification before acquiring the new input. Selected-PPS-pin conflicts are refused for control pins, band selectors, and
per-frequency selector assignments, RF output, and RP1 route preflight/switch. Setup shows BCM or RP1 source status,
backend-specific settling time, and local repair feedback, preserving drafts.
No Pi/Pico protocol schema, capabilities descriptor, or wire format changes.

## Software evidence and adversarial review

The focused `make bcm-pps-calibration-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`
uses the production board/range parser, clock gates, edge tracker, conversion,
worker, selection, and cleanup paths with deterministic worker-boundary
handshakes. There is no short wall-clock deadline asserting worker progress.
Tests cover supported SoCs, wrong/truncated ranges, exact compatibility tokens,
wrong ancestry/rates, PLL lock and mux, fractional mode, long uptime,
uncertainty, signs/intrinsic, qualification, instability, expiry, reference
loss, identity replacement, duplicate observations, reacquisition, and cleanup.

The assessment closed the following issues before the final review:

| Finding | Resolution |
| --- | --- |
| Rounding the timer to 1 MHz introduces a systematic ppm error. | Calculate from the actual divider and require fractional mode for its nonzero fraction. |
| Equal-rate independent oscillator roots do not prove a common reference. | Require exactly one oscillator root along with the timer and PLLD ancestry; reject the split-root fixture for every processor profile. |
| GPIO/timer reads can be reordered across peripherals. | Full-system ARM barriers bracket ordered device reads; counter rollover uses coherent high/low/high sampling. |
| Tight endpoint limits applied to every pulse would reject avoidable intermediate scheduling delay. | Keep 20-microsecond endpoints; allow bounded 500-microsecond intermediate brackets with consecutive-period checks. |
| Exact-integer PLLD nominal matching can reject ordinary PLL programming/report rounding. | Budget the observed nominal deviation and report precision; reject total uncertainty above 0.250 ppm. |
| A narrow bracket alone omits input synchronization allowance. | Include two timer ticks per endpoint, external edge error, and RF-parent rounding in the budget. |
| PPS GPIO was fixed, and conflict/status gates were RP1-only. | Persist the selected GPIO for both families, discover its unique existing PPS source, reset history on changes, and validate JSON/CLI/backend activation and RP1 route operations. |
| Replayed data could count as independent observations. | Require advancing counter intervals and completion timestamps; test duplicate rejection. |
| A changed clock could retain old qualification. | Bind measurements to processor/divider identity and retire acquisition on replacement. |
| Delayed verification could refresh an old endpoint. | Timestamp the endpoint before subsequent identity/clock checks. |
| 32-bit offsets or a fixed 4 KiB mapping omit supported OS layouts. | Use large-file offsets and actual page size with aligned mappings. |
| A failed assertion could leave the fake capture blocked during teardown. | RAII closes its input queue before joining; injected assertion failures verify clean exit. |

Adversarial mutations that retained stale error state or accepted duplicate
observations were rejected at the intended phase. An injected first-assertion
failure exited with clean worker teardown. Forty-eight unmodified focused runs
passed with twelve concurrent processes. The focused provider also compiles and
passes in Linux GCC 13 and ARMv6 Linux emulation; emulation is software evidence.
Portable semantics, full Linux semantics, UI unit/browser tests, and strict
Sphinx rendering are separate validation boundaries. UI screenshots cover both
providers, three source states, desktop/mobile, and light/dark themes. Impeccable
reported four existing application color advisories in unchanged release-check
code. The operator-doc source scan reported seven existing font/color
advisories in unchanged CSS. Both source scans reported zero anti-patterns;
source scans omit parsed-page element checks. Desktop/mobile rendered review
separately verified the affected controls, wiring, and scrolling results table.
Rendered-file detection reported ten warnings and seventeen advisories: inherited
Read the Docs navigation/icon fonts, the pinned 1.12 display-heading leading,
existing palette/type roles, and a reported dark navigation label. HTTP-browser
inspection confirmed actual mobile navigation text/icons are `#ecf1f6` on
`#405e7a`, not the detector's `#404040`; the accessible menu label is clipped.
The display leading matches the existing documentation design, and the
remaining findings concern unchanged shell/Pygments styling. No global theme
redesign or detector suppression was introduced.

The repeated assessment rechecked the corrected code and its integration with
configuration, source health, immutable jobs, GPIO conflicts, and the UI. No
remaining actionable software findings were identified. The original three adversarial
mutants were rejected. After configurable-pin changes, another 48 focused runs
covering BCM, RP1, and pin replacement passed with twelve concurrent processes.
The pin fixture verifies nonzero PPS device indices, ambiguous/replaced
sources, exact factory routing, release of the old capture, and history reset.
An ambiguity-acceptance mutation of the new resolver was rejected by its
intended assertion; a split-oscillator acceptance mutant was also rejected
by the new common-reference assertion. RP1 route preflight, switch, and apply-and-reboot reject the selected PPS GPIO
before invoking any provider operation. Cleanup/recovery operations remain
available.

Final validation commands/results:

| Environment | Command/check | Result |
| --- | --- | --- |
| macOS, simulated-only | `make -C src semantics-test-portable SUDO=` | Pass; explicit portable subset. |
| Linux GCC 13, default backend profile | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=` in isolated candidate clone and again in a fresh artifact-free source tree | Pass, including runtime semantics and cleanup lifecycle. |
| ARMv6 Linux emulation | GCC C++20, `-Wall -Wextra -Werror`, focused BCM production sources and test, hardware disabled | Pass; GCC emits historical initializer-list ABI notes. |
| Parent Make dispatch | `make -C src semantics-make-regression-test SUDO=` | Pass. |
| UI component | `npm run test:unit` | Pass. |
| Calibration browser fixture | `node tests/rp1_pps_browser_test.js` | Pass; BCM/RP1, acquisition/qualified/fallback, both sizes/themes. |
| Operator docs | `python -m sphinx -b html -W -E docs docs/_build/html` using docs venv | Pass; warning-free; desktop/mobile inspected. |
| Repository | `git diff --check`, staged whitespace and complete diff review | Pass. |

Initial harness attempts encountered sandbox socket restrictions, missing PHP
fixture dependencies, missing archive Git metadata, copied Mac build artifacts,
and a Linux image without Node. These were resolved in isolated test runtimes;
the successful profiles use the intended candidate sources and do not weaken
assertions or change application policy. No Pi/Pico contract or capability
advertisement changed; separate network/USB paired suites were not rerun for
this local provider. No CI result is substituted for these local results.

An initial native incremental build retained objects from an earlier snapshot
layout because archived source timestamps predated those objects. The first
physical request crashed; independent register readback confirmed RF and DMA
off, and the installed service and receiver/reference were restored. The
request is conservatively charged eight seconds against the RF budget. Forced
recompilation of changed sources and header dependents produced the reviewed
native candidate. A private-network, hardware-disabled native request and clean
shutdown passed without the memory fault. Its explicit simulated backend
rejects the legacy Test Tone path; it is not simulated RF execution evidence.
The full Linux suite also passed from a fresh tree without any build artifacts.
The final common-reference guard was then validated by another fresh full Linux
suite, portable semantics, ARMv6 focused tests, and its negative mutation.

The bounded baseline group completed before switching to the final stricter
candidate. Its first cleanup comparison included read-only clock BUSYD and
DMA DREQ status and omitted restoration of the disabled GPCLK mode. The
private harness now restores recorded writable clock/divider/pad settings,
compares writable fields, and independently verifies RF/PWM/DMA off and GPIO18
input. A separate read-only check confirmed the installed INI hash and active
service, selected PPS, restored/locked GPSDO, and released receiver before the
final candidate started. The interrupted and failed preflight evidence remains
separate from successful conducted captures; no failed check is counted as a
passed campaign.

## Conducted acceptance cases

The authorized wspr4 comparison uses BCM2711, existing GPIO18 PPS, and GPIO4
RF at minimum drive. The QLG3 supplies PPS/serial UTC. The separate locked
LBE-1421 GPSDO and DUT each pass through 60 dB attenuation to a combiner and
RSP1B on wspr5; no antenna is attached. Test Tones are 7,040,100, 14,097,100,
and 28,126,100 Hz, at most eight seconds each and 180 seconds cumulative,
including conservatively charged failed requests. The GPSDO reference is
500 Hz above each DUT tone and restored without flash writes.

Two repeats per frequency compare a frozen contemporaneous Chrony estimate
with qualified direct PPS and zero residual. A further +0.5 ppm residual
checks approximately −0.5 ppm RF movement. Before measurement, the declared
acceptance target is untrimmed absolute residual at most 0.1 ppm, conditional
receiver/reference allowance at most 0.05 ppm, and residual movement agreeing
with its prediction within the combined comparison allowances. The prediction
uses each job's committed correction, accounting for an automatic PPS update
between groups when isolating the residual term. The shared
BCM oscillator can already be well corrected by Chrony; improvement over that
baseline is measured, not assumed or required to be artificially large.

The simultaneous receiver comparison and conditional allowance follow the
[RP1 analysis method](direct-pps-validation.md#independent-rf-campaign),
with six-second spectra and three independent two-second subwindows.
Every job requires qualified PPS and reference lock, a finite receiver
capture, bounded tone ownership, and independent output-off register readback.
The private candidate leaves the installed binary and INI unchanged, and
restores service, GPSDO, and receiver ownership after completion or error.

## Conducted results

The final candidate qualified after five 120-period observations (about ten
minutes); the first three did not satisfy the unchanged 0.1 ppm spread gate.
It reported 10.337500 ppm, then refreshed to 10.379167 ppm during the campaign.
Chrony retained a selected PPS source and normal leap status. Six zero-residual
PPS tones and three +0.5 ppm residual tones completed on the final binary.

Each cell lists repeat 1, then repeat 2, in **Hz (PPM)**. Conditional allowances
are the largest per row, rounded outward.

| BCM output | RF (MHz) | Host-estimate error: Hz (PPM) | Direct PPS error: Hz (PPM) | Measurement allowance |
| --- | ---: | ---: | ---: | ---: |
| GPIO4 | 7.0401 | +0.308 (+0.0438)<br>+0.272 (+0.0387) | +0.645 (+0.0917)<br>+0.651 (+0.0925) | ±0.221 Hz<br>(±0.0314 PPM) |
| GPIO4 | 14.0971 | +0.644 (+0.0457)<br>+0.613 (+0.0435) | +0.829 (+0.0588)<br>+0.834 (+0.0591) | ±0.291 Hz<br>(±0.0207 PPM) |
| GPIO4 | 28.1261 | +0.806 (+0.0287)<br>+1.763 (+0.0627) | +1.914 (+0.0681)<br>+2.246 (+0.0799) | ±0.432 Hz<br>(±0.0154 PPM) |

The existing host estimate was already accurate on this shared-oscillator
platform. These observations do not establish improvement over that baseline.
All six direct PPS central measurements met the declared measured residual
target of 0.1 ppm, with conditional allowances below 0.05 ppm. They are not
a certificate that true RF error is below 0.1 ppm after uncertainty is included.

### Residual control

The PPS update added 0.041667 ppm between the untrimmed and trim groups.
Together with the +0.5 ppm residual, the committed correction increased by
0.541667 ppm, predicting approximately −0.541667 ppm RF movement at unchanged
parent rate. Actual parent drift and measurement uncertainty remain separate.

| RF (MHz) | Trim error: Hz (PPM) | Measured movement (PPM) | Expected total movement (PPM) | Combined allowance (PPM) |
| --- | ---: | ---: | ---: | ---: |
| 7.0401 | -3.505 (-0.4979) | -0.5900 | -0.5417 | ±0.0627 |
| 14.0971 | -6.480 (-0.4597) | -0.5187 | -0.5417 | ±0.0413 |
| 28.1261 | -12.600 (-0.4480) | -0.5220 | -0.5417 | ±0.0307 |

All three movements agree with the actual committed-correction change within
the combined allowances. Comparing the 7 MHz movement only with −0.5 ppm
would exceed its allowance; the receipt explicitly retains that result rather
than hiding the intervening provider update. Every active-job status snapshot
retained identical committed correction/provenance, even when the provider
refreshed for subsequent jobs. The +0.5 ppm setting is a sign/scale check, not
an optimized residual calibration.

The [machine-readable receipt](bcm-direct-pps-rf-results.json) records all
per-capture values, subwindows, uncertainty assumptions, capture hashes,
source hashes, candidate identities, and committed provenance. Baseline
binary `1c980a5e…` predates only the final common-root guard, with direct PPS
disabled in every baseline job. All calibrated and trim jobs use final binary
`19e08b22…`; the receipt retains their full SHA-256 values. Raw IQ and execution
records remain in the private `bcm-pps-bench-20261104-v2` and `-v4` directories
on wspr5, with DUT receipts under `/tmp/wsprrypi-bcm-rf-20261104-v4` on wspr4.

Fifteen successful eight-second jobs consumed 120 commanded seconds. The
initial failed request is conservatively charged another eight, for 128 of
the authorized 180 seconds. Every job ended with GPIO4 and GPIO18 in input
mode, RF clock and DMA inactive, and PWM output disabled. Final writable
clock/divider/pad settings matched their recorded initial values; read-only
BUSYD/DREQ status is not treated as mutable configuration. The installed
configuration SHA-256 remained `64c582dd8548b1b846dfde0d92f83ed139c7753ab5d39caf2f866485c3dffc17`.
The installed WTP service returned active, PPS remained selected, the GPSDO
returned to its exact initial state without flash writes, and the SDR was
released without receiver errors. No installed binary was replaced.

## Physical qualification boundary

This is BCM2711/Pi 4 conducted carrier-frequency and correction evidence on
GPIO4 with GPIO18 PPS. It does not qualify other Pi boards, RF output routes,
power, spectrum, symbol timing, installation, or a release. ARMv6 emulation
is software evidence. Existing RP1 measurements remain RP1 measurements.

The repeated adversarial assessment checked the final guard, source identity,
qualification and fallback, pin/route conflicts, immutable jobs, actual
correction accounting, capture quality, and restoration receipts. All
actionable findings were closed; no unresolved software or scoped RF
acceptance finding remains. The feature is delivered on its feature branch,
without merging or installing it.

## Documentation impact

The matching operator feature branch updates transmission timing/calibration,
GPIO backend INI and CLI references, the default INI, and Setup calibration
guidance. The new Validation platform page is a Pi 4/QLG3 operator example
with power/signal wiring and matching settings; it excludes the physical test
harness and validation procedure. The calibration guide includes the BCM
Hz/PPM results separately from the unchanged RP1 table. Existing UI screenshots
were considered; the affected Setup reference uses text rather than an
outdated screenshot. Pico settings/control documentation is unchanged because
its contracts and capabilities are unchanged. No operator-documentation
follow-up is left outside the matching feature branch.
