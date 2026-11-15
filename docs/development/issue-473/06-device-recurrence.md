# Phase 6 prompt: Pi device-owned recurrence and ownership integration

Execute only phase 6 of [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
when separately authorized. Read the [execution rules](execution-rules.md), the full
[plan](../device-owned-solar-scheduling-plan.md), and approved phase 1 decisions.
Require phases 1–5 gates, especially target-local authority fencing. Read
`docs/simulated-backend.md` before designing application tests.

## Objective and inspection

Integrate phase 4 policy evaluation into the actual Pi WSPR and repeating
QRSS/FSKCW/DFCW paths, retaining target ownership and current safety. Inspect
`src/scheduling.cpp`, `scheduling_runtime.cpp`, `scheduling_config.cpp`,
`scheduling_websocket_status.cpp`, plan placement, selector lifecycle, runtime
configuration bridge, backend guards, cancellation, and progress persistence.
Trace admission through final launch rather than adding a parallel scheduler.

## Required work

1. Use one configuration/location snapshot to evaluate intended occurrence starts
   before selector preparation, ownership acquisition, or job commitment. For paired
   WSPR evaluate both starts; accept the complete pair only when both select the
   same policy and allowed frozen intent. Otherwise refuse the complete pair.
2. Freeze accepted occurrence intent so later solar-boundary changes alone do not
   interrupt it. Preserve random offsets, pair placement, mode/message/timing,
   frequency metadata, backend restrictions, all derived-tone guards, cancellation,
   reload, ownership, and launch checks. Recheck policy/clock/configuration
   generations at commitment; stale prepared work cannot launch.
3. Implement independent Enable scheduling state per approved state tables. Off
   admits no new recurrence while an accepted complete occurrence may finish.
   Immediate stop remains distinct. Keep Transmit, Enable on Boot, output ownership,
   and safety inhibition independent. Neither disabled scheduling nor a solar wait
   implicitly releases local ownership or grants an external WTP claim.
4. Feature-disabled behavior remains the existing baseline. Solar enabled selects
   gray-line then day/night, with missing required time/location holding recurrence.
   Policy refusal is normal waiting, including CW; it cannot terminate the daemon.
   Use bounded existing wake/recompute mechanisms, not a busy loop or poll-time
   durable writes. Changes to time/configuration recompute future work without
   catch-up bursts, duplicate admissions, or stale preplanned work.
5. Implement approved bounded, crash-consistent progress persistence. Independent
   explicit period lists rotate independently; inherited periods share ordinary
   progression. Refused admissions do not advance rotation. Explicit skip entries
   advance once at their normal slot; a WSPR pair advances once, not per frame.
   Preserve approved restart continuation and disable/enable semantics. Apply the
   approved failure policy when a watermark write or readback is uncertain; do not
   advance memory-only progress or launch unreconciled work in a way that could
   replay after restart. Test crash points around admission and progress commitment.
6. Enforce phase 5 authority fencing at target-local admission, not just the API
   or UI. Keep manual, one-shot, test-tone, and externally owned WTP work governed
   by existing rules. Apply solar policy to Pi-owned recurrence even when output
   is WTP, with explicit execution owner/destination; do not create controller-side
   remote solar dispatch or convert destination semantics.
7. Expose saved and active policy, selection, waiting reason, selected frequencies,
   observation age, prolonged window, and bounded next intended start or unknown.
   Keep forecasts distinct from actual execution evidence. Status serialization
   and consumers must follow strict current contract rules and context bindings.

## Tests, documentation, and acceptance gate

Exercise real application paths with explicit simulated backend, isolated journals,
and both accelerated and bounded real-time simulation. Cover all schedule/solar/
Transmit/boot/owned/held combinations, feature-off equivalence, selector ordering,
complete-pair refusal/freezing, offsets, CW shifts, frequency limits, independent/
inherited rotations, skips, lifecycle failures, disable during accepted work,
cancellation, restart watermarks, persistence failures, reload races, sleep/resume,
clock steps both directions, time/location loss, and no duplicate/catch-up work.
Explicitly test both-start policy disagreement, identical frequencies under different
policies, and rejected admissions before selector/ownership effects. Assert bounded
wakeups and durable write counts, not just eventual scheduling success.

Inspect and run applicable focused targets from `src`, including
`non-wspr-repeat-policy-test`, `wspr-plan-placement-test`, `band-gpio-rotation-test`,
and `selector-shutdown-cleanup-test`, plus new admission/progress tests and the
host-appropriate semantics profile. Add meaningful production-path coverage for
new invariants. Both complete network and USB suites are required for changed
status/control interfaces. Do not use mocks as a replacement for these gates.

Update scheduling, configuration, boot/Transmit, time/location holds, WTP-output
ownership, and troubleshooting operator guidance concomitantly, without advertising
the future shared Scheduling editor. Apply Impeccable if existing rendered status
UI or UI documentation changes and record visual evidence.

The gate requires safe admission and ownership through actual application paths,
restart-safe bounded progress, truthful status, and successful applicable suites.
Software simulation does not qualify GPIO, timing, physical USB, installation,
services, or RF. End with the shared report and review loop; stop before phase 7.
