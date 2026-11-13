Implement fleet device selection in the existing WsprryPi Operations panel, including per-device status and controls, required coordinated Pi/Pico changes, documentation, and validation.

Planning revision: 2026-11-13. The maintainer adopted the review recommendations,
including the initial read-only inspection. This update defines the remaining
controller-side design; it does not start implementation or authorize staging,
commits, pushes, CI, or physical operation. Apply the implementation instructions
below only after a separate instruction to start.

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

Do not add unrelated refactoring, a new dashboard, a simultaneous multi-device
monitoring UI, durable historical analytics, standalone Pico schedule
administration, remote reboot-policy editing, or device power-management
features. Bounded observation by the existing fleet owners is required for the
single-selected-device view and is not a new all-device dashboard.

## Inspect current sources first

The reviewed sources were WsprryPi devel at
`7a9594670002a5f6b1a157708b257b8d58e7b9bf` and WsprryPico devel at
`b4e966797cf016f6b8ed873dbe638522fc4d81c7`. Earlier planning referenced
`4c70b25b` and `d503d5d0`. These are source evidence, not instructions to reset
or substitute sources. Reinspect the intended candidates before implementation.

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
- src/wtp_integration/backend.cpp and target_runtime.hpp
- src/wtp_integration/fleet_store.cpp and pi_status_client.cpp
- src/web_server_control_routes.cpp and privileged_network_policy.cpp
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

Identify a particular assignment/context by `(controller_incarnation,
assignment_generation, device_id)`. Generate an opaque 128-bit incarnation
when the Fleet manager starts or is replaced, and an opaque 128-bit generation
for each loaded or newly assigned context. Keep that generation through rename,
pause, schedule edits, and retained removal; never reuse it for a new assignment
to the same device ID. These are volatile host API metadata, not new fields in
the existing version-1 assignment file. A controller restart changes the
incarnation and invalidates every old action/render baseline. A device reboot
additionally invalidates its boot-bound observation and action eligibility.

Match HELLO's full ID/product to the saved assignment and show the carrier's
actual trust meaning. Plain LAN is an expected-ID match, not cryptographic
authentication. USB metadata and negotiated identity are separate evidence;
TLS authentication is reported only when the existing verification succeeded.

Default to This Pi on page load. Selection is transient browser state and must not change saved configuration, the compatibility WTP endpoint, backend selection, ownership, or schedule state.

Opening Operations or changing selection must not initiate CLAIM, LOAD, ARM, recovery, clock-setting, or settings writes. Any existing background runtime activity remains independently owned by its established lifecycle.

Keep selection stable across snapshot refreshes, name changes, offline
transitions, and list reordering. A full page load still defaults to This Pi.
Reassignment of the same ID with another generation is removal/replacement,
not permission to silently select the replacement or reuse a draft.

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

Use the following field mapping and precedence. Implement one pure presentation
adapter shared by renderers; do not infer state separately in individual event
handlers. Extend the host projection to carry missing evidence explicitly.

| Displayed value | Source and binding | Freshness / missing evidence |
| --- | --- | --- |
| Name and product | Saved assignment or frozen retained-context metadata, with separately identified confirmed member name | Saved names remain usable offline; mark a member name as last confirmed when its binding is no longer current |
| Full identity and firmware | Matched HELLO plus saved expected ID, carrier evidence, and boot ID | Preserve last observed firmware as historical offline; missing HELLO is unverified, not an invented firmware value |
| Schedule enabled/paused and configured signal | Committed assignment and configuration ETag; mode-specific absolute RF reference, period, and UTC phase | Configuration is not a device observation; keep it labeled Assigned schedule even during foreign execution |
| Current job state | Accepted STATUS and authoritative job evidence matching device, boot, and the current controller job | At 15,000 ms age, or after invalidation, stop presenting it as current |
| Output-active observation | The matching STATUS device/output and job evidence, kept distinct from execution state | Show unavailable/unknown when absent or stale; false never means measured RF-off |
| Pending start/countdown | Owner-published committed pending job, request/job ID, immutable start, and consumed-slot record | Never reuse start fields inherited from last_report; missing or invalidated commitment means no pending countdown |
| Future forecast | Server-only pure canonical next-slot calculation from committed schedule, current trusted UTC, current CAPS-derived lead, and last consumed slot | Label If resumed now for paused assignments; absent prerequisites produce null plus a reason, not a guessed start |
| Requested / realized frequency | Assigned mode-specific RF reference; separately, current job-bound LOAD adjustments/acceptance | A report from another job is historical. No adjustments means accepted as requested only with that job's accepted LOAD evidence; do not invent device frequency for a foreign job |
| Last execution / reconciliation | Explicitly identified bounded historical report with device/boot/job/action association | Never promotes current state or satisfies a newer action |
| Action feedback and eligibility | Correlated host action record and current server-computed eligibility | GET receipt time or an older enabled/paused flag is not action completion |

For WSPR, label the assignment frequency as RF center; QRSS/tone as carrier,
FSKCW as space with positive shift, and DFCW as dot with positive shift.
Show bound distinct realized values where supplied, not a fictitious single
frequency for a multi-tone job. Keep requested and realized units explicit.

Apply the first applicable headline rule below. Schedule enabled/paused and
action progress remain separate lines, so they cannot conceal actual execution:

1. Selected generation removed/replaced: Removed output, with retained cleanup
   evidence and only the actions allowed for that retained context.
2. Latched identity/protocol/safety fault, uncertain mutation, unresolved cleanup,
   or crash barrier: Recovery required. Add unavailable/stale qualification
   without hiding the unresolved condition.
3. Failed transport/session: Device unavailable, with last observations historical.
4. Missing/invalidated observation: Device state unknown. Observation age at
   least 15,000 ms: Device status stale, never an unqualified Running or Idle.
5. Fresh STATUS reports a foreign/unmatched active job: Other job on device;
   do not attribute this assignment's mode/frequency to it or offer takeover.
6. Fresh, authoritative, matching current-job evidence: Running, Armed, or
   current terminal Failed/Missed as reported. RF-off intervals do not end Running.
7. A committed pending job without matching remote execution proof: Preparing
   or Waiting, explicitly controller-reported; countdown expiry is Overdue / awaiting status.
8. Fresh idle/unowned evidence: Idle, with the assignment's independent schedule
   state. A previous Complete report belongs only in history.

An action in progress adds Pausing, Reconciling, or Enabling feedback beside its
control; it does not overwrite a higher-priority fault or real Running state.
Carry a structured reason for an unavailable field rather than substituting zero.

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

Publish pending_start and forecast as distinct nullable objects. Set pending_start
only after the slot consumption is durably committed and the same generation's
frozen job is accepted by its owner. ARM handoff is a separate field until
matched endpoint evidence confirms Armed. Forecast calculation neither consumes
a slot nor creates a job. The browser may decrement a supplied countdown from
the validated UTC/monotonic anchor, but never calculate recurrence or advance to
another slot itself. An invalid clock anchor hides the countdown until refreshed.
Offer a paused forecast only while Resume is currently eligible; otherwise
publish null plus the blocking reason. A forecast never promises RF readiness.

Retain decimal-string/64-bit precision. Do not convert epoch nanoseconds or identifiers through unsafe JavaScript Number arithmetic.

## 4. Freshness and polling

Browser refresh and device observation are different events. Display the age of the authoritative device observation, not the age of the latest HTTP response or worker heartbeat.

Use a fixed **15,000 ms** stale threshold, not a user setting. The existing
healthy-idle poll interval is 1,000 ms, WTP transaction timeout 8,000 ms, and
browser poll interval 3,000 ms; their 12,000 ms total leaves 3,000 ms of policy
margin. This is a conservative display/admission policy, not a latency guarantee.
An over-budget response is stale even if it eventually succeeds.

Age must continue increasing locally when refreshes fail. Use a monotonic browser elapsed-time source and account conservatively for request latency.

Record a conservative STATUS age origin and accepted-response monotonic time
in the controller adapter. Use the explicit STATUS request start; for the
initial internal negotiation, its recorded negotiation start is a conservative
origin if the exact STATUS send is not exposed. Label which origin was used;
do not change the WTP client interface solely to obtain a tighter age bound.
Preserve the existing status_observed_ms receipt meaning. The actual sampling
time lies after that origin, so this may overstate age but cannot make old
evidence appear younger. If no valid origin is available, report age unknown
rather than substituting a worker publication timestamp.
GET may sample the controller monotonic clock to age a cached observation; it
must not refresh that observation or touch a device.

At browser receipt, add the full measured HTTP round-trip duration to the
server-provided age as a conservative upper bound. Then add elapsed
performance.now() time. Use checked decimal strings/BigInt for unbounded times;
convert only bounded display intervals to Number. On page restore, visibility
resume, negative/discontinuous deltas, incarnation changes, or an unexplained
revision reset, invalidate the baseline and mark unknown/stale until refreshed.
Use observation_revision for runtime evidence and the existing configuration
ETag for mutation conflicts; routine polling must not manufacture config revisions.

Publish a decimal-string publication_revision that increases whenever the
combined committed snapshot/action state changes within an incarnation. Reject
older publications before applying configuration, observations, or action
results; do not try to order opaque ETags. Per-context observation_revision
advances for accepted STATUS or invalidation, not a worker heartbeat. All
projections/query results carry their publication/incarnation binding. A GET's
current clock sample may increase age without advancing either revision.
A controller monotonic-clock discontinuity inhibits new admission and requires
a fresh incarnation before accepting commands again; do not make expired
admission windows valid by rewinding their clock. Preserve owned/pending work
as unresolved until its existing stop/reconciliation path establishes an outcome.

Use one read coordinator for Operations, Setup's fleet projection, and action
lookups. Only a designated, serialized full-snapshot GET may establish a new
incarnation. On change, invalidate the browser read generation, retire old
render requests, preserve their action references as unconfirmed, and confirm
with a fresh full GET before enabling controls. Late POST/action responses may
resolve only their original action; they cannot switch the current incarnation
back. Every callback checks its captured read/selection/context generation.
Use no-store response/cache policy; a 304 or cached body without a valid current
age sample cannot renew freshness. Within one publication, a decreasing clock
sample cannot reset the age baseline or revive eligibility.

Handle controller process restart, monotonic-clock discontinuity, device reboot, identity change, revision reset, and reconnect without reusing invalid observation baselines or pending-action results. Add minimal controller-incarnation metadata if existing evidence cannot distinguish these cases reliably.

Use bounded polling, initially consistent with the existing approximately three-second fleet UI refresh. Prevent overlapping polls, retry storms, and unbounded background browser work.

GET handlers must copy published snapshots rather than perform device transport
operations, reload files, prune workers, or call destructors/joins. The existing
Fleet::request mutex spans mutations and GET; split that ownership so a slow
mutation, reconcile_retired(), or fsync cannot hold the snapshot publication lock.
Use short registry/publication locks and per-context mutation reservations;
perform transport work and worker retirement on the owning worker. Publish a
new committed configuration ETag only after successful persistence. Until then,
GET serves the previous committed configuration with truthful action progress.
Never expose mutable target pointers to HTTP handlers or hold registry locks
while waiting for output/transport/store locks. Different contexts can progress
independently; recheck generation/revision before committing a reserved action.

Fleet HTTP handlers perform only bounded shape/guard checks, immutable reads,
and short admission/record reservations. Return 202 after reserving an accepted
action; execute verification, persistence/fsync, stop/join, and retirement on
bounded owning executors, not the HTTP pool. Splitting the mutex while keeping
these waits in handlers can still starve every GET. Recheck the reviewed ETag
and generation before persistence; a later conflict becomes that accepted
action's refused result, not an overwrite. A Pause admission may signal the
owner's nonblocking stop intent immediately but cannot run a blocking stop on
the HTTP thread. Shared-disk serialization may delay commits, yet must not
prevent cached reads or stop intent. Queue capacity is bounded by the existing
32 contexts and action reservations; do not add an unbounded persistence queue.

Replacing/restarting the manager is a lifecycle transition, not just publishing
a new incarnation. Inhibit old start/edit admission while preserving verified
owner status/stop paths, retire queued work, and fence old store/journal writers
before a new manager can write or dispatch. Transfer only
published evidence/recovery duties; never create competing sessions or forget
an unresolved owner/lease because its generation changed. Pending old I/O keeps
its resources until quiescent, and if handover cannot finish, publish replacement
pending/unavailable and block new work. Old HTTP references or delayed callbacks
cannot write into the new manager's stores. The current start_wtp_fleet()
constructs/starts candidate workers before replacing manager; inspect and fix
that path within this feature's owning-runtime changes, preserving independent
local Operations. Test actual manager replacement, not just JSON incarnation IDs.


For a healthy newly loaded/assigned context without an in-flight, fault, or
retirement barrier, allow **one initial read-only inspection** on its owning
worker: open the configured carrier, HELLO, STATUS, and CAPS, match expected
device/product, bind STATUS/CAPS to the negotiated boot, and publish evidence. This is independent of browser
opening/selection and cannot CLAIM, LOAD, ARM, ABORT, RELEASE, set a clock, or
write settings. Observe foreign ownership without displacing another session.
Reuse a still-valid verified context when it can be transferred to the owner
without transferring jobs or uncertainty; do not force a duplicate handshake.
Do not call refresh_usb_time() or a management helper that synchronizes clocks
from this observation path. Existing separately owned scheduling/time activity
retains its own authority and must not be mistaken for a viewing side effect.

Consume the initial-attempt permission for the assignment context, not each
replacement transport object. Separate a successful observation from readiness
to transmit: inspect()/poll_idle() use startup-quiescence checks and can report
not ready solely because a valid STATUS shows a foreign owner or active job.
That is a healthy observed-but-ineligible session, not a failed inspection.
Keep observing it read-only; when the foreign work ends, fresh safe evidence
can restore eligibility without reconnecting, taking over, or demanding recovery
for occupancy alone. A failed handshake/transaction, later disconnect, or real
identity/protocol/safety latch still requires explicit Reconcile. Check the
published session/evidence, not a single readiness boolean; no retry loop.
Do not perform the initial inspection for a persisted in-flight or unresolved
removed context. Restoring a worker cannot bypass its safety barrier.
Use existing bounded connect/transaction limits, plus a 45,000 ms aggregate
software inspection deadline. Deadline expiry invalidates the observation;
no late completion makes the context ready. This does not qualify physical
kernel/USB shutdown bounds.

Once Ready and idle, call the existing poll_idle() on that same owner, at most
once per second. Active/preparing/waiting work uses its existing scheduler's
STATUS path; never add competing requests. Failed observation retains the
existing explicit recovery requirement. Do not clear faults or resubmit work.

Poll devices through existing serialized ownership of each transport. Do not create additional competing sessions for the selected view. One slow device must not block the entire fleet snapshot or other outputs.

Observation work must remain bounded for 32 outputs and must not multiply with the number of browsers.

Keep at most one WTP transaction per context in flight, at most 32 across the
fleet, and no backlog of missed observation ticks. Stagger initial worker
inspection by 30 ms per context index (maximum 930 ms); this never shifts a
scheduled job. Poll Operations every 3,000 ms after completion, with one GET in
flight and the existing 7,000 ms read timeout. Suspend browser polling while
hidden; on return, refresh before enabling actions. Slow output A must not block
published reads or mutations for B. Deterministic blocked-transport tests must
prove this isolation rather than relying on all devices timing out quickly.

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

An accepted pause request is not confirmed output-off. Use mandatory correlated
action records as defined below; an older recovery or an ETag change alone
cannot satisfy a new action.

Freshness or uncertainty must prevent unsafe start/resume decisions on the
controller, not just by disabling a browser button. Before committing enable,
check the current ETag/incarnation/generation, fresh matching ID/product/boot,
Ready session and current CAPS, observed unowned/inactive output, compatible
compiled schedule and current frequency policy, and absence of in-flight,
retirement, fault, uncertain-action, or cleanup barriers. Evaluate the current
published evidence under the context reservation and revalidate before commit.
Recompute age against the current server monotonic time, rather than trust a
previously published can_resume boolean. Uncertain persistence/readback keeps
that context's dispatch inhibited until authoritative readback resolves its
current saved state; a refresh receipt alone does not clear the barrier.
For a Pi target, also require a fresh read-only host/wtp-endpoint observation
matching the WTP device and boot, the assignment's takeover generation,
local_requested=false, output_unknown=false, and healthy revocation storage.
Publish its age separately; a changed generation starts retirement rather than
enabling the row. Collect this existing protected GET on the owning runtime at
most once every 5,000 ms while idle, independently of browser count, retaining
its existing bounded 8,000 ms transport deadline. Stateless HTTP connection
setup for that read is not WTP reconnection or recovery. It must not stall WTP
observation, other outputs, or cached GET. Keep the existing forced takeover
checks before CLAIM and ARM. Both evidence sources must be fresh; the 15-second
policy does not promise eligibility under worst-case combined channel latency.

Apply equivalent eligibility to assign with enabled=true before committing its
start-after-save choice, not just to the enable operation. The assignment's
explicit start request may bind the freshly inspected boot before commit; it
cannot skip occupancy, policy, identity, or journal gates. Existing enabled rows
restored at startup still pass all current initial/admission and pre-CLAIM/ARM
checks; loading their enabled flag is not evidence of device readiness.
Reject stale/unavailable evidence with an explicit reason; do not reconnect or
silently recover as part of enable. Keep all existing fresh pre-CLAIM/ARM and
endpoint ownership checks; a fresh snapshot does not reserve the remote device.
Do not disable Pause and stop or Reconcile solely because observation is stale;
the server applies their existing ownership/recovery authority and reports an
unconfirmed output state when it cannot obtain evidence.

After a timeout, transport failure, or revision conflict, refresh authoritative state and require explicit review before another mutation. Do not blindly retry a mutating request. A server operation may have succeeded despite a lost response.

Successful reconciliation leaves the assignment paused. Resuming schedules future eligible slots; it does not replay interrupted work.

Do not stop foreign-owned jobs, automatically take over a target, clear safety faults, or treat lost connectivity as output-off.

### 5.1 Host API, correlation, and completion

These additions are Pi-host resources, not Pico Control/1 or WTP messages:

| Resource | Contract |
| --- | --- |
| GET /api/control/v1/host/fleet | Existing assignment/Setup resource, served from published committed state; add incarnation, per-context generation, and action associations |
| GET /api/control/v1/host/fleet/operations | Compact Operations projection of the same owners, scope wsprrypi-fleet-operations/1; contains identity/evidence, eligibility, age, committed pending start, forecast, and bounded latest reports |
| GET /api/control/v1/host/fleet/actions?ids=... | Query at most 32 explicit action IDs; return their records or explicit unknown/expired indications with the current incarnation |
| POST /api/control/v1/host/fleet | Existing mutation resource with strict correlated request fields and If-Match; acknowledge acceptance separately from completion |

The actions query permits only ids, with 1–32 distinct 32-character lowercase
hex IDs separated by commas, and at most 2 KiB of encoded query data. Reject
unknown/duplicate query keys, malformed encoding, invalid IDs, and excess
capacity before ledger access. It cannot select a transport or device endpoint.

Keep the Operations response at most 512 KiB for all 32 contexts. Project only
the required evidence and latest report summaries, not complete event lists,
certificate paths, full capability documents, or the action ledger. Bound a
frequency summary to 16 distinct requested/realized pairs and explicitly mark
additional/omitted values; never pretend a partial list is complete. Omit optional
history detail with a reason if needed to meet the budget, but never omit or
truncate identity bindings, current fault/uncertainty flags, or admission facts.
Bound action-query responses to 128 KiB. Include escaped maximum-size strings
and 32 outputs in serialization-size tests, not only typical short fixtures.

Extend host route admission, path/method validation, intent guards, serializers,
both Operations and Fleet Setup clients, and tests together. No browser contacts
the targets. Keep the existing ETag semantics, including legitimate changes to
persisted slot/in-flight barriers. Observation, forecast, and action publication
do not write the assignment file or change that ETag. Do not add compatibility
views or a shared control major for these host-only changes.

Each mutation carries a browser-generated random 128-bit action_id, the reviewed
controller_incarnation and assignment_generation, a nullable reviewed_boot_id,
and decimal-string accept_before_ms, in addition to its existing operation/body
and If-Match. Obtain a fresh host clock/incarnation sample after confirmation
without replacing the reviewed target/revision; set accept_before_ms to that
sample plus 30,000 ms. A changed reviewed context requires review again.
A new command is admitted only before that deadline and no more than 45,000 ms
into the current server monotonic future. This admission window does not bound
accepted work's completion or authorize cancelling it on expiry. For assign,
generation is null until the new context is created; target identity still
comes from the strictly validated saved-profile settings. Enable requires a current non-null
boot. Pause/recover can use null or last-known boot without granting permission
to abort foreign work or substitute a different device. Update all six existing
mutation consumers (assign, enable, pause, schedule, recover, remove); do not
leave Setup bypassing the new admission/correlation rules.

Use crypto.getRandomValues for 16 random bytes encoded as 32 lowercase hex
characters; do not depend on secure-context-only randomUUID for the ordinary
HTTP UI or fall back to Math.random. Refuse mutations with an explanatory
message if suitable randomness is unavailable. Validate canonical decimal
accept_before_ms and checked 64-bit addition on both sides.

An action record binds action ID, operation, canonical request digest, reviewed
ETag, incarnation/generation/device, reviewed and result boot/job identities,
acceptance/completion monotonic times, state, work_pending, result evidence,
and a bounded reason. States are accepted, pending, confirmed, refused,
superseded, and outcome_unconfirmed. Return 202 for accepted pending work and
200 for a confirmed result; synchronous guard refusals retain appropriate
existing 4xx errors. Read-only identity/CAPS/STATUS verification may precede a
refusal, but it cannot partially apply settings or issue unauthorized mutating
device operations. An accepted asynchronous action may later publish refused
with its reason; do not pretend a 202 was retrospectively a synchronous 4xx.
If persistence or stop effects may already have occurred, report the achieved
and unconfirmed postconditions rather than imply that nothing changed.

After authentication/intent and shape validation, recognize an existing action
ID before applying the current If-Match to a new mutation. The exact original
request returns its retained action result without repeating side effects;
reuse with different payload/binding is a conflict. The digest includes the
fixed admission deadline, original If-Match, and reviewed identity/context.
An unknown action with an expired admission window
is refused without mutation even if its ETag still matches; changing the
window is a new logical request and requires a new action ID. New actions
require the current ETag and context. An accepted result echoed from an old
action is not a new worker command. No automatic mutating HTTP retries are
introduced.

Keep one ordinary action in progress per context. A validated Pause may persist
disabled immediately and supersede a queued enable/recovery with a nonblocking
stop intent; serialize actual WTP exchanges on the owner. Allow at most one
such priority pause in addition to the active action (64 pending records across
32 contexts). Reject further concurrent requests as Busy rather than build an
unbounded queue. Record supersession without declaring an outstanding transfer
cancelled or quiescent. Check pause intent before any future CLAIM/LOAD/ARM.

Use these postconditions:

- Enable is confirmed when the committed assignment is enabled after the
  server eligibility checks. It means future scheduling is enabled, not that RF
  started. A later job failure is a separate execution report.
- Pause confirms this controller's scope: disabled persistence, dispatch
  inhibition, and quiescence of any tracked controller-owned work. Record
  schedule_paused, controller_work_quiescent, tracked_cleanup_confirmed, and
  current device output/ownership evidence separately. With no tracked job,
  tracked_cleanup_confirmed is null/not applicable, not a fabricated receipt.
  If there was no owned job and no persisted in-flight/uncertainty barrier, pausing the schedule can
  be confirmed while a fresh STATUS reports foreign work, or while device
  output is unknown. Say Schedule paused; other job on device or Schedule
  paused; device output unknown, not Device stopped. Known foreign occupancy
  alone is not an uncertain mutation or a recovery latch.
- If controller-owned work was tracked, retain existing matching terminal and
  cleanup requirements; an empty new context after a crash proves nothing.
  Capture the observation baseline at acceptance and require later STATUS or
  action-bound cleanup evidence, not a heartbeat. Never weaken backend cleanup
  criteria to classify a foreign successor as safe. Missing/uncertain tracked
  cleanup remains outcome_unconfirmed and keeps its barrier. Device stopped /
  output-off observation requires its own fresh matching inactive evidence;
  confirmed schedule pause alone cannot establish it or clear another fault.
- Recover is confirmed only by that action's reconciliation result with fresh
  matching identity/boot and safe inactive/unowned state, cleared allowed
  in-flight barrier, and assignment still paused. Never clear a device fault
  merely to satisfy these postconditions.
- Schedule/assign/remove retain their existing persistence and cleanup gates,
  with a correlated result. Removal cannot claim cleanup from stopping a fresh
  empty context after a crash.

Keep a terminal-history pool of at most 256 records, each at most 2 KiB,
for up to 15 minutes after completion, in addition to the 64 per-context
active/window-protected slots. Total retained action records never exceed 320.
Evict oldest eligible terminal records first, but never evict
one before its original admission window has expired: otherwise an exact late
POST could execute again while the old ETag is still valid. Reserve record
capacity before side effects; refuse ordinary new work as Busy at capacity.
Pending/unquiesced work is never evicted. The existing 64 active/priority slots
also retain completed window-protected results when the terminal pool is full;
these slots are not reusable until safely retired. Keep one priority Pause slot
per context available, and do not admit another enable while that context's
reserved stop capacity is occupied. A full ledger cannot remove the first safe
Pause path for an active context. Never infer work_pending=true just because a
completed record is temporarily held in an active-capacity slot. Bound an
action reason to 128 UTF-8 bytes
and other projected diagnostic text to 512 UTF-8 bytes, with explicit truncation
indications and the serialized limits also enforced; never include credentials.
Operations snapshots carry only relevant latest/pending records, not the entire ledger.

The 45,000 ms browser mutation timeout releases the selection freeze but does
not cancel the server action. Keep unresolved action references in sessionStorage
for the tab (IDs/bindings only, no credentials and no saved device selection),
and resolve them through read-only action/status queries. A controller restart
changes incarnation; the volatile ledger is not reconstructed as successful.
Unknown/expired records remain Outcome unconfirmed until authoritative current
state is read and explicitly reviewed; do not blindly repeat a command or infer
its original outcome from present state. A new explicit action gets a new ID.
Correlation replay guarantees apply to retained records, not indefinite history.
After expiry/eviction the supported workflow is read/review and a new action ID,
never replay of an old action ID. Current generation/ETag/boot and ownership
checks still apply to every newly admitted command.

Uncertainty gates apply to current unresolved work/barriers. An action-bound
successful reconciliation or explicit authoritative readback/review may resolve
current eligibility without rewriting an unknown historical action as successful.
A display refresh alone cannot clear a safety/identity latch or persisted barrier.

### 5.2 Retained removal and restart safety

Retain the removed context's name/product, full expected identity, endpoint
settings references, last known boot/job, uncertainty, and cleanup evidence.
Never rebuild its target from a current catalog entry or a new assignment with
the same device ID. It remains reconciliation-only and counts toward the
existing combined 32-context capacity until actual cleanup is established.

The current removal notices contain only ID and takeover generation. They are
historical notices, not enough to restore an unresolved target after a crash.
Add a minimal private, atomic, independently versioned version-1 pending-cleanup
journal at `<active-INI-path>.wtp-fleet-contexts.json`. This is recovery authority,
not durable action history or analytics. Store the frozen target binding,
retirement ID/reason, operator-remove versus takeover kind, resolution
disposition, source assignment/ETag, last known execution association,
and unresolved barrier. Bound the journal to 32 contexts and 1 MiB, counting
overlapping assignment/journal entries as one logical context for admission.
Apply the assignment store's ownership, permissions, strict shape validation,
atomic rename, and fsync discipline; never store credential contents or
automatically reuse a retired WTP session.

Under the context reservation, inhibit new dispatch before persistence,
then persist retirement intent before deleting an assignment or discarding its
recoverable context. If intent persistence fails, retain the saved row, endpoint,
and process-local barrier; do not report removal accepted/complete or guess
whether a rename succeeded. Resolve storage uncertainty by authoritative
readback. Distinguish the intent's cause and disposition:

- Operator Remove preserves its current cleanup gate: durable intent, committed
  pause, confirmed owned-output cleanup, committed assignment deletion, then
  durable journal-entry removal. A cleanup refusal keeps the assignment paused
  and the recovery barrier intact; it does not claim the assignment was removed.
- Target-local takeover is authoritative independently of this controller's
  cleanup: durable takeover intent, committed assignment revocation, confirmed
  safe reconciliation/cleanup, then durable journal-entry removal. Retain the
  frozen removed context while unresolved; never stop a new foreign owner.

Every step is idempotent by the retirement ID and captured source binding.
Late internal worker callbacks also check generation/job/retirement identity,
not device_id alone, before clearing
in_flight or updating a row; another output's ETag change does not authorize
replacing that output's data with an old document. On restart, a matching
journal intent blocks automatic dispatch even if the assignment still exists.
A half-completed removal remains
paused/unresolved until explicit Reconcile; it must not resurrect a removed
assignment or erase a different replacement. Reject reassignment while an old
context is unresolved. If the saved row conflicts with the captured source
binding, hold the conflict for explicit review rather than overwrite either.
Remove the journal entry only after confirmed cleanup and a committed
resolution disposition. An existing assignment remains paused after an explicit
Reconcile of a failed Remove; require a new reviewed Remove to delete it rather
than silently finish an old failed request. An assignment whose deletion was
already committed remains removed and is never resurrected. The original
pending Remove may finish its requested deletion when its own cleanup succeeds;
if explicit recovery supersedes it, stale callbacks cannot later delete the
retained assignment. Failed writes retain the barrier. A corrupt/unavailable
journal blocks new
fleet dispatch, Resume, assignment, schedule edits, and removal, with a visible
error; it does not disable a safe Pause/owned-job stop for an already verified
in-memory context. Such a stop uses only that existing owner/binding and never
guesses an endpoint from corrupt storage. Report which pause/cleanup persistence
postconditions succeeded and which remain unconfirmed. Reconciliation may
observe a known retained context but cannot erase or rebuild a corrupt journal
as an automatic repair. At startup without a trusted restored binding, do not
open guessed targets. Preserve This Pi's independent runtime. A storage fault
requires explicit maintenance/review if authoritative readback cannot resolve it.
Legacy removal notices without recovery metadata cannot be represented as
reconciled contexts or used to guess an endpoint.

Keep the existing assignment file at version 1 and its closed shape unchanged;
this separate journal adds recovery duty without migrating valid assignments.
Load and validate both stores before starting any fleet worker. Do not infer
that absence of a legacy journal proves an old removed output was physically
off. View retained notices as history, not an affirmative cleanup receipt.

## 6. Controller-specific UI

Keep the global Controller connected indicator about the connection to the controller. Display selected-device availability separately.

While a remote device is selected:

- Hide the controller’s reboot/shutdown buttons on Operations.
- Hide the local reboot-behavior editor and local takeover controls.
- Hide controller-only GPIO correction or backend details unless equivalent selected-device evidence is explicitly supported.
- Label any retained clocks so they cannot be mistaken for device clock qualification.

Apply header changes only where this feature requires them; shared card headers on other pages must retain their intended behavior.

Preserve the existing local Operations behavior when This Pi is selected and when no fleet exists, including the existing single-selected-WTP-backend route.

When This Pi's selected backend is WTP, show This Pi (controller) and a nearby
Output via the identified endpoint explanation. Its controls still operate that
controller route; do not relabel the remote execution as physical RF on the Pi.
This differs from selecting an independently assigned output. Preserve existing
duplicate/self-target and compatibility-route exclusion rules.

### 6.1 Schedule-editor handoff

Expose a narrow FleetSchedules.openAssignedEditor(target) entry point instead of
duplicating its editor in Operations. Pass the captured incarnation, generation,
full ID, reviewed ETag, and return-focus context through a same-origin one-use
sessionStorage handoff to Setup's Fleet tab. Validate the handoff and refresh
committed state before opening; never use a display name or whatever is selected
on arrival. Opening the editor does not pause or save; enabled, in-flight,
unresolved, or replaced contexts explain why editing is unavailable.

Preserve a tab-local unsaved Fleet schedule draft, bound to its original target
and editor revision. If another draft exists, offer Continue existing draft,
Discard draft and open selected schedule, or Cancel; default to continuing the
draft. Do not silently overwrite it or update its reviewed revision while
retaining edits. A removed/replaced draft remains inspectable but cannot save
to its replacement. Consume only the handoff once, not the preserved draft;
clear a draft on confirmed save or explicit discard. Return navigation can
restore focus only when that control remains visible, enabled, and bound to
the current context. Otherwise focus the Device selector or Operations heading;
never select a remote device or focus a hidden controller action just to restore
focus. A full Operations load continues to default to This Pi. Announce semantic
state/action changes politely; do not announce every countdown tick or repeat
unchanged status on each poll.

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
- One initial read-only inspection, successful verified-context transfer,
  inspection timeout/late completion, no automatic second attempt, foreign
  sessions, and initial-inspection refusal for persisted in-flight/retired work.
  Verify the observation path never calls clock-setting/USB management helpers.
- Freshness boundaries at 14,999 and 15,000 ms, 1/8/3-second cadence budgets,
  conservative negotiation/STATUS/HTTP latency, hidden-tab resume, controller
  incarnation change, and same-ID reassignment with another generation.
- Server-side enable refusals for stale, foreign-owned, mismatched, faulted,
  cleanup-pending, or incompatible contexts, including a direct API request
  that bypasses browser disabled controls; retain pre-CLAIM/ARM checks.
- Lost POST responses resolved by action ID, duplicate/conflicting requests,
  bounded ledger expiry/capacity, pending-action priority pause, a late old
  result after reassignment, and unknown actions after a controller restart.
- GET while another context's mutation/reconciliation/fsync is blocked; all
  snapshots remain available without transport or lifecycle work. Verify both
  guarded new GET routes, method/query/shape rejection, escaped size limits,
  and existing Fleet Setup consumers of the changed mutation contract.
- Crash between retirement intent, assignment deletion, cleanup, and journal
  removal; corrupt/conflicting journal; no guessed endpoint, lost barrier, empty
  context cleanup claim, or automatic replay after restart.
- Successful foreign-owned observation versus actual handshake/STATUS failure;
  healthy occupancy becoming idle without Reconcile or reconnect. Preserve
  fault and identity latches instead of inferring health from readiness alone.
- An evicted action's identical POST with unchanged ETag: its admission window
  must have expired, and no new side effects occur. Cover protected-result
  saturation, reserved priority Pause, and mutation-clock discontinuity.
- Late old-incarnation POST/action/GET replies after restart, no-store/304 and
  decreasing-age samples, aggregate query limits, and protected Pi takeover
  observations with stale/mismatched boot or changed generation.
- Start-after-save assignment bypass attempts, corrupt-journal Pause for a
  verified live owner, failure before retirement intent is durable, operator
  Remove cleanup refusal retaining its row, and takeover removal remaining
  removed. Reconcile cannot silently delete a retained paused assignment.
- HTTP pool saturation with blocked transport/fsync/join work: cached GET and
  known-owner Pause admission remain responsive through the real HTTP adapter.
  At most the bounded accepted actions wait on owning executors.
- Actual manager replacement with an old delayed store/journal write or owned
  session: no concurrent writer, identity-lease collision, lost recovery duty,
  or late old-generation mutation of the new manager.
- Pause with no tracked owned work and fresh foreign-active, foreign-inactive,
  stale, and unavailable device observations. Confirm only the achieved scope;
  do not abort foreign work, invent Device stopped, or latch recovery solely
  for known foreign occupancy. Preserve all tracked cleanup/crash barriers.
- Draft-preserving one-use editor handoff, another device's unsaved draft,
  removed/replaced draft, ETag change during navigation, plain-identity wording,
  and This Pi controlling its compatibility WTP backend.

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

Document the 15-second freshness policy and its conservative age, one initial
read-only inspection versus explicit reconnect/recovery, action retention and
unconfirmed outcomes, the pending-cleanup journal's recovery duty, and the new
guarded host resources. No Pico documentation edit is required merely because
controller-only metadata was added; update it only for an actual companion change.

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

## 11. Design status and remaining gates

The maintainer adopted the initial inspection and all preceding review
recommendations. Sections 1–6 define their controller implementation policy;
no further product/workflow decision is currently open. The numeric freshness,
polling, payload, and retention limits are software policies to verify with
deterministic tests, not measured device performance or physical qualification.

No Pico firmware, WTP wire, or shared Control/1 schema change is currently
required by this design. Keep the coordinated validation gates regardless.
If production-path work discovers a missing companion contract or a necessary
reusable-component change beyond current authority, identify the exact owning
interface and stop at that boundary for any required approval. Do not broaden
the task speculatively. Application implementation, runtime validation, rendered
UI review, and documentation builds have not been performed by this plan update.
