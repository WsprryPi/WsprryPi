# Native WTP adapters and GPIO4 recovery acceptance

2026-09-30, application and operator documentation on `devel`.

GPIO and RP1 now enter the Pi WTP server through their native backends. The
shared adapter preserves Si5351 and explicit simulation, backend selection,
frequency policy, local priority and existing RP1 development authorization.
RP1 requires its existing host confirmation for the exact WTP job ID and selected
route. This campaign physically exercised GPIO4 on wspr4; the subsequent
[wspr5 GPIO20 inbound campaign](../wtp-rp1-inbound-report.md) passed all five
finite modes with corresponding SDR observations. Si5351 was disconnected,
and Pico A was excluded from this original GPIO4 campaign.

The [execution prompt](../wtp-gpio4-live-recovery-plan.md#execution-prompt),
[adversarial assessment](adversarial-review.md), [acceptance data](acceptance.json)
and [SDR summary](sdr-observations.json) describe this bounded result.

## Source and device identity

- Application baseline: `fba2b384a21eec55acb8db9ad80ece239bb056ac`.
- Native worker source: `2f67b00` plus the exact overlay in
  [source-manifest.json](source-manifest.json). `src/` is identical between these
  two base commits. The version string alone does not identify the overlay.
- wspr4 candidate: `3.2.0-devel+2f67b00`, SHA-256
  `7f8bff3fffd9a3cd3a87efcc6d66c419590398e153c956244605ef29e42c3ad9`.
- Worker/evidence stage: `/home/pi/wtp-native-recovery.s4izuw3v` on wspr5.
- wspr4 rollback: `/home/pi/wtp-gpio-recovery.rjneyinn/baseline-valid.tar`,
  SHA-256 `c04b74bcf1e226b4ac7b0437fcb050174c3dbf8eef125567ae93160ea4d27107`.

Source overlay hashes were checked after compressed transfer and again against
this working tree before recording acceptance. The installed candidate hash
matches the worker executable. Large IQ recordings remain in the private worker
stage; the repository keeps their hashes and compact observations.

## Physical and runtime results

| Case | Result |
| --- | --- |
| GPIO4 TONE | 14.097100 MHz, ten seconds, complete; corresponding SDR signal, output confirmed off and ownership released. |
| GPIO4 WSPR | AA0NT / EM18 / 20 dBm message metadata, 162 canonical symbols, 110.592 seconds; complete with corresponding full-length SDR signal, output off and ownership released. The reported power is message metadata. |
| GPIO4 QRSS / FSKCW / DFCW | Five-second fixtures each, including RF-off gaps for keyed modes; all complete with corresponding SDR signals and cleanup. |
| Actual Fleet controller crash | Killed the real central scheduler during a 180-second job. Target retained the finite timeline through terminal completion after lease loss, then released ownership. Restart retained the consumed slot and required reconciliation; no repeated dispatch occurred. |
| Lost LOAD / ARM / ABORT replies | Framed proxy discarded one successful reply for each operation. Identical request retries returned identical replies; one physical launch, then confirmed abort. |
| Connection loss | Detached an active client with a five-second lease. Finite job continued, foreign claim was refused, and completion released ownership with output off. |
| Missed start | Suspended the armed managed process beyond its launch window; an independent timer resumed it. `missed`, known off, with no corresponding SDR signal in that launch window. |
| Target process crash/restart | Forced process termination during active GPIO4 output. Automatic service restart quiesced output, produced a new boot ID, cleared ownership and rejected the stale job. |
| Injected clock loss | Typed clock seam made UTC unusable before launch. `missed`, output off, with no SDR signal during the abandoned launch window. Host clocks and chrony configuration were untouched. |
| Injected output-off confirmation failure | Real native shutdown completed, then the typed confirmation seam returned failure. SDR showed output absent while protocol state remained conservatively unknown; new claims and local work were blocked. Restoring confirmation alone did not clear the latch. Explicit reconciliation confirmed off and rotated boot identity. |
| Browser End now / Let it finish | Actual wspr4 browser displayed both choices. End now aborted; Let it finish saved local Enable while effective local output remained inhibited, then completed the remote job. No controller approval was requested. |
| Direct HTTP local Enable | Active GPIO4 job aborted, output off, owner removed and generation advanced. Observed about 0.15 seconds after the write. |
| Monitored INI local Enable | Actual installed INI write caused active-job cancellation, output off, owner removal and generation advance. Observed about 4.84 seconds after file write, including file-monitor detection. It did not wait for job completion or controller acknowledgment. |

SDR acceptance means a corresponding signal, as authorized. FFT windows used
65,536 samples, a 0.25-second step and a 30 dB signal/noise threshold around the
requested carrier. These observations do not qualify decode performance,
calibrated frequency accuracy, spectral purity or precise RF-edge timing.
All eight retained receiver captures reported verified stream/device cleanup.

### Hard process-death boundary

The last strong FFT window in the target-crash capture was about 31.5 seconds
following process termination. GPIO4 RF continued until automatic managed
restart and startup quiescence. The five-second WTP disable timeout applies to a
responsive process; `SIGKILL` is not an output-off operation. This observed
boundary is now in application and operator documentation. The process-crash
case establishes recovery and stale-job rejection, not immediate shutdown on
process death.

The [typed harness](fault-harness.cpp) is a recorded qualification artifact,
separate from production. It requires an explicit physical GPIO4 opt-in, obtains
the normal singleton, has a 360-second lifetime, and restores real clock and
confirmation behavior on exit. It was separately compiled with `-Wall -Werror`;
its executable SHA-256 was
`c56136af3d55baab73aa55b715549a1bd7133564e3f7c5bce6cb6b307ec82bb2`.

## Automated validation

Commands ran from `src`, except the explicitly identified component commands.

- **Full Linux:** `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j3 release semantics-test rp1-gpclk-transmit-backend-test wtp-pi-tone-engine-test wtp-pi-config-transaction-test SUDO=` passed on wspr5. Its existing private Avahi header/library directory was supplied through `AVAHI_AVAILABLE`, `AVAHI_CFLAGS` and `AVAHI_LIBS`; no packages or system configuration were installed for this campaign.
- **Standalone WSPR-Transmitter:** `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 startup-quiesce-test legacy-tone-frequency-test gpio-startup-quiesce-qualification-test SUDO=` passed from `src/WSPR-Transmitter/src`. The qualification-test target uses its guarded hardware-free test profile here; it is separate from physical GPIO evidence above.
- **Shared Si5351 transition regression:** `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 si5351-transition-test SUDO=` passed from that same component, using fake I2C.
- **Focused macOS:** `make wtp-pi-tone-engine-test rp1-gpclk-transmit-backend-test wtp-pi-config-transaction-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= CXX='c++ -Wno-deprecated-declarations'` passed.
- **Portable macOS:** `make semantics-test-portable SUDO= CXX='c++ -Wno-deprecated-declarations'` passed. Both Mac commands selected the installed macOS 26.5 SDK and deployment target. The original SDK selection failed to link; OpenSSL 4 deprecation warnings required the shown compiler override. The first full portable run was blocked by sandbox loopback binding; the rerun with loopback access passed. Linker deployment-version warnings remain environmental. Portable coverage is simulated-only and excludes the full Linux physical-profile executables.
- **Operator documentation:** Sphinx HTML builds with `WSPRRYPI_DOCS_INCLUDE_PICO=1` and `=0` passed. Enabled/disabled output assertions confirmed the native WTP sections follow the existing feature flag. Impeccable's changed-source detector returned `[]`. Desktop and 390×844 mobile rendered content was inspected; no horizontal overflow was found. Actual browser takeover used the installed wspr4 UI, not mocked responses. Fleet assignment editing through a live browser remains a separate gate.
- Final whitespace checks and evidence assertions passed. The review record lists repairs and reassessment. CI is separate from these local/native results.

Native logs remain in the worker stage: `build-final.log`,
`component-tests-final.log`, `si5351-transition-final.log` and
`release-identity.log`. Mac and documentation build logs remain in
`/private/tmp/wtp-native-*` on the development Mac.

## Restoration and remaining acceptance

[Final state](final-state.json): both original managed services are active;
wspr5's original binary and complete configuration are restored. The test central
unit is stopped and its temporary assignments removed. wspr4 keeps the verified
GPIO4 candidate for the final power-removal case, with its pre-takeover
configuration restored exactly, local Enable off, no remote owner, no active
output, known output state and takeover generation 4. The original Si5351
configuration/binary remains in its rollback archive. No persistent generation
was rewound.

October 1 follow-up: [actual operator power removal passed](power-failure/README.md)
on wspr4 GPIO4. Safe boot, retained consumed slot, explicit reconciliation,
corresponding RF cessation and no observed replay close the final case in this
live failure-recovery campaign. Current target WTP boot is
`19d88d5752e3f3ad9765815d3ccf16eb`; it remains idle with generation 4. The separate
source CI run failed the Mac endpoint completion assertion and network browser
startup. Later CI passed both jobs; the refreshed snapshot is linked in the
[current endpoint report](../wtp-rp1-inbound-report.md).

The mixed physical Fleet, both receiver groups and RP1 GPIO20 inbound modes
subsequently passed. Eight-way testing is excluded by operator instruction.
The remaining agreed campaigns are sustained scheduling across reconnections,
live-browser Fleet assignment creation/editing, discovery topology/cache changes
and completion of the canceled GCC release CI job. See the current endpoint
report for evidence links. This original campaign is not a general release
readiness claim.
