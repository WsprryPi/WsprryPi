# Adversarial review and reassessment

2026-10-01, `devel`, base `3dd0a79`. Review covers the narrow Fleet recovery
repair, physical evidence, operator workflow and restoration.

## Findings, repairs and closure

| Finding | Action | Verification |
| --- | --- | --- |
| Actual restarted target remained permanently blocked by its retired `IdentityChanged` context. | Replace only during explicit Reconcile after fresh read-only same-device/product, new-boot, unowned/inactive proof; retain exclusion lease, capabilities/policy and Pi takeover checks. | Focused positive/rejection tests, full Linux semantics and installed physical wspr2 restart with two complete five-target future rounds. |
| Independent Pico LAN observers occupied the image's single client listener. Bare console STATUS reported clock status rather than JobService. | Observe read-only USB INFO; retain ownership/job-ID limitations explicitly. | Both physical Pico engine/state observers were ready before Enable, controller fresh STATUS after reconciliation and independent final LAN checks. |
| Partial serial JSON could end at an inner brace; observer-thread failure did not initially stop preparation. | Accept only newline-terminated response JSON, propagate observer failure and require fresh five-observer readiness before Enable. | Corrected once/continuous preflights and accepted runs; failed attempts excluded. |
| A reconnected Pico could still be closing its previous TCP session. | Bounded eight-second retries of explicit reconciliation within the allotted recovery window. | Subset-two recovery and clean future round. No job retry or consumed-slot rewind. |
| Early Enable admitted a legitimate preceding slot. | Record that startup round separately; gate future harness Enable after the preceding preparation window. | Original startup evidence retained; repaired run contains only its three planned rounds. |
| Browser Reconcile overlapped automatic Resume in the first repaired attempt and canceled its prepared future slot. | Exclude run6, serialize scheduling and browser actions; perform browser check after accepted run7 cleanup. | Run7 completes both future rounds; actual paused browser Reconcile returns 200 and leaves all paused. |
| Unstripped debug executable exceeded the managed runtime preparation's 64 MiB file limit. | Strip debug sections from the private candidate, then repeat normal atomic deployment/service preparation. | Installed 17 MiB candidate hash recorded, canonical service active. No limit/integrity bypass or helper change. |
| Restart-slot Pico A trace failed a conservative minimum threshold despite a corresponding signal. | Retain the false full-window metric, separate initial carriers and complete recovery rounds; do not claim restart-window RF continuity. | Initial 55.22 dB signal, later median 34.19 dB, one 29.27 dB window; both clean recovery rounds pass the unchanged strict criterion. |
| Original and repaired runs used different controller binaries. | Bind both hashes/source and scope first-four vs repaired restart evidence explicitly. | Source manifest, retained original failure and executed harness hashes. Original `src` content equals base commit; final three source hashes match native staging. |
| Temporary Pico B physical image could outlive the campaign or overwrite retained profile data. | Restore exact retained original UF2, compare reserved sectors before/after and verify the saved profile. | 57,344 bytes identical; revision 615888e5364b, original inhibited engine and saved state restored; final unowned/inactive LAN proof. |

## Source reassessment

Re-read the complete source/test diff after the repair. Checked that explicit
operator recovery is required; old-session protocol faults remain latched; full
device/product and changed/coherent boot are required; missing CAPS/STATUS,
uncertainty, ownership, active output and nonterminal/failed state cannot clear
the barrier. Focused tests cover these gates, including wrong device/product,
same/inconsistent boot, active/owned output, missing observations, wrong phases
and protocol fault. Real installed restart exercises the complete context swap.

The old application is stopped before replacing its transport. Read-only
inspection does not claim or submit work. The shared identity exclusion lease
survives replacement. Status readers hold the same mutex as the pointer swap.
Fresh-context failure or CAPS/policy rejection leaves the old persisted barrier.
No reusable WTP client or transmitter component was changed to weaken its latch.

Pi durable revocation/local Enable checks run before replacement and remain
part of subsequent dispatch admission. Recovery cannot erase local takeover,
adopt a foreign owner/job, select a new address/device, replay an interrupted
slot or authorize RF. Existing TLS and Plain LAN settings are preserved.
No additional actionable source defect was found in this bounded reassessment.

## Evidence reassessment

Independently checked actual checkpoint data rather than pass labels: expected
full identities, new wspr2 boot, inactive/unowned proof before recovery, fresh
controller identity/status after it, five running outputs and complete finite
future reports with successful cleanup. Checked unaffected targets' fault-slot
completion. Both accepted observation analyses have zero duplicate-slot IDs,
watermark regressions and unexpected running windows. Controller HTTP outages
and target restart observation errors are retained as expected fault evidence.

Pico USB lacks owner/job IDs and is not promoted into those claims. Controller
protocol evidence plus final independent LAN inspection supplies them. RF claims
apply only to the two current receiver-connected outputs; snubbed outputs have
physical-engine/state evidence. Sample counts, receiver outcomes, zero
overflow/timeout/clipping and cleanup were checked for all six relevant captures.
The interrupted-slot strict metric remains false; initial/clean-round carrier
claims have their own recorded values. Failed original restart and rejected
run6 have not been converted into passed plans.

The final audit compares original schedule definitions and routes, requires
nondecreasing consumed history, canonical process arguments, unchanged INI/helper,
all five paused/no in-flight state, no unknown output, no active campaign timer,
receiver or USB observer, and synchronized GPS/PPS. All six independent endpoint
checks pass after Pico B restoration and the final managed controller restart.
The repaired binary remains intentionally installed and its original is backed up.

## Impeccable reassessment

Inspected actual desktop 1440×1000 and mobile 390×844 Fleet views after cleanup.
Feedback is beside the schedules; paused states and disabled Pause are distinct;
mobile controls wrap without clipping; observation age remains explicit. Actual
Reconcile returned HTTP 200 and did not Resume. The browser event buffer is
truncated, so no exhaustive single-POST-count claim is made. UI source is
unchanged; no new actionable defect was found in the affected workflow. The
separate create/edit browser acceptance remains outstanding.

## Remaining boundaries

This closes the agreed finite five-target reconnection campaign, with the
first four cases on the original binary and the repaired restart case physically
repeated. It does not claim an indefinite soak, continuous restart-window RF,
eight devices, Discovery link/address/multihomed/cache changes, or browser
assignment creation/editing. Existing network/browser CI failure is separately
recorded; local/native source suite passes do not turn that workflow green.
