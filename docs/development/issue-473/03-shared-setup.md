# Phase 3 prompt: shared Setup context and existing-control migration

Execute only phase 3 of [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
when separately authorized. Read the [execution rules](execution-rules.md), the full
[plan](../device-owned-solar-scheduling-plan.md), and approved phase 1 decisions.
Require phases 1–2 acceptance evidence for the intended candidate sources.

## Objective and inspection

Provide one target-aware Setup context for existing controls. Inspect the complete
parent diff and first-party `WsprryPi-UI` sources, especially
`data/views/config.php`, `data/views/wtp-controls.php`, `data/wtp-management.js`,
`data/gps.js`, `data/gray-line.js`, and their server/configuration counterparts.
Use Impeccable before UI changes and follow `WsprryPi-UI/DESIGN.md`; preserve the
existing operational design, typography, colors, and light/dark behavior.

## Required work

1. Add one labeled Device selector to the right of the Setup tabs, outside the
   ARIA tablist and configuration form submission. On narrow screens wrap it to
   its own row above the active pane. Preserve the final order Signal Setup,
   Scheduling, Transmitter, Device I/O, Fleet; Scheduling is implemented in phase 7,
   so omit that tab until then rather than add a dead placeholder. Do not introduce
   new solar configuration or new schedule enablement here; preserve authorized
   existing scheduling behavior when relocating an existing editor.
2. Reuse known/nearby discovery infrastructure without backend-switching effects.
   Show target name/type/identity, connection, read/save, and activation state.
   Nearby or unidentified entries lead to identification/enrollment; selection
   alone never enrolls, writes, claims, enables, changes backend, or starts work.
3. Implement per-target drafts and request context binding. A target switch retires
   in-flight reads and ignores stale replies. Accepted writes remain tracked on
   the old target; uncertain outcomes require authoritative readback before retry.
   Reconnection/reboot renegotiates before editing. Handle deletion, capability
   changes, and unsaved drafts without silent loss or cross-target application.
4. Adapt existing Signal Setup, transmitter, and I/O controls to local and remote
   adapters from phase 2. Keep units, precision, custom values, valid inactive
   values, field validation, and actual persistence semantics. Never route remote
   controls through local autosave. Clearly distinguish draft, saving, saved,
   pending activation/restart, conflict, invalid, disconnected, and read-only states.
5. Rename Pi I/O to Device I/O and expose only supported writable capabilities.
   Unsupported/unverified Pico mappings remain read-only with explanations; do
   not combine WSPR/message schedule controls or fabricate missing profiles.
   Do not remove an existing editor until its replacement preserves the verified
   read/write/activation contract; report a blocked mapping rather than lose access.
6. Move device settings out of Fleet into shared tabs without competing editors.
   Fleet retains discovery, enrollment, profiles, and overview. Keep controller
   actions visibly labeled, and target actions explicitly capability- and identity-
   bound. Do not introduce unsupported restart/power behavior.
7. Give feedback near the triggering control, preserve drafts after validation or
   write failure, and retain keyboard focus and accessible labels through async
   state changes. Do not use color alone to communicate status or disability.

## Tests, documentation, and acceptance gate

Run UI tests from `WsprryPi-UI`, parent source/regression and management tests,
and both network/USB recipes if a shared interface changes. Use production target
adapters with isolated software services. Exercise target switching during reads,
writes, uncertain replies, reconnects, reboot identity changes, and active jobs.
Prove two targets' drafts, saves, feedback, and host configuration remain isolated.

Render and inspect desktop/mobile, light/dark, keyboard navigation and focus,
labels/tab semantics, loading/errors, long device names, disconnected/read-only
states, draft switching, and uncertain save. Review screenshots and actual flows,
fix applicable Impeccable findings, and report evidence and limitations.

Update sibling Setup, Signal Setup, Transmitter, Device I/O naming/links,
Fleet/device-selection guidance, and materially inaccurate screenshots for the
implemented controls. Follow Docs build/render requirements. Do not document the
future Scheduling editor as present.

The gate requires the shared context and migrated existing controls to pass these
reviews without changing backend, active jobs, ownership, or persistence merely
through selection. No recurrence implementation, new schedule enablement, or Pico
firmware parity expansion belongs here. End with the shared report and review loop;
stop before phase 4.
