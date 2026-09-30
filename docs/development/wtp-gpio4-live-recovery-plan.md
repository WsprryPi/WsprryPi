# GPIO4 WTP completion and live failure recovery

## Status and scope

2026-09-30, `devel`. The operator explicitly directed that WTP apply to every
transmission route and authorized the missing native adapters, including the
required `src/WSPR-Transmitter` changes. The native adapters are implemented. GPIO4 RF and non-power recovery acceptance
have passed; actual operator power removal remains pending. See the
[acceptance record](wtp-native-recovery-results/README.md).

The operator selected wspr4 GPIO4 because Si5351 is disconnected, and stated
that this removes Pico A from the combiner. Treat Pico A as excluded from this
campaign. wspr5 supplies the controller and existing SDR. The operator accepts
a corresponding SDR signal; a complete RF-chain inventory is not required.
Use 20m, AA0NT / EM18 for an encoded WSPR frame, and bounded finite jobs.
The operator will perform the actual power removal after the other cases pass.
Do not power off wspr4 during the earlier cases.

## Verified implementation boundary

At the campaign baseline, `src/wtp_endpoint/runtime.cpp` constructed only the
Si5351 and explicit simulated endpoint engines. GPIO/RP1 WTP adapters were
unavailable. Compiling a
GPIO backend into the application does not make it usable through the listener.
The installed wspr4 binary includes `rpi-gpio`, but its version is
`3.2.0-devel+2f67b00`; the service was active with PID 694 at preparation.
wspr5's existing service was active with PID 1966; its SDR API service was active.
These are preparation observations, not live RF acceptance.

The reusable native GPIO implementation is
`src/WSPR-Transmitter/src/wspr_transmit_backend_rpi.{hpp,cpp}`. It already
accepts backend-neutral execution plans and advertises WSPR, QRSS, FSKCW and
DFCW. Its WTP integration needs:

- A parent adapter using the existing singleton, ownership and cancellation
  authority, with explicit GPIO selection and no fallback to another backend.
- Native preparation during ARM lead, admission at the physical first-enable
  transaction, an observed launch, and a timeline anchored to that launch.
- Realized event frequencies from the same configured native frequency plan.
- Verified output disable and propagation of cleanup failures. The current
  `cleanup()` returns success unconditionally after legacy cleanup; this is
  insufficient evidence for a WTP output-unknown failure test.
- GPIO processor/frequency policy and existing local-transmission behavior
  preserved. CAPS must report the selected backend's actual supported modes.

Changing reusable component hooks rather than claiming launch in the parent
engine is necessary to observe actual native output and preserve timing. Review
component tests independently and run the applicable parent integration tests.
Include the RP1 adapter under the same contract and preserve its provider and
route policy. GPIO4 on wspr4 is the selected live route; RP1 live qualification
requires separate evidence. No UI redesign is included.

## Acceptance sequence

| Case | Required observations |
| --- | --- |
| Baseline WSPR | Encoded 162-symbol, 110.592-second WTP job completes on GPIO4; corresponding SDR signal; output known off; ownership released. |
| Controller crash | Kill the test controller during active work; preserve WTP/1 finite completion after lease loss, record target outcome and output; reconnect by identity/boot/STATUS; no second dispatch of the consumed slot. |
| Connection loss | Break only the test WTP connection while preserving management access; verify failure isolation, reconciliation and local priority. |
| Lost replies | Deliberately drop LOAD/ARM/ABORT responses through a test proxy; capture wire identity/replay/status decisions and prove no duplicate physical job. |
| Target crash/restart | Terminate the managed target during active work, recover it, and verify startup quiescence, changed boot identity, cleared stale ownership and rejection of the old job. |
| Clock loss | Deliberately make the test clock unusable before launch; require rejection or missed-start behavior without RF. Restore and verify clock health. Label the fault as injected. |
| Missed start | Suspend the armed target across its launch window, resume it under a bounded external supervisor, and verify no late transmission or replay. |
| Output-off failure | Use a reviewed typed test harness to inject an unsuccessful output-off confirmation while independently making physical output inactive; verify output-unknown latching, blocked claims/local RF and explicit recovery. Do not add production CLI/INI/UI fault switches. |
| Local priority | Repeat target-local reclaim while the controller is absent or uncertain; confirm it never requires controller approval and invalidates saved assignments. |
| Actual power loss — last | Arrange with the operator after the preceding cases pass; record pre-cut work, physical removal, boot identity, output state and recovery without automatic duplicate jobs. |

The failure controller must exercise the actual Fleet recovery path where the
assertion concerns saved schedules or consumed slots. A raw protocol client can
test endpoint behavior but cannot qualify Fleet dispatch persistence by itself.
Separate genuine process/network failures from deliberate clock/confirmation
fault injection in every result. GPIO acceptance cannot diagnose or qualify the
unavailable Si5351 route.

## Preservation, stopping and closure

Before changing binaries, services or configuration, capture current device
identity, configuration, active ownership/work, service state, installed hashes
and clock state, and preserve a verified rollback copy outside the checkout.
Use fresh private stages, compressed source transfer and existing build targets.
Keep wspr5's transmitter service/configuration preserved when using its SDR.

Every test needs a bounded external deadline and a cleanup path: target-local
stop/reconciliation, native GPIO quiescence, receiver release and service/config
restoration. Do not treat a lost reply or closed socket as output-off evidence.
Preserve monotonic revocation generations rather than restoring an older journal.
Record protocol/status, job identity, timing and corresponding SDR observations.

Perform adversarial review, repair findings and reassess before marking a case
passed. If GPIO implementation changes operator behavior, review the sibling
operator documentation under its own instructions and authorization boundary.
This corrects the missing native routes in the previously authorized full
contract. Review, commit and push application and operator documentation in
separate `devel` boundaries. Do not mark actual power removal or RP1 physical
WTP acceptance passed without their corresponding evidence.


## Execution prompt

Complete the Pi WTP server integration for every selected native transmitter
backend on `devel`, following the local endpoint contract and the linked Pico
protocol authority. Preserve the existing working trees and component boundaries.

1. Inspect the repository, applicable instructions, current configuration,
   installed devices, capability metadata and native execution contracts.
   Preserve verified rollback artifacts outside the checkouts. Use compressed
   streaming for large transfers. Do not treat the disconnected Si5351 as a
   runtime failure requiring diagnosis.
2. Add the missing GPIO and RP1 endpoint adapters through a shared native launch
   contract. Keep backend selection explicit and preserve local execution,
   processor/band policy, clock correction, RP1 provider/route eligibility and
   exact-operation development authorization. Never select simulation because
   physical preparation fails. Outbound WTP remains a separate control path.
3. Derive CAPS from native backend metadata. Support its finite WSPR, TONE, QRSS,
   FSKCW and DFCW plans. Prepare before launch, report realization from the same
   configured divider/event program, require adjustment consent, and admit at
   the native enable transaction with an observed launch. Validate contiguous
   positive finite events, native tone alphabets, total duration and backend
   bounds before ARM. Preserve local continuous-TONE behavior separately.
4. Confirm native shutdown and propagate failed confirmation into conservative
   unknown output. Keep local Enable, inbound listener admission and outbound
   per-output schedules independent. Local takeover never asks the central Pi
   for approval. Interactive takeover offers End now or Let it finish; direct
   HTTP and accepted monitored INI Enable cancel immediately. Persist revocation
   generations and remove future saved assignments when a controller returns.
5. Keep the Pi Plain LAN default port 31417 and existing INI/CLI override,
   singleton, boot admission, DNS-SD and identity contracts. Preserve WTP/1
   replay, retained terminal results, lease semantics and boot changes. A finite
   armed/running job continues to its terminal state after lease or connection
   loss; no lost response or disconnected process proves output-off.
6. Run component tests and parent integration tests with explicit host profiles:
   full Linux semantics with development headers and hardware access disabled;
   portable simulated-only semantics on macOS. Test the optional hooks,
   profile/center normalization, native realization, malformed plans, declined
   admission, cleanup failure and unchanged local execution semantics.
7. Use wspr4 GPIO4 on 20m and wspr5's existing SDR for live acceptance. Exclude
   Pico A. Run all five modes, including an encoded 162-symbol 110.592-second
   WSPR frame. A corresponding SDR signal satisfies RF acceptance here; do not
   invent decode, spectral, calibrated-frequency or whole-chain requirements.
8. Exercise actual Fleet crash persistence and consumed-slot reconciliation;
   raw protocol clients alone cannot qualify saved Fleet behavior. Exercise
   connection loss, lost LOAD/ARM/ABORT replies with identical retries, target
   process crash/restart, missed launch, clock loss, output-off uncertainty and
   local priority. Use bounded typed developer injection for unavailable clock
   and unsuccessful confirmation, with real hardware independently off. Add no
   production CLI, INI, environment or UI fault-injection switches.
9. Review the captured RF/status records together. Distinguish responsive-process
   shutdown from hard process death, and record any RF that persists until
   managed startup quiescence. Restore the original wspr5 service/configuration,
   release the receiver, and leave wspr4's verified GPIO candidate idle for the
   final operator power-removal test. Never rewind revocation persistence.
10. Perform an adversarial review of source, execution, restoration and claims.
    Repair findings, repeat affected validation and reassess. Preserve all
    unqualified boundaries, including RP1 physical WTP and the final power cut.
11. Update application references and the already authorized sibling operator
    documentation. Retain the existing Pico feature flag. Use Impeccable for
    affected rendered documentation and browser workflows; build with the flag
    enabled and disabled, and inspect desktop and mobile views. Do not redesign
    or replace unrelated UI/screenshot content.
12. Record exact source/binary identity, commands, outcomes, RF observations,
    restored state, review findings and remaining acceptance. Review staged
    diffs, commit and push each authorized `devel` repository separately, verify
    remote parity, and report CI status only for the actual pushed commit.
