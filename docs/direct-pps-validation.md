# Direct PPS calibration validation

The feature branch implements optional direct RF-clock calibration for Pi 5/RP1.
Independent conducted acceptance, the required software suites, documentation
review, and repeated adversarial assessments passed for the scoped cases below.
Delivery is on feature branches; no merge, installation, or release is implied.

## Sources and delivery boundary

The Pi worktree started at `21d54a3bd201b96522ed318be763e75d89ea7256` on
`codex/direct-pps-calibration`. Original checkouts and existing user changes
were preserved. The matching operator manual uses a separate feature worktree
and commit boundary, based on `f13fba4`.

The actual paired integration fixtures select Pico
`3a7515b000b6f881256e080749be7352e36b5d96`, Mbed TLS
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`, and its framework
`dff9da04438d712f7647fd995bc90fadd0c0e2ce`. No Pico or RP1 provider source is
modified by this feature. The application/UI and a pinned public Linux UAPI
import are the affected source boundaries; `PPM-Manager`, `Chipset-Offsets`,
and `WSPR-Transmitter` remain unchanged.

## Adversarial assessments

The first assessment examined clock identity, capture stalls, source health,
correction signs/composition, preservation of committed work, settings
lifecycle, GPIO conflicts, unsupported platforms, and reference loss.
The following issues were closed before final acceptance:

| Finding | Closure |
| --- | --- |
| Streaming capture can stall on a full FIFO and an installed firmware rejects `SM_GET_FLAGS`. | Single-result hardware program counts ten periods without an in-interval FIFO push, then blocks. No unsupported flag query or physical-memory access is used. |
| Build-host header availability could silently omit a working Pi capture path. | Import the exact public upstream UAPI and its licenses at a pinned revision; verify hashes and ABI sizes. |
| The reference-health age gate must account for the existing 120-second Chrony polling interval. | Use a separate 300-second report-health bound and a 30-second direct-measurement bound; test both. |
| Delayed readback could falsely refresh measurement age. | Date freshness conservatively from trigger plus the ten-period span; reject excessive capture duration. |
| A new source, backend change, disable, or capture error must not preserve qualification. | Stop/release input capture and restart with empty history; test expiry, replacement health, failure, disable, and reacquisition. |
| Direct and host estimates could be confused on switching source. | Keep host-qualified state separately, replace its value in direct mode, and preserve independent saved preferences. Explicit CLI choices override direct mode for that invocation. |
| GPIO17 conflicts could reconfigure the existing PPS input. | Refuse LED, amplifier, shutdown, enabled band selector, and per-frequency selector assignments; show repair feedback beside the new switch and retain the draft. |
| Failed acquisition could hide its cause behind generic zero-correction wording. | Preserve the provider reason in status and show acquisition/qualification/fallback near the control. |
| CLI help did not explain the new source overrides, and repeated identical live-status writes could repeat accessibility announcements. | Correct the help and retain the existing text node when its content is unchanged; native help, UI, and focused browser checks passed. |
| Documentation retained the former blanket deferral. | Update the scope decision and operator guidance while retaining the unimplemented Pi 1–4 boundary. |

A subsequent assessment rechecked the final producer, selector, configuration,
UI, tests, and documentation, including a new regression for qualification
retirement after a previously stable measurement becomes unstable. Independent
RF outcomes, final check results, and source hashes are recorded below. The
subsequent assessment found no remaining actionable issues within this scope.

## Software and visual checks

From the Pi worktree's `src` directory, the final portable profile runs:

```sh
make -j4 semantics-test-portable SUDO=
make rp1-pps-calibration-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
```

The portable suite passed. It is explicitly simulated-only and excludes the
physical-backend runtime/cleanup executables. The focused PPS instruction,
conversion, reference health, qualification, instability, expiry, fallback,
reacquisition, and cleanup tests passed. The vendored UAPI hash/license check
also passed. These are software results, not RF or electrical evidence.

On wspr5's isolated native source, `npm test` and
`node tests/rp1_pps_browser_test.js` passed from `WsprryPi-UI`. The browser
fixture exercises the shipped PHP/JavaScript with device access blocked:
opt-in autosave, independent host preference, draft restoration, keyboard
Space behavior, GPIO17 conflict/repair, residual availability, and live
acquisition/qualified/fallback states. Desktop 1440×1000 and mobile 390×844
captures cover light and dark themes; control and detail captures were reviewed.

Impeccable's four-file source detector found zero anti-pattern failures and
13 advisories in pre-existing, unchanged style declarations. This regex-only
check does not replace the rendered review. The matching operator manual built
with Sphinx `-W -E`; four affected pages were rendered at desktop/mobile sizes
with no page overflow or failed images. Existing screenshots remain applicable.

## Independent RF campaign

The bench uses wspr5, its existing GPIO17 GPS PPS, the conducted combiner and
60 dB attenuation per path, SDRplay RSP1B `2404058C60`, and locked Leo Bodnar
LBE-1421 GPSDO `0673ED0FA107`. Output 1 is changed only in volatile memory to
500 Hz above the DUT carrier; no GPSDO flash write occurs. Every finite TONE
uses supported minimum 2 mA drive and a requested eight-second duration.
Frequencies are 7,040,100, 14,097,100, and 28,126,100 Hz, on GPIO20 and GPIO4.
The cumulative authorized RF budget is 300 seconds, including stopped attempts.

The baseline freezes the contemporaneous signed Chrony tracking frequency as
the manual additional correction. It reproduces intrinsic-plus-host-estimate
composition; it does not claim cold-start qualification or continuous host
refresh during that group. PPS replaces that estimate. The trim comparison
sets only the conducted residual to +0.5 ppm, predicting approximately −0.5 ppm
of RF movement relative to the untrimmed case.

Both carriers are captured simultaneously at 250 ksample/s. Their measured
separation removes common receiver local-oscillator error. A six-second
Hann/interpolated spectrum supplies the central measurement, with three
separate two-second windows for drift/repeatability inspection. The conditional
engineering allowance includes 0.05 Hz per spectral line, 100 ppm residual
receiver sample scale across the measured separation, and 10 ppb GPSDO error.
These are declared engineering allowances, not a metrology certificate or an
unconditional manufacturer accuracy claim. The proposed scoped acceptance is
measured untrimmed residual within ±0.1 ppm, allowance at most 0.05 ppm, and a
baseline improvement exceeding comparison allowances.

Two live attempts were stopped when PPS qualification changed during a
tone. Their IQ and restoration records are retained; they are not accepted
accuracy observations. Each eight-second command allocation remains charged.
Earlier failed bench handshakes emitted no RF. Completed results and subsequent waiting remain
separate: each new PPS job must qualify while RF is off, and the runner stops
on departure from qualification, GPSDO lock loss, receiver error, timeout,
or cancellation.

Raw IQ, private configuration copies, readbacks, and restoration records remain
on wspr5 under root-only `/home/pi/direct-pps-bench-20261104*`. Public summary
results must include candidate/capture hashes and exclude private configuration.
Candidate execution is temporary: the installed executable is not replaced.
Output-off, route, GPSDO settings, installed configuration bytes, and original
service state must be verified after each campaign. This work does not qualify
power, spurs, WSPR timing/decoding, other boards, other PPS wiring, Pi 1–4,
installation, deployment, or release readiness.

### Conducted results and restoration

[Public measurements and hashes](direct-pps-rf-results.json) contain all 30
accepted captures: two baseline and two untrimmed PPS repetitions at each of
three frequencies on each route, plus one +0.5 ppm residual case per frequency
and route. The trim repetition count was reduced to retain budget for stopped
attempts; neither their time nor their failure is discarded.

| Route | Host-snapshot error range | Untrimmed PPS error range | +0.5 ppm residual error range |
| --- | --- | --- | --- |
| GPIO20 | −2.519 to −1.687 ppm | −0.050 to +0.018 ppm | −0.567 to −0.476 ppm |
| GPIO4 | −2.212 to −2.204 ppm | −0.017 to +0.026 ppm | −0.519 to −0.493 ppm |

Every untrimmed six-second result and every two-second subwindow met ±0.1 ppm.
The largest untrimmed subwindow error was 0.091 ppm. Every trim result and
subwindow, after accounting for its commanded −0.5 ppm RF shift, also met
±0.1 ppm. Trim closure is evaluated against the per-job measured-clock
correction, rather than treating the oscillator as unchanged between sequential
jobs. The largest conditional receiver/reference allowance was 0.032 ppm.
Baseline separation from the corrected results exceeds those allowances.
This is scoped engineering evidence with declared bounds, not traceable
expanded metrological uncertainty or an all-board guarantee.

Keeping the SDR receiving between finite IQ captures removed repeated receiver
startup from later comparisons. This controls an identified varying bench load;
it does not establish a temperature coefficient or prove a single cause for
earlier instability. Qualification was allowed to settle with RF off before
each PPS job. The product's stability/fallback policy was unchanged throughout
the RF campaign. It deliberately requires reacquisition when readings become
unstable; it is not a continuously steered phase lock.

All accepted jobs retained identical committed correction/provenance throughout
their active status samples. All per-job output-off inspections reported
quiescent GPIO, clock, and DMA, with no owner, lease, or open endpoint.
Cumulative commanded time was 256 seconds: 240 seconds in accepted jobs and
16 seconds charged to the two stopped attempts. Every requested job was eight
seconds, within the approved ten-second/job and 300-second total limits.

Final readbacks verified the original GPIO20 route, exact installed configuration
bytes, active service state, exact GPSDO output/settings readback, and successful
receiver deactivation/release. The candidate executable was temporary and was
not installed over the running application. Existing PPS/Chrony configuration
and GPIO17 ownership were preserved. Physical PPS disconnection/antenna-loss
faults were not induced; their estimator/fallback paths are covered by controlled
software captures and report fixtures instead.

## Native software environment and commands

The final hardware-free native runs use an unprivileged `bwrap` filesystem
namespace. System files are read-only; only the isolated candidate and Pico
host-build directories are writable. `/tmp` and `/dev` are private, with no
physical GPIO, PIO, SDR, or transmitter nodes. The candidate's empty test-state
directory is mounted over `/var/lib/wsprrypi` only inside that process view.
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` remains set throughout.

Initial attempts exposed two environmental dependencies: the host's root-only
updater directory correctly produced a fail-closed maintenance status for an
unprivileged test, and the live Chrony provider superseded a deliberately
injected estimate in a runtime fixture. The final namespace supplies empty
private application state and an unavailable default `chronyc`, matching the
hardware-free CI environment. The focused Chrony tests still supply their own
controlled reports through the production parser. The host's service, updater
state, Chrony, and reference configuration are not changed by this isolation.
Neither a production check nor a contract validator was relaxed.

Within that environment, from the isolated Pi source root:

```sh
export WSPRRYPI_DISABLE_HARDWARE_ACCESS=1
python3 scripts/generate_control_contract.py --source pico-reference --check
make -C src semantics-test SUDO=
cmake -S pico-reference -B ../direct-pps-calibration-pico-host-20261104 \
  -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build ../direct-pps-calibration-pico-host-20261104 \
  --target usb_management_driver -j2
make -C src wtp-usb-pair-client-build wtp-usb-management-test SUDO=
node WsprryPi-UI/tests/wtp_usb_settings_test.js
WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node WsprryPi-UI/tests/wtp_usb_pair_test.js \
  src/build/wtp-network/wtp_usb_pair_client \
  ../direct-pps-calibration-pico-host-20261104/usb_management_driver
make -C src wtp-network-runtime-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
make -C src wtp-network-interop-test SUDO= PICO_SOURCE=../pico-reference \
  MBEDTLS_SOURCE=/home/pi/bounded-response-20261031/mbedtls
make -C src wtp-config-interop-test SUDO= PICO_SOURCE=../pico-reference \
  MBEDTLS_SOURCE=/home/pi/bounded-response-20261031/mbedtls
```

The contract check, full Linux semantics, actual Pico USB endpoint build,
USB negative/current-settings checks, actual production Pi/Pico USB pair, and
912 production network runtime checks passed. Actual TLS interop, its
60-second scheduled wait/recovery cases, and the complete production settings
pair also passed. These fixtures include the shipped UI producer, real Pi
runtime/coordinator, Pico handlers/journals, strict negative contracts, and
current network and USB carriers. No required suite was skipped.

The last review corrected CLI help to describe direct/host overrides, manual
selection, and residual applicability, and guarded identical status text against
repeated live announcements. These are the only production amendments after RF
testing; estimator, selection, configuration, backend, control-producer, and
protocol behavior are unchanged. Final native help, `npm test`, and the focused
PPS browser suite passed; final desktop/mobile detail captures were reviewed.
The browser suite explicitly verifies that repeated identical status polls
retain the text node. RF evidence identifies the measured debug binary, source
hashes, and these presentation-only amendments explicitly.

The final whitespace check excludes the exact upstream `Linux-syscall-note`
file's retained blank EOF line. All other staged and unstaged changes pass
`git diff --check`; the vendor hash check confirms that the license exception
and UAPI remain byte-for-byte upstream imports. No local skill/runtime artifacts,
private configuration, or raw IQ are staged.


A final post-validation readback again confirmed exact installed configuration,
active original service, GPIO20 route, quiescent output with no lease/owner,
selected host PPS, and the unchanged empty updater-state directory. The local
documentation server and isolated browser processes were stopped after review.

## Documentation impact

The operator manual updates transmission calibration, Setup/Transmitter, INI
backend/default references, and CLI backend options on its separate feature
branch. It documents prerequisites, settling, source ownership, correction
accounting, fallback, and the Pi 5-only adapter. Existing screenshots were
considered and retained because their depicted controls remain applicable.
No operator-documentation follow-up remains for the selected scope. The parent
repository contains the execution prompt, capture contract, scope decision,
validation record, and public RF measurements; private bench data stay outside
Git. Release, installation, other-board, and broader RF qualification remain
separate activities.
