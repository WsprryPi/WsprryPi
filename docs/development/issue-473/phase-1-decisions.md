# Issue 473: phase 1 contracts and proposed decisions

Status: ✅ phase-1 design-and-decision gate complete on 2026-11-15.
All D01–D12 approved. This record establishes design approval; see the
[phase list](../issue-473-phases.md) for current implementation status.
Baseline inspection date: 2026-11-15. This design record itself claims no
application qualification and authorizes no later-phase execution.

Read the [plan](../device-owned-solar-scheduling-plan.md),
[phase prompt](01-contracts-and-decisions.md), and
[execution rules](execution-rules.md). Recommendations below are proposals,
including numeric limits, have the approval status recorded below.

Implementation sequencing amendment (approved 2026-11-16): new Pi pairing/TLS
and controller-to-remote-Pi management are deferred to the
[end-of-phase-6 security milestone](deferred-pi-security.md). These approved
design contracts remain unchanged; phase 2 no longer claims their implementation.
Existing WTP/Pico TLS and the requirement for an automatic certificate-free
operator workflow remain unchanged.

## Evidence and authority

The Git/CI statements in this phase-1 acceptance record describe the design
closeout snapshot, before the user subsequently authorized committing, pushing,
and observing CI. That later delivery request does not authorize phase 2 or merge.

| Checkout | Inspected revision | Initial state | Authority exercised |
| --- | --- | --- | --- |
| WsprryPi | `505c6ddc12196c7b31c393f9ca01050faafd1737` | `devel`, clean | Source inspection, this document, phase indicator; feature branch subsequently authorized |
| WsprryPico | `d65df2aa68b1dfaba0267165a1226aa415a91d62` | `devel`, clean | Read only; no companion edit authority |
| WsprryPi-Docs | `a06c1d5590614a4794684aac794c468bec36b7cf` | `devel`, clean | Read only in phase 1; current operator behavior unchanged |

Working branch: `codex/issue-473-device-owned-scheduling`. No staged changes,
commits, pushes, issue posts, hardware access, or service operations. Repository
instructions were inspected in all three checkouts; no nested instruction file
was found under the affected Pi documentation/application paths. Source evidence
below is static inspection, not a test result or live observation.

### Source ownership and existing paths

Paths beginning `src/` or `WsprryPi-UI/` are relative to WsprryPi. Pico and Docs
paths explicitly name their sibling checkout.

| Boundary | Producers, consumers, validation, persistence, and recovery |
| --- | --- |
| Local configuration | `WsprryPi-UI/data/views/config.php`, `data/index.js` (`scheduleAutosave`, `flushAutosave`), and `data/site.js` produce local merge patches to `/config`. `src/config_handler.cpp` (`patch_all_from_web_revision`, candidate validation/persistence/public snapshot), `config_handler_deserialization.cpp`, `config_handler_serialization.cpp`, `config_types.hpp`, and `runtime_config_bridge.cpp` own the configuration lifecycle. `src/INI-Handler/src/ini_file.cpp` owns file replacement; its existing component is not authorized for edits. |
| Application/startup | `src/scheduling_runtime.cpp`, `scheduling_config.cpp`, `scheduling.cpp`, `runtime_config_bridge.cpp`, and `wtp_pi_control.*` apply configuration, startup quiescence, reload/launch checks, and local admission. `apply_enable_on_boot_startup_policy` implements Never/Follow/Always; route-restoration startup can force idle without changing boot preference. |
| Inbound Pi WTP | `src/wtp_endpoint/{authority,plain_listener,revocation_journal,runtime}.*` serialize local/remote ownership, reserve local output while Transmit is effective, implement local takeover, and persist revocation history. The listener is Plain LAN, not a general authenticated settings service. `src/wtp_integration/pi_status_client.cpp` reads endpoint observations; a status read is not write authority. |
| Browser/member settings | `src/wtp_integration/{browser_api.cpp,browser_http.hpp,configuration.cpp}`, `src/backend_http_guard.*`, and `src/wtp_runtime_bridge.cpp` validate browser intent, selected member, revisions, device/boot, carrier, and remote documents. `WsprryPi-UI/data/{wtp,wtp-management,gps,gray-line}.js` consume these resources. Current member management remains coupled to the active single-output connection. |
| Companion control | Pico `engineering/protocol/{control-policy.json,control-1.schema.json}`, policy checker, `src/network/management.cpp`, `src/usb/management_endpoint.cpp`, and `src/standalone/{config,progress,scheduler,storage}.*` own production resources. Pi `src/wtp_integration/contract/`, `control_contract.hpp`, `gps_contract.hpp`, `gray_line_contract.hpp`, `WsprryPi-UI/data/control-contract.js`, and `scripts/generate_control_contract.py` are consumers/generated adapters, not independent schema authorities. |
| Fleet | `src/wtp_integration/{catalog,fleet_store,fleet_schedule,fleet_runtime}.*`, `fleet_recovery.hpp`, and `WsprryPi-UI/data/{fleet-client,fleet-schedules,fleet-operation}.js` own profiles, assignments, dispatch, consumed slots, uncertain jobs, and explicit reconciliation. Assignments, catalog, and recovery journal have separate duties. Catalog removal or an empty catalog cannot prove retirement. |
| Pico recurrence/location | Pico `src/standalone/scheduler.cpp` admits WSPR/message work using distinct enable states, cursor reservation, clock readiness, ownership, daylight, and gray-line policy. `storage.cpp` persists configuration and cursor records. `src/time/gps_calibration.cpp` owns solar geometry; `src/hardware/gps_settings.*` and `src/time/gps*` own GPS freshness/calibration. |
| Actual integration fixtures | Pi `src/tests/{wtp_configuration_test,wtp_browser_api_test,wtp_pi_authority_test,wtp_pi_revocation_journal_test,wtp_pi_config_transaction_test,wtp_fleet_test}.cpp`, `WsprryPi-UI/tests/{wtp_configuration_interop_test,wtp_usb_pair_test,fleet_operations_pair_test}.js`; Pico `tests/{usb_management_driver,endpoint_driver}.cpp`, management/standalone tests; workflow recipes below. |

### Verified current capability matrix

“Missing” means no supported path established by this inspection; it must not
be filled by implicit controller behavior. Values describe current code, not the
proposed API.

| Capability | Local Pi | Remote Pi | Pico WSPR | Pico message recurrence |
| --- | --- | --- | --- | --- |
| Read/write | Local public config, revisioned PATCH/autosave | WTP execution/status and Fleet assignment; general target-config API missing | Selected-member resources over supported negotiated carriers | Message schedule resources over supported negotiated carriers |
| Persistence | Active INI plus normal runtime reload | Assignment lives on controller; target has its own local INI | Target configuration journal | Target message schedule and cursor journal |
| Apply/restart | Saved and effective differ during committed work/reload | WTP jobs are complete target executions; no remote general apply contract | Station, hardware, and progress domains differ; report pending restart | Saved schedule, pause-pending, in-flight recovery distinct |
| Recurrence owner | Pi application, including when its output is WTP | Existing Fleet recurrence is controller-owned, distinct from remote Pi's own local recurrence | Target standalone scheduler | Target standalone scheduler; message schedule inhibits WSPR scheduling when enabled |
| Scheduling enable | No new independent #473 switch yet | Assignment enable is not target-native enable | Standalone config `enabled` | Separate message `enabled` and pause operation; no atomic combined master |
| Transmit/boot | Operation.Transmit; Never/Follow/Always boot policy | Target's local permission governs inbound claim; controller assignment permission separate | No verified mapping to Pi Transmit/boot tri-state | No verified mapping to Pi Transmit/boot tri-state |
| Output ownership | Local effective enable reserves output between jobs; explicit takeover and uncertainty gates | Target WTP claim/session plus local authority/revocation | Standalone/local owner and external ownership guards | Same target output; pause/recovery has its own state |
| UTC | Existing system-clock scheduling; #473 health/holdover contract missing | WTP target clock admission; controller clock drives legacy recurrence | Explicit clock state/leap/uncertainty/holdover/date guards | Same clock guards plus message duration/expiry |
| Location | Saved locator; native GPS recurrence provider missing | Own saved locator exists locally; no supported remote location-write API established | Locator center or supported target GPS with freshness checks | Same receiver/daylight/gray-line inputs |
| Solar | New day/night/gray-line Pi policy missing | New target-owned solar policy missing | Existing always/day/night daylight gate plus morning/evening overrides | Existing daylight gate plus morning/evening message-frequency override |
| Frequency shape | Lists, skips, selection metadata, backend policy, CW base/shift | Legacy assignment job parameters are not Pi's full config | Single WSPR dial override per gray period; no independent day/night lists | Single message frequency per gray period; no Pi list/profile parity |
| Transport authority | Protected local HTTP guard and browser mutation checks | Plain LAN shared principal for inbound WTP; no authenticated management-write grant | Negotiated engineering mTLS / supported consumer / USB management; carrier-specific restrictions remain | Same negotiated restrictions; not all resources writable on all carriers |

Pico control policy is currently **prerelease**, major `1`, contract
`Wsprry-Control/1`, namespace `/api/control/v1/`, first stable release and frozen
digest both null. WTP, transport/authentication, storage, and shared control remain
independently versioned. Do not invent control v2 or a legacy control view.

## Approval register

Approved on 2026-11-15:

- **D02 workflow/behavior:** user: “Okay, that works for D02.” Automatic UI
  pairing, device-only keys, automatic lifecycle, and UI-based management-owner
  replacement; existing WTP unchanged.
- **D07 handover semantics:** user: “Yes, that works for D07.” Automatic known-
  controller schedule retirement and reconciliation; offline/unknown old authority
  leaves migration pending; normal Transmit-off/WTP-claim behavior is preserved.

- **D01, D03–D06, D08–D12:** user: “I approve the remaining ten decisions.”
  This includes the proposed parameters/bounds and authorizes finishing phase 1
  only. Phase 2, staging, commits, pushes, and hardware operation remain excluded.

All twelve decisions are approved. No unresolved behavior/security decision
remains in the phase-1 register. Exact design details are consolidated in the
[normative contract supplement](phase-1-contract-details.md), which incorporates
existing configuration types/semantics by pinned source reference and defines
pairing, rotation, migration messages, persistent records, and recovery states.
Production implementation, dependency conformance, and security/runtime testing
remain later-phase obligations, not missing decision approvals.

Numeric bounds below are approved design decisions, not claims about existing
code. Every use of “proposed” in these design artifacts means not implemented;
it does not mean the approved decision is awaiting approval.

| ID | Recommendation | Alternative and tradeoff | Phase owner | Approval |
| --- | --- | --- | --- | --- |
| D01 | Explicit target adapters and independent edit context; controller-only mode is an explicit startup choice with zero outputs | Reuse active WTP backend selection: fewer paths, but changes connection/ownership while browsing and violates the plan | 2–3 | Approved |
| D02 | Automatic UI pairing, device-only keys, certificate-free management transport, automatic reconnect/renewal, and target-local Replace/Remove controller; see [pairing proposal](phase-1-pairing.md) | Manual provisioning is rejected by the user; if the automatic lifecycle cannot be delivered, omit new remote Pi management. Existing WTP remains unchanged | 6: deferred security gate | Approved: workflow/behavior |
| D03 | Closed current-control envelopes, full-candidate atomic Pi saves, revisions, durable operation IDs, mandatory authoritative readback after uncertainty | Loose patches or blind retries simplify clients but permit stale enables and duplicate actions | 2, 4, 7 | Approved |
| D04 | Separate durable schedule enable, solar enable, Transmit, boot preference, and RF authority; disable drains an accepted complete occurrence | Reusing Transmit as scheduler enable changes ownership and stopping semantics | 4–6 | Approved |
| D05 | Durable reservation before launch, at-most-once occurrence admission, per-list cursors, conservative skip after uncertain crash | Post-launch checkpointing risks repeats; exactly-once RF execution cannot be proved after a crash | 4, 6 | Approved |
| D06 | Native Pi solar uses saved transmitter locator center only; new clock-health provider with explicit bounds below; GPS ingestion stays out of scope | Native Pi GPS adds receiver/site/freshness/provider work; browser/controller position breaks autonomy | 4, 6 | Approved |
| D07 | Automatically retire known controller schedules and reconcile jobs; offline/unknown authority leaves migration pending; retain existing WTP and Transmit-off claim behavior | Management replacement can proceed independently; it does not prove schedule retirement. No permanent exclusion of a fresh Plain LAN client is promised | 5 | Approved: handover semantics |
| D08 | Expose separate supported Pico WSPR/message controls and existing solar resources; unsupported controls read-only; no new Pico profiles or atomic master | Full parity requires separately authorized firmware and paired contract work | 2–3, 7 | Approved |
| D09 | Preserve old valid Pi recurrence when new settings are absent: scheduling=true, solar=false; no automatic Fleet migration; retain unchanged unsupported values without admitting them | Defaulting scheduling=false would silently stop existing recurrence; normalization would destroy custom settings | 4–6 | Approved |
| D10 | New Scheduling editor uses explicit Save, readback, and Enable saved revision; existing ordinary local controls retain autosave | A combined save-and-enable is possible but complicates failed persistence/activation and per-target drafts | 3, 7 | Approved |
| D11 | Fixed resource/write/forecast/wake bounds below, refusal on exhaustion; engineering status distinguishes saved/active/forecast/executed | Unbounded searches, queues, or poll writes threaten availability and durability | 2, 4, 6 | Approved |
| D12 | Target-local complete candidate validation before any write; identical selected policy and frozen intent at both pair starts, even if overridden frequencies happen to match | Comparing only frequencies loses distinct policy/rotation semantics | 4, 6 | Approved |

**User-directed correction:** manual certificate provisioning and user-facing
credential administration are rejected. D02 now proposes automatic UI pairing
and ownership replacement. Its detailed [pairing workflow](phase-1-pairing.md)
is engineering-only and not implemented; its workflow/behavior is approved. Existing WTP contracts must
remain unchanged. The earlier D07 lane-closure exception is withdrawn, not an
approval request. The compliant automatic handover semantics below are approved
as D07; their implementation and validation remain outstanding.

## Proposed management and configuration contracts (D01–D03, D08–D11)

### Shapes, types, and capability negotiation

All objects below are closed: exact listed keys, required nullable fields, no
duplicate JSON keys, no coercion, nonfinite numbers, or unknown enum values.
UTF-8 JSON maximum 32768 bytes, nesting at most 16; HTTP headers at most 8192
bytes. No compressed request bodies, queries, redirects, or arbitrary resource
paths. Integer values outside JSON's exact integer range use canonical unsigned decimal
strings (`0` or nonzero leading digit; at most uint64). IDs are 32 lowercase hex;
digests/revisions are 64 lowercase SHA-256 hex. Strong quoted HTTP ETags contain
the same revision. Version/shape validation precedes field access.

Proposed Pi resources under the existing current namespace:

| Request | Body | Success |
| --- | --- | --- |
| `GET /api/control/v1/targets` on controller | None | `{contract,schema:"targets",targets:[Target]}`; at most 64 enrolled targets; discovery observations separate and never writable |
| `GET /api/control/v1/targets/{id}/context` | None | Context below, authenticated target read; never a claim |
| `GET /api/control/v1/targets/{id}/settings` | None | Settings snapshot below and ETag |
| `GET /api/control/v1/targets/{id}/status` | None | Status below; age-qualified observations, no claim |
| `PUT /api/control/v1/targets/{id}/settings` | Save below plus If-Match | Operation receipt and new revision |
| `POST /api/control/v1/targets/{id}/enable` | Enable below plus If-Match | Operation receipt; enabled only for that exact saved candidate |
| `POST /api/control/v1/targets/{id}/disable` | Action below plus If-Match | Receipt; no new admissions; accepted occurrence may drain |
| `POST /api/control/v1/targets/{id}/stop` | Stop below plus If-Match | Receipt; request cancellation, never assert output inactive from acceptance alone |
| `GET /api/control/v1/targets/{id}/operations/{operation_id}` | None | Durable receipt, or explicit unknown; no inference from HTTP timeout |

At the target endpoint, the same ID must equal self identity; the controller
routes only an enrolled ID to its pinned endpoint. Local adapter uses the same
validators/transaction owner in process. No browser URL or filesystem path is a
proxy destination. Pico adapter retains current resource methods/payloads and
limits from the authoritative schema; these proposed Pi resources do not magically
exist on Pico. Controller routes unsupported Pico operations to a typed refusal,
not a local Pi write or multiple hidden member writes.

Definitions (brace notation lists exact keys; `contract` is always
`Wsprry-Control/1`):

- `Target = {device_id,product,name,profile_generation,connection_generation}`.
  Product is `WsprryPi|WsprryPico`; name is 1–80 UTF-8 bytes with no controls.
  Generations are nonzero canonical decimal strings. They change on profile or
  connection replacement, respectively; never compare ordering across devices.
- `Binding = {device_id,boot_id,profile_generation,connection_generation}`;
  target boot ID is an identity, not a controller timestamp. Target validates
  device/boot; controller validates both profile and connection generations before
  forwarding. An already-forwarded operation stays attached to its old binding.
- `Context = {contract,schema:"target-context",target:Target,binding:Binding,
  capability_revision,capabilities:[Capability],configuration_revision}`.
- `Capability = {resource,schema_id,schema_digest,read,write,activation,maximum_bytes}`. Resource is one
  of `pi-config|pi-scheduling|pico-wspr|pico-message|pico-gray-line|stop`;
  booleans are negotiated permissions, activation is
  `immediate|next_occurrence|restart|unsupported`, maximum_bytes is integer
  0..32768. schema_id is the exact resource ID defined in the normative supplement; schema_digest
  is its canonical SHA-256 digest. Resources unique, at most six. Only complete matching descriptors
  authorize editing. Unsupported resource has write=false. Revision hashes the
  exact current descriptors and effective permissions.
- `Snapshot = {contract,schema:"target-settings",binding,capability_revision,
  revision,candidate_digest,saved,active_revision,pending_application,observed_output}`. `saved` is
  `{configuration, scheduling}` for Pi. Active revision is digest or null;
  pending_application is `none|next_occurrence|restart|held|unknown`.
  candidate_digest is supplied by the target for the complete saved candidate;
  clients echo it for enable and never derive it from redacted/private state.
- `Status = {contract,schema:"target-status",binding,revision,active_revision,
  observed_output,observation_age_ms,schedule_enabled,solar_enabled,transmit_effective,
  execution_owner,destination_id,authority_state,phase,waiting_reason,clock,selection,
  selected_frequencies_hz,next_start_utc_ns,forecast,prolonged_window}`.
  execution_owner is target device ID; destination_id is its actual output device
  ID or null if unavailable/local output. phase is
  `idle|waiting|prepared|accepted|running|draining|fault|unknown`; waiting_reason is
  `none|disabled|transmit_off|startup|authority|ownership|storage|clock|location|solar|frequency|recovery|unknown`.
  Age is uint32 milliseconds, null if unobserved. clock is
  `{state,uncertainty_ns,holdover_age_ns,generation}`, state
  `synchronized|holdover|unsynchronized|unknown`, uncertainty/age decimal strings
  or null. selection is `ordinary|day|night|morning|evening|unknown`; frequencies
  are at most the current complete occurrence's tone count, decimal Hz strings
  as defined below. next_start is decimal UTC ns or null; forecast is
  `known|unknown`; prolonged_window is boolean or null. Remaining identifiers,
  revisions, booleans, and authority states use the types below. Status is never
  a substitute for fresh settings/context negotiation before a write.
- `Save = {contract,schema:"target-save",binding,capability_revision,
  operation_id,expected_revision,candidate:{configuration,scheduling}}`.
- `Enable = {contract,schema:"target-enable",binding,capability_revision,
  operation_id,expected_revision,candidate_digest}`. Digest binds the canonical
  complete saved candidate (including disabled enable state). Enable changes only
  scheduling.enabled after comparing digest/revision and revalidating readiness,
  authority, capabilities, and current policy. Missing solar time/location may
  leave an otherwise valid enabled schedule held; transactional storage/identity/
  authority readiness is mandatory. It cannot set Transmit or boot.
- `Action = {contract,schema:"target-action",binding,capability_revision,
  operation_id,expected_revision}`. This is the disable route's body;
  no arbitrary command field is accepted. Disable is safe while held. Stop uses
  the separate shape below and does not implicitly clear scheduling.enabled.
- `Stop = {contract,schema:"target-stop",binding,capability_revision,
  operation_id,expected_revision,observed_output:{generation,owner_id,job_id}}`.
  Output generation is a nonzero uint64 decimal string changed on every output
  ownership/job transition, owner/job are IDs or null. Compare all three under
  the runtime lock before cancellation; changed job returns 409 rather than
  stopping a later unobserved occurrence. Settings/status snapshots must include
  `observed_output` for this purpose, including when idle.
- `Receipt = {contract,schema:"target-operation",binding,operation_id,
  request_digest,state,revision,error}`. State is
  `committed|rejected|pending|unknown`; revision is digest or null; error is
  null or `{code,retryable}`. Never return credentials in receipts.

`configuration` is the canonical saved-settings projection defined exactly in
the [normative supplement](phase-1-contract-details.md), incorporating the pinned
public Pi configuration serializer/deserializer and semantic validators. Runtime
Platform observations and internal/legacy Meta fields are not candidate settings.
No new mode, timing, or RF model is defined here. Private WTP credential references
stay server owned and are preserved under the same revision lock, not overwritten
with redacted placeholders. Read-only saved echoes must match their snapshot, not be trusted as writes.
Phase 2 materializes this already-defined closed contract into production schema
code and paired consumers; it may not accept arbitrary keys or rely solely on
existing merge-patch permissiveness.

New Pi `scheduling` shape:

```text
{enabled:boolean, solar:{enabled:boolean, location:{source:"locator", locator:string|null},
  day:Ordinary, night:Ordinary, morning:Gray, evening:Gray}}
Ordinary = {allow:boolean, wspr_override:WsprList|null, cw_base_hz:decimal|null}
Gray = {enabled:boolean, lower_mdeg:integer, upper_mdeg:integer,
        wspr_override:WsprList|null, cw_base_hz:decimal|null}
```

Locator: uppercase valid Maidenhead 4, 6, or 8 characters, evaluated at its cell
center, explicitly identifies transmitter site. On migration copy only a valid
saved transmitter locator; otherwise persist null with solar disabled. Null
never makes feature-off configuration invalid and holds solar admission; changed
non-null malformed text is rejected. No browser/controller location is copied.
Bounds: -90000 <= lower_mdeg <
upper_mdeg <= 90000. Morning/evening defaults disabled, -6000..6000. Day/night
default allow=true; all frequency overrides default null (inherit). `cw_base_hz`
is a positive canonical decimal string with at most nine fractional digits,
using the current Pi base/shift sign model and backend-qualified bounds for every
derived tone. Integers have no trailing dot; fractional text has no trailing zero.
No conversion from absolute RF to USB dial is implicit.

`WsprList = {"Frequency", "Frequency Profile", "Band Preferences"}` reuses
the exact current Pi WSPR list/skip/selection representation
from public configuration, deserialization, and selector policy. Profile is
`existing_common|wrc15`; frequency and preferences retain their current accepted
types and token/skip grammar, validated by those owning parsers; it is never a
Pico single-dial substitution. Its maximum length, supported frequencies, metadata,
and backend limits remain those of the current Pi validator, not invented broader
bounds. Full schema generation must include that representation. Changed values,
including disabled overrides, require full policy validation. Unchanged values
that the existing contract permits retaining may be saved with disable/repair;
retention is never admission authority. An enabled candidate with unusable values
is held, not normalized. Future schema changes remain coordinated current-control
changes; recheck Pico lifecycle policy before implementation.

### Atomicity, concurrency, and uncertainty

One target writer serializes configuration, schedule, enable state, authority
checks, and operation journal updates. Save does not silently enable: new editor
saves scheduling.enabled=false, reads back, then explicitly enables its exact
revision/digest. Editing a running schedule first requires explicit Disable and
drain (or Stop); never disable merely by focusing/editing a field. Until disable
is confirmed, drafts remain local and the old schedule's confirmed state is shown.
Other existing autosave settings retain their current explicit lifecycle guards.

When confirmed scheduling is off, dependent controls are semantically disabled.
The Enable scheduling switch can first express an unsaved draft enable intent,
unlocking fields without a target write. Label it “Not enabled — changes pending”
with a nearby Save and enable action. That action saves disabled, reads back, and
enables only that revision. A failed save/readback/enable preserves the draft and
last confirmed target state separately. Clearing draft enable intent disables the
fields again without clearing values. A running schedule instead offers explicit
Disable before editing; selection and edit visibility never stop it.

If-Match and expected_revision must agree. Revision includes private-preservation
state and every settings mutation, preventing stale copies from overwriting secrets.
Recheck boot/capabilities/configuration/authority under the commit lock. Stage a
complete private file, sync file, rename, sync parent; publish runtime state only
after durable success. Use a parent-owned transaction/WAL if existing INI and new
state need multiple records: recovery resolves one commit decision before any
admission, never treats two independent renames as atomic. Component edits need
separate scope authority. Disk error after rename is uncertain and inhibits new
admission until authoritative recovery, not success or automatic rollback.

Operation IDs are durably bound to authenticated principal, binding, route,
canonical request digest, and result in the same transaction. Same ID/same request
returns its existing result; same ID/different request is 409. Retain last 256
receipts per target plus every unresolved receipt; cap unresolved at 32 and refuse
new mutations at capacity. No receipt eviction while its outcome is unresolved.
A retired/expired receipt returns unknown and requires settings/authority readback,
not replay. Reboot refuses old-binding replay; operation lookup can return a
historical binding but cannot relaunch it. Reads have no mutation side effects.

HTTP refusals: 400 malformed, 401 unauthenticated, 403 unauthorized/origin,
404 unknown enrolled ID/resource, 409 binding/capability/idempotency/authority
conflict, 412 revision mismatch, 413 size, 422 invalid candidate, 428 missing
precondition, 429 rate limit, 503 unavailable/storage/recovery hold. Errors use
`{contract,schema:"target-error",error:{code,retryable}}`; no arbitrary exception
text. Unknown delivery is a client state even if no error body arrives.

On target switch, retire callbacks/timers, preserve drafts by stable ID and
profile generation, and keep accepted writes on their original target ledger.
Fresh context is required after reconnect/reboot; late data never overwrites the
new target's draft. Canonical hashing uses UTF-8 compact JSON with object keys recursively sorted
by Unicode code point, arrays ordered, no insignificant whitespace, and the
serializer's shortest round-tripping finite numeric representation (integral
values within the exact JSON integer range use integer form, and -0 becomes 0).
Strings retain Unicode code points without normalization, literal UTF-8 except
required quote/backslash/control escaping; controls use lowercase hex escapes.
Hash semantic parsed candidates, not raw request bytes. The Pi target computes these hashes using its pinned nlohmann JSON serializer
with the normalization above; consumers compare/echo target-supplied digests,
never reserialize to guess them. Producer tests must pin canonicalization vectors,
including integer/float-equivalent values, exponent spelling, and escaping. Pico
resources continue to use their authoritative existing revision algorithms.
Readback after uncertainty compares binding, operation receipt,
and complete candidate; disagreement exposes conflict and preserves both draft and
confirmed saved state. No automatic draft resubmission or secret copy to a new ID.

## Threat model and remote reachability (D02, D11)

Threats include hostile LAN discovery/DNS, forged browser requests, compromised
profiles, stale controllers, response substitution, proxy SSRF, leaked credentials,
and resource exhaustion. An authorized OS administrator remains trusted; this
proposal cannot resist root changing state or the system clock.

- New Pi management uses the [automatic pairing lifecycle](phase-1-pairing.md):
  target UI opens Pair, controller UI consumes a temporary code, device backends
  derive/store pairing keys, and reconnect/rotation/replacement run automatically.
  No certificate provisioning, paths, fingerprints, or key downloads are exposed.
  TLS 1.3 PSK with fresh (EC)DHE authenticates this new management service; it is
  independent of WTP and does not change existing TLS or Plain LAN profiles.
- Target-local Replace/Remove controller durably revokes old management authority,
  closes its sessions, and fences writes by authorization generation. The previous
  controller need not be online. RF ownership changes do not alter pairing.
  Pairing does not enable schedules, claim RF, or change accepted jobs/settings.
- Keys stay in service-user-owned 0700 directory/0600 regular files, no symlinks
  or browser-selected paths. Browser sees device names, pairing status, permissions,
  and the temporary pairing-code workflow only. Redact keys, codes, passwords,
  and authenticated request bodies from logs/support data and browser storage.
- Controller browser remains under existing protected network/Host/Origin policy;
  remote management is disabled in insecure-disabled mode. Mutations require
  exact same Origin, same-origin Fetch metadata when present, JSON content type,
  existing request-intent header, and a cryptographically random session CSRF
  token, cookie SameSite=Strict/HttpOnly (Secure on HTTPS). No wildcard CORS,
  cross-origin mutation, or Origin:null/missing acceptance. Paired-key machine endpoint
  accepts machine calls only and does not use cookie authority. Existing LAN
  browser access is a trusted-admin model, not protection from hostile same-LAN
  users; public exposure requires a separately configured authenticated frontend.
- Enrolled remote profile contains one exact host/address and port. Resolve and
  validate on each connection; pin one resolved address for that connection.
  Allow explicitly approved RFC1918 IPv4 or ULA IPv6 unicast only; reject loopback,
  link-local/metadata, multicast, public addresses, controller self, and ambiguous
  IPv4-mapped encodings. Local adapter is not reachable via the proxy. No redirects,
  environment proxies, userinfo, arbitrary path/port overrides, or automatic peer/
  carrier fallback. Address changes require revalidation plus same pinned identity;
  identity replacement requires the ordinary Pair again workflow; routine key
  renewal is automatic within the existing pairing.
- Bounds: 3 s DNS, 3 s connect, 10 s handshake, 8 s I/O, 24 s entire operation;
  one mutation per target, at most four concurrent remote connections per
  controller, 64 profiles, 32 unresolved operations, 10 requests/s/client with
  burst 20. Poll observations no faster than 1 Hz; no automatic mutation retries.
  TLS failures hold management without changing target recurrence. Management
  authentication does not acquire a certificate-validity/UTC prerequisite.
- Revocation removes the paired manager grant, bumps authorization generation, closes
  its sessions, and checks the new grant under every mutation commit lock.
  Committed changes remain recorded; revocation cannot undo RF or accepted jobs.
  Replacement uses the automatic pairing lifecycle, never fallback credentials.

## Admission and state tables (D04, D09, D12)

Pi state variables: S schedule-enabled; P solar-enabled; T effective Transmit
permission after boot/takeover; R existing startup/config/storage/runtime readiness
(including existing mode/backend time guards, excluding added solar prerequisites); A valid
target-native recurrence authority; O output state. Conditions are conjunctive;
the following ordered table partitions admission. Manual/test/WTP paths continue
to use their own guards; scheduler state never grants their permission.

| First matching condition | New recurrence | Ownership / accepted occurrence |
| --- | --- | --- |
| Startup records unloaded, invalid, or safety/output state unknown | Hold, even if saved S/T true | Preserve uncertainty; no claim/selector preparation |
| Authority transition/legacy work unresolved or A=false | Hold | Reconcile; do not infer retirement from reboot |
| S=false | No admission | Preserve local reservation determined by T and authority fence; accepted complete occurrence drains |
| T=false or local enable awaiting takeover | No admission | Existing external job follows explicit finish/stop choice, not schedule enable |
| O externally owned, cleanup pending, or faulted | Hold | No stealing ownership; existing recovery rules apply |
| R=false | Hold | Preserve local reservation; show typed readiness reason |
| P=false and all prior guards pass | Existing recurrence planning | No added solar location/date/health prerequisite |
| P=true and missing location/UTC readiness/date | Hold | Never ordinary-policy fallback or implicit GPS fallback |
| P=true and policy says skip | Normal waiting | Keep ownership; no selector prep; no cursor advance for policy refusal |
| Eligible policy, valid frozen complete intent and commit generations | Reserve durable occurrence, then admit | Complete occurrence may finish through a solar boundary or S disable |

Boot behavior (Cartesian product with the admission table): Never sets T=false;
Follow uses saved T; Always requests T=true. Existing startup-idle overrides and
quiescence inhibition take precedence. All leave S/P unchanged. S=false survives
all three boot choices, so Always cannot resurrect disabled recurrence. A saved
S=true/T=false remains stopped. Migration of missing settings uses S=true/P=false
only to preserve existing valid local behavior; Fleet rows are never converted.
A native migration still needs A before any new enable, including direct API/INI.
Unmigrated existing local recurrence stays on its current path and guards; the
new authority gate is not an automatic upgrade-time shutdown or a bypass allowing
new settings to activate on that old path. At explicit handover, retire that
legacy local recurrence owner as well as any controller assignments.

| Event | Durable effect | In-flight work and restart/recovery |
| --- | --- | --- |
| Enable S | Exact saved revision enabled, no change to T/P | No immediate bypass of slot/readiness guards |
| Disable S | Persist false before acknowledgement | No further recurrence; full accepted pair/message can drain; after crash do not replay it |
| Immediate Stop | Cancellation request plus progress outcome | Stop is separate from disable; S may remain true and a later eligible slot may run. UI must say so; offer explicit disable plus stop when both are intended |
| Solar toggle/save | Only explicit saved candidate changes P/policy | Existing cancellation/reload guards still apply; prepared uncommitted intents invalidated |
| Transmit off/on | Existing disable/takeover policy | May stop work under current rules; S never substitutes for that transaction |
| Reconnect / browser close / controller exit | No target configuration change | Target recurrence continues; management never holds an RF claim |
| Config/site/clock generation change | Invalidate prepared intent | Frozen accepted intent remains subject to existing launch/cancel guards; no retargeting |
| Restart | Recover settings, authority/WAL/progress before admission | No replay/catch-up; unknown output/jobs hold until reconciled |

Pico uses its existing WSPR enabled and message enabled/pause-pending states,
not this new Pi S/T/boot product. Show each supported schedule, conflict/refusal,
and active/saved status. An atomic cross-schedule toggle is unsupported. Unverified
Pico Transmit or boot mappings remain read-only. Reusing existing resources does
not authorize new firmware behavior or override Pico clock/safety policy.

## Recurrence progress and clock policy (D05–D06, D11–D12)

Proposed Pi progress record `pi-recurrence-progress/1` is independent of control
and WTP versions. Exact keys: `{schema,device_id,authority_generation,
configuration_generation,clock_generation,consumed_through_utc_ns,cursors,in_flight}`.
Generations/watermark are uint64 decimal strings. `cursors` has exactly
`ordinary,day,night,morning,evening`; each is
`{list_digest,index}`. Digest is SHA-256 and index uint32. In-flight is null or
`{occurrence_id,first_start_ns,last_start_ns,intent_digest,state,successor_cursors}`;
state is `reserved|uncertain`; successor_cursors has the same five closed cursor
keys and row types. Main cursors remain the last resolved progression. IDs/digests use the
management types. Validate cursor bounds against the saved list before use.
Freeze realized random placement in the reserved intent and retain it for the
complete occurrence; a retry never draws another offset. Future unreserved work
continues to use the existing random-placement distribution.

Before selector preparation/ownership acquisition, evaluate one intended-start
snapshot; before actual commitment, recheck every generation and frequency guard.
Durably reserve occurrence ID, latest paired start as consumed watermark, frozen
intent digest, and proposed successor_cursors together before launch can be
attempted. Publish the successor as main cursors at terminal resolution only
for accepted work; a known refusal before acceptance retains the previous cursors.
Existing complete job acceptance remains the runtime linearization point; a crash
between reservation and acceptance can lose one occurrence but cannot replay it.
A refusal before reservation advances nothing. Reservation is not acceptance:
Disable racing a reserved intent prevents its acceptance under the same lock;
only an already accepted complete occurrence may drain. Reserved-but-unaccepted
slots stay consumed conservatively and cannot be retried after re-enable; a
known acceptance refusal does not advance list rotation. After a crash with
unknown acceptance, reconcile first; if output is safe but historical acceptance
cannot be proved either way, conservatively consume the proposed successor once.
This is the approved at-most-once crash policy, not a known-refusal retry. An explicit skip entry consumes
one normal slot/cursor and persists it; no output is prepared. Each explicit
period list advances independently; null/inherited periods share ordinary cursor.
A complete WSPR pair advances once. Pair starts must select the identical policy
identity and complete allowed intent; equal frequencies from different policies
are insufficient. Missed/held slots are not enumerated or accumulated.

Cursor/list digest changes reset only the changed list's successor according to
existing first-entry/random-selection policy, with a new configuration generation;
unchanged lists retain progress. Terminal resolution checks the frozen list
digests/current configuration under the transaction lock: publish successor
entries only for lists still matching that reservation; never overwrite a new
list's reset cursor with an older occurrence's successor. Resolve the consumed
watermark/in-flight record independently, without replay. The consumed UTC watermark never decreases on
edit, disable, clock rollback, migration, or restart. Old occurrence/job IDs stay
retired across generations. Backward steps wait until time passes the watermark;
forward steps recompute the first future eligible start. No catch-up bursts.

File replacement follows the transaction discipline above, private regular files,
checksummed complete records, sync file/parent, and crash injection at every write
boundary. Maximum two progress commits per admitted occurrence (reservation and
terminal resolution), one per explicit skip, plus explicit settings/authority
transactions; none for polls, solar refusals, or forecast evaluation. A terminal
write failure leaves uncertain in-flight state and blocks further admission.
Reservation without acceptance cannot prove RF happened: reconcile output, then
skip the consumed occurrence. Lost/corrupt progress for a previously initialized
schedule is a recovery hold, never an empty default. Initial creation requires a
clean authoritative installation record. Disk retention is bounded to current
record plus transaction recovery; no deletion of unresolved state.

Proposed native Pi solar clock provider: read-only Linux `adjtimex` health plus
CLOCK_REALTIME/CLOCK_MONOTONIC mapping, with CLOCK_BOOTTIME for elapsed
holdover age across suspend; no clock-setting privilege. Synchronized
means TIME_OK, STA_UNSYNC clear, no leap alarm/insert/delete, estimated kernel
maximum error <=250 ms, and date in [2025-01-01T00:00:00Z,
2100-01-01T00:00:00Z). These are trust in the configured OS time discipline, not
proof of UTC accuracy. No RF calibration or PPS inference is allowed.

Poll health at most once/s (fresh commitment checks are event-driven, not polls); synchronized samples may refresh readiness. On loss
of synchronization after a healthy sample in this boot, allow holdover for at
most 3600 s and uncertainty <=250 ms, growing from last maximum error at 50000
ppb (50 microseconds/s). Unknown/unbounded kernel error, leap alarms, unavailable
provider, or no healthy sample this boot holds new solar admissions. Do not carry
holdover across reboot. Step detection compares realtime and monotonic deltas;
absolute discrepancy >100 ms, a suspend/resume discontinuity, or any clock-health
transition increments clock
generation and invalidates preparation. Check again at commitment and retain
existing tighter backend/WTP launch bounds. These added solar prerequisites do
not change feature-off legacy clock behavior.

Geometry reuses Pico's implementation/provenance, not a new approximate sunrise
library: unrounded elevation; day >= -0.833 degrees; qualifying morning/evening
window first, otherwise day/night. Gray lower inclusive, upper exclusive; solar
hour angle determines rising/falling, including polar/noon/midnight cases. Keep
MIT attribution and reference vectors from Pico `src/time/gps_calibration.cpp`.
Pi native GPS remains unsupported; no silent locator fallback from a GPS choice.
Pico retains target-owned GPS enabled/source/future-time/maximum-position-age
checks (default saved age 30 s; advertised maximum 3600 s), its own clock readiness,
and its independently configured calibration age. Do not replace those limits
with the proposed Pi OS-clock policy.

Waiting wakes on relevant events or at the next slot/health deadline, with a
maximum 1 Hz health poll and no solar busy loop. Forecast is bounded to 48 hours
and at most 4096 candidate starts, with unknown beyond that; geometry observations
include age and prolonged-window indication, never propagation claims. Forecast
and observed execution are separate status fields.

## Approved legacy recurrence handover semantics (D07)

The initial proposal to close the shared Plain LAN claim lane is withdrawn by
the unchanged-WTP constraint. Its target-fence schema and handover/rollback table
are no longer candidate contracts. Do not implement that exception.

Existing revocation advances target-local takeover history and fences old
sessions, but a fresh Plain LAN session uses the same shared principal. Management
pairing can automatically revoke an old manager's settings access; it cannot
identify that manager's legacy WTP scheduler without a changed WTP authority
contract. Reboot, pairing replacement, an empty catalog, or a discovered idle
output cannot prove old assignment retirement or resolve unknown accepted jobs.

The plan's no-overlapping-authorities, positive retirement, offline-controller,
crash/recovery, rollback, and clean-install requirements remain binding. The user
approved the automatic handover semantics below while preserving existing WTP.
No manual credential workaround or implicit claim-lane change is authorized.
Until a particular migration completes, that target's new native enablement stays
blocked; existing valid local/Fleet behavior remains under its existing guards.

### Approved resolution: automatic known-controller handover

Keep existing WTP ownership/admission rules. A migration action can automatically
pause the known old controller's assignment durably, reconcile its pending jobs,
and then use the target's existing local-enable/takeover transaction. It must not
silently set Transmit merely because scheduling was enabled. If an accepted job
exists, use the existing explicit finish/stop choice, bound to the observed job.
Scheduling disable leaves effective local Transmit/ownership unchanged.

For offline or unknown old authority, the approved behavior is
**migration pending** until positive retirement/reconciliation is possible, with
read-only observations and automatic recheck on reconnect. Do not block D02
management-controller replacement on that old controller; access can transfer
while recurrence migration remains pending. Retain both facts in target status.
No manual credential operation is needed in either path. Unexpected output/job
uncertainty remains an existing recovery hold, not an automatically assumed idle.

The stronger alternative—allow offline migration while guaranteeing the old
scheduler can never regain control—cannot be promised under unchanged Plain LAN
WTP. Existing local Transmit prevents new remote claims while effective; turning
local Transmit off can make the output available to a new remote session again.
That is current behavior, not proof that an old schedule is permanently retired.
Choosing local reservation in lieu of positive retirement would require an
explicit amendment to the plan and acceptance of this later-claim behavior.

Approved: (1) automatic known-controller retirement, (2) offline/unknown prior
authority leaves migration pending, and (3) normal existing Transmit-off/WTP-
available behavior remains acceptable after migration. Scheduling disable alone
does not release local ownership. Existing client revocation reconciliation and
durable assignment retirement must be exercised through production paths before
any automatic-handover claim; an unavailable controller-management path remains
pending, never simulated success.

Persist migration progress and retain existing assignment/recovery evidence.
Crash/restart resumes reconciliation with new native admission blocked until
retirement is confirmed; a reboot or missing record cannot imply success. Stage
new settings disabled and read back before a separate explicit enable. Rollback
first disables the new recurrence, drains/reconciles accepted work, and verifies
that state before automatically resuming the explicitly selected old assignment.
Unknown rollback outcomes stay pending. Clean-install initialization needs
positive target-local evidence of no prior authority, not merely an empty catalog.
These are required implementation/validation obligations under the approved
semantics, not claims that current code implements the migration workflow.

Pico firmware still requires explicit edit authority for any new shared behavior.
A Pi configured with WTP output remains the recurrence execution owner, with a
separate destination; selecting that output is not native recurrence migration.

## Implementation and negative-test map

These are required future checks, not executed or passing checks. Phase 1 adds
no production interfaces, so compiling/running application, interoperability,
Sphinx, or hardware suites is not phase-1 evidence.

| Decisions/invariants | Production ownership | Required tests and documentation |
| --- | --- | --- |
| D01–D03 target isolation/security | Browser API/HTTP guard, runtime bridge, catalog, new parent-owned Pi management adapter; Pico producer/consumer when changed | Two targets, zero outputs, no hardware init in controller-only mode, wrong identity/boot/profile/capability/revision; late responses, SSRF/DNS/redirect/origin/CSRF/auth/revocation, limits, lost replies, duplicate IDs, no writes on refusal. Extend API/config transaction fixtures and UI management tests. Docs Setup, Transmitter, REST API, troubleshooting. |
| D03/D10 save/readback/enable | Config lifecycle + scheduling adapter; UI per-target drafts | Invalid complete candidate, changed capabilities, secret placeholders, partial disk failure, crash before/after commit, enable-old-revision, readback conflict. Tests through real HTTP handlers, not just mocks. Docs Setup save behavior and INI settings. |
| D04/D09 enable/boot/ownership | `scheduling_config.cpp`, `scheduling_runtime.cpp`, `wtp_pi_control.*`, `wtp_endpoint/authority.*` | Cartesian product of ordered admission/boot/event tables, unknown output, disable through full pair, separate Stop behavior, manual paths, feature-off parity, defaults, external INI/reload bypass attempts. Existing authority/config/guarded persistence fixtures. Docs runtime/Operations/first transmission. |
| D05/D11 progress | Parent-owned progress/WAL, scheduling planner, current selector consumers | Crash each boundary, corrupt/missing/full storage, reservation-before-launch, restart skip, list digest/frozen random placement, inherited/independent rotation, explicit skip, no catch-up, write/wake bounds. `band-gpio-rotation-test`, `selector-shutdown-cleanup-test`, new deterministic durability tests. Docs INI/troubleshooting. |
| D06/D12 solar/time | Pure parent-owned evaluator, clock provider, configuration lifecycle, WSPR/CW admission | Pico vectors, both hemispheres, leap/date boundaries, longitude wrap, poles, nonfinite inputs, locator precision, step/holdover/leap/loss, same-frequency different-policy pair, disabled override validation, all derived CW tones. `wspr-plan-placement-test`, `non-wspr-repeat-policy-test`, new pure tests and actual simulated application tests. Docs WSPR/CW/runtime. |
| D07 approved handover | Existing Fleet runtime/store/recovery and Pi authority/listener; new automatic management coordination | Cover durable known-controller retirement, offline/unknown migration pending, fresh Plain LAN claims under normal Transmit-off behavior, pending/accepted/unknown jobs, crash/rollback, false clean-install proof, direct enable bypass, and unchanged WTP admission. Extend production Fleet/authority/revocation fixtures; document migration/recovery with implementation. |
| D08 capabilities | Pico authoritative policy/schema/handlers; Pi validators/generated JS/C++/adapters | Separate enables/pause-pending, unsupported master/profiles/carriers, superseded/unknown control majors, changed descriptors/limits, malformed nested shapes; refuse before writes. Docs Transmitter/shared controls and Pico guide only with authority. |
| D10 workflow | UI config/context/editor modules, `WsprryPi-UI/DESIGN.md` | Desktop/mobile, light/dark, keyboard/focus, disabled/loading/error/uncertain state, persisted drafts versus confirmed enable, switching during accepted writes. Production browser tests; Impeccable visual review in phases 3/7. Docs Setup/Operations and accurate screenshots. |

For every shared-interface change run both **complete** checked-in
`.github/workflows/debian-non-hardware.yml` recipes, `wtp-network-loopback` and
`wtp-usb-management-pair`, using the actual intended candidate pair. Network
includes `make wtp-network-runtime-test`, `make wtp-network-interop-test`, and
`make wtp-config-interop-test` (from `src`, recipe flags/PICO_SOURCE/MBEDTLS_SOURCE
explicit). Current pinned Mbed TLS ref is
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`; verify at execution time.
USB includes building actual Pico `usb_management_driver` and `endpoint_driver`,
`make -C src wtp-usb-pair-client-build wtp-usb-management-test
wtp-fleet-operations-driver-build BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`,
and the actual `WsprryPi-UI/tests/wtp_usb_pair_test.js` and Fleet pair fixtures.
The abbreviated names here do not replace dependency setup and other checks in
the complete recipes. Generated schema policy check and negative tests remain
mandatory. Unknown/missing companion authority or an unavailable recipe is a
blocker, not permission to weaken validation.

Other applicable application checks: macOS `make semantics-test-portable SUDO=`
from `src`; full qualified Linux profile uses
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. Read
`docs/simulated-backend.md` before application tests, explicitly select simulated
backend, and inspect every target recipe before execution. Software evidence
never qualifies RF, physical USB, timing, GPIO, installation, or services.

## Documentation Impact and review evidence

Updated: this engineering decision artifact, the automatic pairing companion,
the normative contract supplement, the authoritative plan's approved-decision
references, and the phase-list link/indicator.
Considered but unchanged: current Docs `docs/User_Interface/Setup/index.md`,
`Setup/Transmitter/index.md`, Signal Setup WSPR/CW guides, Pi_IO naming,
Operations and first-transmission guidance, `Advanced_Operations/rest_api.md`,
`ini_configuration/runtime.md`, and troubleshooting. They describe current
behavior; publishing the proposals as operator features would be inaccurate.
Still required with implementation: automatic pairing/replacement, save/enable workflow,
controller-only mode, ownership/legacy migration, new INI/defaults, clock/location
holds, separate Pico controls, and target-specific observations in the guides
mapped above. No operator file changed; no Sphinx build or screenshot replacement
is applicable to this engineering-only phase.

Impeccable: loaded skill/context and read incumbent `WsprryPi-UI/DESIGN.md`;
reviewed target/draft/error-state proposals against bench-instrument patterns.
No UI or rendered UI documentation changed, so no new desktop/mobile rendering
or claim of visual qualification. Required production visual evidence stays with
phases 3/7 and affected operator-documentation updates.

### Adversarial assessment loop

1. Challenged the initial design against all ten phase requirements. Found that
   session revocation alone cannot retire a shared Plain LAN controller; added
   persistent lane fencing, explicit plan exception D07, offline-controller
   blocking, and rollback order. Found that Pico cannot promise this new handover;
   added the companion-authority boundary and unsupported migration status.
2. Challenged enable/save, crash, and state semantics. Added explicit disable/drain
   before replacing a running schedule, canonical saved-candidate digest, durable
   receipt/WAL coupling, bounded uncertain operations, monotonic watermark across
   generations, frozen random-placement continuation, and Stop's separate rearming behavior.
3. Challenged action binding and UI edit semantics. Bound Stop to observed output
   generation/owner/job, separated reservation from acceptance under Disable,
   made missing migrated location nullable while solar is off, and specified
   draft enable intent independently of confirmed target enablement.
4. Full reassessment checks source references, existing-versus-proposed claims,
   security limits, clock/solar-off behavior, state-table coverage, plan exception,
   implementation/test ownership, documentation boundaries, and approval status.
   Found missing status shape and missing schema digests in discovery; added both.
   Clarified feature-off legacy execution versus explicit native handover and
   clean-install attestation. Corrected Stop body/observation bindings and
   canonical numeric hashing to avoid cross-language digest ambiguity.
5. Repeated the full requirement/plan/source/diff assessment after those repairs.
   No further known actionable document defect found at that pass. Pending user
   decisions and later implementation evidence remain gates, not review waivers.
6. User rejected manual credential provisioning and required automatic pairing/
   ownership changes with unchanged WTP. Replaced D02 and added its detailed
   pairing/lifecycle proposal; withdrew incompatible D07 and marked the legacy
   handover gate unresolved. Reviewed pairing replacement versus RF ownership,
   offline old-manager revocation, crash/lost-reply recovery, secret exposure,
   automatic key renewal, and the no-manual-fallback boundary. No additional known
   actionable document issue remained at that pass; D07 was then explicitly
   unresolved, not solved.
7. Recorded user approval of D02 workflow/behavior and D07 handover semantics.
   Removed stale pending/unresolved D07 claims, retained the withdrawn lane-change
   prohibition, and checked offline migration versus management replacement,
   Transmit-off behavior, rollback/crash duties, and approval boundaries. Repeated
   the document/consistency assessment after repairs; no further known actionable
   documentation issue remains. Ten decisions still require user approval.

### Final checks performed

- `python3 /private/tmp/issue473_phase1_check.py`: PASS across all five changed
  Markdown artifacts. Checks relative links/anchors, balanced fences, equal table
  widths, whitespace, exactly D01–D12 with all approvals, later phases not started,
  unchanged prerelease control-policy values, empty staging, and exact document-only
  change scope (including untracked artifacts). Earlier title/status assertions
  were updated for the approved/completed phase; this does not waive any gate.
- `git diff --check` and `git diff --cached --check`: PASS. The temporary checker
  explicitly checks all new untracked documents too, which ordinary diff misses.
- Full source/contract/diff assessment completed in the repeated passes below.
  No production source, registered schema, component, test adapter, or generated
  asset changed. Application/component, network/USB integration, Sphinx, and visual
  suites were not run: this phase changes engineering design documents only.
  No CI run/link, physical USB, installation, service, GPIO, timing, or RF evidence
  is claimed. All later implementation gates remain binding.

Source revisions at final review remain Pi
`505c6ddc12196c7b31c393f9ca01050faafd1737`, Pico
`d65df2aa68b1dfaba0267165a1226aa415a91d62`, and Docs
`a06c1d5590614a4794684aac794c468bec36b7cf`. Source inspection used the owning
files referenced above; no production behavior was tested with dirty changes.
Pico now has unrelated user changes, preserved untouched: `scripts/ci/ctest-completion-policy.json`,
`src/network/bootstrap_web/{app-prefill.test.mjs,app.js,app.test.mjs,bundle.js,index.html,package.json,style.css}`,
`tests/initial_hostname_browser.mjs`, and untracked
`src/network/bootstrap_web/{setup-link.js,setup-link.test.mjs}`. This supersedes
only its initial clean-working-tree observation, not its recorded base revision.
Reinspect all candidate refs/diffs before any later implementation or integration.

At design closeout, the Pi branch was `codex/issue-473-device-owned-scheduling`, with two tracked
engineering documents modified and three untracked engineering artifacts; staging
empty. Docs remains clean on `devel`; Pico remains on `devel` with the unrelated
changes listed above. No checkout synchronization, commit, or push occurred.

### Phase-1 acceptance disposition

All twelve decisions are approved; no phase-1 behavior/security decision remains
unresolved. The normative supplement closes the formerly deferred pairing/
rotation encodings, existing configuration field set, target-authoritative binding,
and durable migration state/receipt/recovery contracts. These are design artifacts,
not registered production schemas or deployed endpoints.

The final acceptance checks and repeated adversarial assessments qualify
phase 1 only. Phase 2 remains not started and requires separate authorization.
Production dependency selection/conformance, implementation, generated assets,
network/USB integration, UI render/review, operator docs, and hardware/runtime
qualification are not claimed by this design completion. No commit or push.

### Phase-1 acceptance cross-check

| Prompt requirement | Closing design evidence | Disposition |
| --- | --- | --- |
| 1: current behavior inventory | Owning-source map and four-target capability matrix; revisions and dirty-state qualifications | Satisfied by source inspection |
| 2: exact management contract | Closed envelopes and canonical configuration projection; supplement fixes identity/channel, type/bounds, schema IDs, errors, concurrency and receipts | Satisfied; production code is later work |
| 3: remote security/reachability | Approved automatic pairing, exact auth/rotation messages, device-only storage, origin/CSRF, bounded routing, revocation/recovery | Satisfied as design; no security qualification claimed |
| 4: complete state tables | Ordered admission, boot Cartesian product, event table, separate Pico controls, and D07 transition table | Satisfied |
| 5: durable progress | Reservation versus acceptance, successor cursors, watermark, independent/inherited rotation, skips/pairs, write bounds and recovery | Satisfied |
| 6: time/location | Target locator baseline, native GPS excluded, bounded Linux UTC/holdover/steps/suspend, Pico provider distinction | Satisfied |
| 7: legacy interlock/handover | Approved known-authority retirement; offline/unknown pending; target-local admission, atomic journals, clean proof, crash/rollback; unchanged WTP | Satisfied as design |
| 8: honest Pico mapping | Separate WSPR/message states and unsupported parity; current prerelease policy; explicit companion edit authority | Satisfied |
| 9: implementation/test/docs ownership | Decision/source/phase/negative-test map, both full integration recipes, defaults/retention and independently versioned records | Satisfied |
| 10: save/readback/enable | Explicit draft intent, save disabled, authoritative readback, exact revision/digest enable, partial/uncertain failure handling | Satisfied |
| Approval and scope | D02/D07 separately approved, remaining ten explicitly approved; no phase-2 or Git/hardware authority inferred | Satisfied |

### Final adversarial closure passes

8. Full closeout assessment found deferred pairing/rotation wire formats,
   unspecified configuration projection/migration records, and an ABA revision
   gap. Added the normative supplement with exact closed messages/records,
   source-incorporated saved field/value contract, target-authoritative channel
   checks, generation-bound revisions, and atomic WAL publication/recovery.
9. Reassessed crash/clock/security consequences. Corrected known-refusal rotation
   advancement by retaining proposed successors until acceptance resolution;
   specified conservative unknown-crash continuation, suspend-aware holdover,
   pairing-offer identity freshness, automatic access recovery after storage loss,
   and narrowly scoped retirement proofs that cannot restore management access.
   Clarified that S can be enabled while T is off and that fresh WTP claims remain
   governed by existing local authority. Fixed Markdown table enum separators.
10. Rechecked terminal-cursor publication against concurrent configuration changes
    and added digest/generation fencing so an older accepted occurrence cannot
    overwrite a changed list's reset cursor. Closed exact read-only operation and
    retirement-status recovery shapes to avoid blind mutation retries.
11. Repeated the full prompt/plan/approval/source/diff review after those repairs.
    Checked every row of the acceptance cross-check, exact shapes and binding
    points, lost replies at each commit, rollback/retirement ordering, resource
    limits and key lifecycle, old versus new behavior, source/companion authority,
    documentation impact, and unrun-check disclosures. Final document checks pass;
    no further known actionable phase-1 issue or unapproved design decision remains.
    Later production validation is expressly required, not waived or reported green.

Phase 1's design-and-decision acceptance gate is satisfied. Phase 2 remains ▧
not started and requires separate authorization. No staging, commit, or push.
