# Direct PPS frequency calibration execution prompt

## Objective and authority

Address release punchlist item 2 in an isolated `codex/direct-pps-calibration`
worktree. Implement measurable RF frequency correction against GPS PPS, with
an independent conducted comparison against the GPSDO using wspr5's SDRplay
RSP1B. Preserve user work, settings, component boundaries, UTC authority, and
the current supported protocol contract. Commit and push the completed feature
branch after repeated adversarial review; do not merge or publish a release.

The maintainer has reopened the previously deferred scope. That reopening does
not establish feasibility or measured accuracy. Do not relabel the current
Chrony host estimate as direct RF calibration.

## Orientation and verified starting evidence

- Pi `devel` and remote `devel`: `21d54a3bd201b96522ed318be763e75d89ea7256`.
  Original checkout was clean. Preserve it throughout this task.
- Review `release_tools/Calibration Scope Decisions.md`, the punchlist,
  `src/system_clock_frequency_estimate.{hpp,cpp}`, PPM-Manager,
  configuration parsing/serialization, scheduler and inbound WTP capture
  boundaries, and legacy/RP1 clock models before changing behavior.
- Recent relevant changes include `c605142e` (Chrony age/provenance),
  `985bad10` (WSPR placement lifetime), `1e047a44` (RP1 event profiles), and
  `535717e8` (closed alternative-parent review). Preserve their contracts.
- wspr5 is Raspberry Pi 5, kernel `6.18.34+rpt-rpi-2712`, with `/dev/pps0`
  (`pps@11.-1`, GPIO17), `/dev/pio0`, and `/dev/rp1-gpclk`.
  Chrony currently selects PPS. Its host frequency estimate is not an RP1
  measurement. There is also a separate PTP PPS device; never select it by
  enumeration order or substitute it silently.
- Passive clock-tree inspection shows nominal `pll_sys` 200 MHz parenting
  `clk_sys` 200 MHz, and the RP1 provider owns its selected GPCLK route.
  Verify the PIO clock's actual ancestry before deriving an RF correction.
- USB inventory identifies SDRplay RSP1B and a Leo Bodnar LBE-1421 GPSDO.
  Prior bench scripts identify receiver `2404058C60` and GPSDO `0673ED0FA107`;
  revalidate identity, lock, current frequency, output routing, and attenuation.
- Pico research: `src/time/gps_calibration.{hpp,cpp}` measures raw timer
  intervals against qualified PPS and uses a separate coherent loopback count
  for Si5351. It rejects discontinuities, binds capture generations, propagates
  uncertainty, expires measurements, and freezes correction per job. Adapt
  these principles; do not assume Pico and RP1 share clock architecture.

## 1. Establish the measurement path

Investigate an input-only RP1 PIO program that measures RP1 cycles between
consecutive PPS rising edges. Count both pulse-high and pulse-low time, derive
the complete instruction-cycle equation, account for synchronization and
quantization, and ensure FIFO stalls cannot appear as oscillator error. Use
exclusive per-file state-machine/program ownership and bounded nonblocking
reads. Never clear global instruction memory or commandeer another client.

Read Raspberry Pi's PIO driver/API and clock sources. Determine whether PIO
can observe the already configured PPS input without changing pin mux,
direction, pulls, interrupts, or the Chrony GPIO lease. Reject the approach if
it would disrupt Chrony or cannot establish a known ratio to the RF parent.
Do not use host IRQ arrival times, nominal rate queries, or an RP1 counter
referenced to its own oscillator as independent RF observations.

If no safe current-wiring capture path can be established, report the precise
missing path and obtain direction before substituting an SDR-dependent
calibration workflow, additional hardware, kernel/provider changes, or wiring.
Do not implement a mathematically plausible estimator without a real producer.

## 2. Define acquisition and correction policy

Specify reference identity and quality, pulse/sequence integrity, observation
window, accepted spread and uncertainty, monotonic freshness, bounded holdover,
loss/reacquisition behavior, and capture/parent identity changes. A PPS pulse
alone does not prove GPS lock or UTC association. Do not modify Chrony's UTC
discipline. Keep raw observations separate from correction and UTC.

For RP1, measure full parent error `P` (positive means fast). Existing
intrinsic `I=-46.245 ppm` must apply once. A direct result supplies additional
correction `P-I`, plus any explicitly selected residual; it must not stack a
second full Chrony estimate. Preserve valid manual fallback and existing saved
values. Define the selected authority explicitly and make unavailable,
converging, current, held, and expired states distinguishable.

Keep all values finite and bounded. Preserve existing +/-200 ppm configured
and additional bounds and separately bounded intrinsic composition. Keep
Si5351 calibration, remote WTP device ownership, RF carrier correction, and
symbol-duration correction independent. Do not assume an RP1 observation
qualifies a legacy PLLD or oscillator parent.

Freeze every accepted correction and its provenance into the existing job
commit boundary. Refresh, source loss, reload, cancellation, and fallback may
change future candidates but cannot alter an accepted event/divider program.
Cover local scheduled modes, Test Tone, and the receiving Pi WTP path.

## 3. Implement the complete operator workflow

Use the smallest coherent producer/estimator/selection integration. Review all
configuration defaults, migration, INI/JSON validation, serialization,
persistence, browser population/saves, scheduler, status, and runtime consumers.
No automatic fallback into simulation or an unrequested calibration mode.
Avoid persisting transient learned estimates as operator manual values.

Any UI change requires Impeccable before edits: load project context, preserve
the existing design, read its craft guidance, inspect desktop/mobile renders,
and exercise save, validation, loss, and recovery workflows. Explain source,
sample age, quality, and selected correction in operator terms. Preserve
drafts on validation failure and provide feedback near the action.

Review corresponding operator documentation in the sibling docs repository.
Read its instructions; request explicit cross-repository authority if edits
are needed and not already authorized. Keep documentation commits separate.
Update engineering calibration authority and punchlist accurately; do not
mark item 2 complete while acquisition or independent qualification is missing.

## 4. Deterministic validation

Inspect each Makefile recipe before running it. Use portable explicit simulated
profiles on macOS and the canonical Debian hardware-free profile where available.
Add behavioral tests for positive/negative/zero error; correct intrinsic
reconciliation; residual/manual composition; uncertainty and range boundaries;
duplicate/missing/stuck PPS; overflow; FIFO stalls; source/capture/clock changes;
loss, bounded holdover, expiry, and reacquisition; cancellation and cleanup;
and immutable committed jobs under refresh and reload.

Exercise the production acquisition adapter through a typed fake device seam,
including every error and cleanup boundary. Synthetic counts alone do not
prove actual PIO instruction semantics or clock ancestry. Verify the actual
program with a cycle model and live input-only acquisition if permitted.

Run relevant standalone component and parent integration checks, GPIO/Chrony
correction tests, calibration round-trip, placement and Test Tone lifecycle,
RP1 planner/backend/event tests, and UI/source checks when affected. If shared
Pi/Pico protocol-facing contracts change, coordinate actual companion sources
and pass both actual network and USB pair suites using the checked-in Debian
workflow recipes. Never relax strict parsing or substitute stale sources.

## 5. Independent physical acceptance

Record exact source/binary/provider identities and uncommitted changes tested.
Revalidate wspr5, PPS receiver, GPSDO lock and frequency, RSP1B identity,
conducted combiner paths, and 60 dB attenuation per path. Before RF operation,
confirm exact frequencies, modes, durations, output routes, attached hardware,
and stopping procedure as required by the repository instructions. Preserve
services/settings and use finite jobs with watchdog cleanup and output-off
verification. Do not infer a license to rewire, install, or change services.

Compare baseline, manual adjustment, and PPS correction in repeated sessions
and both authorized GPCLK routes. Capture GPSDO and DUT simultaneously where
possible, correct receiver scale only once, and include quantization,
reference accuracy, capture jitter, line estimation, drift, and repeatability
in the measurement budget. Proposed target from the scope document is residual
<=0.1 ppm with expanded uncertainty <=0.05 ppm; resolve target selection before
claiming acceptance. Validate at predeclared low/high representative frequencies
and observation/temperature conditions. Do not tune to the validation data and
then report the same data as independent acceptance.

Validate reference loss/expiry and subsequent reacquisition without UTC or
committed-job changes. Report physical frequency evidence separately from
power, spurs, GPIO timing, installation, and service qualification.

## 6. Adversarial closure and delivery

Review the full final change against the actual producer, math, reference and
parent identities, config lifecycle, source-selection policy, independent
measurement, concurrency, ownership, cancellation, error recovery, resource
cleanup, committed-job immutability, and documentation. Treat stale evidence,
untested paths, circular clock comparisons, and silent skips as findings.
Fix each actionable finding, rerun affected validation, then perform a fresh
adversarial assessment. Repeat until no actionable finding remains.

Run final whitespace checks and inspect the complete staged diff. Commit and
push only the feature branch; verify the remote revision. Report behavior,
important files/components, exact checks/results, rendered UI evidence,
physical evidence and limits, outstanding work, Documentation Impact,
original and feature worktree states, and exact commit/remote. Do not claim
completion or release readiness beyond the available evidence.
