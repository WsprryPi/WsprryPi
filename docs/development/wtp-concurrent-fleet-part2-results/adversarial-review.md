# Part 2 adversarial review and reassessment

2026-10-01. Controller source `9a1b8c1`; final RF batch 20:56–21:00 UTC.

## Findings and repairs

| Finding | Resolution | Verification |
| --- | --- | --- |
| Observer used the display action `resume` as a Fleet API operation. | Use the documented API operation `enable`; retain the failed preparation separately. Preserve error response bodies in subsequent harness failures. | Successful final attempt: fifteen distinct completed jobs and all five active together in each slot. Earlier local Enable withdrawn before its first RF window. |
| Read-only WebSocket observer supplied HTTP Origin port 31415 to listener 31416. | Supply the matching listener Origin; leave request guards unchanged. Clear the deadline timer when the observer closes early. | Accepted read-only connection, no errors, local status transitions include transmitting/complete/disabled. Rejected connection retained separately. |
| Completion log elapsed time is not exactly 110.592 seconds. | Report intended frame duration and measured execution elapsed separately; do not infer calibrated RF edge timing from host polling or FFT windows. | Native normal completion at 110.725809 seconds; corresponding local RF through the frame interior and absent afterward; 110.5-second span of sampled on-signal centres. |
| A three-round recurrence test can prepare the fourth future slot before being paused. | Retain and disclose those canceled future jobs; keep consumed slots rather than editing/replaying history. | Fourth-slot starts 21:02:01 UTC, canceled before RF. Independent final status empty/unowned/inactive on all six endpoints. |
| Preflight occurred before the second managed-service restart. | Name the record `preflight-before-attempt1`; use the fresh launch-state and final HELLO boot identities for later states. | Actual confirmation arguments and boot identity recorded for the passing process. No claim that all snapshots share one boot/session. |
| Positive aggregate checks could hide wrong deployed code or incomplete receiver cleanup. | Independently bind installed binary/helper, source hashes, raw capture hash, unique job IDs and six physical final statuses. | All assertions in `verification.json` passed; capture exit zero, zero overflows/clipping, cleanup verified. |

## Reassessment

Reviewed request admission, API operations, stop deadlines, source/deployment
provenance, cancellation of future recurrence, report selection, complete job
identities, independent physical status and RF evidence claims.

The final batch contains fifteen distinct successful intended jobs, three common
remote windows, local transmitting status overlapping the first window, one
normal local completion and corresponding SDR evidence for all three newly
connected outputs. The source and installed binary/helper were unchanged from
the reviewed deployment. Pause/off state survived restoration of the canonical
service, and independent WTP reads found no owner, job or active output on any
member. No additional actionable application defect or evidence issue was
identified in this scope.

The original unresolved native-frame and second receiver-group acceptance is
now closed. The separately listed eight-simulator, sustained-reconnection,
actual local-takeover browser and discovery-topology campaigns remain open.
No release, calibrated RF timing/frequency, decode or RF-chain claim is made.
