# Phase 7 prompt: shared Scheduling tab and native remote operation

Execute only phase 7 of [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
when separately authorized. Read the [execution rules](execution-rules.md), the full
[plan](../device-owned-solar-scheduling-plan.md), and approved phase 1 decisions.
Require phases 1–6 gates for intended sources, including production target-local
fencing, actual device-owned recurrence, and the completed
[deferred Pi security milestone](deferred-pi-security.md). Do not start this phase
with pairing or authenticated remote Pi management still deferred. Use Impeccable
before any UI edits.

## Objective and inspection

Add shared scheduling controls through the phase 3 target context and phase 2
local/Pico management adapters and the completed deferred remote Pi adapter,
backed by phase 6 runtime. Inspect `WsprryPi-UI/DESIGN.md`,
Setup templates, target drafts, existing gray-line/GPS controls, configuration API,
status consumers, and the complete configuration/admission lifecycle. Preserve the
incumbent operational visual language and use production interfaces in reviews.

## Required work

1. Insert Scheduling between Signal Setup and Transmitter. Preserve the shared
   Device selector outside tablist/form submission and narrow-screen placement.
   Keep target identity/type, connection, write/activation state, execution owner,
   and output destination visible, especially Pi-owned schedules using WTP output.
2. Provide per-target Enable scheduling independently from solar-policy enablement,
   Transmit, boot behavior, immediate stop, and ownership. Ordinary schedules can
   run without solar prerequisites when solar policy is disabled. Disable dependent
   scheduling fields visually and semantically while preserving their values;
   leave target selection and the switch usable for a writable capable target.
3. Add day/night allow/skip, inherited or explicit frequency overrides, morning/
   evening gray-line controls, explicit units/bounds, target-site location, and
   relevant policy/status feedback. Preserve lists, custom values, selectors,
   precision, mode frequency semantics, and inactive saved values. Preview never
   persists or grants authority. Show saved, active, and draft values distinctly.
4. Use target-local locator center and only supported approved GPS providers.
   Explain time/location holds and capability restrictions; never use browser or
   controller position/timezone as execution input. Label forecasts/observation age
   separately from actual execution. Bound forecasts and allow an unknown result.
5. Save only to the selected identity/revision-bound target. Use the approved atomic
   save-and-enable transaction or explicit save-disabled/readback/enable sequence.
   Confirm persistence and pending application separately. Enabling edit fields
   must never enable stale last-saved settings while the draft is unsaved. Preserve
   the draft and last confirmed enable state separately after capability loss,
   conflicts, failed writes, disconnects, or uncertain replies.
6. Keep Pico controls faithful to current capabilities. Show unsupported day/night
   profiles as unavailable with explanations; do not implement controller sunrise
   writes. Expose separately labeled WSPR/message schedule states unless the target
   has a verified atomic master contract. Never present multiple non-atomic writes
   as one confirmed switch. Firmware parity is not implicitly authorized here.
7. Preserve target drafts, ignore late replies on context changes, track accepted
   old-target writes there, and renegotiate after reconnect/reboot. Place feedback
   near its action. Retain keyboard focus, labels, contrast, accessible disabled
   state, and target-specific error/recovery guidance.

## Tests, visual review, and documentation

Exercise local/remote Pis through the same controls and production adapters. Test
new entries, unsaved enable attempts, disabled fields, blank inheritance, rejected
frequency values, unavailable providers, separate Pico schedule states, malformed/
stale replies, reconnect, write uncertainty, capability loss, and draft switching.
Verify phase 5 enforcement through direct APIs bypassing the UI as well as through
controls. No selection, preview, read, or failure may silently persist or enable. Verify
server-side validation still rejects invalid values from semantically disabled or
manually submitted controls; UI disability is not the validation boundary.

Using isolated software processes and explicit simulated outputs, run two targets
at different sites with target-local clocks/location. Close the browser, shut down
only the isolated management-controller process, reconnect, and prove schedules
continue autonomously without duplicated/replayed work or controller-location
leakage. Keep target processes and configuration/journals separate from the
management-controller process. Capture target admission/execution records for
scheduled occurrences during the controller's absence; a cached browser forecast
or post-reconnect status alone does not prove autonomy. Never stop installed
services or operate live hardware for this test.

Run UI tests from `WsprryPi-UI`, parent source/configuration/runtime regressions,
and both complete paired suites for changed shared interfaces. Render and inspect
desktop/mobile, light/dark, keyboard flows, tab/focus order, long content, empty/
loading/disconnected/read-only states, saved/pending/failed states, and disabled
scheduling. Resolve applicable Impeccable findings and report visual evidence.

Update Setup/Scheduling, Operations, first-transmission, Signal Setup frequency
inheritance, Fleet/device selection, INI, time/location holds, and troubleshooting
guides concomitantly. Review materially changed screenshots and build/render Docs.

## Acceptance gate

Local and remote Pis share controls and a target-owned evaluator; remote schedules
continue independently, Pico is accurately capability-gated, authority fencing
cannot be bypassed, and enable/save semantics cannot launch stale drafts. No
controller-side remote solar recurrence or sunrise configuration writes exist.
End with the shared report and review loop; stop before phase 8.
