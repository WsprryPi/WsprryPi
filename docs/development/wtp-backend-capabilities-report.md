# Backend-derived Pi WTP capabilities — execution report

Date: 2026-09-29. Branch: `devel`. Application starting revision:
`90cae3809e84d75005db27ce5fa4e3c7c2d1e754`.

## Outcome

Executed the [implementation prompt](wtp-backend-capabilities-prompt.md).
The Pi Si5351 WTP adapter derives CAPS from the native backend and executes
finite multi-event plans. The artificial 20m / TONE / ten-second restriction
has been removed. Existing backend qualification and frequency policy remain
in force; this work does not replace them with a separate WTP qualification list.

Software, native planning, and local-takeover checks passed. All five mode
fixtures produced corresponding signals on the SDR, satisfying the operator's
RF visibility criterion. **Full live WSPR job completion remains unresolved:**
both accelerated and normal-cadence fixtures stopped on the native backend's
active readiness check. Output-off was confirmed. This report does not mark
that runtime finding closed or claim every acceptance check passed.

## Implementation

- `wtp_endpoint/capabilities.hpp` maps the selected backend's mode mask and
  numerical frequency bounds into CAPS. Si5351 reports WSPR, TONE, QRSS, FSKCW,
  DFCW and the planner envelope 7,812.5 Hz–200 MHz. Existing amateur-band and
  experimental-frequency policy still governs LOAD admission.
- Protocol resources are bounded at 512 contiguous events and 24 hours. ARM
  lead remains two seconds, maximum admitted uncertainty 500 ms, and output-off
  timeout five seconds. These are not new physical qualification limits.
- `wtp_endpoint/tone_engine.*` preserves mode, timing, frequency changes and RF
  gating in the native execution plan. It validates per-event realization from
  the configured joint planner and requires frequency-adjustment consent.
- Component `src/WSPR-Transmitter` exposes configured per-event realization,
  shares planner frequency bounds, admits valid partial tone sets, and supplies
  scheduled first-enable / silent-prefix timeline hooks. Guarded high-frequency
  PLL fallback is available to all supported modes; experimental integer-MS
  preference remains unchanged. Local execution retains its existing defaults.
- Component `WsprryPi-UI` replaces the obsolete ten-second helper text in Fleet
  assignments. No new controls or layout changes were introduced.
- `tools/wtp-pi` adds the no-ARM capability matrix and multi-mode RF fixtures.
  WSPR uses 162 repeated four-tone events, not an encoded callsign frame.

## Tests and checks

All commands below passed unless explicitly described otherwise.

### Host software (macOS)

Commands ran in `src`, with `SUDO=`. The portable host used
`SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk`,
`MACOSX_DEPLOYMENT_TARGET=26.5`, and `CXX='c++ -Wno-deprecated-declarations'`.
The default SDK 27 linker was incompatible with this host's arm64e.x1 inputs;
the installed SDK 26.5 resolved that build-environment failure.

- `make semantics-test-portable SUDO=`: passed. This is the documented
  simulated-only subset, not full Linux physical-profile semantics. The first
  sandbox attempt could not bind the WebSocket test port; the authorized
  unsandboxed rerun passed.
- `make wtp-pi-tone-engine-test wtp-pi-authority-test wtp-pi-control-test wtp-pi-config-transaction-test wtp-fleet-test wtp-production-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`:
  passed; production integration reported 257 checks.
- `make -C WSPR-Transmitter/src si5351-planner-test`: passed.
- Final focused `wtp-pi-tone-engine-test` rerun: passed.
- `python3 -m py_compile tools/wtp-pi/probe.py tools/wtp-pi/capability_matrix.py`:
  passed.
- In `WsprryPi-UI`, `npm test`: passed.
- `node tests/wtp_pi_fleet_ui_integration_test.js`: passed, with desktop/mobile
  screenshots. Impeccable review covered assignment helper text and existing
  Fleet/local-takeover workflows; the changed text fits at both sizes.
- `git diff --check` and staged whitespace checks: passed.

### Native Linux, wspr4

Used isolated checkout `/home/pi/wtp-caps-validation.wglNdL`, explicit
`BACKENDS=si5351,simulated ANCILLARY_GPIO=0`, and `SUDO=` for compilation/tests.
The installed service binary and INI were not replaced.

- `make -j3 JOBS=3 debug wtp-pi-tone-engine-test wtp-pi-config-transaction-test wtp-fleet-test`:
  passed with explicit staged Avahi include/library flags. Missing Avahi
  pkg-config metadata emitted warnings; the staged headers and native libraries
  supplied the actual build dependencies.
- `make -C WSPR-Transmitter/src -j3 si5351-transition-test si5351-planner-test`:
  passed. The transition test includes 75 band/mode combinations, realization,
  gated timeline anchoring, leading silence and output-disable readback.
- Live `capability_matrix.py --host 192.168.1.68 --port 31418 --execute`:
  passed 75 band/mode LOAD/ABORT cases from 2200m through 2m and three partial
  tone-set / leading-silence cases. It never sent ARM.
- All 526 native source/header hashes matched the retained manifest. The RF
  executable SHA-256 was
  `cdd9064e6d041ff911ef3f5ee8236921688b0d386653ee1580c24d42e045bce9`.
  A final self-contained `<limits>` include was subsequently rebuilt and tested;
  it changes no runtime behavior. Final identity is retained with the results.

An initial incremental native build preserved source mtimes older than stale
objects, leaving an inconsistent component configuration layout. It was rejected
before counting RF acceptance and produced a shutdown bus error. Changed native
source/header mtimes were refreshed, all affected objects rebuilt, and native
component/integration tests rerun before the recorded RF campaign. A final
rebuild initially attempted to copy over the running isolated binary and failed
with `Text file busy`; it succeeded after the isolated process was stopped.

### Live RF and local precedence

The isolated endpoint used Si5351 on wspr4, TCP port 31418, with local schedule
disabled. SDR capture on wspr5 used the existing receiver. Frequencies were on
20m. No full RF-chain inventory was required or collected.

| Fixture | Requested duration | Protocol/runtime result | SDR result |
| --- | --- | --- | --- |
| TONE | 12 s | Complete, output off | Corresponding signal |
| QRSS | 5 s | Complete, output off | Corresponding gated signal |
| FSKCW | 5 s | Complete, output off | Corresponding signal |
| DFCW | 5 s | Complete, output off | Corresponding gated signal |
| WSPR, accelerated 162 events | 12 s | Failed after about 6 s, output off confirmed | Corresponding signal before failure |
| WSPR, normal 162 × 0.682666… s | 110.592 s | Failed after about 6.5 s, output off confirmed | First campaign already establishes mode visibility; second capture retained separately |
| Local End now, QRSS | 10 s request | Immediate local cancellation passed | Corresponding signal |
| Local Let it finish, DFCW | 5 s | Pending local enable, then local ownership passed | Corresponding signal |
| Direct HTTP Enable, TONE | 10 s request | Noninteractive immediate cancellation passed | Corresponding signal |

The two WSPR failures reported `Si5351 active readiness failure, register 0=193`.
No readiness mask, output-off check, or capability advertisement was weakened.
The configuration authority refuses native configuration changes while remote
ownership exists, and no second transmitter process was present. The exact
cause of this physical readiness fault is not established. Historical native
Si5351 transition failures also exist in `si5351-results/rejected-attempts.json`;
that does not prove this has the same cause.

The next unfinished runtime step is to isolate the readiness fault under a
normal WSPR sequence and demonstrate a complete 110.592-second job. The existing
backend's qualification is not retracted, but this execution cannot claim that
live completion check passed.

Local recovery cleared the failed remote job without controller approval.
The isolated process then exited, wspr4's original `wsprrypi.service` was restored
active with `ExecMainStatus=0`, wspr5's WsprryPi service remained active, and the
receiver helper finished with verified SDR cleanup. Receiver reacquisition
attempts made before the prior bounded capture released its device were rejected;
a fresh output path was used for the successful takeover capture. No temporary
process or test transmission was left running.

## Adversarial review and reassessment

Source/test findings repaired and reassessed:

1. Hard-coded CAPS and independent single-tone realization could disagree with
   the configured native multi-tone plan. CAPS now uses backend metadata;
   realization comes from that exact configured plan.
2. ARM lead could consume event duration; subsequent gated RF-on could be
   mistaken for a late initial start. The native timeline is anchored once,
   later gates retain their offsets, and stop still rejects later RF-on.
3. Leading RF-off intervals needed a scheduled silent start. They now receive
   explicit admission and preserve the silent duration, with component tests.
4. Rejected preparation could leave a stale prepared plan. It is invalidated
   before validation; configuration/cleanup failures are tested and fail closed.
5. Requiring exactly the maximum number of tones rejected valid partial mode
   alphabets. One through the mode maximum is accepted and matrix-tested.
6. The high-frequency planner fallback excluded CW-derived modes despite their
   existing backend policy. Guarded fallback now covers every supported mode;
   the numerical-only analysis test explicitly opts out.
7. Runtime frequency-policy changes could leave stale endpoint admission policy.
   They now participate in output-settings reconfiguration comparison.
8. The probe previously accepted any inactive terminal state as success. A normal
   job now requires Complete, exposing rather than hiding the WSPR runtime fault.

Final source reassessment found no additional actionable defect in the changed
adapter/component/UI paths. **The live WSPR readiness finding remains open.**
The task therefore has a delivered CAPS correction and passing software checks,
but does not have unconditional runtime acceptance or an all-findings-closed claim.

## Documentation Impact

Updated application endpoint, Fleet, contract and historical-report navigation;
the comprehensive prompt and this evidence report are new. Updated the authorized
sibling operator pages `User_Interface/Setup/index.md`,
`User_Interface/Setup/Transmitter/index.md`, and
`Command_Line_Operations/transmitter_backends.md`, entirely inside the existing
Pico documentation flag.

Both flag-off and flag-on Sphinx HTML builds passed with warnings treated as
errors; three feature-flag tests passed. Default-off content was unchanged.
Local fragment links and gated rendered HTML, published sources, and search were
checked. Desktop/mobile rendered documentation was reviewed with Impeccable;
existing screenshots remain accurate. REST resources, INI settings and service
controls were considered but unchanged because their contracts did not change.
No additional operator documentation edit is required for this CAPS correction.

## Evidence and boundaries

See [results](wtp-capabilities-results/), particularly
[native matrix](wtp-capabilities-results/native-matrix.json),
[SDR analysis](wtp-capabilities-results/sdr-analysis.json),
[first RF plot](wtp-capabilities-results/sdr-first.png), and
[takeover RF plot](wtp-capabilities-results/sdr-takeover.png).
Wire/HTTP observations and capture metadata are retained; large raw IQ remains
outside Git with hashes and original paths in the metadata. These observations
do not measure calibrated frequency accuracy or spectrum quality.

GPIO/RP1 WTP server adapters, a 24-hour physical endurance test, all-band RF
requalification, and full default-profile Linux semantics were not run as part
of this correction. Existing local backend qualification remains authoritative.
Application/component and sibling documentation changes use separate commits
on their respective `devel` branches.
