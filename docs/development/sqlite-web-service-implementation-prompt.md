# SQLite and separate web service implementation prompt

## Objective and authorization

Implement SQLite-backed persistence and a separate browser-facing WsprryPi service while preserving normal hybrid Pi operation, current Fleet management, device-owned scheduling, and existing safety and authorization behavior. Retire ongoing INI use after one-time migration. Provide a local command for advanced settings, coordinated backup/restore through both Maintenance and the CLI, downloadable encrypted backups, and updated diagnostic packet generation.

This is an implementation prompt, not evidence that these features exist. Its presence or TODO link does not authorize execution. When explicitly invoked, implement the authorized software scope below. If the invocation selects a slice, stop at that slice; if it authorizes the entire prompt, proceed through the slices without inventing additional approval steps. Do not implement unrelated TODO items.

Do not stage, commit, push, publish, or create a pull request. Preserve unrelated tracked, untracked, and ignored work. Target-device access, installation, installed-service mutation, installed host/application configuration changes, hardware operation, transmissions, and Pi resource measurements require separate explicit authorization. Synthetic configuration/state changes and isolated process/service-adapter fixtures are permitted for software validation; they do not authorize operating installed services or hardware.

## Start from current authority

Read the applicable [repository instructions](../../AGENTS.md), nested instructions, [simulation contract](../simulated-backend.md), [development guide](README.md), and [canonical software validation workflow](../../.github/workflows/debian-non-hardware.yml). Inspect the current branch, revision, complete working tree, staged and unstaged diffs, and affected component boundaries before editing. Run `git diff --check` and `git diff --cached --check`.

This prompt was authored against `devel` at `e4f8c2f364638a57acbc8cb1e7994ffd40269b5a`. Treat that as provenance, not a ref to check out or overwrite. Reinspect the actual candidate; do not assume the source or working tree still matches this snapshot. The pre-existing feasibility prompt is a separate experiment and must not be overwritten or executed as production implementation instructions.

Read the current [controller-only plan](controller-only-linux-plan.md) and [issue #475 implementation prompt](issue-475-implementation-prompt.md). Use the accepted #475 platform baseline where available. Do not implement missing platform support under this prompt or make #475 depend on unused SQLite infrastructure. Continue independent work when a dependency is unavailable, and identify the exact remaining dependency instead of claiming full completion.

The decisions in this prompt replace the older plan's ongoing INI requirement and extend its scope only for the backup/restore and computer-migration workflows specified here. Other #475 role, platform, identity, ownership, and qualification requirements remain applicable.

## Required architecture

```text
Browser
  |
Apache: existing PHP/HTML/JS/CSS and HTTP/WebSocket proxying
  |
WsprryPi web-service process
  |
Authenticated local IPC
  |
WsprryPi backend
  +-- private SQLite persistence
  +-- local transmitter and native scheduler, on normal Pi installations
  +-- Fleet management and device connections

Local settings/backup commands ---- local IPC ---- backend

Explicit restore ---- narrow maintenance worker ---- stopped backend state
```

Keep one application and executable name, `wsprrypi`, with explicit entry points. Ordinary Pi backend startup owns local transmission and Fleet management together. Preserve `--controller-only` for operation without local output. Add an explicit web-service entry point and local settings/backup/restore commands. Internal maintenance dispatch may use the same executable. Exact new command spelling is an implementation detail to document and test, not an already supported interface.

Dispatch non-transmitter entry points before hardware probing, station validation, output initialization, or backend identity provisioning. Entry points must have closed option validation; refuse incompatible role/output options rather than ignoring them. The dedicated controller build compiles no physical or simulated local output backend. Omitting its required backend role flag must not attempt transmitter initialization; help/version and explicit non-transmitter entry points remain usable.

Do not add a hybrid flag, public hybrid role, second controller backend, or automatic controller/simulator fallback. Hardware initialization failure remains a failure. A normal Pi remains able to transmit locally and manage remote devices through the same backend, subject to existing admission and busy checks.

The backend normally owns all database access. The web process, Apache/PHP, and ordinary CLI clients must not open the database, WAL, private key stores, or journals. A narrowly scoped maintenance worker is the explicit exception for admitted restore work while the backend is stopped. Use OS identities and permissions to enforce these boundaries; sharing an executable does not imply sharing process privileges, and the executable must not become setuid.

The database is not a command queue. Use IPC for commands and preserve durable receipts, reservations, reconciliation, and uncertain outcomes where external effects require them. SQLite cannot atomically commit a radio or remote-device effect.

## Slice 1: map ownership and freeze the implementation contract

Inspect the complete configuration lifecycle: defaults, parsers, typed configuration, validation, serialization, UI population/save behavior, persistence, scheduling, runtime consumers, recovery, tests, and installation. Locate the current managed-store, journal, catalog, pairing, recurrence/progress, browser-session, and support-job owners by their production symbols. Inspect companion consumers before changing any shared interface.

Create a concise engineering ownership map within the affected maintained documentation. For each persistent record, identify its current owner, namespace, validation, revision/identity binding, transaction participants, migration input, destination, backup inclusion, restore behavior, and whether it contains secrets. This is a bounded inventory of existing application state, not a new storage framework or generic audit system.

SQLite becomes authoritative for supported local settings, the covered Fleet catalog and authority records, operation receipts, and their coordinated recovery records. Preserve records that currently recover together in one durable decision. Do not migrate only a UI snapshot or settings file while leaving its authority/receipt decision inconsistent.

Keep independent pairing/key stores, journals, and execution-progress owners under their current responsibilities unless inclusion in an existing coordinated decision requires a specific adapter. Do not globally replace a reusable journal class used by independent owners. Preserve record payload contracts where storage replacement does not require semantic change. Maintain one live Fleet catalog owner in the backend, with explicit transaction participation rather than nested independent commits.

Controller-only adapters must not instantiate local transmitter settings ownership, require callsign/grid, or create local recurrence merely to access Fleet storage. Empty catalogs and offline targets remain normal states.

Identify all ongoing INI consumers, including installer/update merging and ownership, service arguments, network safety, route maintenance, logging helpers, and support collection. Decouple independently owned sidecar paths from the retired filename without incidental policy or logging redesign. Separate saved settings, process options, generated platform observations, and transient counters.

Do not introduce obsolete unreleased settings into the database, migration contract, backup inventory, or recovery behavior. Unknown fields must not become supported settings by being copied into an unrestricted JSON container.

Slice result: an exact affected-path/owner map, storage and IPC contracts, platform dependency list, and focused validation plan sufficient to implement the following slices without unresolved ownership assumptions.

## Slice 2: SQLite and one-time migration

Use the SQLite C API with a small application-owned persistence layer and the system library where available. For a qualified native platform without a system library, use an explicitly pinned, packaged SQLite dependency through the same C API; do not silently drop that platform or download an unpinned runtime dependency. Do not add an ORM, separate database server, or general SQL administration API. Start with WAL, `synchronous=FULL`, 4 KiB pages, approximately 1 MiB cache, a 1,000-page automatic checkpoint target, and a 500 ms busy timeout. Check effective settings and handle errors explicitly. The busy timeout is not a bound on fsync or total request time.

Serialize writes, keep transactions short, and perform no network waits while holding database transactions or authority locks. Apply final admission/revision checks at the authoritative commit boundary. Publish changed in-memory settings only after the required commit succeeds. Keep database access, locks, and checkpoints out of time-critical RF paths; execution consumes validated in-memory snapshots.

Preserve existing receipt retention, operation-ID namespaces, principal/authorization bindings, digest/context checks, and uncertainty semantics. Do not turn store-scoped operation IDs into global IDs across independent owners or remote devices. Represent unsigned 64-bit generations without signed-integer overflow. Database/storage versions remain independent of shared settings/control versions.

Implement explicit fresh initialization and recoverable one-time migration. Recover existing committed managed transactions or report the unresolved condition before importing. Capture a consistent source snapshot, validate supported role-specific state, commit database contents and migration identity, verify the result, then retire the active INI. Preserve the original migration backup. Handle interruption before and after each publication boundary idempotently; never infer that an absent established database means a fresh install.

Import complete trusted state rather than public configuration JSON or browser save payloads. Preserve actual custom values instead of replacing them with initialized defaults. The audited advanced INI settings without dedicated UI editors are:

| Section | Keys |
| --- | --- |
| `Meta` | `debug_logging` |
| `Operation` | `Web Port`, `Socket Port` |
| `Experimental` | `Allow Unqualified Frequency`, `Allow Non-Amateur Frequency` |
| `Si5351` | `TX Output` |
| `CW` | `Fade Shape`, `Fade In Ms`, `Fade Out Ms`, `Fade Slice Ms` |

Preserve separately managed `Security.Privileged Network Safety` through its actual owner. Account explicitly for parser-supported `Meta.Loop TX` and `Meta.TX Iterations` startup semantics without promoting transient execution counters into ordinary saved configuration. Missing values follow current documented defaults/validation, not an invented blanket rule. Report unsupported source fields without activating them.

After successful migration, do not read, watch, regenerate, or automatically reimport INI. There is no compatibility projection or double write. Update saved-state revision, Stop, recovery, reset-to-defaults, and installation paths to use the new owner. Preserve configuration-recovery behavior without an INI after-image requirement. Ordinary read routes remain free of initialization or repair side effects.

Missing/corrupt established databases, inconsistent identity, or failed recovery must not silently produce replacement state, output permission, or a new controller identity. Preserve enable-on-boot semantics only after valid initialization. Ensure installers/updaters preserve established databases and do not recreate an active INI on subsequent runs.

Provide versioned schema migrations for future supported database revisions, with transactional validation and refusal of unsupported newer formats. Do not add historical settings/control API views or downgrade fallback. Use SQLite-consistent snapshot APIs rather than copying a live main database without its WAL state.

Slice result: role-aware durable state, complete INI retirement across affected consumers, restart/recovery behavior, focused fault-injection coverage, and preserved current configuration/runtime semantics.

## Slice 3: local advanced-settings command

Provide a small local command for reading and persistently editing the ten advanced settings above through authenticated backend IPC. Keep the existing configuration UI unchanged apart from the separately authorized Maintenance additions.

Use a closed typed field registry, existing semantic validation, explicit expected-revision checks, and atomic multi-field updates. Reject unknown fields, wrong types, invalid combinations, stale revisions, and unsupported role-specific fields before mutation. Use a distinct authorized CLI principal; do not expose internal principal serialization. Do not expose arbitrary SQL, generic JSON merge, browser-supplied paths, remote-target editing, or an offline normal-settings writer.

Report saved versus active state, restart-required changes, failures, and uncertain outcomes. Preserve listener activation rules. Existing startup flags keep their documented runtime meanings; they must not silently persist changes. Refuse ordinary settings edits if the backend is unavailable. Validate CLI help before any hardware or identity initialization.

Slice result: documented, tested persistent access to the advanced settings without expanding their UI or weakening safety policy.

## Slice 4: backend-owned lifecycle and authenticated IPC

Move supervisors and persistent owners currently attached to web-server objects into backend-owned lifecycle before extracting browser listeners. Cover pairing, device connections, browser session authority, support jobs, Test Tone/connection-bound cleanup, status/event production, catalog ownership, and scheduling. Preserve current cancellation, availability-lease, drain, and admission semantics rather than assuming every operation survives a browser-service disconnect.

Implement a closed versioned local request/response/event contract with operation-specific bounds, bounded queues, peer verification, connection/boot identity, and distinct handling for control versus bulk/event traffic. Derive limits from production endpoints rather than prototype-wide constants. Use Unix-domain sockets on Linux/macOS and local-only named pipes with explicit access controls on Windows; reject remote pipe access and provide no unauthenticated TCP fallback.

The backend applies request/session, permission, revision, and origin checks using the verified IPC peer and trusted request context. Preserve existing proxy/origin handling. Never accept an `authorized=true` assertion or serialize an internal browser principal. Protocol mismatch is unavailability, not a hidden older-contract fallback.

Preserve lost-reply semantics: no automatic mutation/Start replay, receipt/readback reconciliation, and explicit unknown outcomes. Ordinary refresh must not clear a stop requiring specific authoritative readback. Local Stop durably records Transmit-off before cancellation; this receipt is not RF-inactive evidence.

A remote target selection, failure, or removal must not change local output configuration or authority. Existing busy checks still apply while transmitting. Backend shutdown, web shutdown, and restore maintenance must remain distinct lifecycle paths.

Slice result: bounded typed local IPC and backend owners independent of browser listener lifetime, exercised through actual processes.

## Slice 5: separate HTTP and WebSocket service

Move browser-facing HTTP route handling, including `/config`, `/version`, browser APIs, Maintenance endpoints, and browser WebSockets into the explicit WsprryPi web-service process. Keep authenticated device-to-device listeners with backend cryptographic authority. The web process learns verified role, capability, identity, and listener configuration from backend-owned interfaces; it does not provision a second identity or infer backend role from its own build.

Apache continues serving the existing PHP/HTML/JavaScript/CSS and reverse-proxying HTTP and WebSockets. Preserve browser URLs and listener settings during extraction, including current custom values. Do not replace Apache/PHP, consolidate ports, or implement browser-hop HTTPS/trust redesign as incidental work. That security design remains the separate TODO item.

The backend publishes status/events over IPC; the web process formats/delivers them and forwards validated requests for backend admission. Web restart must not restart the backend, reset identity, or stop/rearm autonomous schedules. Show backend unavailability truthfully. Apply backpressure and bounded resynchronization to disconnected/slow browser event consumers without blocking control or RF execution.

Preserve existing routes, response contracts, privileged network safety, support downloads, and browser error/recovery behavior. Update route generation, service dependencies, and installation metadata to the split topology. Web startup and maintenance reconnection need an explicit trusted listener-bootstrap contract; keep the last admitted endpoint available during restore rather than requiring a read from a stopped backend.

Follow the mandatory Impeccable workflow for affected UI behavior. Preserve existing design language and operator drafts. Render and inspect affected workflows on desktop/mobile and light/dark themes. Static render fixtures establish layout only, not live authentication or restore correctness.

Slice result: actual separate backend/web processes, preserved HTTP/WebSocket workflows, OS-enforced private-state separation, and documented service topology on the qualified platform baseline.

## Slice 6: shared backup/restore engine, CLI, and Maintenance

### Backup content and consistency

Provide the same backend backup/restore engine to Maintenance and the local CLI. Exact command spelling must be documented and tested. Do not implement separate archive formats or divergent validation for browser and CLI use.

A versioned encrypted backup includes a validated manifest, integrity information, a consistent SQLite snapshot, required companion persistent state, and source application/storage/role/platform metadata. Create one complete recovery archive containing the identity and credential material required for the advertised same-installation recovery, identified in the private inventory. The purpose selected at restore determines which material may be applied; do not omit necessary recovery material merely because the archive might later be used for portable settings migration. Preserve durable pending/unknown-operation records needed for reconciliation, without turning them into runnable commands during restore. Exclude logs, executable/OS installation, transient sessions, ephemeral queues, diagnostic artifacts, and remote devices' independent state.

Coordinate every included persistence owner across the snapshot barrier, including pairing/key mutation where relevant. Capture a coherent durable decision without holding time-critical execution paths. If current activity cannot safely admit the barrier, report the actual busy condition; do not stop RF or remote devices simply to make backup possible. SQLite snapshot success alone is insufficient for a multi-owner backup.

Use an established authenticated encryption format and maintained implementation; do not design custom cryptography. Select the format and packaged dependency explicitly in the slice-1 contract, verify availability across the qualified platform profiles, and record format/resource bounds before implementing archives. Support portable passphrase recovery, with no dependency on the source computer's secret store or maintainer keys. Never log/store the plaintext passphrase, include it in process arguments, or put it in diagnostics. Test the chosen format and resource behavior on the supported baseline without inventing new performance thresholds.

Bound archive generation, upload, expansion, staging, and retention. Stream encrypted downloads without buffering the full archive in the web process or exposing database/key-store access. Publish only completed archives; report partial failures and remove incomplete artifacts. Permit retrying a completed download without generating a new snapshot. A job identifier is not authorization, and download completion must not be overstated as verified client-side storage.

### Maintenance UI

Add a clearly labeled Backup and restore section using the incumbent Maintenance layout. Keep backup controls distinct from diagnostic/support packet generation and reset-to-defaults. Protect new endpoints under the existing privileged maintenance/session/network policy; do not make uploads/downloads public or weaken those checks to make recovery convenient.

Backup flow: Create backup; enter and confirm its passphrase; show local progress; enable Download backup after successful publication. Keep failures and retry actions beside the initiating control. Explain that this protects the local installation and does not independently back up remote devices.

Restore flow: select/upload archive; supply passphrase; validate without mutation; show source/date/compatibility/purpose and concrete destination changes; explicitly confirm the exact staged restore; preserve a recovery copy; apply maintenance replacement; reconnect and report verified outcome. Keep a cancelled or invalid upload from modifying current state. Confirmation must not trigger merely because a file was selected or validated.

Offer explicit recovery of this installation versus moving settings to another computer. Do not require SSH for the normal admitted browser restore. Keep advanced settings editing in the local command rather than adding unrelated controls here. Preserve keyboard focus, labeled inputs, polite progress announcements, responsive layout, and truthful busy/failed/unknown/reconnected states through Impeccable review.

### Restore worker and durable handoff

Normal backend settings commands remain online-only. Restore is an explicit maintenance exception: the web service stays available while an installer-provisioned narrow worker coordinates the exact backend/service shutdown, state replacement, and restart. Keep its durable admission/progress record outside the state being replaced. The worker must not initialize transmitter hardware or act as a second live backend writer.

Bind admission and confirmation to the staged archive digest, captured destination state/identity, intended purpose, and authorized request. Changed source/destination context invalidates confirmation. Recheck before maintenance begins. Validate authorization for progress, download, and outcome readback before and after backend restart; possession of a job ID or an old browser connection is insufficient.

Use fixed service identities, approved state roots, and narrow lifecycle operations. Never accept arbitrary shell commands, service names, restore destinations, archive executors, or traversal/symlink paths. Any necessary service-control privilege belongs to a fixed scoped adapter, not the web process or general executable. Stop/exclude every writer of the state being replaced before publication.

Stage and verify the complete replacement first. Use a durable publication/recovery protocol spanning database and companion files, not an unsupported assumption of multi-file atomic rename. Recovery runs before persistent owners open restored state. Faults must select a verified prior or restored generation, or report a recoverable hold; never silently expose a mixed generation. Preserve a protected destination recovery copy until completion is established.

Once admitted, restore continues independently of the browser connection. Reconnection reads durable job status and does not resubmit the restore. Handle backend/service start failure and listener changes explicitly: preview changed addresses before confirmation, retain the maintenance endpoint needed for outcome reporting, and provide the new address. Do not require successful backend startup to preserve failure diagnostics or job state. Provide documented CLI recovery when an installation is too damaged to admit the normal browser flow.

### Restore semantics and computer migration

Validate format, authenticity, contents, schema, role support, required companion records, and destination compatibility before mutation. Reject unsupported newer formats. Do not add generic downgrade, identity conversion, arbitrary role conversion, or platform-porting machinery.

For another computer, apply portable settings and Fleet definitions through destination-specific adapters. Do not blindly apply old GPIO assignments, adapter identifiers, paths, ownership, or hardware identity. Show unresolved destination settings and keep affected operations held. Preserve custom saved values where portable; do not silently substitute presets.

Preserve pairing continuity only where current identity/credential contracts support relocation. Otherwise retain target definitions without granting authority and use existing pairing recovery. Do not silently revive revoked authority from an old backup: reconcile with newer established authority where available, and require pairing recovery where current authority cannot be established. Never treat old receipts as fresh permission on a different identity or boot. Identity relocation requires retiring the source installation; do not claim that a complete credential clone can always be detected.

Do not replay pending commands, restore transient sessions, or resume work from a saved in-memory queue. Preserve unresolved-operation evidence for reconciliation. Existing remote devices keep their autonomous schedules; controller restore does not stop/adopt/reassign them or prove their current state.

Keep saved enable-on-boot preferences but establish a new persistent restore hold before the restored backend can initialize automatic output admission. Do not inherit an old backup's cleared hold as permission to run. Local transmission remains disabled until explicit destination review and resumption. A restart must not bypass this hold. Report held remote authority/scheduling state distinctly from local RF state, and apply existing stop/drain requirements before destructive local replacement rather than treating a durable Stop receipt as physical completion.

Slice result: one archive/validation engine, CLI and Maintenance workflows, encrypted download/retry, consistent cross-owner snapshots, durable restore handoff, supported migration/recovery behavior, and fault/reconnect coverage.

## Slice 7: diagnostics, platform integration, and documentation

Update diagnostic/help packet generation alongside storage and service changes. Replace INI-derived diagnostics with an allowlisted, redacted export of supported settings, including advanced settings; include storage/schema/migration errors, backend/web versions, service and IPC state, relevant logs, and existing OS/network/hardware observations. Do not include raw databases, decrypted backup contents, or private credential stores. Text redaction cannot sanitize binary database pages.

Preserve the existing readable candidate, download, encryption, and private-intake workflow. Browser routes live in the web service; job ownership and private backend collection remain behind typed backend interfaces. Do not broaden maintainer intake administration. Standalone collection must still gather useful host/service evidence when either process is broken, explicitly marking unavailable backend information instead of inventing defaults. Update trusted path resolution, ports, service discovery, dependencies, help text, and collector tests.

Revalidate the split topology and maintenance lifecycle on each already-qualified platform profile claimed under #475. Preserve zero-output controller builds, native service managers, permissions, IPC, USB/network management, and existing hosting dependencies. Linux-only evidence cannot establish macOS/Windows correctness. Do not implement a missing #475 platform profile within this work or imply its qualification.

Update parent engineering documentation for storage/IPC ownership, initialization/migration, advanced commands, service topology, backup/restore, failure recovery, and diagnostics. Amend the #475 plan/prompt only to reconcile accepted requirements affected here. Record exact operator-documentation follow-ups for INI retirement, commands, Maintenance workflows, archive protection, computer migration, pairing recovery, restore holds, and service changes. Read sibling instructions and obtain explicit cross-repository authority before editing the operator documentation repository; otherwise leave it untouched and report the remaining documentation work.

Use direct operator prose and preserve one physical source line per prose paragraph/list item. Document current implemented behavior separately from proposed behavior and limitations. Do not add development diaries or claim features verified by software fixtures have been physically qualified.

## Validation and evidence

Use existing build/test entry points from `src` after inspecting their behavior. Prefer focused meaningful tests for persistence, IPC, process lifecycle, and recovery rather than tests that merely mirror implementation. Use isolated state roots, synthetic credentials, bounded files/processes, loopback or explicit software fixtures, and cleanup that preserves user state. Never point fault injection or restore tests at installed application state.

Choose the host's semantics profile explicitly:

```sh
# Linux with the required libgpiod C++ development headers:
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=

# Non-Linux or Linux without those headers; an explicit subset:
make semantics-test-portable SUDO=
```

Inspect and run applicable configuration, scheduling, UI/source-regression, and production-browser tests. Adapt obsolete INI assertions to verify authoritative database state and absence of ongoing INI writes without deleting behavioral coverage. Use the simulated backend explicitly for normal-Pi software execution fixtures; it must not appear as a local output backend in the dedicated controller artifact.

Both actual production-path network and USB integration suites in the canonical CI workflow must pass against the intended Pi/Pico source pair. Read current recipes rather than copying stale commands. Identify both revisions, uncommitted changes, explicit companion source selection, and pinned dependencies. Mocks, builds, stale green CI, and skipped/failed/cancelled suites do not substitute for successful paired evidence. Follow current single-major control policy and strict validators; coordinate producer/consumer changes if an interface change proves necessary rather than weakening validation.

At minimum, exercise:

- Complete migration with nondefault hidden/private values, minimal controller-only input, custom listener ports, interrupted publication, failed validation, disk-full conditions, absent/corrupt established state, repeated migration invocation, and post-migration updates that never recreate or consult INI.
- Receipt identity/digest/context checks, metadata-only changes, stale revisions, atomic multi-field settings edits, rollback/error handling, final admission races, and lost-response/unknown-outcome reconciliation.
- Actual backend/web separation, private-state access refusal, peer and option validation, unsupported IPC versions, malformed/oversized messages, bounded queues, slow consumers, disconnect/reconnect, web restart, and backend restart.
- Hybrid local simulated scheduling with permitted incoming/outgoing management and at least two remote targets; verify local/remote isolation, existing busy refusals, autonomous target behavior, and no silent authority changes.
- Zero-output controller builds, unprivileged operation, no station requirement, offline/empty catalogs, early help/version/frontend/maintenance dispatch, and refusal of local controls at both presentation and server boundaries.
- Cross-owner backup consistency under competing mutations; wrong passphrases; corruption/tampering; incomplete archive publication; bounds, retention, download authorization and retry; and absence of credentials from logs/diagnostics.
- Restore preview without writes; stale confirmation; unsupported format/role; unsafe archive members and expansion; insufficient storage; interrupted multi-store replacement; stale authority; source/destination identity differences; restart failure; changed endpoint; browser loss; duplicate requests; and a reboot-surviving restore hold.
- Same-installation recovery and supported computer migration without pending-command replay, automatic local transmission, remote schedule adoption, or implicit identity creation. Unknown remote outcomes remain unknown until existing authoritative recovery succeeds.
- Diagnostic exports including advanced values with secrets excluded, collection with backend/web unavailable, preserved support workflow, and no raw database or decrypted backup included.
- Live Maintenance create/download/upload/validate/confirm/restore/reconnect behavior through isolated processes, with desktop/mobile light/dark rendering, keyboard operation, focus recovery, and truthful progress/error states. Inspect actual rendered output through Impeccable; static fixtures alone do not establish runtime behavior.

Run relevant component standalone tests when a component is changed and parent integration tests for its consumers. Do not hide a reusable-component modification inside parent integration work; follow approval boundaries for broadening component scope.

Pi 1/Pi 5 integrated memory, latency, and host-write observations require separate explicit device authorization. Reuse retained feasibility evidence as context, not as production qualification or a guaranteed savings percentage. Preserve measurements, errors, and partial results. Add no arbitrary performance acceptance threshold, manufactured compliance checklist, or claim about measured NAND wear/card lifetime. Pending live platform/service/hardware validation stays explicitly pending.

## Explicit exclusions

Do not add database command queues, SQL/admin browsers, new advanced-settings UI controls, automated backup scheduling, cloud backup, remote-device backup orchestration, generic archive tooling, logs/telemetry ingestion into SQLite, unrelated key-store rewrites, browser-hop trust redesign, frontend redesign, Apache/PHP replacement, port consolidation, new transmitter features, new platform profiles, component cleanup, or broad refactoring.

The Maintenance backup/restore UI and its bounded service handoff are the only new UI workflow authorized by this prompt. Necessary preservation of existing UI behavior during process extraction is in scope. Do not reinterpret the local settings command as authorization for a new settings panel.

## Adversarial assess-and-fix loop

After each slice, challenge it from an operator and failure-recovery perspective. Check concrete persisted/runtime outcomes, not just classes or happy-path tests. Ask whether hidden settings were lost, state owners can disagree, authority can roll back, commands can replay, unavailable services are misreported, restore depends on the state it replaces, companion interfaces drift, UI drafts/progress are lost, or scope has expanded.

Fix actionable in-scope findings and rerun affected validation. After assembling the complete implementation, perform a new full assessment covering migration, hybrid/controller roles, private-state access, IPC, process lifecycle, backup/restore, computer migration, diagnostics, platform dependencies, UI/accessibility, documentation, and authorization boundaries. Repeat assess/fix/reassess until no further actionable in-scope issues remain. Do not invent findings to prolong work, count an unchanged repeated check as independent evidence, or declare completion while required failures remain. If an issue needs missing authority or an unavailable external dependency, identify the exact blocker, complete independent authorized work, and report the deliverable as incomplete at that boundary.

## Completion report

Lead with behavior implemented and the exact remaining limitations. Report parent branch/revision and working-tree state; every modified component; important files; migration and backup format decisions; exact test commands and results; both companion source revisions; Impeccable workflows and visual evidence; service/platform/runtime evidence; outstanding validation; and any scope or dependency blocker. Include a Documentation Impact section listing updated, considered-but-unchanged, and still-required documentation with reasons. Distinguish automated software evidence from installation, physical USB, GPIO/timing, and RF qualification.

Run final diff/error checks and review the complete parent/component diffs. State explicitly that nothing was staged, committed, pushed, or published. Do not call the work complete, release-ready, or physically qualified without the corresponding required evidence.
