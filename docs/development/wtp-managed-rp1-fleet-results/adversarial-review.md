# Adversarial review and reassessment

2026-10-01, `devel`, base `6a32789cfb84881df9e6839b27a9538f69e7bed1`.

## Findings and repairs

| Finding | Repair | Verification |
| --- | --- | --- |
| Managed RP1 preparation checked the unresolved route before binding launch confirmation. CW deferred launches had the same ordering problem. | Permit preparation with supplied transient confirmation and existing local/startup/reload gates; reconcile, validate and bind before the ordinary commitment gate. Recheck admission at deferred CW launch. | Full Linux semantics; managed WSPR/CW positive cases and ownership/reload/missing-confirmation rejection. |
| Fresh INI and HTTP candidates lost the process launch confirmation. | Carry it into validated runtime candidates; keep it absent from persisted/public configuration. | Managed INI planning and HTTP transaction assertions. |
| Native dialog blocked actual browser Pause/Remove. Background refresh could change the reviewed revision. | Bootstrap dialog with default Cancel, named target, captured revision, no retries, disabled duplicate controls and feature/page cancellation. | Mocked browser cancellation/Escape/feature-hide/412/double-confirm cases; actual Cancel and Confirm on the installed controller. |
| Service command parsing could accept multiple ExecStart records or ambiguous/invalid confirmation data. | Fixed command prefix, bounded unique JSON fields, strict booleans/route/operation ID, supported options only, one service record. | Ten Python tests, including duplicate keys/options, malformed fields, alternate config and chained record rejection. |
| Repeated Enable during a locally owned scheduled wait made effective Enable false and canceled the schedule. | Preserve already effective local schedule admission; a disabled schedule, Test Tone, unknown output or revocation failure still cannot gain admission. | Portable authority tests, full Linux suite and native eight-second managed wait across repeated Enable; stopped before RF. |
| Reconnected Fleet rows retained obsolete unavailable feedback. | Clear only stale status-fetch errors after a successful fetch; discard fetch results/errors during mutation or after hiding/closing. | Updated browser failure/recovery regression and actual reconnection. |
| Changed installed companion correctly failed the provider's integrity binding. | Use supported neutral recovery/removal and digest-bound redeployment, with the exact installed provider source and unchanged kernel modules. | Provider receipt, restored GPIO20 idle route, verified ownership and source/installed hashes. No hash check bypass. |
| Updated unit definition did not describe the already running process arguments. | Restart while outputs are off and inspect the actual process command line. | Verified argument present for the managed wait; removed the temporary drop-in and verified the canonical command after cleanup. |
| Native source build initially lacked Avahi development headers. | Install Avahi build headers, rebuild both guarded translation units and rerun full Linux semantics. | Final binary links Avahi, publishes the endpoint, discovers all six physical members and matches native source hashes. |
| Restoring a future browser fixture's schedule alone retained its canceled future consumed slot. | Explicitly remove/recreate the paused assignment through the public API; preserve its definition and do not edit private consumed history. | Final restored Pico B context has `last_start_ns=0`, original schedule and Enable off. |

## Reassessment

Re-read the complete source/test diff and reviewed authorization ordering,
finite request binding, serialization, local priority, revocation epochs,
service argument ambiguity, dialog transitions, revision conflicts, duplicate
actions, deployment provenance and cleanup claims. All implementation findings
above are repaired. The focused checks were rerun after each repair, followed
by full Linux semantics on the final Avahi-enabled source. No additional
actionable source defect was identified in this scope.

Impeccable static detection reports no findings. Desktop/mobile mocked and
live Remove dialogs, plus the live desktop Pause dialog, were inspected.
The target/consequence, default Cancel focus, wrapping and incumbent styling
were retained. Cancel sent zero observed POSTs; confirmed Pause sent exactly
one POST at the reviewed revision, with no event-log truncation.

The actual Pause test canceled a future pending schedule; it did not claim an
active physical RF cancellation. Confirmed Remove deliberately created a new
assignment context when the original paused definition was recreated. This is
not a claim that its retired consumed-slot history was restored.

## Unclosed acceptance boundary

Follow-up: this boundary was subsequently closed by
[the Part 2 physical acceptance](../wtp-concurrent-fleet-part2-report.md).
The paragraphs below retain the original snapshot before the operator's rotation.

The final normal 110.592-second native frame alongside five remote targets,
and corresponding SDR signals from wspr2/Pico A/wspr5, remain untested after
the last repair. The stopped combiner rotation was requested; no operator
response was available before this report. The native waiting-state check is
only preparation/admission evidence. It is not a completed physical frame.

Earlier remote/RF passes remain historical evidence. They do not substitute
for this pending final-code concurrency pass. No release, RF-chain, frequency
calibration, WSPR decode or general discovery-topology qualification is claimed.
