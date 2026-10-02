# Si5351 inventory isolation: execution report

2026-10-02, WsprryPi `devel`, base `960914333884158c87040c030d49ecaeb8795e65`.

**The configuration/discovery stall is repaired and the release candidate is
installed on wspr4.** Explicit inventory still reports the actual hardware scan
failure, while GPIO management remains responsive. The independent WTP reply
routing TODO remains pending.

## Implemented behavior

- Public snapshots use transient cached inventory; loading GPIO/WTP settings
  performs no Si5351 scan. Unchecked presence is unconfirmed.
- Fresh inventory and single-address readiness use an isolated process entered
  before singleton/output setup, with two seconds for the scan, at most 25 ms
  for cleanup attempts, a 4096-byte reply limit and one outstanding worker.
- A kernel-blocked worker is killed and retained for nonblocking reaping;
  additional scans are rejected until it exits. Application descriptors are
  closed in the worker. No clock configuration or output enable is performed.
- Physical web/INI validation runs outside the shared configuration lock.
  Changed revisions reject stale candidates. New selections still require fresh
  server-side inventory and existing transmission readiness remains enforced.
- Setup preserves inactive saved settings and ignores late backend/bus replies.
  Busy/timeout/failure states preserve the saved address as unconfirmed.
  Active configuration population starts one scan after saved bus/address fields
  are ready; operator backend selection continues to refresh inventory.

The reusable `src/WSPR-Transmitter` component, physical band/mode capabilities,
WTP protocol, frequency policy, schedules and RF output settings were unchanged.
The modified component is `WsprryPi-UI`; remaining implementation is parent
application code in `src/`.

## Validation

| Command and environment | Result |
| --- | --- |
| wspr5: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 si5351-inventory-process-test si5351-inventory-isolation-test i2c-bus-selection-test debug SUDO=` | Passed after repairing isolated build metadata and a missing test declaration. |
| wspr5 final source: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 semantics-test wtp-pi-config-transaction-test release SUDO=` | Passed: default Linux semantics, cleanup, UI/source, I2C, GPIO policy, websocket/network lifecycle, WTP configuration transactions and release build. Expected failure-injection diagnostics appear in the retained log. |
| `npm test` from `WsprryPi-UI` | Passed, including new inactive-backend/late-reply/timeout unit coverage. |
| `WSPRRYPI_CONDITIONAL_GPIO_SCREENSHOT_DIR=... node tests/conditional_transmit_gpio_integration_test.js` | Passed with saved-value, replacement and unconfirmed-failure assertions. |
| `node --check` for affected JavaScript; Python harness syntax; `git diff --check` | Passed. |
| Final single-load scan repair: `npm test`, conditional GPIO browser regression, and Linux `build/bin/ui_source_regression_test` | Passed after updating existing source assertions for the deferred population scan. |
| wspr5: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 si5351-inventory-isolation-test SUDO=` | Passed for both initial logging states. |
| wspr5: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 release backend-profile-factory-test BACKENDS=si5351 ANCILLARY_GPIO=0 SUDO=` | Passed, GPIO-free profile. |
| Strict profile and file-access scripts run as `pi` inside `sudo unshare --net -- runuser -u pi -- sh -c ...`, with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` | Passed: unavailable-bus rejection, one nonexistent worker path, no GPIO/MMIO/mailbox/RP1 access or I2C ioctl. |
| wspr5: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 wtp-pi-tone-engine-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`, plus five executions of `build/wtp_pi_tone_engine_test` | Passed after removing fixed-sleep/UTC-phase assumptions from the test. |
| Local Mac C++ compilation/linking | Not attempted, following the standing user instruction. |

Full logs, final source hashes, installed binary hash and screenshots accompany
this report. Linux tests used isolated source on wspr5, without modifying its
installed application or operational state. Build metadata identifies the base
commit and dirty candidate source; it is not represented as an already committed
binary. Follow-up validation changes only tests and CI. No separate new `WSPR-Transmitter` component test was necessary because
that component's source was unchanged; its parent integration paths ran in the
full profile.

## Live wspr4 evidence

The original root-owned binary and INI were backed up in a compressed archive;
private WTP state was separately backed up with mode 0600. The candidate was
installed atomically and the existing managed service restarted with automatic
rollback armed. Initial WTP status was empty, unowned and inactive. Device ID
was preserved across the expected application boot-ID change.

- Actual Si5351 scan on I2C bus 1 returned timeout after
  **2.029 seconds**; a competing scan reported busy.
- **40 concurrent configuration/discovery reads passed**; maximum response time
  was **26.7 ms**.
- Two reversible `WSPR.Use Random Offset` transactions passed during inventory
  work; maximum response time was **35.4 ms**. Local Enable stayed off.
- Original INI bytes were restored with matching SHA-256. No Fleet assignment
  files existed on wspr4; central wspr5 assignments were not operated on.
- The service stayed active/running throughout the regression after deployment;
  its boot ID did not change during the requests. Final state was inactive,
  unowned and known off, with no remaining inventory workers.
- The successful candidate remains installed. The rollback timer was cancelled;
  private backups remain on wspr4 for recovery.
- The canonical transactional publisher installed the matching UI from commit
  `402c0c20b57464f92912c91733bab24d1edc18ae` with packaged state and verified
  served JavaScript hashes. A live browser showed Controller connected and GPIO4.
  Subsequent configuration reads remained responsive; original INI bytes, service
  PID, boot ID, empty ownership and known-off output were preserved. Publication
  and post-publication health receipts accompany this report.

An initial harness attempt incorrectly expected an unwrapped host configuration
and PATCH support. The host API correctly uses a `config` envelope and PUT.
The harness was corrected before any write and the complete case rerun.
The underlying physical I2C response failure was not repaired or qualified;
this result closes management responsiveness and bounded inventory behavior.
No transmission, CLAIM/LOAD/ARM, RF test or Pico contact occurred.

## CI follow-up

The [first pushed CI run](https://github.com/WsprryPi/WsprryPi/actions/runs/37014606369)
exposed two test assumptions. On macOS, the INI conflict fixture wrote false to
an already-false logging setting, creating no newer revision. It now changes the
prepared value and exercises both initial states on every host. The strict I2C
audit expected an unavailable bus to be opened; metadata validation now correctly
rejects it earlier. The audit checks that early rejection and separately traces
the isolated worker's open of a verified nonexistent path, retaining all forbidden
GPIO/mailbox/MMIO/RP1/other-adapter and I2C-ioctl checks. These follow-up changes
affect tests and evidence, without changing the installed application behavior.

The first local audit invocation used the full build profile and correctly hit
its root requirement; the strict build then hit the installed service's singleton
guard. Audits use the explicit GPIO-free Si5351 profile and an ephemeral network
namespace, leaving wspr5's service untouched. The release-matrix job also reached
its 15-minute deadline after compiling all six profiles. Its deadline is now
30 minutes, retaining every profile and warning gate. Repaired CI results are
recorded in the completion report.

The next CI run passed the inventory portable semantics and strict I2C stages,
then exposed `later RF-on completes` in the existing macOS tone-engine fixture.
It now waits for actual terminal state, with a two-second bound, and uses
controlled time to place the later gate beyond the initial UTC-second boundary.
The real-time fixture starts at a full synthetic UTC second, avoiding a randomly
near-expired first admission window. Completion, Failed/Missed, output-off,
clock-loss and cancellation assertions remain; production engine code and its
deadlines were not changed. Linux validation and repeated test results accompany
this report.

## Adversarial reassessment and Impeccable

[Review and repairs](si5351-inventory-isolation-review.md) cover child lifecycle,
descriptor inheritance, output limits, stale cache/readiness, revision races,
INI validation, inactive-backend scans and dishonest timeout state. After
repairs, the final Linux suite, UI regressions and real wspr4 timeout/concurrency
case passed. No unresolved finding remains within this slice.

Impeccable was used for the Setup interaction and error state review. Rendered
1440px desktop/light and 390px mobile/dark states were inspected, including
available, missing and timeout inventory. The saved address remains visible,
timeout is distinct from missing hardware, retry guidance stays beside the
control, and the incumbent layout remains intact. Screenshot fixtures do not
qualify RF or a live browser-to-device Fleet workflow.

## Documentation Impact

- Updated: `docs/i2c-bus-selection.md`, discovery TODOs, comprehensive execution
  prompt, this report, adversarial review and retained validation evidence.
- Considered unchanged: `docs/wtp-pi-operation.md`, `docs/wtp-fleet.md`, and the
  sibling operator documentation. WTP ownership, Fleet behavior and the protocol
  are unchanged.
- Separately required operator documentation: sibling
  `docs/User_Interface/Setup/Transmitter/index.md`, **I2C Bus/I2C Address**, should
  explain separate on-selection inventory refresh, unconfirmed timeout/error
  state, preserved saved addresses and retry behavior. Sibling
  `docs/Advanced_Operations/rest_api.md` should describe the explicit inventory
  deadline and responsive cached configuration reads. Cross-repository changes
  were not made in this focused implementation request.

Implementation and UI publication are from commit `402c0c2`; the test/evidence
follow-up commit and repaired CI results are recorded in the completion report.
The routing TODO remains open: Ethernet reconnect with WiFi preferred must
recover usable WTP replies without manually repairing route preference.
