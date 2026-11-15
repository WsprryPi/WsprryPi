# Shared device setup and device-owned solar scheduling

Status: implementation plan, not implemented or qualified. Updated 2026-11-15.

Feature tracking: [WsprryPi #473 — Shared device setup and device-owned solar
scheduling](https://github.com/WsprryPi/WsprryPi/issues/473). This document is the
authoritative implementation plan and execution prompt; the issue tracks its
ordered slices and delivery evidence. Keep both references synchronized. Neither
the issue nor this plan authorizes beginning implementation.

See the [brief phase list and individual execution prompts](issue-473-phases.md).
The prompts expand each slice; this plan remains authoritative.

Phase-1 decisions D01–D12 were approved on 2026-11-15. The
[decision register](issue-473/phase-1-decisions.md),
[automatic pairing workflow](issue-473/phase-1-pairing.md), and
[normative contract details](issue-473/phase-1-contract-details.md) specify their
approved design. D02 requires device-managed UI pairing with automatic keys and
ownership replacement; no manual certificates. D07 automates known-authority
retirement/reconciliation, leaves offline/unknown migrations pending, and preserves
existing WTP claims when local Transmit is off. Management replacement and RF
ownership remain independent. These decisions authorize no phase-2 implementation.

## Objective and authority

Provide shared Setup controls for local Pis, remote Pis, and Picos. Each target
stores and executes its own recurring schedule. The controller provides device
discovery, authorized management, and observations; the browser is an editor.
Neither evaluates remote solar schedules nor dispatches their occurrences through
the existing Fleet recurrence runtime. Shared controls mean shared presentation
and explicit target adapters, not shared controller-side execution.

The user selected solar day/night behavior rather than fixed AM/PM clock windows.
The user also authorized concomitant changes in the sibling WsprryPi-Docs
repository. This document authorizes no implementation by itself. Implement only
the slices subsequently authorized. Companion Pico changes, if required, need
explicit repository authority before implementation; missing authority blocks
the affected slice rather than permitting an incomplete compatibility claim.

This plan does not authorize live transmission, hardware operation, installation,
service changes, new deployments, or publishing releases. Application, companion,
and operator-documentation commits and pushes remain separate boundaries.

## Inspected baseline

Planning references: Pi `e27354eb`, Pico `d65df2aa`, both on local `devel` when
inspected. Reinspect branches, diffs, nested instructions, contracts, and source
revisions at implementation time; these hashes are evidence pointers, not
permanent dependency pins.

- `WsprryPi-UI/data/views/config.php`: Setup tabs and local autosave form.
- `WsprryPi-UI/data/views/wtp-controls.php`: Known and nearby devices; browsing
  explicitly differs from Use this device, which changes the active connection.
- `WsprryPi-UI/data/wtp-management.js`, `gps.js`, and `gray-line.js`: existing
  Pico management, GPS observations, and standalone solar controls.
- `src/config_types.hpp`, `config_handler*`, `runtime_config_bridge*`,
  `scheduling.cpp`, `scheduling_runtime.cpp`, `scheduling_config.cpp`, and
  `scheduling_websocket_status.cpp`: Pi configuration and execution lifecycle.
- `src/wtp_integration/{browser_api,configuration,fleet_runtime,fleet_store,
  fleet_schedule}.*`, `src/wtp_pi_control.*`, and `src/wtp_runtime_bridge.cpp`:
  selected-member management, Fleet recurrence, and ownership boundaries.
- Pico `src/standalone/{gray_line.hpp,scheduler.cpp}`,
  `src/time/gps_calibration.cpp`, `src/hardware/gps_settings.hpp`, and
  `engineering/development/standalone.md`: solar calculation and admission rules.
- `docs/simulated-backend.md`, `.github/workflows/debian-non-hardware.yml`,
  `WsprryPi-UI/DESIGN.md`, and sibling Docs `AGENTS.md`: validation and UI rules.

GPS is implemented on Pico and exposed through Pi management. A general native
Pi GPS location provider feeding Pi recurrence was not found. Existing Pico
daylight policy is an always/day/night gate, not arbitrary independent day/night
frequency profiles. Its gray-line override has a single WSPR dial and message
frequency; Pi frequency lists and selector metadata must not be reduced to that
shape. Do not equate GPIO identifiers named `gps` with GPS receiver support.

## Product contracts

### Device context and shared controls

Setup order: Signal Setup, Scheduling, Transmitter, Device I/O, Fleet.

Place one labeled Device selector to the right of the tabs, outside the ARIA
tablist and configuration form submission. On narrow screens, wrap it onto its
own row above the active pane. Keep target name, type, identity detail, connection,
and read/save/activation state visible. Reuse the known/nearby catalog and
discovery infrastructure; do not reuse backend-switching side effects.

Selection is observation/edit context only. It never enrolls a discovery result,
switches a transmitter backend, claims an output, enables a schedule, writes a
configuration, or starts a job. Only verified and explicitly enrolled targets
are writable. Nearby/unidentified entries lead to identification/enrollment.

Bind every read, draft, write, action, and reply to stable device identity,
profile/connection generation, boot/session context where applicable, and
configuration revision. Keep drafts by target. Retire in-flight context on switch;
ignore late replies. A write already accepted on the old target remains tracked
there, never retargeted. An uncertain outcome requires authoritative readback
before another write. Reconnection and reboot require fresh negotiation.

Local and remote edits use the same validation/presentation components with
target adapters. Never map remote writes onto the existing local autosave form.
Respect explicit save/apply/restart semantics; show them rather than forcing
identical persistence behavior. Controller-scoped actions remain visibly labeled
as controller actions; target power/restart controls require supported target
capabilities and explicit target-bound intent.

### Schedule ownership and enablement

Each target owns durable configuration, admission, recurrence progress, and
runtime status. Its schedule continues when the browser or management controller
disconnects. Ownership and connection policies must allow that independence.
Management reads must not claim RF or keep autonomous scheduling blocked.

The per-target Enable scheduling switch permits new recurring occurrences. Off
admits no new occurrence; an accepted complete occurrence may finish. Immediate
stop remains a distinct action. Disable dependent scheduling fields semantically
and visually while preserving their values. Keep target selection and the switch
usable when the target is writable; explain unavailable permissions/capabilities.

This switch is distinct from transmit permission, output ownership, Enable on
Boot, and safety inhibition. Turning it off must not implicitly offer the output
to WTP clients. A waiting solar window must not release local ownership either.
Define and test the complete state table before runtime implementation. Existing
Transmit and boot behavior remain authoritative; solar configuration saves never
implicitly turn them on. Defaults and migration preserve existing recurrence.
Restart cannot resurrect disabled work or replay missed occurrences. Persist only
the recurrence watermarks needed for duplicate prevention, with bounded writes
and explicit crash-consistency rules; do not flash-write every scheduler poll.

Manual/one-shot requests, test tones, and externally claimed WTP jobs keep their
existing authority and safety rules. Solar policies apply to device-owned
recurrence, including the existing Pi schedule when its configured output is WTP;
they must not silently turn that legacy output selection into a native remote
schedule. Show the execution owner and destination explicitly in that case.

### Solar policy

For Pi-native recurrence, feature-disabled behavior is exactly the current
behavior. When enabled, select a qualifying morning/evening gray-line override
first; otherwise select day/night policy. Day and night independently allow/skip
occurrences and may override frequencies. Disallowing both ordinary periods
allows gray-line-only operation. No mode/message/timing profile expansion is
included: existing recurrence cadence, messages, and modes remain authoritative.

Use Pico's geometric solar-center calculation and provenance. Day is elevation
at least -0.833 degrees. Gray-line defaults are -6 to +6 degrees, lower inclusive
and upper exclusive, represented as integer millidegrees within -90000..90000
with lower strictly less than upper. Use unrounded elevation for comparisons.
Solar hour angle determines morning/evening, including noon/midnight and polar
conditions. Report prolonged windows without promising propagation or inventing
sunrise events. No timezone, DST, Internet, or browser dependency is required.

Blank frequency overrides inherit saved mode settings. Pi WSPR overrides preserve
lists, explicit skip entries, selection metadata, and qualified frequency policy.
Each explicit period list has independent rotation; inherited periods share the
ordinary list progression. Refused admissions do not advance rotation; explicit
skip entries advance once at their normal scheduled slot. A paired WSPR occurrence
advances once, not once per frame. Define restart continuation consistently with
the durable progress policy. CW overrides use Pi's current base-frequency and
shift model, with every derived tone checked before persistence and admission.
Changed overrides, including those in disabled periods, must pass current policy.
Retain existing well-formed but newly unsupported saved values where the current
configuration contract permits, so an operator can disable or repair them without
silently rewriting custom settings. Such retention never authorizes admission;
every actual transmitted tone must pass the current backend and frequency guards.

One configuration/location snapshot evaluates intended occurrence starts before
selector preparation, ownership acquisition, or job commitment. Both starts in a
paired WSPR occurrence must select the same policy and allowed frozen intent.
Refuse the complete pair otherwise. Once accepted, solar-boundary changes do not
interrupt it; existing cancellation, reload, ownership, and launch safety checks
remain effective. Validate policy/clock/configuration generations at commitment
so stale prepared work cannot launch. Recompute future work after clock steps or
configuration changes without catch-up bursts or duplicate admissions. A policy
refusal is normal waiting, not a fatal CW setup failure or daemon shutdown.

### Location, time, and capabilities

Solar geometry uses the transmitter site's location, never an implicit controller
or browser location. Saved locator center is a supported baseline. GPS is offered
only where the target has a supported provider. Receiver ingestion for native Pi
GPS is not assumed or silently added. A separate provider decision is required
before promising native Pi GPS support.

For existing Pico GPS, retain its target-owned receiver/freshness contract. A Pi
may use a GPS provider only after defining acquisition, source identity, site
association, timestamp/age limits, and restart/loss handling. Do not depend on the
UI controller to refresh location for a supposedly autonomous remote schedule.
No silent GPS-to-manual fallback is permitted. Missing required time/location
holds new occurrences, including ordinary fallback when solar evaluation is
required. Disabling solar policy removes only its added prerequisites.

Define target-local UTC readiness separately from RF frequency calibration and
PPS availability. Document acceptable clock health, loss/holdover, clock steps,
and supported date range; Pico's 2025..2099 calculation range is not an accidental
unbounded Pi contract. Solar previews and next-start forecasts are observations,
not execution evidence. Forecasting must be bounded and may report unknown.

Pico adapters expose current capabilities honestly. Do not simulate missing
day/night profiles by controller-side writes at sunrise. A common UI may show an
unsupported setting with an explanation. Achieving identical new profiles on
Pico requires a separately authorized coordinated firmware slice.

The generic enable switch must not falsely combine Pico WSPR and message
recurrence. The adapter must identify the affected schedule and expose any
separate enable/pause states. A unified atomic master requires a supported target
contract and coordinated firmware implementation; otherwise show clearly labeled
per-schedule controls. Unsupported or unverified mappings remain read-only. Never
issue multiple non-atomic writes while presenting a single confirmed master state.

## Ordered implementation slices

Each slice requires source review, appropriate tests, documentation impact review,
and explicit remaining limitations before proceeding. Protocol-producing and
consuming changes must be integrated together, even if developed incrementally.

### 1. Contract, security, migration, and capability decisions

- Inventory all local/remote producers, consumers, current save/apply rules,
  ownership states, storage formats, and selected-member coupling.
- Define the target-management API, strict schemas, identity/revision checks,
  resource limits, readback, atomic writes, and idempotent recovery after lost
  replies. Ordinary WTP execution does not authorize arbitrary management.
- Reuse existing authenticated transports where suitable. Define remote Pi
  provisioning and credential storage, authorization, browser origin/CSRF policy,
  address/redirect restrictions for a management proxy, timeouts, and secret
  redaction. Plain-LAN WTP consent does not grant configuration-write permission.
- Resolve scheduling/Transmit/boot/ownership state tables, UTC readiness, storage
  defaults, recurrence progress, native Pi GPS scope, and Pico capability mapping.
- Define legacy-assignment transition below before allowing new schedule enables.

Gate: decisions affecting behavior/security are recorded and approved. Do not
invent unresolved semantics during later UI or scheduler implementation.

### 2. Target management and controller-only separation

- Add bounded, identity-bound management adapters for local Pi, remote Pi, and
  existing Pico resources. Remote Pi reads/writes reach services on that Pi;
  background UI selection cannot mutate the host configuration.
- Decouple selecting/managing a Pico from choosing it as the host's WTP backend.
  Preserve active sessions, claims, and jobs across editing-context changes.
- Make controller/UI operation explicit without local hardware initialization,
  local station identity, local backend, or a fabricated local output. Never infer
  controller-only mode as a fallback after failed physical initialization.
- Establish readiness and admission inhibition until target configuration and
  ownership state are loaded and validated at startup.

Gate: production-path local/remote management tests pass, including a controller
with zero local outputs and multiple independent targets; no recurrence dispatch
is added to Fleet runtime.

### 3. Shared Setup context and existing-control migration

- Add the shared selector and context lifecycle described above.
- Reuse existing Signal Setup, transmitter, and I/O components through adapters;
  rename Pi I/O to Device I/O and gate fields by actual capability.
- Relocate device settings from Fleet into shared tabs without leaving competing
  editors. Fleet retains enrollment, discovery, profiles, and fleet overview.
- Follow Impeccable and incumbent design. Keep current units, precision, custom
  values, save/activation distinctions, and clear target-specific feedback.

Gate: desktop/mobile, light/dark, keyboard, loading/error, uncertain save, draft
switching, disconnected target, and delayed-response reviews pass. Selecting a
device cannot redirect an active backend, stop work, or persist settings.

### 4. Pi solar evaluator and complete configuration lifecycle

- Add a pure Pi-owned solar/policy evaluator with injected UTC/location/config
  inputs; preserve Pico attribution and source reference without coupling a
  reusable component to application internals.
- Add defaults, parsing, strict validation, structures, atomic persistence,
  serialization, API adapters, and reload propagation together.
- Preserve existing configurations and unknown-version refusal. Do not silently
  normalize custom frequencies or apply Pico engine limits to Pi backends.

Gate: deterministic geometry, precedence, malformed input, precision, frequency,
round-trip, overflow, and failed-write tests pass; feature-off behavior matches
the baseline. Parent-owned code is preferred; reusable component changes require
the repository's separate scope approval and standalone tests.

### 5. Legacy-authority interlock and explicit transition

- Implement the authority interlock before exposing any new schedule enable.
  Retain existing assignments and recovery journals until safe retirement.
  Do not bulk-convert schedules on startup or discovery, and do not delete assets
  needed to reconcile uncertain work.
- Require explicit review to migrate an assignment: inhibit new controller
  dispatch, reconcile pending/accepted jobs and ownership, confirm retirement,
  then save the target schedule disabled and verify it before explicit enable.
- Use the approved D07 generation/retirement design to prevent automatic restart
  of a retired saved assignment after migration, without changing WTP admission.
  An offline previous controller or unknown outstanding work blocks enablement
  until its authority is positively retired; merely hiding the old assignment is insufficient. A clean target's
  authoritative ownership record must establish that no legacy retirement is
  required; an empty catalog in the current browser is not proof.
- Partial failure leaves the new schedule disabled with a recoverable record.
  Rollback verifies the target schedule is disabled and reconciled before the
  old authority can resume. Support clean new installations without legacy data.

Gate: crash/restart tests at each handover step prove no overlapping authorities,
no silent schedule enablement, and durable recovery. Existing legacy runtime may
remain for unmigrated work; it receives no new solar scheduling implementation.
New assignments use target-owned scheduling only. Target-local admission enforces
the interlock independently of the UI and current controller process.

### 6. Pi device-owned recurrence and ownership integration

- Integrate admission into existing WSPR and repeating QRSS/FSKCW/DFCW paths.
  Preserve pair planning, random offsets, mode timing, selector metadata, backend
  restrictions, ownership, cancellation, and startup/reload safety.
- Add the independent enable state, progress policy, ordinary waiting paths,
  and clock/configuration-generation fencing. Solar holds must wake/recompute
  through bounded existing scheduling mechanisms rather than busy loops.
- Expose saved/active policy, selection, intended next start or unknown, waiting
  reason, selected frequencies, prolonged window, and observation age. Existing
  runtime execution evidence remains distinct from prediction.

Gate: actual application paths pass accelerated and bounded real-time simulation
for admission, pair refusal, shifts, rotation, skips, lifecycle failures, clock
steps, restarts, disable/enable, cancellation, and no duplicate/catch-up work.

### 7. Shared Scheduling tab and native remote operation

- Add Scheduling between Signal Setup and Transmitter, with per-target Enable
  scheduling, solar controls, explicit units, location, inheritance, and status.
- Keep schedule enablement distinct from whether solar selection is used, so
  ordinary existing schedules can run without solar prerequisites.
- Save to the selected target, read back confirmed persistence, and display any
  pending application. Dither disabled fields without losing saved data.
- Verify remote Pi scheduling after browser closure, controller shutdown, and
  management reconnection using isolated software processes, not live hardware.
- Test two targets at different sites using target-local clocks and location.
  Controller timezone, station identity, or location cannot leak into either.

Gate: local and remote Pis use the same controls and target-owned evaluator;
Pico remains accurately capability-gated. Slice 5 authority fencing is enforced
through production APIs, including direct requests bypassing the UI. No
controller-managed solar recurrence or sunrise-triggered configuration writes exist.
Enabling and saving new entries must be one validated transaction or an explicit
save-disabled/readback/enable sequence. Never enable last-saved values as a side
effect of enabling edit fields for an unsaved draft. Capability loss or a failed
write preserves the draft and the last confirmed enable state as separate values.

### 8. Documentation, integration, and closeout

Update sibling WsprryPi-Docs concomitantly with applicable slices, presenting only
implemented behavior. Follow its AGENTS.md; keep separate Git boundaries.
Review/update:

- `docs/User_Interface/Setup/index.md`, Signal_Setup WSPR/CW guides, Transmitter,
  Pi_IO material (including Device I/O naming/link effects), and Setup screenshots.
- `docs/User_Interface/Operations/index.md`, `first_transmission.md`, and relevant
  Fleet/device-selection guidance.
- `docs/Advanced_Operations/ini_configuration/` and configuration troubleshooting:
  new settings, defaults, boot/Transmit semantics, time/location holds, remote
  management, controller-only operation, and legacy authority migration.
- Engineering protocol/security/provider contracts in their owning repositories.

Activate `docs/.venv` and run `python -m sphinx -b html docs docs/_build/html` in
the Docs repository; review warnings, links, rendered desktop/mobile pages, and
materially changed screenshots. Use Impeccable for affected rendered UI docs.

Gate: all required checks below pass for intended candidates; no remaining
implementation or documentation blocker is concealed by a UI completion claim.

## Validation and completion evidence

Inspect target recipes before running. Tests are unprivileged and hardware-free;
never infer RF or installation qualification from them.

- Pure tests: exact inclusion bounds, noon/midnight, hemispheres, longitude/date
  rollover, leap years, poles, supported date limits, nonfinite inputs, invalid
  locators, stale/future GPS timestamps, and deterministic Pico reference vectors.
- Application tests: every enabled/disabled/held/owned/boot combination, paired
  frame snapshots, complete occurrence freezing, frequency/shift limits, selector
  sequencing, reload races, sleep/resume and clock steps, persistence failure,
  restart watermarks, and explicit skip/list rotation semantics.
- Management tests: two devices, wrong/changed identity, boot/profile/revision,
  unsupported capabilities/carriers/versions, extra/missing/wrong-type fields,
  refusal before writes, authentication failures, lost replies, bounded retries,
  credential redaction, and configuration isolation.
- UI tests: shared target context, drafts, unsupported Pico fields, disabled
  scheduling, target action labels, stale replies, no-local-output state, and
  desktop/mobile/light/dark screenshots using production interfaces.
- Run focused Makefile targets as applicable, including
  `non-wspr-repeat-policy-test`, `wspr-plan-placement-test`,
  `band-gpio-rotation-test`, `selector-shutdown-cleanup-test`, and current UI/source
  regressions. Add focused targets for the new behavior rather than copy-only tests.
- From Pi `src`, macOS uses `make semantics-test-portable SUDO=`; qualified Linux
  uses `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. The portable
  subset is not evidence that the full profile passed.
- For shared interface changes, run both complete checked-in Debian recipes:
  `wtp-network-loopback` and `wtp-usb-management-pair`. Network includes
  `wtp-network-runtime-test`, `wtp-network-interop-test`, and
  `wtp-config-interop-test`. USB includes the actual Pico `usb_management_driver`,
  `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and
  `WsprryPi-UI/tests/wtp_usb_pair_test.js`. Select candidate Pico sources and all
  pinned dependencies explicitly. Preserve production crypto, framing/ACK,
  serialization, and journal coverage. Mocks and stale green CI do not suffice.
- Keep one current shared settings/control view. Before implementation, inspect
  the authoritative Pico control policy for whether the first stable transition
  has occurred. Follow its major-version rules; never relax validation, add hidden
  downgrades, or conflate WTP/transport/storage versions with control versions.

Report exact source revisions and dirty changes, commands/results, visual evidence,
documentation builds, relevant CI links, unrun checks, components modified, and
each repository's Git state. Companion changes must exist on the intended paired
refs. Failed/cancelled/skipped required integration checks remain open.

The requested no-CI commit applies to saving this plan only. It does not waive
implementation validation. No physical USB, target timing, GPIO, RF, installation,
service lifecycle, or deployment claim is supported by this plan.

## Adversarial review disposition

Review the concrete contracts above after every revision. Stop implementation at
the first unmet slice gate. Transport/security selection, clock readiness,
provider choice, storage progress, ownership, and authority fencing must be
resolved in slice 1; they are explicit prerequisites, not permission to invent
fallbacks. A plan may sequence design decisions without claiming those designs
already exist. Completion requires both shipped behavior and supporting evidence.

Plan review passes completed:

1. Corrected selection/backend coupling, schedule/ownership conflation, overstated
   Pico parity, and legacy duplicate-authority risks.
2. Moved authority fencing ahead of enablement, made enforcement target-local,
   and specified honest Pico WSPR/message enable mapping instead of a false master.
3. Added preservation of unsupported saved values without allowing their use,
   and transactional enable/save semantics that cannot launch stale saved entries.

The phase-1 decision register records the approved design and its final design
assessment. Later phases must revalidate those contracts against their candidate
sources. Design approval is not implementation, security, or runtime qualification.
