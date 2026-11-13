Implement fleet device selection in the existing WsprryPi Operations panel, including per-device status and controls, required coordinated Pi/Pico changes, documentation, and validation.

## Objective

An operator managing a fleet must be able to select one device at a time in the existing Operations page and inspect or operate that device using the established layout and visual language.

Place a labeled Device dropdown in the main Operations card header, beside “Wsprry Pi Operation,” using the available space between the heading and existing header utilities. On mobile, stack it beneath the heading.

Deliver working behavior through the production UI, controller, and device paths. A cosmetic selector or fixture-only demonstration is insufficient.

## Authorization and boundaries

This task authorizes narrowly necessary source, test, and documentation changes in:

- /Users/lbussy/GitHub/WsprryPi
- /Users/lbussy/GitHub/WsprryPico
- /Users/lbussy/GitHub/WsprryPi-Docs

Keep each repository’s changes and validation separately reviewable. Read its current AGENTS.md and any applicable nested instructions before working there. This explicit cross-repository authorization covers this feature only.

Preserve existing user changes. Do not stage, commit, push, publish, merge, change branches, or discard work.

Do not install software on production devices, replace running firmware or binaries, operate services, change system configuration, reboot, manipulate GPIO, or generate RF. Use hardware-free validation. Isolated test environments may use the setup and containment required by the checked-in validation recipes; that does not authorize production-host changes.

Implement the complete bounded feature, including necessary companion changes. Do not stop after one implementation phase unless blocked by a concrete dependency or materially ambiguous behavior.

Do not add unrelated refactoring, a new dashboard, all-device simultaneous monitoring, durable historical analytics, standalone Pico schedule administration, remote reboot-policy editing, or device power-management features.

## Inspect current sources first

The planning baseline was WsprryPi devel at 4c70b25b and WsprryPico devel at d503d5d0. These are historical reference points, not instructions to reset or substitute sources.

Inspect current branches, full working-tree status, staged and unstaged diffs, applicable instructions, and source revisions. Run git diff --check and git diff --cached --check.

Relevant WsprryPi starting points include:

- WsprryPi-UI/data/views/operation.php
- WsprryPi-UI/data/operation.js and operation.css
- WsprryPi-UI/data/site.js
- WsprryPi-UI/data/card_header.php and clock_and_reboot.php
- WsprryPi-UI/data/fleet-schedules.js
- WsprryPi-UI/data/views/fleet-schedules.php
- src/wtp_integration/fleet_runtime.cpp
- src/wtp_integration/fleet_schedule.cpp
- src/wtp_integration/application.cpp
- src/wtp_integration/scheduler.cpp
- src/wtp_integration/status.cpp
- docs/wtp-fleet.md
- docs/wtp-status-recovery.md
- .github/workflows/debian-non-hardware.yml

Read the relevant Pico architecture, implementation, WTP specification, shared control schema, and control-policy.json before making companion changes.

Inspect the full local status/configuration/control path. The current Operations Enable and Stop handlers act on the controller Pi; changing visible labels is not sufficient to retarget them.

Use the mandatory Impeccable skill throughout UI work. Preserve the existing Bootstrap components, typography, palette, themes, and interaction patterns. Do not create or commit incidental skill/runtime artifacts.

## 1. Selector and device identity

Show the selector when at least one remote assignment or unresolved retained remote context exists. A saved profile without an assignment does not activate this feature.

Include:

- This Pi, explicitly identified as the controller.
- Every assigned remote Pi or Pico, including paused and unavailable assignments.
- Removed outputs whose cleanup remains unresolved.

Use stable full device identity for routing and selection. Names are display values only. Disambiguate duplicate names with product or a short identity suffix, while making the full identity available for inspection.

Default to This Pi on page load. Selection is transient browser state and must not change saved configuration, the compatibility WTP endpoint, backend selection, ownership, or schedule state.

Opening Operations or changing selection must not initiate CLAIM, LOAD, ARM, recovery, clock-setting, or settings writes. Any existing background runtime activity remains independently owned by its established lifecycle.

Keep selection stable across ordinary refreshes, name changes, offline transitions, and list reordering.

If the selected assignment disappears:

- Retain its unresolved context if one exists.
- Otherwise show that the selected output was removed, disable its actions, and offer an explicit return to This Pi.
- Do not silently move action focus to another device.
- Keep this removal state visible until acknowledged, even if the last remote assignment has disappeared.

Keep catalog-only devices and standalone Pico schedule administration outside this implementation.

## 2. One panel with explicit device context

Introduce a focused presentation model or adapter between the existing status sources and the Operations panel.

This Pi uses the existing local runtime/configuration feeds and control paths. Remote selections use the controller’s fleet snapshots and assignment operations. Retained removed contexts expose observation and reconciliation only.

Keep source state separate from rendered selection. Local websocket events must continue updating local state without overwriting a remote view. Audit all existing renderers, event handlers, recovery controls, and shared globals that can affect this panel.

Associate asynchronous work with the relevant device identity and selection generation. Ignore stale responses for rendering. Do not lose pending-operation tracking simply because a view changes or a response becomes irrelevant to the visible device.

Clear or replace previous-device values immediately on selection changes; never briefly display device A’s values under device B’s name.

Freeze selection while a confirmation or mutation request is pending. Bound this freeze with the request lifecycle. After a timeout, preserve an “outcome unconfirmed” state for that device while allowing navigation; do not imply the server cancelled the operation.

Do not allow hidden local controls or retained local event handlers to execute controller actions while a remote context is displayed.

## 3. Status presentation and evidence

Before implementation, define a field mapping and state precedence table tying every displayed value to its authoritative source, identity binding, freshness rule, and unknown state.

Reuse the current state headline, detail text, control area, and summary layout. Present:

- Device name, product, verified identity, and firmware where available.
- Assignment enabled/paused state, separately from observed execution state.
- Waiting, preparing, armed, running, recovery required, failed, unavailable, or unknown as supported by evidence.
- Configured mode, frequency, repeat interval, and UTC phase.
- A confirmed pending start and countdown when supplied by the scheduler.
- The latest execution and reconciliation result, explicitly labeled historical.
- Observation age and connection availability.

Distinguish assigned settings from observed execution. If the endpoint is running a foreign or unmatched job, do not describe that job using this controller’s configured mode or frequency.

Use authoritative evidence matching the device, boot, and job to identify the selected job as running. A local “executing” phase, “scheduled” summary, ARM handoff, or elapsed countdown is not sufficient.

A running job may contain intentional RF-off intervals. Keep job execution state distinct from instantaneous output-active observations. Neither establishes measured RF output.

Keep historical completion separate from current state; a previous success must not mask a newly pending job, disconnection, or fault.

Display requested frequency and device-reported realized frequency distinctly where available. Do not relabel software realization evidence as measured frequency.

Do not invent percentage progress or an actual completion time. If only planned timing exists, label it as planned. A countdown reaching zero must not change the state to running.

Derive future occurrences from the canonical fleet scheduling logic, preserving WSPR alignment, preparation lead, consumed slots, and no-replay behavior. Do not create an independent browser recurrence implementation. Distinguish a forecast from a committed pending start.

Retain decimal-string/64-bit precision. Do not convert epoch nanoseconds or identifiers through unsafe JavaScript Number arithmetic.

## 4. Freshness and polling

Browser refresh and device observation are different events. Display the age of the authoritative device observation, not the age of the latest HTTP response or worker heartbeat.

Define and test a stale threshold appropriate to the actual device polling cadence and bounded transport latency. Document it as an implementation policy; do not add a user configuration setting for it.

Age must continue increasing locally when refreshes fail. Use a monotonic browser elapsed-time source and account conservatively for request latency.

Handle controller process restart, monotonic-clock discontinuity, device reboot, identity change, revision reset, and reconnect without reusing invalid observation baselines or pending-action results. Add minimal controller-incarnation metadata if existing evidence cannot distinguish these cases reliably.

Use bounded polling, initially consistent with the existing approximately three-second fleet UI refresh. Prevent overlapping polls, retry storms, and unbounded background browser work.

GET handlers must copy published snapshots rather than perform device transport operations.

Where required, integrate idle observation into the owning fleet runtime path. The existing WtpApplication::poll_idle() provides bounded, read-only STATUS observation for healthy idle sessions. Verify its current contract before using it.

Do not reconnect, clear fault/identity latches, recover uncertain work, claim ownership, or resubmit jobs as a side effect of observation. Disconnected or unresolved contexts retain their existing explicit recovery requirements.

Poll devices through existing serialized ownership of each transport. Do not create additional competing sessions for the selected view. One slow device must not block the entire fleet snapshot or other outputs.

Observation work must remain bounded for 32 outputs and must not multiply with the number of browsers.

## 5. Controls and action completion

Use this mapping:

| Context | Operation |
| --- | --- |
| This Pi | Preserve existing local Enable, Stop, recovery, and reboot-behavior semantics |
| Assigned remote output | Resume schedule through fleet enable |
| Assigned remote output | Pause and stop through fleet pause |
| Eligible remote context | Reconcile through fleet recover |
| Assigned remote output | Open its existing Fleet schedule editor |
| Removed unresolved context | Reconciliation only |

Use accurate remote labels such as Schedule enabled, Resume schedule, and Pause and stop. Do not present remote assignment controls as editing a target’s standalone Enable or reboot policy.

Retain the existing confirmation behavior for remote Pause and stop, naming the selected device and explaining the effect. Cancellation or dismissal performs no mutation.

Reuse existing authenticated/request-guarded controller APIs, ETags, identity validation, ownership checks, and admission policies. The browser must not contact arbitrary device endpoints directly.

Capture the target identity and reviewed revision when an action begins. Never resolve the target again from whatever happens to be selected when the response arrives.

An accepted pause request is not confirmed output-off. Track request acceptance, execution progress, and confirmed or uncertain outcome separately. Correlate results with the particular action and relevant device/boot/job; a previous successful reconciliation must not satisfy a new request. Add minimal operation correlation/status fields if needed.

Freshness or uncertainty must prevent unsafe start/resume decisions. Do not indiscriminately disable Pause and stop or reconciliation solely because observation is stale; expose them where existing backend authority permits the operation, with truthful pending and uncertain feedback.

After a timeout, transport failure, or revision conflict, refresh authoritative state and require explicit review before another mutation. Do not blindly retry a mutating request. A server operation may have succeeded despite a lost response.

Successful reconciliation leaves the assignment paused. Resuming schedules future eligible slots; it does not replay interrupted work.

Do not stop foreign-owned jobs, automatically take over a target, clear safety faults, or treat lost connectivity as output-off.

## 6. Controller-specific UI

Keep the global Controller connected indicator about the connection to the controller. Display selected-device availability separately.

While a remote device is selected:

- Hide the controller’s reboot/shutdown buttons on Operations.
- Hide the local reboot-behavior editor and local takeover controls.
- Hide controller-only GPIO correction or backend details unless equivalent selected-device evidence is explicitly supported.
- Label any retained clocks so they cannot be mistaken for device clock qualification.

Apply header changes only where this feature requires them; shared card headers on other pages must retain their intended behavior.

Preserve the existing local Operations behavior when This Pi is selected and when no fleet exists, including the existing single-selected-WTP-backend route.

## 7. Pi/Pico coordination

Prefer existing WTP STATUS, identity, capabilities, timing, and job evidence. No Pico firmware change is presumed necessary solely to add the selector.

Verify the complete field/action mapping against actual Pi and Pico producers over USB and supported network carriers. If a concrete gap requires companion changes, implement the smallest coordinated solution within this task.

For any change to protocol/API schemas, capabilities, descriptors, carrier support, units, session policy, or protocol-facing interfaces:

- Update producer, all affected consumers, strict validators, serializers, generated assets, test adapters, and documentation together.
- Validate document shape before field access.
- Reject unsupported versions/carriers, missing or extra fields, invalid types, contradictory descriptors, and identity/boot/profile/revision mismatches before mutation.
- Follow the current authoritative control-policy.json. Under its prerelease policy, retain control major 1, its identifier, and namespace while evolving the one current contract.
- Keep WTP, transport/authentication, and storage versions independent.
- Do not add historical views, permissive version ranges, validation bypasses, older-peer substitutions, or hidden carrier fallback.

Identify any necessary reusable-component change, explain why it belongs there, and run its standalone tests. Broader component refactoring is outside scope.

## 8. Validation

Inspect test recipes before execution. Use the repository’s existing infrastructure and hardware-free backends.

Add meaningful behavioral coverage for:

- No fleet, one remote output, 32 outputs, duplicate names, and unresolved removed contexts.
- Rapid selection changes, out-of-order responses, local websocket updates during remote viewing, and page reload.
- Selected-device removal, renaming, endpoint substitution, controller restart, and device reboot.
- Every supported state, stale/unknown observations, foreign ownership, missed starts, faults, and uncertain cleanup.
- Correct action routing: actions for A cannot mutate B or the controller.
- Confirmation cancellation, concurrent editors, ETag conflicts, pending actions, lost responses, readback, and explicit retry.
- Pending pause/recovery results that cannot inherit historical success.
- Correct scheduling/countdown semantics and 64-bit values.
- Bounded polling across 32 outputs, multiple browsers, and a slow or disconnected endpoint.
- Viewing/selection causing no settings writes, CLAIM, LOAD, or ARM.
- Preservation of existing local Operations and Fleet Setup workflows.

Use relevant current targets from src, including wtp-fleet-test, wtp-fleet-capacity-test, wtp-status-test, and wtp-application-test, plus affected API, component, UI, and source-regression coverage.

Choose the semantics profile explicitly:

- On suitable Linux hosts: WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
- On macOS or other portable-only hosts: make semantics-test-portable SUDO=

Portable coverage is a subset and does not replace required Debian integration.

Both actual production integration suites are completion gates:

1. Follow .github/workflows/debian-non-hardware.yml’s wtp-network-loopback recipe, including wtp-network-runtime-test, wtp-network-interop-test, and wtp-config-interop-test.
2. Follow its wtp-usb-management-pair recipe, building the actual Pico usb_management_driver, running wtp-usb-pair-client-build and wtp-usb-management-test, and running WsprryPi-UI/tests/wtp_usb_pair_test.js through the production Pi/Pico pair.

Use explicitly selected intended candidate sources and pinned dependencies. Include all relevant uncommitted changes in the tested candidate and record their provenance. Do not test an unchanged checkout while claiming coverage for edited sources.

Existing suite success alone is insufficient if it never exercises this feature. Extend production-path coverage to exercise selection, fleet status, and selected-device actions through the real controller coordinator/client and actual Pico handlers over network and USB. Include a Pi target using its hardware-free backend and demonstrate isolation between at least two outputs. Use deterministic fixtures for the 32-output scale cases.

Do not replace production-path tests with mocks. Failed, timed-out, cancelled, or skipped required checks remain unresolved.

Run Impeccable review and render the actual interface at desktop and mobile sizes in light and dark themes. Review long names, selector wrapping, keyboard/focus behavior, status announcements, loading, offline/stale states, confirmations, and uncertain outcomes. Confirm that the selected target remains obvious and rapid updates do not overwhelm assistive technology.

Read docs/simulated-backend.md before application-level simulated transmission tests. Hardware-free evidence does not qualify installation, physical transport, services, timing, GPIO, or RF.

## 9. Documentation

Update current operator behavior and limitations, including:

- When the selector appears and which devices it includes.
- Switching views without changing configuration or output.
- Local versus remote controls.
- Schedule state versus observed job/output state.
- Freshness, offline behavior, uncertain outcomes, reconciliation, and removal.
- Controller connection/power controls versus selected-device status.

Relevant operator documentation in WsprryPi-Docs includes:

- docs/User_Interface/Operations/index.md
- docs/User_Interface/Setup/index.md and relevant Fleet sections
- docs/Advanced_Operations/configuration_troubleshooting.md
- docs/Advanced_Operations/rest_api.md when its contract changes

Update WsprryPi’s engineering fleet/status documentation and any affected Pico contract documentation.

Use the documentation repository’s prescribed virtual environment and build workflow. Run applicable strict build, link/reference, and extension checks. Render affected pages and review them with Impeccable at desktop/mobile sizes. Replace screenshots only where materially inaccurate.

Keep operator prose focused on capabilities, actions, and limitations. Do not insert development diaries or test implementation details.

## 10. Adversarial review and completion

After implementation, assess the actual diff and evidence adversarially. Trace wrong-device mutations, stale replies, misleading running/output-off claims, ownership transitions, controller/device restarts, uncertain actions, polling contention, and schema mismatch before writes.

Fix actionable findings, rerun affected checks, and reassess until no actionable findings remain. Do not weaken validation or redefine scope to obtain a clean result. If blocked, report the exact blocker and unfinished acceptance gate instead of claiming completion.

Finish with:

- Implemented behavior and important files changed, grouped by repository/component.
- Exact source revisions and tested uncommitted changes.
- Exact test commands/results and relevant CI links where available.
- UI workflows and rendered evidence reviewed.
- A Documentation Impact section listing updated, considered-but-unchanged, and still-required documentation.
- Any remaining runtime or hardware qualification.
- Current branch and working-tree status for each repository.
- Explicit confirmation that nothing was committed, pushed, deployed, or operated on live hardware.

Do not claim readiness or validation beyond the evidence obtained.
