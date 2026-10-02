# Physical Fleet reconnection acceptance

2026-10-01 Central time (recordings continue into 2026-10-02 UTC), `devel`,
base `3dd0a79894782b5dfd59fbd14ef6a282223eec6b`.

**The agreed five-target Fleet reconnection campaign passed.** It exposed a
target-restart reconciliation defect, which was repaired and physically retested.
This closes repeated scheduling/reconnection for the available fleet. Discovery
topology changes and browser assignment creation/editing remain separate work.

## Scope and results

wspr5 was the installed central controller. Local Enable stayed off throughout.
The five saved assignments drove wspr1 GPIO4 (14.099100 MHz), wspr2 GPIO4
(14.101100 MHz), wspr4 GPIO4 (14.098100 MHz), Pico A GP2 (14.100100 MHz) and
Pico B GP2 (14.102100 MHz). Each job was a finite ten-second TONE, scheduled
every 120 seconds at UTC second one. The current receiver connections were
wspr2, Pico A and the SDR on wspr5. The other target outputs were snubbed.
There was no recabling, Si5351 operation, local GPIO20 transmission or
eight-target test in this campaign.

| Case | Actual interruption | Result |
| --- | --- | --- |
| Controller connectivity | Controller IPv4 host routes to all five targets blackholed for 75 seconds during active work, beyond the maximum 60-second ownership lease. | Fresh status and explicit Reconcile/Resume; all five completed the next shared slot. |
| Target subset one | Same interruption for wspr2, wspr4 and Pico A. | All five completed the next slot; wspr1/Pico B also completed the fault slot. |
| Target subset two | Same interruption for wspr1 and Pico B. | All five completed the next slot; wspr2/wspr4/Pico A also completed the fault slot. |
| Controller crash | Managed controller killed with SIGKILL during work; systemd restarted it after 30 seconds. | Persisted in-flight barriers survived; explicit reconciliation cleared them before future work. All five completed the next slot. |
| Target restart | wspr2's installed service restarted during its active job. | Original controller failed reconciliation; repaired controller confirmed the new boot and empty/unowned/inactive status, then all five completed two future shared slots. |

The first four accepted cases used the original installed binary. Their nine
shared rounds comprise a baseline and four fault/recovery pairs. The repaired
restart repeat used three shared rounds: interrupted work, recovery and another
complete recurrence. All five independent observers saw active output in each
selected round. No duplicate job for a recorded slot, backwards consumed-slot
watermark or running output outside the recorded scheduled windows was observed.
The interrupted wspr2 job is deliberately not counted as a successful completion.

The original run's fifth case remained a recorded failure; the final two slots
of that original plan were withdrawn. Its separately recorded startup round
was valid scheduled work before the nominal baseline, caused by early Enable.
It was not replay. The reproduction harness now gates Enable until the preceding
slot's preparation window has passed. Failed preparation attempts and a browser
overlap attempt are excluded from acceptance and retained privately.

## Repair

After a changed target boot, the reusable WTP session correctly invalidated the
old ownership/job assumptions. Fleet kept using that retired context indefinitely,
so explicit Reconcile could not clear its dispatch barrier even after a fresh
independent observer proved the target empty and off.

Fleet now replaces that context only during explicit Reconcile after an
`IdentityChanged` result. It performs fresh read-only HELLO/CAPS/STATUS inspection
on the saved binding, requires the same full device identity and product, a
different coherent boot, ready/idle state, unowned inactive output and no fault
or uncertainty. It rechecks current capabilities/frequency policy and Pi durable
takeover state before replacement. The output exclusion lease is retained;
old ownership, jobs and transactions are discarded. Status readers are protected
during the context swap.

Protocol faults, a different device/product, the same old boot, missing evidence,
occupied output, running/loaded/armed work and failed states stay blocked.
Reconciliation sends no new job and does not abort another owner's work. The
assignment remains paused; Resume considers future slots. Its consumed-slot
watermark is preserved. No reusable component, endpoint protocol, authorization,
TLS/Plain LAN policy, UI source or configuration schema was changed.

Changes are confined to `src/wtp_integration/fleet_runtime.cpp`, the new
`fleet_recovery.hpp`, and `src/tests/wtp_fleet_test.cpp`, plus documentation/evidence.
The focused test covers changed-boot admission and the rejection boundaries.

## Evidence

- [First four case assertions](wtp-fleet-reconnection-results/original/accepted-case-verification.json),
  [controller/independent observations summary](wtp-fleet-reconnection-results/original/accepted-controller-analysis.json)
  and [SDR results](wtp-fleet-reconnection-results/original/accepted-rf-analysis.json).
- [Original failed reconciliation](wtp-fleet-reconnection-results/original/failed-reconciliation.json)
  retains the latched context and in-flight barrier.
- [Repaired restart assertions](wtp-fleet-reconnection-results/repaired/case-verification.json),
  [controller/independent summary](wtp-fleet-reconnection-results/repaired/controller-analysis.json),
  [two future-round results](wtp-fleet-reconnection-results/repaired/final-repeat.json)
  and [SDR results](wtp-fleet-reconnection-results/repaired/rf-analysis.json).
- [Deployment/source manifest](wtp-fleet-reconnection-results/source-manifest.json)
  binds the original and repaired binary hashes, changed source and harnesses.
- [Execution contract](wtp-fleet-reconnection-results/execution-prompt.md) and
  [adversarial review/reassessment](wtp-fleet-reconnection-results/adversarial-review.md).

Pi observers used independent read-only LAN HELLO/STATUS sessions. Pico observers
used USB INFO, which reports the shared JobService's actual state/output without
occupying its single LAN listener. USB INFO does not expose ownership or remote
job IDs. Those claims use fresh controller WTP STATUS and independent final LAN
inspection; no USB ownership fields were invented.

The SDR saw both connected carriers in all nine accepted original shared slots
and both clean repaired recovery slots. The repaired interrupted slot began with
wspr2/Pico A signals at least 82.92/55.22 dB above nearby masked median noise.
wspr2's carrier then disappeared after its restart. Pico A remained visible
(median 34.19 dB), but one window measured 29.27 dB and failed the analysis's
conservative 30 dB minimum across the entire slot. That full-window verdict is
retained as false. Both subsequent complete slots pass the original full-window
criterion, with wspr2/Pico A minima above 82.39/54.75 dB and clear post-job absence.
No continuous-RF or precision-edge qualification is claimed during the restart.

All six relevant receiver captures retained exactly their requested samples,
reported zero overflows, timeouts and clipped samples, and verified stream/device
cleanup. SDR acceptance remains the operator-selected corresponding-signal
criterion; this does not qualify the RF chain, calibrated frequency, decode or
spectral purity. The finite observation campaign is not an indefinite soak.

Large IQ files, complete observations, build logs, rejected attempts and private
backups remain outside Git on wspr5 under
`/home/pi/wtp-fleet-reconnection-20261001`; Mac observations are archived there
under `mac-records`. Large transfers used tar piped through gzip.

## Restoration

[Final service/configuration audit](wtp-fleet-reconnection-results/restoration.json)
confirms the canonical managed command, original INI/helper hashes, unchanged
five assignment definitions, monotonically retained consumed-slot history,
all assignments paused, no in-flight barrier, local Enable off and no unknown
output. Routes exactly match their baseline. There are no active campaign timers,
receiver processes or USB observers. GPS/PPS remains synchronized.

The repaired controller remains installed on wspr5:
`663b46d503c69ff4c718ba1d6ecfe2e5877e661b42bfeb1fb76c3feeb0dfa700`.
The original binary is retained privately. The native staging tree's original
`src` matches the base commit; three hashed source files supply the repair.
Its retained build metadata displays `6a32789`, so the deployment manifest,
rather than that label, identifies the tested candidate.

Pico B temporarily used the previously qualified 138 MHz physical GP2 image
`3e1337074003`. Its exact original `615888e5364b` 150 MHz image was restored.
[Restoration evidence](wtp-fleet-reconnection-results/pico-b-restoration.json)
confirms identical 57,344 reserved bytes and unchanged saved profile/provisioning.
The original image reports `inhibited-standalone-simulator`; its restored state
is not a claim that physical RF can resume on that image. Pico A was not flashed.
[All six final LAN endpoints](wtp-fleet-reconnection-results/final-six.json)
independently report empty, unowned and output inactive after restoration.

## Validation and UI review

| Host | Command/check | Result |
| --- | --- | --- |
| wspr5, native staging `src` | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 wtp-fleet-test SUDO=` | Passed. |
| wspr5, native staging `src` | `make -j2 debug SUDO=` | Passed; stripped candidate installed with managed service preparation. |
| wspr5, native staging `src`, libgpiod C++/Avahi headers present | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 semantics-test SUDO=` | Full Linux suite passed with hardware access disabled. Expected negative-fixture warnings and IPv6-loopback fallback were logged. |
| Mac, repository `src`, Xcode SDK explicitly selected | `make -j2 wtp-fleet-test SUDO= CXX=/usr/bin/clang++ BACKENDS=simulated ANCILLARY_GPIO=0` | Focused portable Fleet suite passed. This is not the full Linux suite. |
| Mac / wspr5 | Recorded case, controller, SDR, restoration assertions; Python syntax, JSON and final diff checks | Passed with the interrupted-slot RF limitation recorded above. |

The repair changes the operator Reconcile outcome, so Impeccable was applied to
the incumbent Fleet workflow. Actual browser Reconcile against wspr2 returned
HTTP 200, displayed feedback beside the schedules, and left all five paused.
Desktop 1440×1000 and mobile 390×844 views were inspected: controls wrap, disabled
Pause is distinct, status age remains visible, and focus/feedback are retained.
[Desktop](wtp-fleet-reconnection-results/fleet-desktop.jpg),
[mobile feedback](wtp-fleet-reconnection-results/fleet-mobile.jpg) and
[mobile row controls](wtp-fleet-reconnection-results/fleet-mobile-rows.jpg) record
the review. No UI source change was needed. Browser event retention was truncated;
the retained request/response proves the action, not an exhaustive POST count.
Browser assignment creation/editing was not exercised by this check.

## Remaining agreed testing

| Area | Remaining |
| --- | --- |
| Live Fleet browser | Create/edit assignments through the actual browser. Reconcile is now exercised; prior Resume/Pause/Remove and both local takeover choices retain their recorded passes. |
| Discovery | Link loss, address changes, multihomed recovery and confirmed residual cache expiry using wspr5's Ethernet/WiFi interfaces. Route blackholes in this campaign left those links and mDNS up. |
| CI | Existing base-commit network/browser job failed at “Render network controls with mocked responses.” Base Mac, full Linux, strict I²C and GCC 13 release jobs passed. A new pushed-commit run is a separate CI result. |

Eight-way testing is excluded. Previously closed installed-service, clean-card
installation, power-failure recovery, physical takeover/INI/HTTP cancellation,
mixed Fleet concurrency and wspr5 GPIO20 mode acceptance remain recorded passes.

## Documentation Impact

- Updated: `docs/wtp-fleet.md` explains explicit changed-boot reconciliation;
  this result, execution contract, compact evidence, review and development backlog.
- Considered unchanged: `docs/wtp-pi-operation.md`, the endpoint contract and
  sibling operator Fleet/local Enable documentation. Reconcile then Resume,
  local priority, ports and authorization policy remain the documented workflow.
- No sibling operator-doc change is required for the repaired existing Reconcile
  behavior. No separate repository or UI/reusable component source was modified.
