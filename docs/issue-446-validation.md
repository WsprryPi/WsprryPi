# Legacy GPIO exact test-tone frequency

## Behavior and scope

Follow-up status: the separately authorized September 13, 2026
[conducted RF comparison](development/issue-446-rf/README.md) passed, and the
operator-documentation follow-up was completed in `Wsprry_Pi_Docs`. The
hardware-free evidence below records the original implementation phase.

Issue #446 corrects the legacy GPIO TONE path in `WSPR-Transmitter`. The
committed frequency stays in the RF-carrier domain. The legacy plan adapter
uses zero spacing for TONE, so every table entry and the continuous symbol-zero
emitter target that frequency. Setup, start, and watchdog recovery reconstruct
the same plan without adding an offset to the request or status.

For example, a 14,097,100 Hz TONE request now targets 14,097,100 Hz. The affected
implementation at `654d51ab253fb5aaa26cf0ef3e8208cd2ffcfd6f` instead targeted
14,097,097.802734375 Hz because it used WSPR symbol zero. The 2.197265625 Hz
displacement is independent of the dial/audio offset and is not a clock PPM
constant. Preserve historical raw measurements and identify the exact build
when interpreting them. Do not apply the old adjustment to corrected builds.

GPIO divider planning and table construction are pure functions in
`src/WSPR-Transmitter/src/gpio_frequency_plan.{hpp,cpp}`. Production DMA setup
uses that result directly, and the emitter uses the plan's shared symbol
frequency calculation. Clock-model composition and parent selection are
unchanged; they now receive the actual single-frequency range for TONE.

The existing safe-divider constraint requires both dither words to have the
same integer part. An exact TONE whose pair crosses that boundary fails during
configuration, before GPIO routing in table setup, rather than silently
substituting another carrier. Existing configuration-failure cleanup still
applies. Preparation may already have allocated hardware resources; this is
not a new claim of hardware-free configuration. Normal modulated tables retain
their existing hardware-limit center adjustment, with their divider words
checked before routing. Reported carrier values remain synthesis targets, not
measured RF.

WSPR continues to use four tones at -2.197265625, -0.732421875, +0.732421875,
and +2.197265625 Hz relative to its center. QRSS, FSKCW, and DFCW keep their
existing compatibility reference compensation. Si5351, RP1, simulated, and WTP
TONE plans receive no extra frequency correction. No calibration constants,
CLI options, UI, scheduler rules, selectors, or persistence formats changed.

## Review and test evidence

The [execution prompt](issue-446-execution-prompt.md) records the initial review,
relevant recent history, bounded implementation scope, and acceptance criteria.
The working branch started clean at `654d51ab253fb5aaa26cf0ef3e8208cd2ffcfd6f`.

The focused regression reaches the production plan adapter, table planner,
divider words, and symbol-frequency function. It covers 13 representative
frequencies from 137.5 kHz to 50 MHz, three legacy processor profiles, three
additional PPM values, repeated reconstruction, the Pi4 oscillator path,
integer-divisor boundaries, and independent WSPR table-word comparisons.
It also checks the shared QRSS/FSKCW/DFCW and TONE event frequencies across all
five backend kinds. Rejected divider crossings are asserted as failures, not
skipped or counted as synthesized tones.

An isolated mutation check restored the old WSPR spacing for TONE. The test
failed at the intended assertion that symbol zero must equal requested RF;
the working source was not modified for that check.

All runtime tests in the original implementation phase were hardware-free. Pi
access used the `wspr4` SSH alias outside the sandbox and the isolated directory
`/tmp/wsprrypi-issue446.vepaDN`; its installed executable and normal checkout
were not changed during that phase.

## Adversarial assessment

First pass findings and resolutions:

1. A center-only compensation could leave hardware-limit adjustment and
   recovery in different frequency domains. The zero-spacing representation
   leaves both request and applied TONE frequency in the carrier domain.
2. Generic range validation alone admits divider pairs whose integer fields
   differ. The pure table planner rejects those exact TONE requests and checks
   all emitted pairs. Tests include calibrated boundary crossings and adjacent
   safe frequencies; they do not weaken tolerances to accept shifted output.
3. A regression linked to the existing startup test pulled Linux device
   headers into portable validation. The pure arithmetic source and standalone
   focused target now run on macOS as well as Linux, and the parent full and
   portable semantics suites both include that target.
4. Moving the arithmetic could alter non-TONE rounding or validate after GPIO
   routing. The upper-range expression retains the original arithmetic;
   independent WSPR word comparisons and shared CW frequency tests cover the
   extraction. Production setup validates the table before routing GPIO.
5. Testing only the common compiler would miss the separate legacy path.
   Tests use the actual legacy request adapter and table implementation, and
   the old-mapping mutation fails. Source review confirms setup, start,
   emission, and restart all use the same committed-request adaptation.

Second pass rechecked the complete component and parent diff, public header
dependencies, backend-specific source selection, unchanged modulated-table
arithmetic, exact-tone failure before routing, recovery reconstruction,
calibration ownership, caller conversion paths, and test sensitivity. All
first-pass findings are resolved. No further actionable source-scope findings
remain. At that implementation-stage assessment, conducted RF acceptance and
the separate operator-documentation update remained external gates; the linked
follow-up records their completion.

## Final validation results

Commands below are run from `src` unless another directory is stated.

| Environment | Command | Result |
| --- | --- | --- |
| macOS | `make legacy-tone-frequency-test SUDO=` | Passed, including final shared-mode assertions. |
| macOS, outside sandbox for loopback sockets | `make semantics-test-portable SUDO=` | Passed; explicitly the simulated-only portable subset. |
| Isolated Debian Trixie aarch64 container | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -C WSPR-Transmitter/src -j4 test SUDO=` | Passed all component contract tests. |
| Same container | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j4 semantics-test SUDO=` | Passed the full Linux semantics suite, including runtime and cleanup lifecycle tests. |
| Same container | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j4 wspr-tone-regression-test qrss-execution-regression-test test-tone-request-test test-tone-frequency-plan-test test-tone-response-test test-tone-selector-plan-test selector-shutdown-cleanup-test guarded-mode-change-persistence-test rp1-gpclk-transmit-backend-test SUDO=` | Passed all listed parent regressions. |
| `wspr4`, isolated component `src` | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 test build/obj/debug/./wspr_transmit.o COMM_CXX_FLAGS=-I../../build/generated SUDO=` | Passed component tests with final shared-mode assertions and compiled the transmitter object; no transmitter executable was run. |
| Temporary macOS source copy | Restore old TONE spacing, compile focused test, run it | Failed at the intended carrier assertion, confirming regression sensitivity. |
| Working branch | `git diff --check` | Passed. |

The Pi's generated capability header was produced inside its temporary snapshot
with `python3 scripts/generate_backend_capabilities.py --backends
rpi-gpio,rp1-gpclk,si5351,simulated --ancillary-gpio 1 --output
src/build/generated/backend_capabilities.hpp`. The host reported `aarch64`,
kernel `6.18.39+rpt-rpi-v8`, and installed libgpiod C++ headers. The local
container used the existing `wsprrypi-issue412-qualification:debian-trixie`
image, no network, no hardware mounts, dropped capabilities, and an
unprivileged user. Its source snapshot had local Git metadata only for the
build generator; it was not an installed or published build.

Initial validation limitations were resolved: the original macOS startup-test
link required Linux headers (replaced by a pure focused target); the sandbox
blocked loopback binding (portable suite passed outside it); the first
container snapshot lacked Git metadata (fixed within that disposable copy);
and direct Pi component object compilation needed the generated capability
header (generated locally and included explicitly). The Pi's unavailable Wi-Fi
was restored by the user before successful access through `wspr4`. None of
these failures was counted as a passing check. Portable make emitted jobserver
warnings; container tests emitted expected missing device-tree diagnostics.
The final listed commands passed without relaxing checks.

## Documentation Impact

Updated in this repository: component README, execution prompt, and this
frequency/validation note.

Reviewed but unchanged: `docs/bounded-tone.md` (duration and containment
semantics are unchanged), the shared clock-model/calibration implementation,
and the separate operator documentation. No UI source or screenshots changed,
so no interface rendering or Impeccable review was required for this backend
change.

Operator-documentation follow-up in `../Wsprry_Pi_Docs`, subsequently authorized
and completed after the matched RF check:

- `docs/User_Interface/Maintenance/index.md`: retain the exact-RF description
  and explain that a legacy GPIO tone can be rejected at a divider boundary.
- `docs/Command_Line_Operations/service_test_controls.md`: describe the same
  exact-carrier/rejection behavior for CLI test tones.
- `docs/Advanced_Operations/timing_calibration.md`: distinguish affected-build
  symbol-zero displacement from clock correction and corrected-build results.

## Original physical-acceptance boundary

During the implementation phase, no transmissions were authorized or performed.
No GPIO manipulation, installation, service operation, reboot, or RF measurement
was performed.
Hardware-free Pi tests establish software behavior on that CPU/toolchain, not
physical output. The later installation and separately authorized RF session
are documented in the [conducted validation record](development/issue-446-rf/README.md).
That record identifies the source/binary, processor, parent, GPIO route,
frequency, finite duration, fixed correction, reference/receiver, conducted
path, and stop/cleanup evidence. Its pass applies to the focused frequency
mapping fix, not general hardware or release qualification.
