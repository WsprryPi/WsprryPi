# Issue 473 phase 2: local Pi and existing Pico target management

Status: ✅ complete under the user-approved sequencing amendment of
2026-11-16. Phase 3 has not started. Subsequent user authorization permits
feature-branch commits/pushes for delivery; see the delivery section below.
No merge, installation, service, or hardware operation is authorized.

## Current acceptance scope

The user explicitly deferred new Pi pairing/TLS, controller-to-remote-Pi
management, and their dependent remote editor/migration acceptance to the
[mandatory end-of-phase-6 security milestone](deferred-pi-security.md), before
phase 7. The original eight-phase structure is retained. D02/D07 and the eventual
security/ownership contracts, existing WTP, and existing Pico TLS remain unchanged.

Phase 2 now covers protected local Pi context/settings/status/Stop and durable
recovery; independent authenticated existing Pico contexts/resources; explicit
controller-only startup; and readiness/persistence foundations. Controller-to-
remote-Pi enrollment, configuration, actions, and ownership transfer remain
unavailable. A Pi's own protected browser interface remains available under its
existing eligible LAN/loopback policy. No insecure forwarding or certificate
workaround has been added.

Both complete required network and USB suites pass for the production source
pair recorded by `p213-input-final-source-manifest.json`. The two runner logs are
`/private/tmp/issue473-linux-pair/runner-p213-input-final.log` (network) and
`runner-p213-input-final-resume.log` (successful USB/profile continuation after
correcting an executable temporary-directory setting). Final frozen-source
verification passes. This closeout changes only planning/operator documentation
and adds negative tests to two existing Python scripts. Source-hash comparison
confirms no application/UI/schema/build or Pico change since that successful
paired run. Current production-main refusal tests and Docs evidence are recorded
in the amended closeout below.

## Historical execution record before the sequencing amendment

The following checkpoints retain the original scope, findings, approvals,
corrections, failed/cancelled runs, and successful acceptance evidence. Earlier
statements that phase 2 is blocked on remote Pi pairing apply to the original
gate; the explicitly approved amendment above moves those obligations without
claiming that pairing or remote management has been implemented.

## Current Pico implementation and review

Implemented for configuration, application/station/hardware, pins preview,
progress, GPIO drive, amplifier timing, filters, Si5351, GPS, gray-line, and UDP
logging: independent authenticated transports; enrolled-profile context binding;
strict native validation; bounded requests; durable operation reservation before
forwarding; redacted response replay; historical receipt lookup; explicit
same-resource readback/review after an uncertain response. This adds no Pico
scheduler feature and never constructs a WTP execution application.

The production controller HTTP fixture uses two actual Pico TLS/API/journal
services with distinct device identities, CAs, and credentials. The fixture runs the restored controller-only service, with typed loopback
admission and OS identity seams. Configured credential loading,
TLS identity checks, crypto, HTTP authorization, catalog, contexts, and persistence
remain production paths. Earlier passes are checkpoints; final-source reruns are
recorded separately below.

Review corrections in this continuation:

- Context establishment negotiates once within its operation budget, rather than
  performing two sequential 24-second exchanges.
- Controller requests use the approved depth-16 limit and reject malformed
  metadata before replacing an existing target context.
- Native authenticated profile transfers retain the crypto session, refresh
  policy without replaying writes, and fence obsolete browser read permission.
- Replay rechecks current native authorization before returning stored data.
- Management journals use checksums, private-file validation, and a controller-wide
  unresolved-operation bound. Corruption inhibits management HTTP fixture startup before readiness.
- The canonical closed `target-error` family normalizes controller refusals,
  including outer authorization refusals, without exposing peer exception text.
- Evicting bounded terminal history retires the browser context, so a captured
  expired request cannot become a new mutation. Historical lookup never dispatches.

Known phase-level gaps remain: pairing-dependent remote Pi management and its
complete phase-wide negative/recovery and operator workflows. Production
activation of the local Pi service and startup owner is implemented and tested
in the later checkpoints below. Existing Pico editor migration,
retirement of superseded member settings routes, and the in-process Pi settings
coordinator are implemented in the checkpoints below. The
96-file private journal-directory bound remains enforced. Catalog-bound history
epochs now detect missing files or a missing directory across a fresh process;
explicit catalog retirement prunes only terminal, reviewed orphan histories and
retains every unresolved operation. These storage checks do not qualify remote
Pi pairing or its unfinished browser workflow.
None of these gaps is waived by focused test passes.

## USB aliases, storage safeguards, and P2-04 recovery checkpoint

Strict USB name, Wi-Fi, logging, AP, and restart adapters now preserve native
payloads and acceptance semantics. The Pico firmware and software pair share the
AP request validator; the firmware still performs the action only after delivery.
The production ARM firmware builds; the refreshed current-candidate build also
passes (`/private/tmp/issue473-pico-firmware-current-build.log`). No firmware was
installed or run on hardware.

Private catalog storage version 2 owns monotonically increasing profile
generations and per-device management-history epochs. The public catalog remains
version 1 with unchanged profile fields. Reading legacy private version 1 leaves
its bytes unchanged; an explicit catalog mutation writes the private current
format. Missing journal files or a missing journal directory inhibit a fresh
controller before listeners. A 100-target add/remove lifecycle test confirms safe
terminal-history retirement without discarding unresolved operations.

P2-04's canonical mapping, generated browser/C++ consumers, strict lifecycle
checker, runtime header, authenticated lookup, and pre-dispatch hold are
implemented. The actual HTTP pair creates uncertainty using a real producer
storage failure, opens a fresh context, verifies the earlier Location/resource,
proves no new receipt or device write, reviews authoritative readback explicitly,
and permits a subsequent new action. Review retains the historical unknown
outcome. Releasing a hold now rechecks current native authority after readback.

Focused evidence (hardware-free):

- `make -C src pico-management-history-test ...`: PASS, including fresh-process
  deletion detection and bounded terminal lifecycle. Log:
  `/private/tmp/issue473-history-storage-test-2.log`.
- `make -C src wtp-catalog-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
  WTP_CATALOG_BUILD_DIR=/private/tmp/issue473-catalog-epoch`: PASS.
- `node WsprryPi-UI/tests/pico_management_usb_pair_test.js
  /private/tmp/issue473-controller-native/bin/pico_management_usb_pair_client
  /private/tmp/issue473-alias-usb-pico/usb_management_driver`: PASS; log
  `/private/tmp/issue473-recovery-usb-pair.log`.
- `node WsprryPi-UI/tests/pico_management_transport_pair_test.js
  /private/tmp/issue473-pair-network
  /private/tmp/issue473-controller-native/bin/pico_management_pair_client`: PASS;
  log `/private/tmp/issue473-recovery-network-pair.log`.
- `node WsprryPi-UI/tests/pico_controller_http_pair_test.js
  /private/tmp/issue473-pair-network
  /private/tmp/issue473-controller-native/bin/pico_controller_pair_fixture`: PASS;
  log `/private/tmp/issue473-recovery-http-pair.log`. Its two startup errors are
  expected assertions for removed authority files/directories.

These native passes preceded the final added authorization recheck. The subsequent
full Debian run passed all requested candidate-pair steps with that recheck and the
receipt-failure correction. Its runner is
`/private/tmp/issue473-linux-pair/runner-recovery-2.log`; controller, network runtime,
network interoperability, configuration interoperability, actual Pico USB drivers,
USB management/client tests, independent USB context, USB UI, and full USB pair all
passed. The earlier `runner-recovery.log` failed at the configuration-client link
recipe and is superseded by this successful rerun. Network linkage checks exposed missing USB-helper source entries in
`consumer_interop` and `wtp-config-client-build`; both source lists were repaired,
and the full native network CMake build passed. No protocol downgrade or validator relaxation was used.

### Browser-client foundation and receipt-failure review

`pico-target-client.js` now provides captured per-target handles, one shared
same-origin CSRF bootstrap, mandatory context/operation headers, bounded requests,
finite recovery metadata validation, explicit historical lookup, and secret-free
in-memory operation tracking. It never retries a native mutation. Its focused
race tests pass, including an accepted old-target result after its handle is
retired. The actual two-target HTTP fixture also uses this client for a save and
historical receipt lookup; log `/private/tmp/issue473-browser-client-http-pair.log`
passes. The module is not yet wired into the visible editor. Its tests do not
qualify target switching or per-target drafts in that unfinished interface.

Further adversarial review found that a remote save followed by controller
receipt-storage failure omitted its resource hint. That path now retains the
captured operation/resource metadata. The actual TLS pair injects a private-file
permission fault only after the real remote save. It verifies the saved device
value, metadata, and refusal of a later mutation without another native write.
Restoring file permissions deliberately does not erase the running service's
file-change detection; cold authoritative recovery remains required. The corrected
focused test passes in `/private/tmp/issue473-receipt-fault-pair-2.log`.

## Pi structural schema continuation: integration unfinished

The authoritative current Control/1 schema now includes the approved closed Pi
configuration/candidate, scheduling, observed-output, save/enable/disable/stop,
and settings-snapshot shapes. The server and browser validators share 93
structural vectors, including every missing/extra configuration section, type and
integer bounds, canonical Hz overrides, nullable fields, and exact current control
identity. Explicit nonfinite-number tests cover values that cannot appear in valid
JSON. Native WTP integer validation retains its previous limits; controller
integers use their explicit declared bounds, including the approved exact JSON
integer preference range.

These definitions do not implement Pi semantic validation, capabilities/policy
digests, context/status, an endpoint, persistence, or admission. No new Pi service
is advertised. The next implementation must call the owning validators and enforce
read-only/private-preservation, revisions, and the atomic transaction contract.
The full successful `runner-recovery-2.log` checkpoint predates these additional Pi
schema definitions. Required integration suites must pass again for the final
phase candidate; that earlier pass does not qualify this continuation.

## P2-03 paired checkpoint before the USB alias continuation

The full paired run below passed before the subsequent strict USB alias adapter
continuation. Those new adapter changes are not qualified by that earlier pass;
focused tests and affected full pair suites must run again against them.

Sources: Pi `c48a00dbbd6d9d65014c56f286ace074df9240e9` plus its uncommitted phase-2
changes; Pico `05cc90d67442c536f1cf7bee74a94914ad032c23` plus the authorized
uncommitted changes in `/private/tmp/issue473-pico-candidate`; Docs
`a06c1d5590614a4794684aac794c468bec36b7cf` plus its CLI guide edit.
Mbed TLS is the clean pinned checkout at
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Debian evidence uses container image
`sha256:10c9d9b806935f1162f7ccaef018da951d0be3c5710aaf61590d27e246f2a0f1`,
unprivileged UID 501, no external network, no capabilities, no hardware devices,
and `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`.

| Command / check | Current result |
| --- | --- |
| `make -C src -j4 pico-management-target-test pico-management-journal-test pico-target-controller-test controller-contract-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj BIN_DIR=/private/tmp/issue473-controller-native/bin` | PASS; focused tests also rerun in Debian |
| `node WsprryPi-UI/tests/pico_management_transport_pair_test.js /private/tmp/issue473-pair-network /private/tmp/issue473-controller-native/bin/pico_management_pair_client` | PASS, including actual lost reply, explicit readback/review, terminal-history eviction, and old-context refusal |
| `node WsprryPi-UI/tests/pico_controller_http_pair_test.js /private/tmp/issue473-pair-network /private/tmp/issue473-controller-native/bin/pico_controller_pair_fixture` | PASS, two independent actual Pico services, production HTTP/CSRF/crypto/persistence, duplicate security headers, concurrent isolated writes, and refusal before journals |
| `npm run test:unit` in `WsprryPi-UI` | PASS against regenerated current shared schema |
| `python3 scripts/generate_control_contract.py --source /private/tmp/issue473-pico-candidate --check` and `python3 scripts/tests/control_contract_pair_test.py` | PASS; canonical producer/consumer parity |
| Pico `python3 scripts/generate_control_contract.py --check`, `python3 tests/control_policy_tests.py`, and `node --test control-policy.test.mjs` in its bootstrap web directory | PASS; current prerelease policy, unchanged WTP boundary, and three browser policy tests |
| Sibling Docs `source docs/.venv/bin/activate` then `python -m sphinx -b html docs docs/_build/html` | PASS, no warnings |
| Final `git diff --check` / cached checks across all three edited trees | PASS; staging areas empty |
| Complete Debian network runtime/interoperability/configuration recipes | PASS: runtime, interoperability, and configuration; no required skip, including actual second-IPv4 rebind |
| Complete Debian USB driver/client/management/UI/pair recipe | PASS: actual Pico USB driver, production Pi client, management, UI, and paired framing/ACK/journals; no required skip |

The final ordered runner and per-stage logs are in
`/private/tmp/issue473-linux-pair/`; `run.py` records the exact commands and
`final-source-manifest.json` records dirty-source file hashes. The final HTTP
header correction and matching test were copied into the container before its
HTTP fixture build; all non-document Pi candidate hashes match. An earlier run
was intentionally stopped after source review fixes and is not final evidence.
The nested Make recipes emitted jobserver-reset warnings (`-j2`/`-j4` forced in
submake); they did not fail or skip checks. No compiler warning was observed in
the final pair logs. No CI run was triggered because no push is authorized. The final runner ended `ALL REQUESTED CANDIDATE PAIR STEPS PASS`. The review
remains open at the phase level; the implementation gaps above preclude a clean
phase assessment. Passing these suites does not waive those missing workflows,
cold-start deletion recovery, or the pairing-provider gate.

## P2-04: approved recovery-resource metadata

Status: approved by the user; server metadata and canonical mapping implemented.
Browser presentation and the complete phase gate remain unfinished.

A fresh browser can encounter a stored unknown operation after losing its own
request metadata. The approved closed Receipt deliberately has no resource/route
field, so the client cannot identify the correct authoritative native read without
guessing or displaying internal IDs. The private record already retains that route.

The approved outgoing header is `X-WsprryPi-Recovery-Resource`, containing only the
original native Pico resource name from the finite approved controller registry.
Return it with unknown/recorded Pico receipts and authenticated operation lookup.
Publish the fixed reconciliation-read mapping in the canonical controller registry:
ordinary settings map to their corresponding GET; progress actions map to progress;
UDP test maps to UDP logging; AP/restart map to the existing boot-bound hostname
read. This header is information, never a destination URL or authorization grant.

When an earlier unknown operation inhibits a new settings action, refuse the new
action before dispatch or reservation, using the existing closed target-error
`recovery_required` envelope, `Location` of the earlier operation, and the recovery
resource header. The UI preserves the new draft, names the affected settings/action,
and performs the correct current authenticated read plus explicit operator review.
It never retries either native mutation automatically. Reviewing an AP/restart
request never asserts that the original action ran; its historical result stays
unknown. Changed profile/connection/boot/authority fences remain mandatory.

Native Pico request/success payloads and public Receipt/error bodies stay unchanged;
WTP, native authentication, and storage versions remain independent. No new raw
peer strings, credentials, browser-controlled hosts, or certificate workflow is
introduced. This adds a controller-facing header and registry semantics beyond
P2-03. The user supplied that additional approval; no native payload or WTP amendment is authorized.

## P2-06: preserve configuration feature parity

Status: approved direction. The user retained WTP for controller-dispatched
transmissions and the existing management APIs for configuration, then explicitly
authorized completing all adapters necessary for feature parity. The earlier
proposal to omit the Wi-Fi control is withdrawn. Routine Wi-Fi state should load
with configuration; the operator should not need a separate diagnostic read.

The existing authoritative `engineering/protocol/network-diagnostics.schema.json`
is a closed diagnostic contract. The earlier statement that there was no such
schema was incorrect. The target-bound network adapter now validates that schema,
checks native revisions before dispatch, preserves native GET/PUT payloads, and
uses the same durable operation/replay/recovery path as other target settings.
`enabled`, `requested_enabled`, and `withdrawal_pending` remain distinct; an
accepted transition is not evidence that Wi-Fi has finished changing state.

The existing page now loads Wi-Fi state after confirming management and when
loading configuration, with separate nearby status feedback and an optional
**Refresh Wi-Fi status** action. Independent editor migration remains unfinished.
The adapter and browser unit checks and both required production-pair suites
pass for the reviewed sources; the completed evidence is recorded below. WTP wire formats and execution remain
unchanged. No hardware or installation qualification is implied.

## P2-05: approved narrow INI-Handler serialization API

Status: approved by the user. The pure serializer, delegated save path, standalone
conformance tests, and README update are implemented within this boundary. Parent
recovery integration remains in progress.

Atomic Pi recovery needs the exact bytes of the post-save INI image before the
commit decision becomes durable. Comparing a parsed key/value map is insufficient:
the component also preserves comments, unknown keys, duplicate layout, removed
keys, and the owner's raw Security field. The existing `save()` writes its output
directly and exposes no pure serialization method. Duplicating that renderer in
the parent would create two persistence contracts; temporarily pointing the global
singleton at a scratch file would change shared parser state during preparation.

Proposed component change:

- Add a const `IniFile::serialize_with_data(proposed_data)` method returning the
  exact INI byte string using the component's existing layout, removals, and raw
  passthrough rules. It performs no file I/O, filename switch, state publication,
  repair, or reset.
- Have `save()` use the same serializer while retaining its existing explicit
  temporary-file/rename lifecycle. Add no parser syntax, escaping, normalization,
  WsprryPi-specific dependency, or new privilege requirement.
- Update the component header/README and standalone tests. Verify prediction equals
  the actual save, preview leaves the live file/current data/pending edits alone,
  and comments, duplicate layout, omitted/added keys, raw Security data, and errors
  preserve current behavior. Run `make -C src/INI-Handler/src test SUDO=` using its
  owned scratch fixtures, then affected parent configuration tests and both full
  required source-pair suites.

The parent transaction will store the rendered after-image and its hash in private
WAL storage together with the before-image hash. Recovery can then distinguish an
untouched before-image, an already applied exact after-image, and conflicting
operator changes; it never guesses from partial field equality or rolls back an
uncertain accepted save. The parent remains responsible for destination validation,
fsync/rename/directory durability, runtime publication, and admission holds. New
management writes must also reject values that cannot round-trip through the
existing INI representation before reserving a decision; the legacy public input
contract remains separate.

Affected component: `src/INI-Handler`. Expected edits are its `ini_file.hpp`,
`ini_file.cpp`, standalone `main.cpp` tests, and README. No WTP, transport,
certificate/pairing workflow, or later-phase scheduler feature changes are proposed.
The repository instruction explicitly requires approval before broadening a
parent-only task into component changes. This proposal is that separate boundary.

## P2-03: approved Pico request binding and durable replies

Status: approved by the user; implementation in progress, not advertised. P2-02 fixes routing, but the
existing native Pico payloads do not carry the new controller profile/connection
binding or durable operation ID. The approved Pi Save/Receipt bodies cannot be
inserted into those payloads without changing their existing schemas.

The approved design requires `X-WsprryPi-Target-Context` on target-bound Pico resource calls
after explicit context establishment:
a server-issued 32-lowercase-hex opaque handle that binds the enrolled catalog
profile, verified device/boot, profile/connection generations, effective management
authority, and capability revision. The server resolves its stored profile;
browser-supplied hosts, URLs, credentials references, or binding fields never
override it. Context establishment uses `X-WsprryPi-Profile-ID` for the exact
enrolled catalog profile and returns the context handle in the
`X-WsprryPi-Target-Context` response header, leaving the approved Context body
closed and unchanged. Duplicate/missing profile headers are refused; discovery
does not create a profile or grant access. Context establishment is explicit, and replacing/reconnecting a
context retires old read permissions without changing WTP selection or accepted
writes. Resource methods, native request documents, and native successful response
documents remain unchanged.

The approved design requires `X-WsprryPi-Operation-ID` (32 lowercase hex) on native
settings mutations, with one ID per explicit settings action. Context establishment,
read-only preview, and existing native login/logout/recovery exchanges retain their
independent session/proof semantics; they do not replay settings writes through this
ledger. Reject duplicate headers, missing IDs, and
malformed/stale contexts before forwarding. Hash the complete canonical native
request, exact target route/method, If-Match, principal, and stored binding. Reserve
a private durable receipt before forwarding; do not retry a remote mutation.
Keep 256 terminal receipts plus all unresolved reservations, with a 32-unresolved
limit. Same ID/different request returns 409. Same ID/same completed request
returns the recorded, strictly validated public native response and ETag without
forwarding. Only public/redacted native response fields may enter that replay
record; passwords, transport keys, raw peer errors, and unvalidated bodies may not.
An unresolved or unvalidated reply returns a typed unknown-operation response and
blocks subsequent mutations until authoritative readback/review. A reboot or
changed authority cannot replay a write against a replacement binding.

Return the operation ID and a `Location` pointing to the existing approved
`/api/control/v1/targets/{device_id}/operations/{operation_id}` lookup. That lookup
returns the approved public Receipt shape. A separate bounded private Pico replay
record must contain the original binding/principal/request digest plus the public
response/status/ETag; it must not relax the closed Pi `PrivateReceipt` format.
Validate and sync that new record before use, fail closed on corruption, and
exercise crash/lost-reply/revocation tests before exposing mutations.

The user approved this proposal, including mandatory controller-facing protocol headers
and private replay persistence beyond the route decision. It does not add fields
to native Pico payloads or alter WTP, Pico authentication, or Pico scheduling.

## P2-02: approved target-bound Pico route

The user approved `/api/control/v1/targets/{device_id}/pico/{resource}` for the
controller-side Pico adapter, preserving existing Pico resource payloads and
methods. Each enrolled target/profile owns a separate management context; verify
profile, connection, and boot bindings before forwarding. This route does not
select a transmitter backend or grant RF/WTP authority. Keep the current Control/1
major/namespace and canonical Pico ownership; update owning schema/generators,
strict consumers, test adapters, and both required production pair suites together.

Transport work adds a management-only Pico client and a shared dedicated USB
management channel. It constructs no WtpApplication and reserves no RF output.
Execution retains its existing WTP interface and ownership. Existing production
WTP regression passes after the channel change. The new management target API,
durable uncertainty tracking, and canonical schemas are still in progress; paired
regressions of the transport foundation are running; the earlier foundation assessment below does not qualify this new work.

Read the [phase-2 prompt](02-target-management.md),
[execution rules](execution-rules.md), [approved decisions](phase-1-decisions.md),
and [current normative contract](phase-1-contract-details.md). The phase-1 gate
remains a completed design gate; it never established dependency conformance.
This report separates dependency preflight from target-management implementation. It does not claim delivery of the complete target-management APIs or
qualification of the remaining target-management workflows.

## Current sources, authority, and changes

| Repository | Inspected revision | Branch / initial tree | Work in this phase |
| --- | --- | --- | --- |
| WsprryPi | `c48a00dbbd6d9d65014c56f286ace074df9240e9` | `codex/issue-473-device-owned-scheduling`, initially clean | Controller service foundation, production route guards, tests, CI recipes, phase indicator, and this record; uncommitted |
| WsprryPico candidate | `05cc90d67442c536f1cf7bee74a94914ad032c23` | `/private/tmp/issue473-pico-candidate`, `codex/issue-473-phase2-paired-candidate` | Authorized current controller schema/registry, generators, shared validation vectors, generated assets, and independent fixture credential generation; uncommitted |
| WsprryPi-Docs | `a06c1d5590614a4794684aac794c468bec36b7cf` | Initially clean on `devel`; now `codex/issue-473-device-owned-scheduling` | CLI foundation and current limitations documented; uncommitted |

The user explicitly authorized the required Pico companion changes through the
scope question in this chat. That authorization includes no new Pico scheduling
features, commits, pushes, hardware operation, or extraction of components.
The first branch-create attempt was refused by the filesystem sandbox; the
same authorized Git operation succeeded with escalation. No automatic approval
review rejection occurred. No service/configuration/hardware operation occurred.

Source drift from phase 1: Pico advanced from `d65df2aa` to `929553d4` through
separate work. Rechecked the current authoritative policy and generated controls
rather than treating the earlier CI run as evidence for the new source pair.
Both current control policies remain prerelease major 1, with no stable freeze.

The main Pico checkout advanced through concurrent hostname work, now committed
externally as `f5f7323784e30d079432824ddb4a9c9a587e3b27` and
`05cc90d67442c536f1cf7bee74a94914ad032c23`. Task-owned companion changes live in the
isolated feature worktree above; they do not modify or qualify that external work.
All new paired commands explicitly select this dirty candidate worktree, rather
than relying on an unmerged companion branch being delivered on `devel`.

## Preflight owning-source findings and implementation boundaries

- `src/wtp_runtime_bridge.cpp` uses one selected `WtpTargetRuntime` and
  `require()` refuses management when no Pico output backend is selected.
  Management actions use `app->idle_management`, and backend replacement/probing
  is guarded by the execution runtime's replaceability. Independent management
  therefore needs a separate context/transport owner, not a call to Use device.
- `src/wtp_integration/target_runtime.hpp` combines execution, output lease,
  transport and management state. Do not manufacture a second execution runtime
  or acquire an output lease merely to populate a settings editor. Existing
  `configuration.cpp`, USB management and consumer-radio serializers/validators
  remain the reusable production paths once correctly separated.
- Pico owns `engineering/protocol/control-1.schema.json` and
  `scripts/generate_control_contract.py`; Pi vendors/generates its copies through
  `scripts/generate_control_contract.py`. The current 41 definitions contain no
  new target-management envelopes. New Pi-only resource definitions must be
  integrated in that current authority and its paired validators/registries,
  while Pico advertises only its real supported resources.
- Existing catalog/Fleet persistence belongs to `catalog.*`, `fleet_store.*`,
  `fleet_runtime.*`, and `fleet_schedule.*`; keep catalog, assignment, recovery,
  output selection and pairing authority separate. No new recurrence dispatch,
  solar runtime, schedule enablement, or shared Setup layout belongs in phase 2.

## P2-01: pairing dependency / internal wire amendment

**User decision:** retain OpenSSL on Pi and Mbed TLS on Pico. Hold BoringSSL
adoption pending the acceptance gates below. If regular dependency maintenance or
seamless packaging cannot be demonstrated, reject it for pairing. The narrow
source evaluation was report-only; it authorized neither a production dependency
nor an internal pairing wire amendment. The approved P-256/RFC 9382 design and
WTP remain unchanged. There is no pending approval question for that amendment.

### BoringSSL acceptance gates

Candidate source inspection establishes an available SPAKE2 API and upstream tests,
not a qualified production provider. BoringSSL expects regular source-pin updates,
provides no stable API/ABI or maintained release branches, and shares symbols with
OpenSSL. These are acceptance costs, not work to defer until after adoption. See
the upstream [integration guidance](https://boringssl.googlesource.com/boringssl/+/refs/heads/main/INCORPORATING.md)
and [build guidance](https://boringssl.googlesource.com/boringssl/+/refs/heads/main/BUILDING.md).

| Gate | Required evidence | Current disposition |
| --- | --- | --- |
| Maintenance ownership | Named owner for upstream/security monitoring, routine refreshes, emergency rebuilds, and redistribution | Not established |
| Upgrade burden | Demonstrated upstream revision upgrade through CI without a persistent crypto fork or extensive integration repairs | Not demonstrated |
| Existing platform support | Build and exercise pairing on ARMv6/AArch64 Bookworm/Trixie and Intel/Apple Silicon macOS development profiles | Not run; ARMv6 support unproven |
| Seamless packaging | Preserve installation workflow without user-installed BoringSSL packages, certificates, or manual keys; prove isolation from OpenSSL | Not demonstrated |
| Resource budgets | Measure artifact size, build time, peak memory, and pairing latency against baseline; approve explicit budgets | Not measured or approved |
| Security and integration | Upstream tests, production failure/recovery tests, symbol audit, and both complete Pi/Pico network and USB suites | Not run for a BoringSSL candidate |
| License and provenance | Exact source pin, dependency inventory, applicable license and attribution notices in distributed artifacts | Candidate library license inspected as Apache 2.0; packaging not implemented |

An on-demand helper statically linked to BoringSSL libcrypto alone is a possible
isolation approach, not an accepted architecture or measured footprint. The existing
embedded logging-worker mechanism accepts ELF only; it does not establish macOS
helper support. Existing release validation permits known OpenSSL shared libraries
and rejects unknown shared dependencies. No BoringSSL helper or library has been
added. OS package updates would not update a bundled BoringSSL copy.

The practical C++ RFC 9382 candidate inspected was
[`backkem/spake2_cpp`](https://github.com/backkem/spake2_cpp/tree/628fbc87e1c17b3f3f9f66c20cba650d6d19faf6)
at `628fbc87e1c17b3f3f9f66c20cba650d6d19faf6`. Its README discloses no formal
security review. Its checked-in vector harness includes an absent `lib_rfc/`
directory; correcting only that include path in a temporary review copy permits
its four supplied vector cases to pass. That is not the production dependency gate.

Source/negative checks found raw random-scalar generation without group-order
rejection sampling, ordinary `BN_free` for secret scalars, low-level acceptance
of identity points/zero scalars, and access to caller-derived `w` only through a
debug path. Some checks could be enforced by an adapter; they do not demonstrate
a remotely exploitable bug through the approved strict envelope. They do mean
this snapshot is not accepted as the production provider without additional
hardening and review. Nothing was vendored or patched into either application.

The other inspected RFC 9382 candidate,
[`pakery`](https://github.com/djx-y-z/pakery/blob/6abbf8235e40d7bce975a44aaf423d0e3c436df6/README.md),
also discloses no independent audit and introduces a Rust build/FFI boundary.
It has not been qualified or ruled out as inherently unsafe; no claim is made
that no suitable RFC 9382 implementation exists anywhere.

### Earlier proposed amendment, held and not applied

The official [BoringSSL API](https://boringssl.googlesource.com/boringssl/+/8f2554dc344d403fd6d628773d39637c323eb801/include/openssl/curve25519.h)
provides `SPAKE2_CTX_new`, generate/process operations, identity binding, 32-byte
messages and up to 64-byte key material. It references the earlier SPAKE2 draft,
not the approved P-256 RFC 9382 encoding. Candidate source pin:
`8f2554dc344d403fd6d628773d39637c323eb801`; upstream SPAKE2 test-file blob:
`02cc870c07f16e88a312010f85685db1c5bff713`.

| Area | Approved current design | Proposed change if approved and qualified |
| --- | --- | --- |
| Pair/replace UX | Target UI opens a temporary code; controller Fleet Pair dialog enters it | Unchanged; no certificates, files, fingerprints or extra user step |
| Pairing exchange | RFC 9382 P-256 shares, 65-byte SEC1 points | Use BoringSSL's native 32-byte SPAKE2 messages; distinct PairShare type |
| Password input | scrypt-derived scalar modulo P-256 order | Pass scoped scrypt output as the library's password input; no application EC scalar arithmetic |
| Shared secret / confirmation | RFC 9382 transcript/key schedule | Bind the existing roles/device IDs/boot IDs/generations/nonces plus both shares; derive role-separated confirmation MAC keys and verify both before committing trust. Freeze exact revised encoding/labels and negative vectors before implementation |
| Rotation | Authenticated P-256 ECDH rotation over paired TLS | Unchanged; its 65-byte RotationPoint is distinct from PairShare |
| Long-term management | Device-stored PSK, TLS 1.3 PSK-DHE, automatic renewal/revocation | Unchanged; no certificate fallback or WTP credential reuse |
| Build boundary | Existing OpenSSL plus a conforming PAKE dependency | Pin and qualify BoringSSL pairing code behind an isolated library/worker boundary; prevent OpenSSL/BoringSSL symbol/header collisions; preserve platform builds and licensing |
| Shared control / WTP | One current Control/1; independently versioned auth; unchanged WTP | Unchanged; no hidden dual pairing protocol or downgrade |

Alternative: retain the exact current RFC 9382/P-256 contract and hold remote
pairing while a conforming provider is qualified. A hand-written PAKE, manual
certificate setup, unauthenticated settings writes, or vector-only security
claim is not an approved fallback. Removing remote Pi management would alter
phase 2's product/acceptance scope and requires explicit approval, not an implicit
implementation shortcut.

The earlier combined evaluation/amendment approval proposal is superseded by the
user's hold. Passing the dependency gates would permit a later adoption decision;
it would not itself approve this wire amendment. The existing phase-1 documents
remain unchanged. No alternate pairing protocol has been exposed.

## Earlier dependency-preflight checks and limitations

All tests below are hardware-free. Temporary upstream review files/logs are in
`/private/tmp/issue473-spake2-review` and `/private/tmp/issue473-spake2-*.log`;
they are not application dependencies or repository content.

| Check | Result |
| --- | --- |
| `git status --short --branch`, `git diff --check`, `git diff --cached --check` in all three repositories | Initial clean trees; no whitespace errors |
| `python3 -B scripts/generate_control_contract.py --source ../WsprryPico --check` | PASS against the intended current Pico source |
| `python3 -B scripts/tests/control_contract_pair_test.py` | PASS: schema/lifecycle/checker/browser/Si5351/GPS/gray-line parity regression checks |
| `pkg-config --modversion openssl` | Local OpenSSL 4.1.0 available; does not establish a matching PAKE provider |
| Original upstream vector harness syntax check | FAILED: missing `lib_rfc/spake2.h`; no application code involved |
| C++20 build and execution of include-path-corrected temporary vector harness | PASS: 4/4 supplied cases; OpenSSL deprecation warnings, not production build qualification |
| Temporary low-level rejection probe | Candidate accepts identity point and zero scalar; rejection-policy test exits 1. This is a dependency-review finding, not a failing WsprryPi test |
| Full network and USB recipes | NOT RUN for a phase-2 candidate: no new producer/consumer implementation yet; earlier green design-commit CI is not a substitute |
| Application/management HTTP fixture startup and management runtime tests | NOT RUN: the required production implementation is not present; no working controller-only claim |
| UI render, Impeccable visual workflow, Sphinx | NOT RUN: no UI or operator documentation changed |

At preflight no production/component source was modified. The independent service
foundation above now changes parent application sources, tests, and CI recipes;
the subsequent continuation also changes the authoritative Pico schema, its
generators, and generated shared assets in the isolated candidate worktree.
The actual future pairing sources and dependency pins
must be selected explicitly for both full integration recipes. No pairing/remote-
settings runtime, physical USB, GPIO, RF, installation, service, or release
qualification is claimed. Current controller software evidence is listed above.

## Adversarial preflight review

1. Challenged Pi-only implementation against canonical ownership. Found missing
   companion authority; obtained explicit narrow Pico authorization and created its
   feature branch. Confirmed metadata parity against its newer base revision.
2. Challenged dependency claims beyond happy-path vectors. Reproduced the broken
   harness path, ran its corrected vector cases, inspected scalar/point handling,
   and reproduced low-level acceptance of disallowed edge values. Did not vendor
   an unqualified provider or write new PAKE primitives.
3. Challenged the BoringSSL recommendation against the frozen design. Identified
   its different protocol/share format and need for separate mutual confirmation;
   made P2-01 an explicit amendment requiring approval rather than calling it
   RFC 9382-compatible. Added platform/linkage/negative-test gates and the unchanged
   operator workflow boundary.
4. Full reassessment of preflight artifacts, owning paths, approval status, actual
   checks and dirty changes: no further known actionable preflight-report issue.
   The dependency decision and unimplemented acceptance requirements stay open;
   this review does not complete phase 2 or waive a required suite.
5. Reassessed the source evaluation against the user's subsequent decision. Found
   stale pending-approval language and a combined evaluation/wire-amendment proposal;
   replaced them with the explicit dependency hold, separate amendment boundary,
   acceptance costs, and outstanding evidence. Rechecked the resulting report:
   no provider, packaging, benchmark, or phase-completion claim is made. Production
   implementation and required validation remain open.

## Documentation Impact

Controller-only runtime, CLI, UI, fixtures, CI hooks, and the operator CLI guide
have been restored. The plan and saved prompt no longer exclude that scope.
Impeccable guided the UI review; eight current Setup/Operations captures cover
1440-pixel desktop and 390-pixel mobile layouts in light/dark themes. The Sphinx
build passes, and the restored operator guide was rendered and visually inspected
at 1280 and 390 pixels without horizontal overflow. This is restoration
of the previously implemented foundation, not completion of phase 2.

### Existing Pi persistence boundary

A parent-only `ConfigPersistenceCoordinator` hook now runs after the existing
configuration validation and revision checks and before runtime publication. It
requires an active INI and exactly one persistence attempt. The ordinary public
patch API keeps its existing path. The separately approved P2-05 component change
adds pure serialization using that same existing renderer.
The future target transaction owner can reserve its WAL decision and invoke the
existing INI writer under the established commit locks, then finish publication
or retain uncertainty.

`make -C src pi-config-persistence-coordinator-test BACKENDS=simulated
ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
BIN_DIR=/private/tmp/issue473-controller-native/bin` passes, including semantic
and stale-revision refusal before the coordinator, pre-write coordinator failure,
skipped/repeated persistence refusal, normalized candidate delivery, INI/comment/
safety-owner preservation, and post-persist failure without runtime publication.
Log: `/private/tmp/issue473-pi-persistence-coordinator-test-2.log`. Expected macOS
platform discovery messages report absent Linux device-tree files. The test uses
the explicit simulated backend, suppresses scheduler execution and runtime apply,
and never initializes physical output. This hook is not the completed WAL,
startup recovery, semantic target resource, or target endpoint.

### Local Pi service inspection

The phase-1-pinned public configuration serializer, deserializer, and value-policy
files have no committed drift at the current Pi HEAD. They remain the owning
configuration contract. The existing patch/save path performs revision, hardware
selection, runtime/takeover, and persistence checks; a new target service cannot
substitute a permissive JSON merge for those checks. The existing INI writer is
atomic by rename but does not itself fsync the file and parent directory. The
approved parent transaction/WAL must supply durable commit/recovery coordination
before publishing settings and receipts. The approved P2-05 serializer now lets
the parent freeze the exact after-image before reservation. The closed Pi structural shapes above are now materialized; semantic resource
validation, policy-bound capability digests, and the service remain unimplemented
and unadvertised.

### P2-05 implementation and recovery storage checkpoint

The four approved INI-Handler files now provide const `serialize_with_data()`,
with `save()` delegating to the same renderer. Standalone tests cover exact save
parity, comments (including non-UTF-8 bytes), CRLF input, duplicate layout,
removed/reintroduced keys, raw Security preservation, and unchanged parser/file
state during preview. No parser syntax or component Makefile change was made.

`make -C src/INI-Handler/src test SUDO=` failed on macOS because the existing
Linux flags pass `-lstdc++fs` to compilation with `-Werror`. The explicit macOS
host override passed: `make -C src/INI-Handler/src test SUDO= CXX=clang++ CC=clang
CXXFLAGS='-std=c++20 -Wall -Wextra -Werror -MMD -MP' LDFLAGS=-pthread`.
Log: `/private/tmp/issue473-ini-serialization-component-macos-test.log`.
The unchanged default recipe is also included in the fresh Debian source-pair run.

The parent management preview reuses the owned persistence projection and PPM
checks. It rejects line breaks, NUL, INI comment delimiters, and surrounding
whitespace that would lose content on reload. This restriction belongs to the new
management transaction preparation; legacy save behavior is unchanged.

Adversarial contract review rejected the initial combined Pi-state journal
prototype: it did not implement the approved separate prepared/committed WAL.
That unexposed prototype has been replaced, not retained as an alternate storage
format. `PiManagementJournal` now stages private old/new byte images and uses the
exact `pi-management-transaction/1` fields from the approved normative supplement.
The caller supplies only server-owned fixed destinations for at most six kinds.
Preparation changes no final path; commit durably records the decision; recovery
restores every old image for prepared decisions or applies every new image for
committed decisions. Prior absence is explicit, and all images/final signatures
are checked before any recovery mutation. Staging is checked again before commit. Original files and their directory
entries are synced before the prepared decision, including legacy-writer inputs.

Focused native tests pass using the real parent validation/coordinator hook and
INI renderer/writer. They cover cold-process rollback/roll-forward, partial apply,
authoritative prior absence, binary comment preservation, conflicting external
edits, corrupt later images, corrupt staging before commit, duplicate WAL keys, wrong versions/identities,
injected destination paths, missing images, symlinks, and unsafe permissions.
Command: `make -C src -j4 pi-management-journal-test BACKENDS=simulated
ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
BIN_DIR=/private/tmp/issue473-controller-native/bin`.
Log: `/private/tmp/issue473-pi-staged-wal-test-8.log`.
Expected macOS device-tree diagnostics remain visible; execution and runtime
application are suppressed and the backend is explicitly simulated.

This is a transaction storage primitive, **not a completed Pi management service**.
Exact managed-settings and private operation records, admission/legacy-writer
coordination, startup recovery, anchored history validation, semantic resource
digests, and target endpoints still require integration. Superseded source-pair
runs were intentionally cancelled when review changed the storage implementation;
they are not successful completion evidence. The pre-removal source pair in
`/private/tmp/issue473-linux-pair/runner-staged-wal-final.log` passed network
runtime/interop but failed configuration interop with `recovery_required` (409)
at the HTTP pair's concurrent-save assertion. USB was not reached. This is not a
successful full-pair gate. Its manifest is a superseded source checkpoint.
After removal, the updated native HTTP pair passes; full current-source Debian
network/USB gates remain outstanding.
The actual parent INI writer runs only while the committed WAL is retained;
recovery applies/syncs all records and retires the WAL before runtime publication.
No legacy file write is permitted after transaction retirement.

### Adversarial storage corrections

Terminal receipt eviction now occurs on completion or explicit reconciliation,
not reservation: all 256 terminal records survive alongside unresolved operations.
Actual eviction retires the edit context. Missing catalog authority refuses startup
without creating an empty replacement. Pico history garbage collection requires a
durable matching retirement marker, including interrupted deletion recovery; an
unanchored orphan cannot masquerade as completed garbage collection.

The private catalog version-2 record now has seven exact fields: `version`,
`generation`, `profiles`, `management_history`, `management_retired`,
`pi_management_history`, and `checksum`. Public catalog version 1 is unchanged.
The local Pi history anchor grants no enrollment, remote permission, or RF access.
Focused retention/history/anchor tests passed, as did the actual native network
lost-reply/retention test and HTTP catalog-loss startup test. Evidence:
`/private/tmp/issue473-retention-native-pair.log`,
`/private/tmp/issue473-retention-retirement-tests-2.log`, and
`/private/tmp/issue473-catalog-loss-http-pair.log`.

### Current review and documentation impact

P2-05's component and staged-WAL review loops corrected lossy INI representations,
pre-reservation capacity handling, the nonconforming combined-state prototype,
pre-commit image validation, unsafe destination permissions, baseline directory
durability, and the integration fixture's post-retirement legacy-write ordering. A further review
and focused regression run found no additional issue in this storage primitive.
This is not a phase-wide zero-findings claim: the integration gaps below remain.
The complete UI unit suite passes at this checkpoint (`npm run test:unit` from
`WsprryPi-UI`; `/private/tmp/issue473-ui-unit-current.log`). The restored controller-only UI has fresh browser evidence; the independent
editor remains unfinished.

Documentation updated: INI-Handler README, this progress record, and the active
phase indicator. Existing `docs/wtp-fleet.md` remains part of this task. The controller-only
addition to the sibling CLI guide has been restored. Documentation considered but unchanged:
operator save/recovery instructions, because the Pi service and recovery workflow
are not yet exposed. Still required: operator pairing/target editor/recovery
instructions and rendered review once their actual implementation is available.

### Full phase assessment: unresolved integration findings

These remain acceptance failures, not deferred work behind a completion claim:

1. The Pi WAL and exact managed-settings/private receipt record helpers are
   tested primitives. Their lifetime authority, semantic policy-bound descriptors,
   local Pi routes, and server-owned principal bindings still need integration.
2. Normal startup and every cooperating legacy writer must enter the
   managed transaction/admission owner. Cold storage-fixture recovery is not
   evidence that application startup or recurrence admission does so.
3. The independent browser client is not loaded into the live editor. Existing
   selected-backend editor routes remain until all consumers migrate; that is not
   the final one-current-view contract. Per-target drafts, late accepted writes,
   fresh-browser recovery/review, and mobile/desktop rendering still need coverage.
4. Native `/network` currently returns implementation-dependent diagnostic state
   and acknowledges a requested transition before it is applied. The new adapter
   has no verified closed mapping for it; generic unchecked passthrough is refused.
5. Automatic remote Pi pairing still lacks a qualified conforming RFC9382/P-256
   provider. Keep OpenSSL/Mbed TLS, the existing WTP contract, and the BoringSSL
   maintenance/packaging hold. No manual-certificate workaround is authorized.
6. Two actual Pico services prove Pico management isolation. They do not prove
   local/remote Pi isolation, paired-key revocation, complete application crash
   recovery, or the unfinished operator workflow. Those phase-wide gates and
   corresponding operator documentation remain open.

## Controller-only restoration verification

The implementation is reinstated at the user's request. At the restoration checkpoint, thirty runtime, UI,
fixture, workflow, and Makefile files exactly matched the recorded pre-removal
SHA-256 manifest. Shared-file changes were restored through contextual patches;
retained target-management and INI-Handler changes were preserved. The package
unit-test entry and operator CLI-guide addition are restored separately.

Current evidence:

- PASS: `make -C src -j4 controller-only-test
  /private/tmp/issue473-controller-native/bin/pico_controller_pair_fixture
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
  BIN_DIR=/private/tmp/issue473-controller-native/bin`;
  `/private/tmp/issue473-restored-controller-native.log`.
- PASS: Debian `make -j4 controller-only-test BACKENDS=simulated
  ANCILLARY_GPIO=0 SUDO=` in the unprivileged isolated image, including both
  the typed fixture and actual Linux executable with a pinned machine-ID file;
  `/private/tmp/issue473-restored-controller-linux.log`.
- PASS: `npm run test:unit` from `WsprryPi-UI`;
  `/private/tmp/issue473-restored-controller-ui-unit.log`.
- PASS: `WSPRRYPI_CONTROLLER_FIXTURE=/private/tmp/issue473-controller-native/bin/controller_only_fixture
  WSPRRYPI_CONTROLLER_SCREENSHOTS=/private/tmp/issue473-restored-controller-ui
  node tests/controller_mode_browser_test.js` from `WsprryPi-UI`;
  `/private/tmp/issue473-restored-controller-browser.log`.
- PASS: `node WsprryPi-UI/tests/pico_controller_http_pair_test.js
  /private/tmp/issue473-pair-network
  /private/tmp/issue473-controller-native/bin/pico_controller_pair_fixture`;
  `/private/tmp/issue473-restored-controller-http-pair.log`.
- PASS: sibling `source docs/.venv/bin/activate` then
  `python -m sphinx -b html docs docs/_build/html`;
  `/private/tmp/issue473-restored-controller-docs-build.log`.
- PASS: eight restored application screenshots visually inspected; source/hash
  comparison and whitespace checks. No installer or hardware qualification.

Phase 2 was unfinished before removal and remains unfinished after restoration.
The full current-source network/USB gate is still open; the prior Debian
configuration interop `recovery_required` failure needs investigation. Continue
that work and the service/editor integration without another go-ahead request.
Nothing was staged, committed, pushed, installed, or run against live hardware.

## Phase-2 continuation after restoration

The cross-target management lookup used nonblocking attempts on a short shared
map lock. Two unrelated target requests could collide and produce a misleading
`recovery_required` response before dispatch. Lookup/publication now waits for
that local critical section; catalog reads and native I/O remain outside it, and
per-target operation exclusion remains intact. A two-target synchronized-read
regression covers independent contexts and no execution writes. Native and
Debian focused tests and the actual two-Pico HTTP pair pass:
`/private/tmp/issue473-concurrent-targets-after.log` and
`/private/tmp/issue473-phase2-http-concurrency-fixed.log`.

`pi_managed_records.{hpp,cpp}` now encodes and validates the exact approved
`pi-managed-settings/1` and `pi-management-operations/1` records. Canonical
candidate/request/revision hashes bind the authenticated principal, route,
authorization generation, configuration generation, and private-preservation
digest. Tests cover integer/float equivalence, A/B/A revisions, private-state
changes, identity/type/route refusal, no credentials in receipt fields, and 256
terminal plus 32 unresolved records with no unresolved eviction. Read validation
never prunes history. Explicit retention reports eviction to the future owner,
which must retire its browser context.

The production INI/coordinator/WAL test now stages real approved settings and
receipt records instead of placeholder strings, then validates them in a fresh
process after committed partial-apply recovery. Native commands pass:
`make -C src -j4 pi-managed-records-test pi-management-journal-test
BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
BIN_DIR=/private/tmp/issue473-controller-native/bin`.
Log: `/private/tmp/issue473-pi-records-wal-test.log`.
These helpers are not a completed target service, authority owner, startup
interlock, RF path, or new WTP operation. Editor migration remains unfinished;
P2-06 authorizes feature parity adapters.

## Exact next step

Migrate the existing Pico settings editor to explicit enrolled-target contexts, and retire its superseded
external selected-backend settings routes after updating all paired consumers.
Implement the approved local Pi management service and atomic settings/receipt
coordinator. Full remote Pi management additionally depends on the held pairing
provider. Preserve explicit backend selection, accepted execution work, and the
phase-3 boundary. Complete documentation, visual review, and the phase-wide negative
and recovery tests before any completion claim.

Pairing-dependent remote management remains blocked on provider qualification.
Keep BoringSSL on hold behind the gates above; failure of maintainability or
packaging rejects that candidate, not the existing TLS libraries. Any different
pairing protocol requires separate approval. Phase 2 remains ⏳; phase 3 remains ▧.
No file was staged, committed, or pushed. Pico has the authorized branch change; concurrent hostname UI work was externally
committed as `f5f7323784e30d079432824ddb4a9c9a587e3b27` and
`05cc90d67442c536f1cf7bee74a94914ad032c23`, leaving that tree clean. A separate
feature worktree at `/private/tmp/issue473-pico-candidate` starts at the latter
revision and contains the authorized uncommitted companion changes for paired tests; Docs has the uncommitted CLI guide
update on its feature branch. This is an
incomplete-phase report.

### Exact temporary dependency-check commands

Run from the Pi checkout after the pinned read-only source fetch described above.
The corrected harness differs only in includes `lib_rfc/` → `spake2/`; the probe
constructs the identity-point encoding and an all-zero scalar and checks whether
the library rejects them. These commands neither run WsprryPi nor access hardware.

```sh
c++ -std=c++17 $(pkg-config --cflags openssl) \
  -I/private/tmp/issue473-spake2-review -fsyntax-only \
  /private/tmp/issue473-spake2-review/rfc_vectors_test.cpp

c++ -std=c++20 $(pkg-config --cflags openssl) \
  -I/private/tmp/issue473-spake2-review \
  /private/tmp/issue473-spake2-review/rfc_vectors_review.cpp \
  /private/tmp/issue473-spake2-review/spake2/*.cpp \
  $(pkg-config --libs openssl) \
  -o /private/tmp/issue473-spake2-review/rfc_vectors_review
/private/tmp/issue473-spake2-review/rfc_vectors_review

c++ -std=c++20 $(pkg-config --cflags openssl) \
  -I/private/tmp/issue473-spake2-review \
  /private/tmp/issue473-spake2-review/rejection_review.cpp \
  /private/tmp/issue473-spake2-review/spake2/*.cpp \
  $(pkg-config --libs openssl) \
  -o /private/tmp/issue473-spake2-review/rejection_review
/private/tmp/issue473-spake2-review/rejection_review
```

Final `git diff --check` and `git diff --cached --check` pass. Local Markdown checks
cover both changed documents' links, fences, table widths, whitespace, approval
status, unchanged phase-1 completion, and phase 2 in progress with later phases
not started. The recorded upstream negative-probe exit is 1; 4/4 happy-path vector
cases pass. No phase-2 acceptance test result has been fabricated from either.


## Feature-parity adapter review checkpoint

The network alias preserves native GET/PUT methods and bodies. The controller
registry and fixed recovery read mapping include `network`; generated Pi/Pico
assets are synchronized. Pi vendors the unchanged authoritative diagnostic schema
with a provenance digest, rejects missing/extra/wrong-type diagnostic fields,
validates the observed transition against the request, and rechecks identity,
boot, and authorization across a diagnostic read before publishing or writing.
Malformed or inconsistent post-write replies remain unknown outcomes rather than
successful state changes. The existing durable operation reservation, replay,
and explicit readback/review apply to this alias.

Adversarial review fixed target-context alias routing, source-pair test fixture
ordering, missing schema input in the generator drift test, absent native fixture
transition metadata, and browser screenshots of collapsed controls. Regression
coverage includes stale revisions, malformed input/readback, unexpected fields,
reboot during a read, ambiguous results, exact replay, and independent targets.
A further adapter assessment added explicit refusal when the native capability
advertises network management as unsupported, with a before-write regression.
This is an adapter checkpoint, not completion of the unfinished phase-wide acceptance.

Validation completed:

- `make -C src -j4 pico-management-target-test pico-target-controller-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj BIN_DIR=/private/tmp/issue473-controller-native/bin`: PASS. The final added negative observation assertion also passed via `pico-management-target-test`.
- `python3 scripts/generate_control_contract.py --source /private/tmp/issue473-pico-candidate --check`: PASS.
- `python3 scripts/tests/control_contract_pair_test.py`: PASS, including diagnostic-schema drift refusal.
- `python3 /private/tmp/issue473-pico-candidate/scripts/generate_control_contract.py --check`: PASS.
- `npm run test:unit` in `WsprryPi-UI`: PASS. The initial sandbox-localhost refusal was rerun successfully with fixture socket access.
- `node tests/wtp_network_ui_integration_test.js` in `WsprryPi-UI`: PASS; visible Wi-Fi feedback reviewed at 1280 and 390 pixels. Current controls, drafts, conflicts, themes, and narrow layouts were exercised using the existing mock-HTTP browser fixture.
- `cmake --build /private/tmp/issue473-pico-firmware --target WsprryPico -j4`: PASS, including generated assets and linked flash boundary checks. The host picotool build reports no USB support; it was used only as the existing build helper, with no device operations.
- Documentation: activated `docs/.venv`, then `python -m sphinx -b html docs docs/_build/html` in WsprryPi-Docs: PASS. The updated Wi-Fi instructions were rendered and visually inspected at 1280 and 390 pixels.
- `git diff --check` in Pi, the Pico candidate, and Docs: PASS.

The superseded source-pair checkpoint was deliberately stopped after its network
interop passed, because final adapter and test-review fixes required a new source
snapshot. Its cancelled remaining checks are not counted as passing. The reviewed run is tracked in `/private/tmp/issue473-linux-pair/runner-network-parity-reviewed.log`.
Its configuration target caught an incorrect new test expectation: actual network
withdrawal prevents authenticating a replay over the withdrawn carrier. The
adapter correctly returned the accepted response and retained its durable receipt.
The corrected HTTP test verifies exact replay while networking stays enabled, then
verifies disable withdraws network management and preserves the committed receipt
and transmitter state. That focused production HTTP pair passed. USB separately
exercises enable, disable, and replay while its physical-management carrier remains
available. No production change was needed for this test correction.

The reviewed run passed `wtp-network-runtime-test` and `wtp-network-interop-test`.
Pi and Pico production source hashes were checked byte-for-byte unchanged before
rerunning `wtp-config-interop-test` and the entire USB recipe. This continuation is
tracked in `/private/tmp/issue473-linux-pair/runner-network-parity-resume.log`;
its completion must be recorded below before claiming adapter qualification.

Documentation Impact: updated the existing Wi-Fi operator instructions in
WsprryPi-Docs and this phase record. The wider independent-target and remote-Pi
operator workflow still requires documentation when those services and editor
integration are complete. Existing WTP execution documentation remains applicable
because WTP and execution ownership are unchanged.


### Completed adapter source-pair qualification

PASS: the reviewed network runtime and network interoperability targets, followed
by the corrected complete configuration target and every USB recipe step. No
failed, cancelled, or skipped required check is counted as a passing result.
The final continuation exited zero with `ALL REQUESTED CANDIDATE PAIR STEPS PASS`.
The earlier configuration failure is resolved by its successful complete rerun.

Exact production sources:

- Pi `c48a00dbbd6d9d65014c56f286ace074df9240e9` plus current uncommitted changes, branch `codex/issue-473-device-owned-scheduling`.
- Pico `05cc90d67442c536f1cf7bee74a94914ad032c23` plus current uncommitted changes in `/private/tmp/issue473-pico-candidate`, branch `codex/issue-473-phase2-paired-candidate`. The main Pico checkout was not edited.
- Mbed TLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.

The reviewed and resumed manifests are
`/private/tmp/issue473-linux-pair/network-parity-reviewed-source-manifest.json`
and `network-parity-resume-source-manifest.json` in that same directory. A final
hash comparison confirmed identical Pi/Pico production sources across both
successful gates and the current working trees. Only the corrected HTTP/USB tests
and strengthened browser screenshot assertions changed between snapshots.

From `/work/wsprrypi/src`, the successful checked-in recipe commands were:

```sh
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-network-runtime-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-network-interop-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-config-interop-test
cmake -S /pico -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-usb-pair-client-build wtp-usb-management-test
node ../WsprryPi-UI/tests/pico_management_usb_pair_test.js /work/wsprrypi/src/build/bin/pico_management_usb_pair_client /tmp/pico-usb/usb_management_driver
node ../WsprryPi-UI/tests/wtp_usb_settings_test.js
WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js /work/wsprrypi/src/build/wtp-network/wtp_usb_pair_client /tmp/pico-usb/usb_management_driver
```

Logs: `runner-network-parity-reviewed.log`,
`runner-network-parity-resume.log`, `network-runtime.log`,
`network-interop.log`, `config-interop.log`, `independent-usb.log`, and
`usb-pair.log` under `/private/tmp/issue473-linux-pair`. The configuration target
includes both independent management transport and two-target production HTTP
fixtures. The full USB pair exercised native production framing/ACK, current
schemas, protected requests, storage, and refusal paths. The independent USB test
also passed network enable, disable, fresh observation, and exact durable replay
while USB remained available.

The final adapter assessment found no remaining actionable issue. Impeccable
review covered visible feedback in light/dark themes at desktop/mobile sizes;
the browser fixture now waits for the feedback to be inside the viewport after
resizing. These are software, build, and rendered-UI results. Physical Wi-Fi/USB,
installation, services, clocks, GPIO, and RF remain unqualified. No GitHub CI was
started because nothing was pushed.

The first-party UI component changed for automatic status, nearby feedback, and
its tests. The previously approved INI-Handler component changes remain in the
working tree and its standalone Linux tests passed in this validation sequence;
this adapter slice added no further INI-Handler change. The parent, Pico candidate,
and Docs working trees remain dirty, with nothing staged, committed, or pushed.
At that checkpoint, phase 2 remained ⏳ and phase 3 remained ▧. The next
unfinished step was independent editor migration and retirement of superseded
settings routes, followed by the local Pi management service; pairing-dependent remote Pi service
qualification is still held by its provider gates.


## Active phase-2 editor and service continuation

The user instructed continued phase-2 execution under the original rules. The
existing Fleet editor is being migrated to independent enrolled-target contexts,
without the phase-3 shared selector or tab relocation. Existing control modules
now expose scoped mounts. The new manager uses captured target handles and
separate DOM instances per stable device/profile generation, keeping inactive
drafts and accepted old-target operations separate. A read-only retained-draft
view makes inactive entries inspectable without restoring their authority. It uses the existing
resource readback and explicit-review contract for uncertainty. Public selected-
backend settings routes and their obsolete host advertisements are retired in
the working implementation; the explicit firmware and execution paths remain
separate. Production two-Pico browser qualification is in progress. These edits
are not yet a completed migration or phase acceptance. Continue through review,
repairs, documentation, both paired suites, and the remaining Pi service work.


## P2-07: approved INI stock-preview boundary

Status: approved by the user on 2026-11-16; implementation and validation in
progress. The production Pi management
owner must include existing reset and repair actions in the same transaction as
settings and receipts. `IniFile::reset_to_stock()` and `repair_from_stock()`
currently replace the live file before the parent can validate or stage that
complete transaction. The approved `serialize_with_data()` predicts ordinary
saves, but does not reproduce stock-template layout and reset/repair semantics.

Proposed narrow component change: add a pure
`serialize_from_stock(semantic_version, preserve_compatible_values)` method.
Return the exact bytes that the corresponding existing reset or repair would
save, including stock comments, version substitution, and compatible-value
preservation. Reuse that implementation in the existing reset and repair entry
points. Do not change parser syntax, supported keys, public reset/repair behavior,
WTP, or runtime hardware policy. Test byte equality against both existing
operations and prove preview leaves the file and in-memory data unchanged;
update the standalone component README and run its standalone suite.

The parent will validate the proposed configuration before final-file mutation,
stage its exact after-image with settings/receipt/history records, and publish
only after the durable decision is recovered and verified. The component will
remain independent of parent transaction and scheduling types. Existing repair
and reset controls will retain their behavior through that parent owner.

This additional API is outside P2-05's explicitly narrow serializer approval.
The [repository component policy](../../../AGENTS.md) says: “Obtain approval
before broadening a parent-only task into component changes.” P2-07 extends
P2-05's expressly narrow component API boundary; the user has now approved it.
Independent phase-2 validation and implementation continue while it is pending.


## Reviewed editor and Pi transaction-owner checkpoint

Phase 2 remains ⏳. Phase 3 has not started. The Pico editor migration and
retirement of its superseded public settings routes are implemented and locally
qualified. The next unfinished implementation is the local Pi service/startup/
admission integration, including routing, capability negotiation, bound actions,
and all existing configuration writers. Reset/Repair staging requires P2-07
approval before extending INI-Handler. Remote Pi pairing/service qualification
retains P2-01's existing provider hold. There is no manual-certificate workaround,
new pairing wire, BoringSSL adoption, new solar runtime, or native scheduling
activation in this checkpoint.

### Implemented boundaries

- First-party `WsprryPi-UI`: scoped management/USB mounts and independent per-Pico
  editors; immutable saved-profile binding; per-target draft retention; explicit
  consumer login; fresh readback after selection/re-login; accepted old-target
  request tracking; bounded redacted history; durable receipt lookup and explicit
  review after uncertainty. Profile replacement, including A-to-B-to-A, cannot
  restore the old generation's editing authority. Manage settings brings the
  editor into view and moves keyboard focus to its heading. Firmware/execution
  workflows remain separate.
- Parent application: public selected-member settings routes and the network
  alias return typed refusal; the internal native-resource adapter remains the
  target context's mapper. Obsolete host settings advertisements are removed.
- Parent Pi storage: `PiManagedStore` coordinates actual INI after-images, exact
  managed settings, principal-bound operation receipts, and the catalog history
  marker in one WAL. Its tests exercise real configuration validators/writers,
  explicit initialization, legacy saves, revision ABA, exact historical replay,
  authorization isolation, interrupted commit recovery, and missing/replaced
  authority. Safe file observations include inode and nanosecond metadata.
- Parent configuration: separately installed persistence and snapshot owners
  allow ordinary saves and verified reloads to share that store. Snapshot checks
  do not save or advance a revision. These hooks are tested, but are **not yet
  installed into production startup**; this is not an activated Pi service or a
  claim that runtime admission integration is complete.
- Parent `wtp_endpoint`: output generations record ownership/job/local-output
  transitions. A typed Stop reservation compares boot, generation, owner, and job
  under the authority lock and fences new local/remote admission until released.
  This is the service's tested prerequisite, not a completed public Stop route.
  WTP request/response bytes and its version are unchanged.
- Authorized Pico candidate: current shared Control/1 context/status/capability
  definitions, matching strict C++/browser validation, authoritative vectors,
  lifecycle registry/checker, and generated firmware/browser assets. No new Pico
  scheduling behavior was added. Resource schema digest/descriptor production and
  negotiation still belong to the unfinished Pi service.
- The existing approved `src/INI-Handler` serializer changes remain intact. This
  continuation added no stock-preview API or further INI-Handler source change.
  `src/WTP-Server` source was not modified; its standalone tests were exercised.

### Adversarial review and iteration

Repeated review against the phase prompt, current diff, approved contracts, and
rendered workflows found and repaired these concrete issues:

1. Page-wide editor assumptions could couple settings availability to the host
   backend. Scoped mounts, captured handles, per-device DOM, route retirement,
   and the two-target production browser test cover the replacement behavior.
2. Returning through a consumer login needed to restore the retained draft onto
   a fresh authenticated handle only for the same server-issued profile
   generation. Rendered re-login/profile-ABA tests cover that transfer and require
   a fresh resource revision. Password inputs clear after login.
3. Receipt history needed bounded terminal eviction without losing unresolved
   operations. Strict metadata checks reject malformed/foreign/overflow session
   bindings. A committed or expired receipt is reviewed without an inappropriate
   native reconcile; the server independently retains its own write holds.
4. Profile removal left recovery controls actionable outside the inert editor.
   Both controls now disable immediately, and handlers refuse paused entries,
   including a synthetic click that bypasses the disabled button.
5. Equal-byte file replacement needed to retire an old storage observation;
   inode, nanosecond metadata, final-descriptor checks, and replacement tests now
   detect it without initializing new authority.
6. Treating an unchanged file-monitor reload as persistence could create a save
   loop. Snapshot validation is now separate; three consecutive owning-parser
   reloads leave both INI identity and managed revision unchanged.
7. A preexisting local-work shortcut could bypass the Stop reservation. The
   common local admission path now checks that reservation first. Tests cover
   local/remote ownership changes, idle ABA, stale boot/owner/generation, and
   refusal to admit replacement work during cancellation.
8. The unbounded repetition in the exact decimal-Hz structural regex could use
   input-sized native regex stack. That exact pattern now has an iterative shape
   validator; 24,000-digit positive and noncanonical cases preserve the contract
   without bypassing the later owning backend range policy.
9. Manage settings needed visible navigation and keyboard focus. The explicit
   action now focuses and reveals the editor heading; the final real browser
   asserts focus and reviews responsive light/dark renders.
10. Old profile drafts remained in memory but were not inspectable after choosing
    a replacement profile. **Retained drafts** now supplies read-only field text
    without restoring any connection, exposing passwords, or sending a request.
    Actual two-target browser assertions prove the old generation's entries remain
    visible after ABA while the current editor requires its new generation.
11. Retained inspection initially assumed every generated field had an ID. The
    real editor caught that assumption; wrapped/ARIA/fallback labels now handle
    ID-less fields safely. Maximum-length profile names wrap at mobile width.
    Subsequent actual-pair and consumer browser assessments pass.

A subsequent full assessment of these implemented changes found no further known
actionable defect. This finding does not close the missing Pi service,
startup/admission, provisioning, descriptor, or phase acceptance work above.

### Source selection and validation

All results are local, hardware-free software evidence against uncommitted
feature-branch changes:

- Pi: `codex/issue-473-device-owned-scheduling`, base
  `c48a00dbbd6d9d65014c56f286ace074df9240e9`.
- Pico: `/private/tmp/issue473-pico-candidate`, branch
  `codex/issue-473-phase2-paired-candidate`, base
  `05cc90d67442c536f1cf7bee74a94914ad032c23`. The ordinary sibling Pico checkout
  remains unchanged and clean. The companion changes have not been delivered to
  `devel`, `main`, or a remote branch.
- Docs: `codex/issue-473-device-owned-scheduling`, base
  `a06c1d5590614a4794684aac794c468bec36b7cf`, plus its three documented dirty paths.
- Selected Mbed TLS source: `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.

The reviewed unprivileged Debian run passed **both complete canonical recipes**.
It used `--network none`, no attached hardware devices, the explicit simulated
profile, the current candidate Pico source, and the selected Mbed TLS tree.
`/private/tmp/issue473-linux-pair/runner-editor-store-reviewed.log` ends with
`ALL REQUESTED CANDIDATE PAIR STEPS PASS`. Its recorded source hashes are in
`editor-store-reviewed-source-manifest.json` in that directory. Commands from
`/work/wsprrypi/src` included:

```sh
make -C /work/wsprrypi/src/INI-Handler/src test SUDO=
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls controller-only-test wtp-pi-authority-test pi-managed-records-test pi-managed-store-test pi-management-journal-test pi-management-anchor-test pi-config-persistence-coordinator-test pico-management-target-test pico-management-history-test pico-usb-settings-test pico-management-journal-test pico-target-controller-test controller-contract-test build/bin/pico_management_pair_client build/bin/pico_controller_pair_fixture build/bin/pico_management_usb_pair_client
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-network-runtime-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-network-interop-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-config-interop-test
cmake -S /pico -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-usb-pair-client-build wtp-usb-management-test
node ../WsprryPi-UI/tests/pico_management_usb_pair_test.js /work/wsprrypi/src/build/bin/pico_management_usb_pair_client /tmp/pico-usb/usb_management_driver
node ../WsprryPi-UI/tests/wtp_usb_settings_test.js
WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js /work/wsprrypi/src/build/wtp-network/wtp_usb_pair_client /tmp/pico-usb/usb_management_driver
make -j4 BACKENDS=si5351,simulated ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=build/issue473-si5351-objects BIN_DIR=build/issue473-si5351-bin guarded-mode-change-persistence-test
```

The runtime, interoperability, and complete configuration integration targets
all passed. The last includes actual consumer crypto/handler tests, independent
management transport, and production two-target HTTP/CSRF/context/journal tests.
The actual USB driver/client suites passed framing/ACK, native handlers,
serialization, storage, and independent network enable/disable/readback/replay
through retained USB. Authority and owning configuration fixtures also passed.
The deliberately injected corrupt/missing-authority startup refusals print
errors as expected; they are successful negative tests, not service readiness.

After that paired snapshot, only phase/engineering documents and the UI's local
focus/retained-draft presentation plus browser fixtures changed. A hash comparison
confirmed all Pi native/shared-contract/protocol-client and Pico source files
still match the reviewed paired snapshot. The final presentation hashes and
actual-browser qualification are recorded in
`editor-retained-ui-source-manifest.json`. The final
browser run reads the current UI and uses freshly rebuilt, unchanged native
candidate binaries. No protocol/source-pair discrepancy is being waived.

Additional successful checks:

- `python3 scripts/generate_control_contract.py --source /private/tmp/issue473-pico-candidate --check` and
  `python3 scripts/tests/control_contract_pair_test.py` from Pi.
- `python3 scripts/generate_control_contract.py --check` and
  `python3 tests/control_policy_tests.py` from the Pico candidate; generated
  operator assets use the existing pinned esbuild recipe.
- `cmake --build /private/tmp/issue473-pico-firmware -j4`: current firmware link
  and image checks passed; log `issue473-editor-context-firmware-build.log`.
- `npm run test:unit` from `WsprryPi-UI`: final pass in
  `/private/tmp/issue473-editor-retained-final-ui-unit.log`.
- `node tests/pico_controller_http_pair_test.js /private/tmp/issue473-pair-network /private/tmp/issue473-controller-native/bin/pico_controller_pair_fixture --browser`:
  final pass in `/private/tmp/issue473-target-editor-browser-retained-final2.log`.
- `node tests/pico_target_editor_state_browser_test.js`: rendered consumer
  lifecycle/receipt/revocation pass in
  `/private/tmp/issue473-consumer-editor-retained-final2.log`. This
  presentation fixture injects transport; it does not substitute for the actual
  paired carrier/crypto tests above.
- `node tests/wtp_network_ui_integration_test.js` and
  `node tests/wtp_usb_ui_integration_test.js`: desktop/mobile and light/dark
  component passes in `issue473-editor-component-browser-2.log` and
  `issue473-editor-usb-component-browser-final.log` under `/private/tmp`.
- Parent source regression: built its existing binary with the explicit
  simulated profile and ran
  `/private/tmp/issue473-controller-native/bin/ui_source_regression_test` from
  `src`; pass in `/private/tmp/issue473-editor-retained-ui-source-regression.log`.
- The focused native controller/store/journal/persistence/contract tests passed
  with `BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` and isolated object/binary
  directories. Mac metadata probes emit expected missing `/proc` diagnostics.

Initial native validation attempts exposed a preexisting Linux-format WTP server
archive and a guarded persistence test requiring Si5351 capability. No archive
was discarded. The standalone WTP server core was built/tested in the separate
`BUILD_DIR=/private/tmp/issue473-native-wtp-server`, and the authority fixture
was linked against that native library and passed. The guarded regression then
passed in the explicit Si5351-capable Linux profile above. Those corrected
results do not claim the full physical-backend semantics suite ran.

### UI review and Documentation Impact

Impeccable review preserved the existing bench-instrument design. Production
browser workflows covered controller-only management with two actual Pico
services, draft isolation, late replies, lost replies, fresh-browser recovery,
explicit review, profile replacement/ABA, read-only retained entries without
password disclosure or mutation, and Manage keyboard focus. Consumer
re-login, committed/expired receipts, and paused recovery controls received the
additional rendered fixture. Light/dark renders at 1440 and 390 pixels were
reviewed under `/private/tmp/issue473-target-editor-ui`; USB/component checks also
cover 390-by-844 mobile rendering. No horizontal overflow was observed.

Documentation updated: phase indicator/progress and `docs/wtp-fleet.md`; sibling
Setup/Transmitter instructions and Advanced INI transmitter-backend guidance.
The existing controller-only CLI documentation change remains in the Docs tree.
The sibling build passed with no warnings using its activated `docs/.venv` and
`python -m sphinx -b html docs docs/_build/html` workflow; log
`/private/tmp/issue473-editor-retained-docs-build.log`. The updated editor guide rendered
at 1280 and 390 pixels; evidence is
`/private/tmp/issue473-independent-editor-docs-{1280,390}.png` and
`issue473-editor-docs-render.log`. The retained-draft guidance was rebuilt and
rendered again at both widths in `issue473-retained-editor-docs-{1280,390}.png`
and `issue473-retained-docs-render.log` under `/private/tmp`.

Documentation considered but unchanged: Pico operator instructions, because
this continuation adds shared definitions rather than new device-side controls;
existing unrelated Setup screenshots do not depict this new editor. Further
Pi service, readiness, pairing, and recovery instructions remain required when
those behaviors are implemented. No operator guide claims they are available.

Pi, isolated Pico candidate, and Docs working trees are dirty and unstaged;
no existing source changes were discarded. No staging, commit, push, PR,
installation, service operation, physical USB/Wi-Fi operation, GPIO, or RF test
occurred. No GitHub CI run was started because nothing was pushed. Full phase-2
acceptance, installation/service readiness, physical transport, clocks, timing,
and RF remain unqualified. P2-07 is pending; do not treat its preview proposal as
approved or silently begin phase 3.


## P2-07 authorized continuation

The user approved the stock-preview API, standalone tests, and README changes
on 2026-11-16. Phase 2 remains ⏳ and phase 3 remains ▧. The component preview
and shared Reset/Repair implementation now join the parent transaction store.
Earlier references to P2-07 as pending describe the
preceding checkpoint, not the current authority. No commit, push, or hardware
operation is authorized.

### Implemented stock preparation and transaction integration

`IniFile::serialize_from_stock(version, preserve_compatible_values)` returns the
exact Reset or Repair after-image without changing the live file, singleton
state, pending edits, or layout. Repair reads fresh saved values and retains only
matching stock keys. Existing Reset/Repair reuse this preparation. Ordinary
parser syntax and passthrough behavior remain unchanged; the component README
documents the pure API and caller-owned validation/durability boundary.

The parent prepares that after-image through the existing semantic parser in a
private, exclusively created temporary file, restores the live singleton, and
checks input ownership, regular-file type, link count, modes, size, file stamps,
and configuration revision before publication. Unsaved component edits refuse
preparation. `PiManagedStore::persist_stock` joins the exact bytes, managed
settings, preserved scheduling, and receipt history under the existing WAL.
Preparation does not publish runtime configuration or initialize output. The
ordinary writer, snapshot validator, and stock coordinator are installed or
cleared together; production installation remains unfinished below.

Component tests cover exact golden Reset/Repair bytes, version substitution,
no-op and raw passthrough behavior, pending edits/removals, missing inputs, and
failure purity. Parent tests cover real semantic parsing, exact bytes and
generations, restart/crash recovery, invalid or oversized stock, untrusted modes,
pending edits, parser restoration, and temporary-file removal. Native focused
commands passed:

```sh
make -C src/INI-Handler/src test SUDO= CXX=c++ \
  CXXFLAGS='-std=c++20 -Wall -Wextra -Werror -MMD -MP' \
  LDFLAGS=-lpthread OBJ_DIR_DEBUG=/private/tmp/issue473-stock-preview-obj
make -C src -j4 pi-managed-store-test pi-management-journal-test \
  pi-config-persistence-coordinator-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
```

Evidence: `/private/tmp/issue473-stock-preview-standalone-reviewed.log` and
`/private/tmp/issue473-stock-parent-reviewed.log`. The initial native standalone
invocation omitted dependency-generation flags required by its existing Makefile;
the corrected invocation above passes. This was a command correction, not a
source workaround.

Both complete canonical software pairs passed for the stock-preview checkpoint:
`/private/tmp/issue473-linux-pair/runner-stock-preview.log`, with exact file
hashes in `stock-preview-source-manifest.json` beside it. This includes the INI
standalone tests, configuration/controller/authority/store tests, all network
runtime/interop/configuration checks, actual Pico USB driver and Pi clients,
independent USB management, full USB UI pair, and guarded persistence with the
explicit Si5351-capable Linux profile. Subsequent descriptor/service changes
require another full candidate run; that checkpoint is not final-source evidence.

### Pi resource identities and local service continuation

The authoritative companion schema now incorporates immutable Pi configuration
and management policy-source references. Each `pi-config`, `pi-scheduling`, and
`stop` descriptor hashes the canonical resource root, complete current controller
definition closure, and these value-policy dependencies. Coordinated schema or
incorporated policy changes therefore retire the corresponding type identities.
Pico does not advertise these Pi resources as its own capabilities. Both generated
consumers recognize exact identifiers/digests and reject malformed descriptors,
wrong namespaces, unknown digests, unsupported write activation, and invalid limits.

`PiLocalManagementService` implements bounded, protected-gateway-facing local
context/settings/status documents, strong revision checks, native configuration
reservation, full owning semantic validation, atomic INI/settings/receipt saves,
exact replay, and historical receipt lookup. It uses server-owned local principal
and authorization context; request documents cannot supply authority. Listener
ports and existing WTP TLS file references are read-only echoes. Scheduling writes,
new activation, and public Stop remain unavailable rather than claiming runtime
semantics that have not been implemented. This class is **not installed into
production startup or HTTP routing**, and is not a remote authenticated Pi service.

The focused fixture uses the actual parent configuration/store/WAL and WTP native
authority with an inhibited RF engine. It covers stale identity/boot/context/type/
revision, malformed and semantically invalid candidates, read-only fields,
non-mutating reads, exact durable replay/lookup, a racing real WTP claim, missing
output authority, and actual receipt retention/eviction through the production
transaction owner. No simulated idle output substitutes for missing authority.

Review fixes: receipt lookup no longer depends on current output availability;
actual terminal eviction advances a monotonic service context epoch; configuration
reservations exclude new scheduled local work as well as new tones/remote claims;
configuration and bound Stop reservations exclude each other. Accepted existing
work retains its existing lifecycle. A subsequent full assessment also corrected
status to `authority_state:unclassified`: observing native output is not proof
that the later legacy-controller inventory and authority transition have run.
The response-bound assessment then found that a 32-KiB candidate can exceed the
resource limit once wrapped in its settings readback. Complete responses are now
bounded, and saves preflight the normalized candidate with maximum-length output
and connection metadata before any publication. Oversized legacy readback returns
a small typed refusal. Tests exercise both the maximum-sized request and a stored
legacy candidate at the canonical storage limit.
The lock-order review replaced waiting entry locks with typed busy refusals and
removed duplicate recursive locks around the parent's validation lifecycle while
retaining native admission protection. The focused fixture holds each real
lifecycle/configuration lock from another thread and verifies refusal without
persistence. This preserves the parent's existing revision checks and deliberate
unlock around slow validation.
No WTP wire or carrier contract changes.

The current unfinished step is production Pi startup recovery and admission-owner
installation, followed by protected local routing and complete bound Stop
semantics. Existing safe-default startup, legacy writers, file reload, output
replacement, and failure paths must share that owner before activation. Automatic
remote Pi management remains held on the previously recorded conforming PAKE
provider gate. New native recurrence and phase 3 remain out of scope.

Documentation Impact: the component README, this progress record, and phase
indicator reflect this continuation. Existing operator guides were considered and
remain unchanged here because the Pi service is not activated. Operator-facing
startup, save/recovery, Stop, and automatic pairing documentation remains required
when those behaviors are available. Earlier Pico editor/operator documentation and
its visual evidence remain applicable; this continuation changes no UI presentation.

### Final-source validation and repeated assessment, 2026-11-16

The final repeated assessment found no further actionable defect in the approved
INI change or the implemented inactive Pi service subset. This is not completion
of phase 2: the startup/admission installation, production Pi routing, bound Stop,
automatic pairing, and full local/remote Pi evidence above remain open. Phase 2
stays ⏳; phase 3 stays ▧. No additional approval is needed for P2-07.

The final isolated Linux run completed successfully with
`ALL REQUESTED CANDIDATE PAIR STEPS PASS` in
`/private/tmp/issue473-linux-pair/runner-pi-service-final.log`. Its runner is
`/private/tmp/issue473-linux-pair/run_pi_service_final.py`; every exact command and
result is in that log, with individual `pi-service-final-*.log` outputs beside it.
`pi-service-final-source-manifest.json` records both HEADs and every selected
tracked/untracked candidate file hash. After capture, only the two engineering
progress/indicator documents changed; production sources, generated assets,
and tests still match the captured hashes. The earlier fully green
`runner-pi-service.log` is a checkpoint. The intervening
`runner-pi-service-reviewed.log` was deliberately cancelled after the lock review
and is **not** counted as qualification; the final complete rerun supersedes it.

The container ran as UID/GID 501:20 with `--network none --cap-drop ALL
--security-opt no-new-privileges`, no hardware devices, read-only candidate and
dependency mounts, a private temporary filesystem, and the existing isolated
build volume. Both actual canonical recipes passed, including:

```sh
# In the isolated Linux /work/wsprrypi/src, with candidate sources mounted at /pico:
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-network-runtime-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-network-interop-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-config-interop-test
cmake -S /pico -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls wtp-usb-pair-client-build wtp-usb-management-test
env WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js /work/wsprrypi/src/build/wtp-network/wtp_usb_pair_client /tmp/pico-usb/usb_management_driver
make -j4 BACKENDS=si5351,simulated ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=build/issue473-si5351-objects BIN_DIR=build/issue473-si5351-bin guarded-mode-change-persistence-test
```

The runner also passed the component standalone test, actual production-main
controller-only startup checks, all Pi records/store/journal/anchor/coordinator/
service tests, native authority tests, closed controller vectors, Pico management
tests, independent USB management, and USB UI checks. No failed, cancelled,
skipped, or old-source run substitutes for either final paired suite.

Additional successful checks:

- `make -C src -j4 pi-management-service-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj BIN_DIR=/private/tmp/issue473-controller-native/bin`:
  `/private/tmp/issue473-pi-service-locks-final.log` (native macOS).
- `npm run test:unit` in `WsprryPi-UI`:
  `/private/tmp/issue473-pi-descriptor-ui-unit-reviewed.log`.
- `python3 scripts/generate_control_contract.py --source /private/tmp/issue473-pico-candidate --check` and
  `node WsprryPi-UI/tests/controller_contract_test.js` in Pi: PASS.
- `python3 tests/control_policy_tests.py`,
  `python3 scripts/generate_control_contract.py --check`, and
  `node scripts/build_operator_assets.mjs` in the isolated Pico candidate: PASS.
- `cmake --build /private/tmp/issue473-pico-firmware -j4`:
  `/private/tmp/issue473-pico-firmware-pi-descriptors.log`, firmware build PASS.
- Both immutable Pi policy source revisions and every incorporated path were
  verified with `git cat-file -e`; all exist. Final diff whitespace/index checks
  pass in Pi, the isolated Pico candidate, and Docs.

The initial native UI invocation could not bind its loopback fixture inside the
sandbox; the authorized rerun passed. Two overlapping focused native checks were
not used as final evidence; a serial rerun and the complete final Linux run pass.
macOS `/proc`/device-tree diagnostics in portable fixtures are expected environment
limitations, not physical-platform validation. The guarded persistence check uses
the explicit Si5351-capable Linux profile; this does not claim the full physical-
backend semantics suite ran.

Source boundaries: Pi `codex/issue-473-device-owned-scheduling` at
`c48a00dbbd6d9d65014c56f286ace074df9240e9` plus tested dirty changes; isolated Pico
`codex/issue-473-phase2-paired-candidate` at
`05cc90d67442c536f1cf7bee74a94914ad032c23` plus tested dirty changes; Mbed TLS
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The ordinary Pico checkout remains
clean and unchanged. Docs remains on `codex/issue-473-device-owned-scheduling` at
`a06c1d5590614a4794684aac794c468bec36b7cf` with the three earlier unstaged guide
changes. The Pi tree has 137 modified/untracked paths, none staged. The reusable
component modified in this continuation is `src/INI-Handler`; generated UI contract
consumers/tests are the `WsprryPi-UI` portion. No WTP-Server component source changed.

There was no staging, commit, push, merge, PR, release, or GitHub CI run. No physical
USB/Wi-Fi operation, installation, installed-service change, GPIO, transmission,
or RF qualification occurred. OpenSSL/Mbed TLS remain in place; BoringSSL was not
adopted, and the agreed provider/maintenance/packaging hold remains unchanged.

## P2-08 approved: include production Mac roles

Status: the user selected **Include production Mac roles now** on 2026-11-16.
This does not reopen P2-07 or remove the approved controller-only Pi scope.

The existing `wtp_pi_device_id()` derives the stable Pi identity from
`/etc/machine-id`. The production controller-only path uses that same source.
Native macOS software fixtures inject a typed test identity; that is not a
production identity source. Installing the new configuration transaction owner
unconditionally would therefore change otherwise working Mac transmitter startup.
Inventing a persisted fallback identity would also change identity and recovery
semantics without an approved policy.

The unselected Linux-only option was: retain the current Linux Pi identity and WTP contract;
qualify the new production local-Pi management and controller-only roles on Linux.
Keep Mac compilation and hardware-free development fixtures, and preserve its
existing transmitter startup without advertising the new production management
roles. Do not create a random identity, use a fallback role, or bypass previously
initialized managed-state recovery on an unsupported host. Explicitly document
that limitation. No new platform identity storage or deployment is included.

The approved scope includes production Mac management/controller roles now, with a
separately defined stable OS/device identity and recovery policy, corresponding
build/packaging and runtime tests, and operator guidance. A fixture injection or
an arbitrary machine-ID file would not establish that support.

`AGENTS.md` requires direction when ambiguity materially changes compatibility,
persistence, or workflow. This platform boundary is not specified by the approved
Pi-target design; the dependency table's Mac development-profile checks do not
authorize a new production identity policy. The user's selection now authorizes
that Mac scope; do not impose the unselected Linux-only limitation.

Implementation policy: the no-argument identity provider uses Linux's existing
machine-ID mapping unchanged. On Mac it queries the OS host UUID through
`gethostuuid` with a two-second maximum wait, rejects unavailable/nil/sentinel
identities, and hashes its lowercase 32-hex bytes under the distinct
`wsprrypi-wtp-macos-device-v1:` domain before truncating SHA-256 to 128 bits. This
uses the existing system C library; it adds no user identity file, certificate,
package, subprocess, or mutable selector. Existing typed Linux file fixtures
remain explicit. Identity labels do not authenticate management or WTP.

An OS/device identity change must fail existing saved identity bindings; it is
not permission to replace managed history or enroll a peer automatically. Boot
IDs remain fresh and independent. Mac production-main controller startup now
participates in the existing controller test instead of being excluded by host.
The actual OS identity check and Linux/Mac golden mappings pass; startup,
packaging/profile, paired integration, and operator qualification are still
being completed for this continuation.

## P2-09 approved: protected native-startup web recovery

Status: the user approved protected web recovery on 2026-11-16. P2-07 and
P2-08 remain approved; implementation of this separate workflow is in progress.

The existing transmitter startup catches an invalid INI and publishes safe
defaults with Transmit off, allowing the existing configuration UI to start.
The new owner correctly refuses to initialize durable managed settings from those
defaults: they are not the saved configuration. Wiring it without choosing the
failure workflow would either remove web recovery or risk showing defaults as
confirmed saved values. The phase-1 admission hold is approved, but that UI
behavior is not specified.

Approved workflow:

1. Keep the protected web UI available in the **transmitter's blocked recovery
   state**. Do not select controller-only mode or initialize a transmitter
   backend, selectors, ancillary GPIO, or WTP execution listener from defaults.
   All output admission stays closed.
2. Display the current known configuration as an explicitly unconfirmed,
   redacted recovery draft. Preserve invalid/unsupported saved values and drafts;
   do not normalize them merely by rendering a control. Missing or wrong-type
   fields carry bounded typed diagnostics rather than becoming saved defaults.
3. Permit an explicit complete Save, Repair, or Reset through owning semantic
   validation and the exact transaction store. No GET, page load, preview, or
   field focus initializes managed history. A rejected candidate preserves both
   the original file and the operator draft.
4. After a valid explicit mutation, retry normal native startup automatically:
   validate saved records, recover ownership, and quiesce the selected output
   before marking readiness. Existing Transmit/boot/authority rules still apply;
   recovery does not itself enable a new native schedule. No manual process,
   service, machine, or certificate setup/restart is required for this retry.
5. Corrupt or missing previously initialized authority/history remains a
   read-only storage recovery hold. Configuration Reset is not permission to
   erase that evidence or invent clean-install proof.

This includes a bounded read-only Control/1 host-readiness document and a closed,
redacted configuration-recovery-draft document/adapter, their authoritative shared
definitions and generated consumers, and the smallest existing-Setup recovery
banner/actions needed to distinguish drafts from saved settings. It does not add
the phase-3 shared Device selector or tab redesign. The exact definitions, strict
refusal tests, desktop/mobile Impeccable review, and both paired suites remain
required with implementation.

The alternative is to fail the native process before listeners when initial
configuration or durable state cannot validate, leaving files unchanged. That is
smaller, but removes the current web-based configuration-repair path and requires
out-of-band repair before restarting. Neither option automatically enters the
controller-only role after physical or configuration failure.

`AGENTS.md` says: “If an ambiguity would materially change behavior,
compatibility, persistence, hardware operation, or user workflow, stop and ask for
direction.” That decision has now been resolved by the user. The automatic retry
applies to configuration recovery before initial output startup; it cannot clear
existing unknown-output evidence, process-lifetime quiescence latches, or the
required explicit output reconciliation.

### Protected recovery foundation and adversarial corrections

Implemented foundations, not a production-available recovery page:

- Authoritative current Control/1 host-readiness, redacted recovery draft,
  complete recovery Save, and explicit Repair/Reset action documents; generated
  Pi/Pico/browser consumers and 149 closed contract vectors. WTP is unchanged.
  Pico operator and radio bundles were regenerated with pinned esbuild 0.28.2.
- Read-only owning-parser observations happen before required-section, legacy
  migration, and semantic validation. Only saved fields appear in the draft;
  missing fields remain unavailable. Private connection-file references and
  unknown fields are excluded from the browser projection. Invalid Band GPIO
  input retains both value and polarity; nonfinite saved text and subnormal
  numbers are not silently converted to defaults in the observation.
- A saved-file observation expires when the transaction atomically replaces
  the INI, including equal-byte replacement. Another editable recovery draft
  requires actual parser readback. This prevents a stale cache from clearing
  private references after a successful save.
- Recovery uses a strong digest of the bounded fixed-file observation, managed
  revision, device identity, and context epoch. Initial absence differs from
  replacement. Initial Save, its exact INI, settings generation 1, retained
  receipt, and catalog anchor share one WAL decision. Lost-reply recovery never
  replays a write based solely on a browser's last rendered values.
- Save validates a private after-image through the owning staged INI validator,
  then commits without publishing runtime state or constructing a live backend.
  Repair/Reset reuse the same helper and exact stock transaction. Stored
  scheduling is preserved. The real saved file must pass owning-parser readback
  before the startup owner can activate; a durable save alone is insufficient.
- Failed `prepare()` retains refusing ordinary, snapshot, and stock persistence
  owners as well as the closed output-admission predicate. Clearing ordinary
  endpoint hooks cannot remove that predicate. Native configuration reservations
  retain the exact endpoint authority rather than releasing through a replacement.
- `PiRecoveryService` is an uninstalled protected-browser adapter. Its trusted
  caller supplies process startup state and verified identities. It enforces
  authentication/CSRF intent, body bounds, exact closed commands, binding,
  capability, If-Match, source revision, and exact durable replay. It does not
  infer readiness from defaults, select a process role, start listeners, or
  automatically retry startup itself. Missing/currently unavailable parser
  observations are read-only; no default configuration is represented as saved.

Focused native evidence:

```sh
make -C src -j4 pi-managed-startup-test pi-managed-store-test \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
```

PASS in `/private/tmp/issue473-recovery-actions-reviewed-test.log`: invalid/partial
saved drafts; authentication, CSRF, body, binding, capability, and revision
refusals before persistence; semantic rejection; staged explicit Save; failed
Repair and valid Reset; one-decision first-write receipt recovery; exact replay
before/after readiness; unchanged runtime until parser readback; initialized
history mismatch becoming read-only storage recovery. Expected device-tree
metadata diagnostics on Mac do not constitute hardware access or qualification.

`controller-contract-test` and its JavaScript consumer pass all 149 vectors;
Pico `python3 tests/control_policy_tests.py` passes, including closed host-route
methods, duplicates, foreign destinations, and mandatory recovery documents.
The running paired checkpoint uses
`/private/tmp/issue473-linux-pair/run_protected_recovery_foundation.py`; its
source manifest is `protected-recovery-foundation-source-manifest.json` in that
directory. Later staged-Save/service changes are outside that copied checkpoint
and require another current-source integration run. No incomplete run is counted
as a successful network/USB gate.

Next unfinished step: production pre-parse ownership and protected HTTP/WS
startup recovery wiring, automatic normal-startup retry after validated readback,
and the minimal existing-Setup recovery actions with Impeccable desktop/mobile
light/dark review. The local Pi production service, bound Stop, remote pairing
dependency, and complete phase-wide evidence remain open. Phase 2 stays ⏳;
phase 3 stays ▧. Nothing was staged, committed, pushed, installed, or operated
on radio hardware.

Documentation Impact: engineering progress and the phase-list continuation link
were updated. Existing Mac/controller operator guidance was reviewed and built
earlier. No recovery-page operator promise is added while startup, routing, and
UI remain unfinished; the implemented workflow must be documented and rendered
concomitantly when those production paths are available.

## P2-10 approved: close the component's file-open race

Status: approved on 2026-11-16. P2-09 remains approved; no protected recovery
workflow decision is reopened. Implementation and review are in progress.

The new parent input guard rejects a currently unsafe fixed INI before the
component opens it. However, `IniFile::set_filename()` immediately calls
`load()`, and `load()` opens a fresh `std::ifstream`. Stock preview also reopens
its inputs. Replacement between the parent's checked observation and that open
could follow a symlink, read an unbounded replacement, or block on a FIFO. A
check-before-open parent guard cannot close that race; a later revision refusal
cannot undo a blocked read. Production recovery wiring must not rely on it as
the complete safe-read boundary.

Approved narrow component change:

- Add an optional reusable file-reader callback to INI-Handler, returning file
  bytes for an internally selected filename. Its default preserves ordinary
  standalone file loading. The component gains no application, protocol, device,
  POSIX, certificate, or management-policy dependency.
- Use that reader for live loads and the approved stock-preview reads, then
  feed the existing parser/version substitution/layout logic unchanged. Do not
  add parser syntax or normalize saved values.
- The Pi startup owner installs the application's bounded no-follow reader:
  open with no-follow/nonblocking flags, validate the opened descriptor and its
  fixed-path binding, require an owned regular input, enforce current size and
  mode limits, and refuse changed observations. Restore the default component
  reader only when its owner is safely torn down.
- Add standalone reader/error/layout/pending-edit checks, parent unsafe-input
  and replacement tests, and component README/API documentation. Re-run the
  component standalone suite, parent startup/stock/recovery suites, and the
  required current-source paired validation.

P2-05 and P2-07 approved serialization and stock preview; this reader API is
outside those narrow component approvals. `AGENTS.md` requires: “Obtain approval
before broadening a parent-only task into component changes.” That approval has
now been granted. `set_file_reader()` is implemented without platform/application
dependencies; the startup owner installs the bounded no-follow policy for its
fixed INI, stock, and private preparation inputs. This does not authorize
commits, pushes, hardware access, or phase 3.

## P2-11 approved: preserve first-initialization rollback proof

Status: approved on 2026-11-16. This is separate from P2-10's component read
boundary. Implementation and review are in progress.

A hardware-free production-class probe reproduced this sequence:

1. A first initialization has a durable **prepared**, uncommitted WAL; its old
   settings and operations are absent, and its old catalog has no Pi history.
2. Recovery restores all original input and correctly leaves history
   uninitialized. The current journal then retires the prepared proof.
3. Another process restart before a first valid commit sees a surviving private
   directory without that proof and reports `pi_management_storage_hold`.

The second hold is safe, but it incorrectly treats a proven never-initialized
rollback as ambiguous lost history. Simply allowing every empty directory, or
deleting it after retiring the proof, would weaken initialized-history protection
or retain a crash window between those two operations.

Approved decision: retain verifiable proof of a never-completed first
initialization until a validated first complete transaction atomically replaces
it. Recovery may retry that first initialization automatically while the proof,
original file observations, and absent history remain consistent. A change or
loss of that proof, or any evidence of previously completed initialization,
continues to cause read-only storage recovery. Reset never erases history.

Implementation would keep the current independent private WAL format and its
prepared/committed distinction, coordinate replacement under the same owner,
and exercise repeated cold rollback, interruption while replacing the proof,
source replacement, missing/corrupt proof, and initialized-history deletion.
No WTP, pairing, certificate, RF, or process-role change is proposed.

Evidence: `/private/tmp/issue473-initial-prepared-probe.cpp` links the current
production catalog/store/journal objects and runs entirely in a private temporary
directory. `/private/tmp/issue473-initial-prepared-probe` reproduces successful
first rollback followed by the incorrect second cold-recovery hold. The
reproduction changes no repository files or installed state.

`AGENTS.md` requires direction when an ambiguity materially changes persistence
or recovery behavior. This proposes the explicit distinction between proven
first rollback and missing previously initialized history; it does not waive
existing corruption safeguards. That decision is now approved. The journal
retains a verified initial prepared WAL and atomically replaces its exact proof
before releasing obsolete staging. Repeated cold recovery remains uninitialized
until a complete first commit. Changed/missing/corrupt proof, source observations,
or initialized history remain held; Reset cannot erase them.

## Recovery validation checkpoint and remaining review findings

The second full isolated run completed successfully: all three network targets,
production controller/two-Pico HTTP paths, actual Pico USB driver, Pi USB
client/management, independent USB context, full UI USB pair, and final guarded
persistence regression. Evidence and exact commands:

- `/private/tmp/issue473-linux-pair/run_recovery_service_reviewed.py`.
- `/private/tmp/issue473-linux-pair/runner-recovery-service-reviewed.log` ends
  with `ALL REQUESTED CANDIDATE PAIR STEPS PASS`.
- Per-step logs are `recovery-service-reviewed-*.log` in that directory.
- Exact source hashes:
  `/private/tmp/issue473-linux-pair/recovery-service-reviewed-source-manifest.json`.
- Pi base: `c48a00dbbd6d9d65014c56f286ace074df9240e9` plus manifest-recorded
  uncommitted sources. Pico candidate base:
  `05cc90d67442c536f1cf7bee74a94914ad032c23` plus manifest-recorded uncommitted
  sources at `/private/tmp/issue473-pico-candidate`.
- Mbed TLS: `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`, explicitly selected.

Later review required recovery/local management service identities to match
their startup/store owners. Those checks and tests pass with the current native
source:

```sh
make -C src -j4 pi-managed-startup-test pi-management-service-test \
  pi-managed-store-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
```

Log: `/private/tmp/issue473-recovery-identity-binding-test.log`. Changes after
the paired snapshot are the startup/store/service identity getters and checks,
their two test files, and this engineering document. The Pico source still
exactly matches its paired manifest. Focused tests do not turn the earlier pair
into final current-source phase evidence; production wiring and approved fixes
will require another complete run before phase completion.

Additional independent local validation:

- `npm run test:unit` in `WsprryPi-UI`: PASS;
  `/private/tmp/issue473-protected-recovery-ui-unit.log`.
- `make -j4 semantics-test-portable SUDO=` in the fresh native source copy
  `/private/tmp/issue473-native-recovery-portable-awhqqbdm/src`: PASS;
  `/private/tmp/issue473-protected-recovery-portable-isolated.log`.
  `/private/tmp/issue473-native-portable-copy.json` records copied source hashes.
  The first ordinary-checkout run stopped on an existing ARM Linux
  `Chipset-Offsets/build/chipset_offsets_test`; that artifact was preserved.
  This is the explicit portable subset, not full physical-backend semantics.
- `make -C src -j4 release BACKENDS=si5351 ANCILLARY_GPIO=0
  SI5351_TRANSPORT=mcp2221a-hid SUDO=
  OBJ_DIR_RELEASE=/private/tmp/issue473-macos-profile/obj
  BIN_DIR=/private/tmp/issue473-macos-profile/bin`: PASS;
  `/private/tmp/issue473-macos-recovery-foundation-build.log`.
- `python3 -B src/tests/controller_only_test.py --fixture
  /private/tmp/issue473-macos-profile/bin/wsprrypi --production-main`: PASS;
  `/private/tmp/issue473-macos-release-controller-test.log`. Actual Mac identity,
  production main, temporary state, protected loopback HTTP/WebSocket, corruption
  refusal, and no transmitter INI writes are exercised.
- Native Si5351 library/symbol/device-object audit: PASS;
  `/private/tmp/issue473-macos-release-explicit-object-audit.log`. The standard
  audit initially stopped because it assumes default object layout. The same
  assertions were rerun against the explicit out-of-tree object directory;
  the repository audit script was unchanged.
- `cmake --build /private/tmp/issue473-pico-firmware -j4`: PASS;
  `/private/tmp/issue473-pico-firmware-protected-recovery.log`, using candidate
  sources and SDK `079c6f39023649b154152db30f1d781e884879bc`.
- Pi `python3 scripts/generate_control_contract.py --source
  /private/tmp/issue473-pico-candidate --check`, Pico
  `python3 scripts/generate_control_contract.py --check`, 149 native/browser
  contract vectors, and expanded lifecycle/host-route negatives: PASS.

Review corrections include draft-cache JSON initialization, original invalid
field spelling, first-publication cache retirement, rejected-candidate profile
import poisoning, unavailable-observation errors, parser input guards, and
owner/service identity mismatch. At that checkpoint, P2-10's file-open race
and P2-11's repeated initial rollback were unresolved; both were subsequently
approved and implemented in the checkpoint below. Production startup/HTTP/UI,
automatic retry, local Pi bound Stop, remote pairing, and complete phase-wide
evidence remain unfinished. This is not a no-issues phase acceptance.

No new recovery UI was added or rendered; its Impeccable desktop/mobile,
light/dark, and workflow checks remain required. Previously recorded Pico editor
and Mac/controller documentation renders remain separate checkpoints. No new CI
was triggered. No installation, physical USB, services, GPIO, transmitter
hardware, timing, or RF was qualified.

Documentation Impact: engineering progress and phase indication were updated.
Operator recovery guidance still requires the completed production workflow
before an accurate update/render. Existing Docs changes remain on its feature
branch at base `a06c1d5590614a4794684aac794c468bec36b7cf`. Pi and the private
Pico candidate remain dirty feature branches, the ordinary Pico checkout is
clean, and nothing was staged, committed, pushed, or merged.


## Approved reader/proof implementation and recovery gateway checkpoint

P2-10 and P2-11 are approved, implemented, and covered by the component and
parent checks below. Their approval is not pending. Phase 2 stays ⏳; phase 3
stays ▧. This checkpoint does not declare the phase-wide review complete.

- INI-Handler has an optional reusable file-reader callback for live and stock
  input. The parent installs bounded, owned, regular, no-follow reads before
  recovery/parsing, restricts filenames to the fixed live/stock/private preview
  inputs, and refuses any held/unverifiable input. Component parser syntax,
  ordinary-reader behavior, and save semantics remain intact.
- An uncommitted first-initialization prepared decision retains all verifiable
  rollback images. Repeated cold recovery validates that exact proof and input
  observations. The first validated full transaction atomically replaces the
  proof; corrupt/missing/inconsistent history never becomes an empty reset.
  Live commit also rejects an equal-byte external replacement after prepare.
- The process registry obtains the actual Linux or Mac identity and an independent
  process boot ID. Constructor failure keeps redacted, read-only storage recovery
  and refusing persistence/input owners. It does not select another role/backend.
- The private network-safety writer and rollback now use the same complete managed
  INI/settings/history transaction. A narrow optional INI-I/O adapter preserves
  the admin unit's independent test boundary. The parent proves that only the
  permitted private setting changed, refreshes raw passthrough afterward, and
  refuses smuggled public/Experimental edits. Ordinary configuration edits still
  require native admission. No real Apache/service command is run by these tests.
- Protected readiness/recovery HTTP handlers use the existing peer, Host, Origin,
  bounded-request, rate, and server-issued CSRF session boundary. While a selected
  host is unready, legacy actions/probes/status are refused; legacy GET /config
  returns a redacted unconfirmed recovery document with 409. Its proxy-client
  admission is explicitly elevated to the protected recovery classification.
  WebSocket commands return an authority-pending error before legacy dispatch.
- The recovery-listener coordinator owns only its HTTP/WebSocket listeners and
  metadata monitor. It forces enforced network protection for recovery without
  persisting that temporary override, waits for a saved-input notification, and
  automatically invokes owning-parser readback once per changed revision.
  It retains the original listeners across successful readback, including when
  saved ports change, and restores normal policy from the actual saved INI.
  Its cleanup does not run output/GPIO teardown from unvalidated defaults.
- Current saved listener ports can be retained from a rejected parser draft
  before later CLI overrides. Only independently valid, distinct listener values
  are accepted; no other default or rejected transmitter field is published.

Adversarial corrections in this checkpoint: require parsed/validated source for
native-output holds; serialize host storage-state reads under the configuration
lock; prevent post-startup receipt replay from leaving a redundant retry signal;
protect the legacy recovery view against an ineligible proxied client; and keep
missing private policy values at the existing enforced parser default.

Validation:

- Standalone INI-Handler: `make -C src/INI-Handler/src test SUDO= CXX=c++
  CXXFLAGS='-std=c++20 -Wall -Wextra -Werror -MMD -MP' LDFLAGS=-lpthread
  OBJ_DIR_DEBUG=/private/tmp/issue473-protected-reader-component-obj`: PASS,
  `/private/tmp/issue473-protected-reader-component-test.log`.
- Focused startup/store/journal/service reader/proof checks: PASS,
  `/private/tmp/issue473-reader-proof-final-review-test.log`.
- Actual production OS identity, private managed-network success/rollback,
  protected loopback HTTP/CSRF/proxy/Origin refusal, durable receipt replay, and
  saved readback: PASS, `/private/tmp/issue473-recovery-gateway-reviewed-test.log`.
- Recovery listener lifecycle and automatic notification/readback: PASS,
  `/private/tmp/issue473-recovery-listener-runtime-test.log`; run via
  `make -C src -j4 pi-managed-host-runtime-test pi-managed-host-test
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
  BIN_DIR=/private/tmp/issue473-controller-native/bin`.
- Standalone privileged-network admin fake-command suite: PASS,
  `/private/tmp/issue473-private-admin-standalone-run.log`. The initial invocation
  of its legacy Make run recipe failed because that recipe prepends ./ to an
  absolute BIN_DIR. The same built executable was run by its absolute path;
  no unrelated Make recipe was changed.
- Both complete candidate network/USB recipes, component suite, and Linux default
  physical-plus-simulated profile tests with hardware access disabled: PASS,
  `/private/tmp/issue473-linux-pair/runner-recovery-reader-proof.log`. This earlier
  snapshot excludes the later registry/gateway/private-writer/listener work.
  Pi base `c48a00dbbd6d9d65014c56f286ace074df9240e9`, Pico candidate base
  `05cc90d67442c536f1cf7bee74a94914ad032c23`, with their exact dirty-source hashes
  in `recovery-reader-proof-source-manifest.json`; Mbed TLS
  `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.

The gateway/listener coordinator is now selected before production INI parsing
and awaited before native startup. The isolated production-main fixture passes
configuration recovery, explicit protected save/replay, automatic native
readiness, and clean shutdown with explicit simulation and private state.
Next unfinished work is the Setup recovery UI and actual desktop/mobile
light/dark review. Local bound Stop,
external-WTP observation integration, remote automatic pairing under P2-01's
provider hold, current-source paired reruns, and the full phase review remain.
No new recovery UI was added/rendered in this checkpoint.

Documentation Impact: updated this engineering record and retained the accurate
phase indicator. Operator recovery instructions/rendering still require the
completed production/UI workflow; existing sibling Docs edits are preserved.
All work remains on the existing feature branches with dirty trees. The ordinary
Pico checkout stays untouched. No staging, commit, push, merge, installation,
service operation, GPIO exercise, physical USB test, transmission, or RF occurred.


### Production startup and local Pi gateway continuation

Production main now prepares/recovers the managed owner before parsing a
transmitter INI and retains it until workers stop. The normal loop awaits the
protected recovery coordinator before entering native startup; it preserves
listeners started for recovery and activates readiness only after validated
source, startup quiescence, native ownership/revocation loading, and a native
configuration reservation. Native exceptions unwind workers before owner
teardown. CLI-only and explicit controller-only paths keep their own boundaries.

The local Pi is included in the closed target list. Its context/settings/status
and settings transaction adapter are routed through the same protected browser
session; Pico-only context headers cannot masquerade as a Pi binding. The HTTP
controller reuses the managed owner's catalog instance and mutex. Duplicate
local/Pico identities refuse the list instead of selecting one implicitly.

Review of the actual HTTP library showed that ordinary handlers run after body
buffering. Shared Control/1 mutations now use its existing content-reader API
with a 32-KiB accumulation bound for both declared and chunked input. Management
header/declared-body/read-body checks also run before document access; managed
listeners have bounded read/write waits. This leaves unrelated upload routes at
their existing payload policies. An existing library-level 413 is retained when
the reader rejects a request before invoking the receiver.

The executable fixture exposed a draft projection error: the owning INI parser
returns a valid hexadecimal Si5351 address as an integer, while the current
control view represents it as the existing canonical string. Recovery now uses
the owning address serializer for that fixed valid range; out-of-policy values
remain explicit unavailable fields. No value policy or shared schema changed.

The recovery browser client foundation is implemented but is not yet loaded by
the page or connected to visible controls. It preserves typed drafts, excludes
private connection-file values from submissions, validates current closed
responses/bindings, bounds streamed replies, rejects duplicate decoded keys,
uses server-issued CSRF protection, and retains an immutable accepted request
for identical-operation retry after uncertainty. A newer draft survives a late
receipt. A later refusal cannot erase an earlier uncertain request. Its focused
Node checks pass; the full UI unit suite is being rerun with loopback permission.
This does not claim a rendered or finished recovery page.

Current validation evidence:

- Production main/loop recovery and automatic readiness: PASS,
  `/private/tmp/issue473-linux-pair/production-recovery-reviewed-production-main.log`.
  The launcher injects only private ledger and loopback HTTP seams; the actual
  production parser/startup/handlers/transaction/readiness/shutdown execute.
  It requires explicit `--backend simulated`. It exclusively claims an absent
  simulator trace and refuses to overwrite an existing artifact. No RF event
  or backend initialization from rejected defaults is observed.
- Native protected declared/chunked body and aggregate-header limits, CSRF,
  proxy/Origin refusal, replay, and automatic saved readback: PASS,
  `/private/tmp/issue473-recovery-http-limits-test.log`.
- Address projection and startup recovery regressions: PASS,
  `/private/tmp/issue473-recovery-address-projection-test.log`.
- Both complete paired recipes are rerunning under
  `/private/tmp/issue473-linux-pair/runner-production-recovery-reviewed.log` with
  exact frozen hashes in `production-recovery-reviewed-source-manifest.json`.
  A running check is not accepted final evidence. The subsequently added browser
  client foundation is outside this snapshot and still needs production wiring.

Remaining: visible Setup recovery workflow and Impeccable review; production
Mac transmitter recovery evidence; bound Pi Stop; typed observation of an
external WTP destination; pairing under the existing provider/maintenance/
packaging hold; current-source paired acceptance; operator guidance; and a full
phase-wide adversarial assessment. No phase-2 completion or no-further-issues
claim is made. Phase 3, Git publication, and hardware remain unauthorized.


## Protected recovery UI review and policy-transition findings

The existing-Setup recovery editor and protected browser client are now wired.
The actual Mac production-main fixture, production PHP/templates/scripts,
protected HTTP session, owning parser, managed store, and native simulated
startup/shutdown pass the browser workflow in
`/private/tmp/issue473-recovery-browser-test.log`. No source configuration changes
on page load. A native semantic rejection preserves both original INI and the
operator's draft; a valid explicit save starts normal recovery automatically.
The corrupt-initialized-history scene keeps Save/Repair/Reset disabled and
never offers defaults as saved values. Private connection-file fields and TLS
identity controls are omitted from the editor.

Current visual evidence: `/private/tmp/issue473-recovery-ui-captures/` has editable
and read-only captures at desktop 1440×1050 and mobile 390×844, in light and dark.
Impeccable review retained the incumbent bench-console typography/colors, moved
explicit actions into the first viewport, constrained desktop field width,
removed irrelevant firmware/power checking messages from this separate recovery
flow, and clarified read-only state without duplicated bottom actions. The
remaining four complementary renders still need final inspection after the last
refinement. No shared Device selector or normal Setup tab redesign was added.

Adversarial findings corrected:

- Existing jQuery cache-busting prevented the legacy /config recovery bridge
  from reaching the new UI. Only its exact bounded numeric `_` query is retained;
  the new Control/1 routes remain query-free. An authoritative Control response
  no longer falls through to a direct-port peer or an older settings view.
- The Pico firmware guard initially owned hidden recovery controls. It now
  exempts only the separate active native recovery panel, retires its stale
  disabled-state entries, and keeps its real unknown/maintenance hold on every
  ordinary Pico/control action. No firmware-ready observation is fabricated.
- Background readiness observations could overwrite action feedback. Bounded
  busy/rate refusals now retry observations without erasing the latest action.
- Chrome actually retried a PUT internally after a dropped accepted reply and
  surfaced a later 409/busy response. Recovery now retains that identical
  operation instead of treating any first visible 4xx as proof of refusal.
  The Pico client/editor applies the same conservative accepted-write tracking.
  An earlier unresolved-operation hint retains its approved pre-dispatch refusal
  semantics; current-operation ambiguity requires read/review.
- The browser test drops accepted replies, proves native startup can become
  ready without discarding that uncertain operation, checks an identical retry,
  confirms no new managed generation, and retains edits typed after submission.
  Returning to normal Setup after those later edits requires an explicit discard.
- Native startup restores the ordinary private network policy only after native
  readiness, not merely saved-parser readback. Private raw INI lines can survive
  ordinary saves without remaining in the component's data map; restoration now
  uses bounded, verified saved bytes. Every unready host remains protected even
  if its saved ordinary preference is Off. A late transition to a recovery view
  rechecks enforced proxy/client admission.
- Production controller contexts retire after HTTP workers stop and before the
  main-owned catalog is destroyed. The native fixture rejects every second or
  non-simulated backend override before production main; its trace and ledger
  stay inside its private fixture directory.

Mac actual production-main recovery: PASS,
`/private/tmp/issue473-macos-production-recovery-test.log` and subsequent lifecycle/
policy focused logs. Pico shared browser field-rule access is generated from the
unchanged current schema; standalone operator/browser tests and rebuilt assets
pass, and the current ARM firmware build passes
(`/private/tmp/issue473-pico-recovery-field-firmware.log`). No firmware was installed.

The intermediate paired run passed network runtime, network interop, and config
interop, then correctly refused stale Pico operator assets at USB configuration.
Those assets were rebuilt. That run is not accepted paired completion: Pico
browser assets changed after its initial manifest, and USB did not run. The next
runner must freeze both complete source trees and rerun both full recipes.

## P2-12 approved: protect recovery independently of the saved Off preference

Status: approved on 2026-11-16; exact-route implementation is under validation.
P2-10/P2-11 remain approved. Ordinary network-safety Off is preserved on disk and
restored after verified native startup; this proposal does not silently change
that preference or weaken its existing management restrictions.

Reproduction: actual production main starts from a private invalid transmitter
INI with `Privileged Network Safety = insecure-disabled`, enters enforced
protected recovery, and accepts a validated save. It then reaches normal native
readiness and restores the saved Off preference. Under the current all-management
boundary, `GET host/readiness` and an exact accepted recovery replay both return
403 afterward, although ordinary local GET /config returns 200 and the durable
receipt remains committed. This prevents an uncertain browser reply from being
resolved automatically without changing the user's safety preference. The
hardware-free characterization passes as an expected current limitation:
`/private/tmp/issue473-network-safety-off-recovery-characterization.log`, run with
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/pi_managed_main_test.py
--fixture /private/tmp/issue473-controller-native/bin/pi_managed_main_fixture
--network-safety-off` from src.

Proposed exact boundary:

1. For a selected native managed Pi only, always apply enforced LAN/loopback,
   Host, Origin, and trusted-proxy-client admission to the existing
   `/api/control/v1/host/readiness`, `/host/configuration-recovery`, and
   `/host/management-session` routes, independently of the saved ordinary
   network-safety preference. Keep request/time/header/body limits and the exact
   closed Control/1 documents unchanged.
2. Permit those routes through the shared handler after this independent
   protection. Keep server-issued CSRF/session checks mandatory for mutations.
   A ready host can only replay a matching existing recovery operation; the
   owning service still refuses any new recovery mutation outside its approved
   writable configuration-recovery state.
3. Leave every other new management API unavailable while ordinary network
   safety is Off. Do not alter WTP, pairing, authentication, storage, native
   payloads, role selection, or the saved ordinary preference.
4. Validate Off-mode accepted-reply loss/replay through actual production main
   and browser; reject ineligible proxied clients and foreign Origin even while
   ordinary safety is Off; prove missing CSRF cannot mutate, and other management
   stays 403. Repeat enforced-mode and both full frozen-source paired recipes.

This enforces stronger protection for the exceptional recovery surface instead
of requiring an operator to change an unrelated saved preference just to confirm
an already accepted save. Retaining the current boundary would leave this Off-
mode recovery workflow incomplete and require a deliberate operator action.

`AGENTS.md:34` requires direction where ambiguity materially changes behavior,
compatibility, persistence, hardware operation, or user workflow. The prior route
approval did not specify how this exceptional recovery observation/replay
interacts with a restored Off preference. That security/workflow distinction
required approval before adding the exception. The user approved independently
protected recovery. Continue Pi adapter, Stop, documentation, and validation
work; no approval remains pending for this boundary.

## Approved recovery boundary and native Stop continuation (2026-11-16)

P2-12 is implemented for the exact three native-host routes. Their peer/proxy,
Host, and Origin admission always uses enforced policy, including after the
saved Off preference resumes. Shared routing permits only those exact routes
through its ordinary Off restriction. CSRF, rate, header/body/time limits,
current document closure, and the owner's write-state checks remain mandatory.
A ready owner may replay an exact accepted recovery operation but cannot accept
a new recovery mutation. Other target management stays unavailable under Off.
The saved preference, WTP, pairing, and storage documents are unchanged.

Current focused software evidence:

- `make -C src -j4 pi-managed-main-test pi-managed-host-runtime-test
  pi-private-network-persistence-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
  BIN_DIR=/private/tmp/issue473-controller-native/bin`: PASS in
  `/private/tmp/issue473-p212-native-review-test.log` before native bound Stop.
- Production main with `--network-safety-off`: PASS in
  `/private/tmp/issue473-p212-safety-off-main-test.log`. It checks fresh session,
  exact receipt replay, refused foreign Origin/proxy clients and missing CSRF,
  no new recovery write after readiness, other management 403, unchanged saved
  history on refusals/replay, and existing Stop persistence.
- Actual browser test with `WSPRRYPI_RECOVERY_SAFETY_OFF=1`, explicit private-state
  production-main binary, and simulated backend: PASS in
  `/private/tmp/issue473-p212-recovery-browser-test.log`. Accepted dropped replies
  remain unconfirmed until exact retry; native readiness does not erase them or
  later edits. Ordinary target management remains 403 under saved Off.
- Full current UI `npm run test:unit`: PASS in
  `/private/tmp/issue473-recovery-ui-current-unit.log`; rendered Pico editor
  lifecycle regression: PASS in
  `/private/tmp/issue473-pico-editor-current-browser.log`.
- All eight recovery UI desktop/mobile light/dark captures were reviewed.
  Recovery operator guidance was added to the sibling Transmitter guide.
  Sphinx PASS with no warnings in `/private/tmp/issue473-p212-docs-build.log`;
  desktop/mobile rendered checks PASS in
  `/private/tmp/issue473-p212-docs-render.log`.

Adversarial corrections made during this continuation:

1. Ordinary GET /config can outlive its earlier request-admission snapshot.
   Its final handler now checks the selected owner's readiness under the
   configuration lock and returns a no-store hold without disclosing a protected
   draft or stale ordinary settings. A direct-handler regression covers this
   boundary independently of the earlier guard.
2. The existing operator Stop used a direct component INI write, bypassing the
   installed managed store. Its parent bridge now validates the actual saved
   input, changes only its Transmit permission, and joins the complete transaction.
   It does not save transient runtime edits or change scheduling.enabled. A held
   owner never falls back to the component writer. Private-setting/Stop tests
   prove refusal-before-write, preserved private preferences, and cold recovery.
3. Native bound Stop now checks exact binding, capability, If-Match/revision,
   and observed generation/owner/job, then reserves the captured native runtime.
   Competing output, reconfiguration, enable, and recovery paths cannot replace
   that reservation. The complete Transmit-off after-image and accepted receipt
   commit before the existing cancellation path runs. This receipt acknowledges
   accepted cancellation, not inactive output. It never clears scheduling.enabled.
   An exact duplicate returns the stored receipt without cancelling later work.
4. If cancellation dispatch throws after acceptance, its native admission fence
   remains held until process recovery. It cannot release permission for another
   occurrence merely because an accepted reply exists. Startup consumes the saved
   Transmit-off policy; it never redispatches that old Stop against replacement work.
5. Stopping a remote owner changes writable capability advertisements. Historical
   exact receipt lookup now precedes the current capability comparison, after
   request/binding/If-Match validation; only a new mutation needs current capabilities.
   A new remote-owner replay regression exposed an internal cancellation conflict:
   the competing-enable fence also blocked Stop's own revocation helper. Moving
   the refusal to the two external enable entry points preserves internal bound
   cancellation while refusing competing enable/persistence.

Native Stop focused checks: production service tests cover racing replacement
output, output ABA, authentication/CSRF, durable-before-cancel ordering, exact
replay against later output, ID reuse, failed-dispatch admission hold, remote
ownership and capability changes, cold receipts, and bounded-history eviction.
Actual production main covers advertised native Stop and exact protected HTTP
replay, plus existing Stop, both enforced/Off recovery modes, saved scheduling,
and no simulated RF-on events. Current PASS logs:
`/private/tmp/issue473-bound-stop-service-test.log`,
`/private/tmp/issue473-bound-stop-native-main-test.log`; the final remote-owner
correction rerun is recorded separately when finished.

The standalone Mac authority target initially encountered an inherited Linux
archive in the component's ordinary build directory. Existing artifacts were
preserved. `make -C src/WTP-Server test
BUILD_DIR=/private/tmp/issue473-wtp-server-native` and the parent authority test
linked against that isolated native archive both PASS. This required no
WTP-Server source change. The full Linux target remains required on the final
candidate pair.

The frozen-recovery-ui pair passed all three network suites and the complete
actual USB driver/client/management/UI pair, including independent USB management
and GPIO-drive coverage. Its Linux default-profile check is still running.
That immutable snapshot predates P2-12 and the native Stop continuation; it is
checkpoint evidence, not the final candidate gate. A new complete frozen-source
pair run is required after the current changes settle.

Phase 2 remains in progress. Typed external-WTP management/observation and its
admission/reservation integration, the automatic Pi pairing provider hold,
current-source paired reruns, and the complete phase-wide review loop remain
open. Phase 3 has not started. No staging, commits, pushes, installation,
service operations, GPIO, physical USB, or RF qualification occurred.

## Outgoing WTP management and readiness review (2026-11-16)

The complete immutable P2-12/native-Stop checkpoint passes in
`/private/tmp/issue473-linux-pair/runner-p212-native-stop.log`, including all three
network suites, actual Pico USB drivers, Pi management/client and both USB pair
consumers, GPIO-drive coverage, guarded persistence, and Linux default-profile
checks. The source manifest identifies both copied source trees and the pinned
Mbed TLS revision. No live source edits changed those frozen files.

Subsequent implementation, still requiring its own paired rerun:

- Typed outgoing-WTP observations use the actual selected application's verified
  identity, current output/job snapshot, and a bounded monotonic generation.
  An unverified destination is null; management reads never negotiate, reconnect,
  claim, load, arm, or save. Native output reports its verified self identity.
- Pi management combines the captured native authority with the outgoing
  application's reservation. Exact Stop closes new outgoing dispatch under its
  existing dispatch-permit mutex; settings reservations fence new host work but
  preserve a previously accepted finite job. Other-thread output replacement,
  firmware mutation, and competing management cannot replace that owner.
- Existing WTP ARM admission now checks the installed durable-state readiness
  guard and unknown-output/revocation state. No wire message or carrier changes.
  Counter exhaustion closes new dispatch rather than wrapping an observation.
- Host/storage readiness is separate from an offline WTP peer's output readiness.
  A verified WTP-role host can serve protected configuration and retain its
  existing explicit Reconcile action without claiming that its peer is ready.
  Physical startup latches, unknown-output evidence, invalid configuration, and
  corrupt/missing initialized history retain their holds.
- Successful existing explicit WTP reconciliation can clear the corresponding
  native outgoing-output hold only after rechecking that exact verified idle
  observation. A later job/selection, absent identity, unknown output, or physical
  backend refuses that update. No manual process restart is substituted.
- The readiness owner uses atomic lifecycle/validation flags and the store's
  own transaction lock. Ordinary configuration-mutex contention is no longer
  mistaken for invalid history during a previously accepted job's ARM. The
  complete saved-state verification still rejects corrupt/external history;
  teardown/startup flags remain closed independently.

Focused PASS evidence:

- `/private/tmp/issue473-wtp-active-edit-review-test.log`: typed outgoing
  observations/reservations, an already accepted in-memory WTP job completing
  while a settings reservation is held, saved-state mutex-contention regression,
  configuration/stock/external-file refusal gates, both simulated-main recovery
  policy modes, and actual offline-WTP-role main.
- `/private/tmp/issue473-wtp-explicit-reconcile-review-test.log`: the above with
  exact-observation native reconciliation, unknown-output admission, native bound
  Stop/replay/history coverage, and protected Pi management while the WTP peer
  remains offline.
- `/private/tmp/issue473-wtp-production-current-test.log`: 882 existing
  production WTP software checks passed at the pre-reconciliation checkpoint.
  Its injected transport/cleanup failures are expected assertions, not hardware
  observations. A final rerun against the settled source is required.

The new actual-main WTP fixture accepts only an explicit WTP backend and installs
an always-offline typed software transport before application threads. It cannot
open a USB/device/network connection and fails if any transport write occurs.
It uses private identity history and loopback HTTP only, with station/output
controls disabled. The simulated-main fixture continues to require explicit
simulation and refuses physical/external overrides. Both actual-main targets
retain the production singleton guard; their Make dependencies serialize them
under parallel make instead of bypassing that boundary.

`wtp-managed-output-test` uses the existing scripted WTP peer in memory. Its
software ARM/execution assertion verifies that editing does not cancel an
already accepted finite job; it is not RF, electrical, or physical USB evidence.
`pi-managed-wtp-main-test` proves offline peer management, unknown destination,
closed host/peer readiness distinction, real configuration save/replay, and the
retained explicit reconciliation path with zero transport writes. Both targets
are included in the Mac simulated and Linux default-profile CI recipes.

Documentation Impact: the recovery operator guide is updated and its current
Sphinx build and desktop/mobile render pass. Existing Pico status/recovery
instructions were inspected and remain accurate: polling is read-only and
Reconcile remains an explicit action. This adds no phase-3 shared Device selector
or new schedule controls. Final engineering/security review and paired evidence
remain required before closing this independent implementation slice.

The approved pairing dependency hold remains. A narrow additional primary-source
check found that [RustCrypto's SPAKE2 README](https://github.com/RustCrypto/PAKEs/blob/master/spake2/README.md)
documents only its Ed25519 group and disclaims an independent audit. It therefore
is not demonstrated as a drop-in provider for the approved P-256/RFC 9382
contract. No provider was adopted or locally reimplemented, and no conclusion
that all possible conforming providers are unavailable is claimed. BoringSSL
maintenance/packaging gates and its distinct wire amendment remain unapproved.

### Final outgoing-adapter assessment corrections

The next frozen run (`runner-wtp-output-management.log`) passed the production
main, typed management, storage, controller, and network-runtime steps, then
failed the standalone `consumer_interop` build: its client-only compilation
inherited a native-server header through the outgoing observation DTO. This is
an unresolved required run, not paired completion. The correction separates the
plain `ManagedOutputObservation` from native authority and routes native
readiness/reconciliation through the existing production `wtp_pi_control` hook
boundary. The paired adapter now compiles that production hook source; it does
not substitute a mock admission result or relax protocol validation.

Further review corrections, covered by the focused software fixtures:

- A retained cancellation fence reports held application, no effective transmit
  permission, and no writable new capabilities. The scheduler's original local
  permission field is preserved so reserving Stop cannot itself cancel current
  work before durable acceptance.
- A known foreign outgoing WTP owner makes the new Pi Stop capability unavailable.
  Neither a management read nor a stale Stop reservation borrows that owner's
  lease or cancels its job. The existing WTP ownership rules remain unchanged.
- An explicit reconciliation result distinguishes an absent native host from a
  confirmed matching host update and an unresolved host permission. Peer success
  cannot silently become a host-success report when its exact update was refused.
- Outgoing generations compare verified identity, ownership, retained unique
  accepted job, and meaningful output state. Harmless status timestamps/revisions
  no longer invalidate Stop continuously. The retained job ID detects a complete
  host-job ABA between management observations.
- Output selection availability reports the active management reservation before
  another frontend can start a profile/output switch.

Focused native logs:
`/private/tmp/issue473-wtp-foreign-owner-review-test.log`,
`/private/tmp/issue473-wtp-adapter-boundary-review-test.log`, and
`/private/tmp/issue473-wtp-stable-observation-review-test.log` pass their respective
current checkpoints. These fixes require a new complete frozen pair before their
integration gate can be closed. No additional decision or provider was adopted.

### Reviewed lifecycle and paired-adapter follow-up

`runner-wtp-management-reviewed.log` passed network runtime and network interop,
then failed the other standalone configuration-client link recipe because it
also omitted `wtp_pi_control.cpp`. Both explicit client compile recipes have now
been inspected and corrected: CMake's consumer adapter and Make's configuration
client compile the actual production control-hook source. The USB pair and parent
application use their ordinary complete source/object sets. No failure is waived;
a fresh complete pair is required for the corrected candidate.

The whole-job ABA fixture now runs a complete in-memory job without any management
observation between its start and finish. Owner/job return to the same idle tuple,
but the retained unique job changes the generation and refuses the old idle Stop.
This supplementary test passes in
`/private/tmp/issue473-wtp-whole-job-aba-test.log`.

Teardown review found that clearing native hooks could precede destruction of a
still-ready managed owner. The installed owner now receives the endpoint-stopping
notification and atomically closes its phase before those hooks are cleared.
Existing direct CLI/standalone behavior without an owner is preserved. The owner
refuses subsequent configuration publication/persistence, and all three local
admission paths stay closed. Focused production/startup/Stop/WTP checks pass in
`/private/tmp/issue473-wtp-teardown-final-review-test.log`. The new lifecycle source
is outside the prior snapshots and must be included in the fresh paired run.

## P2-13 approved: protected INI publication callback

Status: approved by the user. Implementation and focused validation are in
progress; final paired acceptance has not yet run with this change.

The adversarial write-boundary review reproduced a distinct component defect.
`IniFile::save()` opens the predictable `<ini>.tmp` with truncating `ofstream`,
then renames it. A pre-existing symlink at that temporary name causes an unrelated
owned file to be overwritten and moves the symlink onto the live INI. The parent
journal subsequently refuses the unsafe final path, but cannot undo that separate
file overwrite. Protected input reads do not protect this publication path.

The reproduction compiled the actual current component alone and used only an
owned, mode-0700 temporary directory with two artificial files; both files were
removed afterward. No repository/configuration/user data, device, or service was
changed. Source and result:
`/private/tmp/issue473-ini-temp-write-repro.cpp` and
`/private/tmp/issue473-ini-temp-write-repro.log` (both overwrite and final symlink
conditions reproduced). This result is a defect reproduction, not a passing
security acceptance test.

Proposed narrow boundary:

1. Add an optional reusable `FileWriter(path, complete_bytes)` callback beside
   the approved reader. Use it for component live/stock publication and restore
   the existing default when empty. Keep parser syntax, serialized values, layout,
   pending-edit success/failure semantics, and component independence unchanged.
2. The Pi owner supplies the protected writer only during its live complete
   transaction. Bind it to the fixed owning INI, require exact committed-journal
   after-image bytes, and publish through the journal's bounded, private,
   no-follow, exclusive temporary-file/atomic-rename policy. Reject uncoordinated
   writes, mismatched bytes, unsafe paths, and repeated publication; never fall
   back to the component's predictable temporary name.
3. Update component standalone tests and README, parent publication/recovery
   regressions, and adversarial symlink/FIFO/hardlink cases. Prove unrelated files
   remain unchanged, pending edits remain available on refusal, committed recovery
   remains complete, and normal Save/Repair/Reset/Stop still work. Rerun both full
   actual network and USB suites for the final candidate.

Checking/removing `<ini>.tmp` immediately before the current writer would retain
its check/open race. Calling the same writer inside a larger transaction does not
make its open safe. The reusable component needs an explicit publication-policy
boundary, while the Pi owner retains filesystem/durability/authorization policy;
this is the same separation used by P2-10's approved input callback. No WTP,
Control/1 document, pairing contract, or operator workflow change is proposed.

The repository Component Policy explicitly requires separate approval before
broadening the parent task into this additional component API. P2-10 approved
input reads, not this write boundary. Separate P2-13 approval has now been granted.
Phase 2 must not be marked complete
with this reproduced write defect or the existing pairing-provider hold open.


### P2-13 implementation and review checkpoint

The user approved this boundary. `IniFile::set_file_writer()` now applies to
Save and stock publication without a parent dependency. Empty callbacks retain
the standalone default; callback failures preserve pending data and layout.
The README identifies the default writer's trusted-temporary-path requirement
and the callback owner's authorization, atomicity, and durability obligations.

The Pi startup owner installs its writer before parsing, binds the fixed INI
and parent, and delegates to the live store's committed-journal publisher.
Constructor failure installs a refusing writer. The journal requires a live
preparer, a durable committed decision, matching complete INI after-image,
unchanged original file observations, valid retained images for every record,
and one publication attempt. It uses the existing bounded random exclusive
no-follow temporary writer and directory synchronization, leaving the full WAL
for normal complete recovery. No storage format or wire contract changed.

Adversarial review corrections and evidence:

- A wrong byte image, uncommitted decision, cold reconstructed writer, repeated
  publication, absent decision, corrupt companion image, and equal-byte replaced
  final file refuse before publication. Full decision recovery remains available.
- Actual managed saves bypass pre-existing predictable temporary symlinks, FIFOs,
  and hardlinks. Their artificial unrelated file remains unchanged, and the
  hostile temporary entry remains untouched. An uncoordinated component save
  refuses while preserving pending edits.
- Standalone component tests exercise live, Reset, Repair, throwing callbacks,
  pure previews, callback reset, and reader/writer composition. The strict
  C++20 `-Wall -Wextra -Werror` component suite passes in
  `/private/tmp/issue473-protected-writer-component-test.log`.
- Parent build found and corrected an explicit JSON-string extraction and a
  test-local variable collision before acceptance. A sandboxed HTTP fixture
  lacked permission to bind a loopback port; its rerun uses only local socket
  permission, never hardware or privileged operations.
- The pre-P2-13 frozen pair passed network runtime, network interop, configuration
  interop, actual USB drivers/client, independent USB management, and the full
  USB UI pair. Its final default-profile WTP-main check exposed a harness error:
  the GPIO-capable executable correctly refused an unprivileged WTP startup.
  The workflow now retains default-profile simulated-main tests and runs the
  offline WTP actual-main fixture in a separate explicit GPIO-free build. The
  production privilege boundary is unchanged. The failed overall run is
  `/private/tmp/issue473-linux-pair/runner-wtp-teardown-reviewed.log` and is not
  an all-green gate.
- The first P2-13 container was intentionally stopped before acceptance so its
  replacement includes the final negative test. It is not passing evidence.
  Final frozen candidate run: `runner-p213-writer-reviewed.log`, with
  `p213-writer-reviewed-source-manifest.json`, under the same evidence directory.
  Its result remains pending at this checkpoint.

All eight final recovery screenshots (editable/read-only, light/dark,
desktop/mobile) under `/private/tmp/issue473-final-recovery-ui-captures/` were
visually reviewed using Impeccable. No new visual defect was identified; no UI
source changed for P2-13. The actual-browser persistence workflow is being
rerun with the protected publisher. The approved component README changes need
no new operator procedure; the already updated protected-recovery instructions
remain applicable. Automatic remote Pi pairing guidance remains unimplemented
under P2-01's dependency hold.


P2-13 focused acceptance now passes:

```sh
make -C src -j4 pi-management-journal-test pi-managed-startup-test \
  pi-management-service-test pi-managed-host-runtime-test pi-managed-wtp-main-test \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
```

Result: PASS, `/private/tmp/issue473-protected-writer-reviewed-test.log`. This
includes production-main recovery in Enforced and Off modes, offline-WTP main,
constructor-failure guards, live/stock transaction ownership, Stop/replay,
corrupt/uncertain history, and the added protected-writer negatives. Expected
platform-detection messages in the in-process fixtures do not indicate hardware
operation; the actual-main fixtures explicitly select their software backend.

```sh
# Working directory: WsprryPi-UI
WSPRRYPI_MANAGED_MAIN_FIXTURE=/private/tmp/issue473-controller-native/bin/pi_managed_main_fixture \
WSPRRYPI_RECOVERY_SAFETY_OFF=1 \
WSPRRYPI_RECOVERY_SCREENSHOTS=/private/tmp/issue473-p213-recovery-ui-captures \
node tests/configuration_recovery_browser_test.js
```

Result: PASS, `/private/tmp/issue473-p213-recovery-browser-test.log`, actual Chrome
157, production local main/PHP/proxy. Includes a dropped accepted Save reply,
Chrome's hidden retry, preserved later draft, exact explicit replay, and corrupt
history remaining read-only. No devices, installed services, or RF were used.

`python3 -B scripts/tests/control_contract_pair_test.py` passes. The workflow
graph regression passes all 13 cases in the same unprivileged container with
`TMPDIR=/work/issue473-p213-ci-temporary`; the host Python lacks PyYAML, and the
first container attempt's default no-exec `/tmp` refused executable fixture
scripts. No host dependency was installed. The final graph result is
`/private/tmp/issue473-p213-ci-graph-test.log`.


### Further input-boundary assessment

The next full source review found controller-only startup still used a
`is_regular_file`/size precheck followed by the component's ordinary reopen.
P2-10's protected reader was installed for transmitter startup but not this
second role, leaving a replacement race. The controller now installs the same
bounded no-follow actual-reader policy, bound to its fixed file and canonical
parent. It also explicitly refuses component writes; controller-owned catalog
and journal persistence are separate and unchanged. This reuses approved
component APIs and adds no protocol, setting, or operator action.

`controller_only_test.py` now checks symlink, hardlink, FIFO, oversized, and
unsafe-mode input refusal using both the fixture launcher and actual production
main, preserving original bytes. `make -C src -j4 controller-only-test
BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj
BIN_DIR=/private/tmp/issue473-controller-native/bin` passes in
`/private/tmp/issue473-controller-protected-input-test.log`.

The previous P2-13 pair snapshot excludes this correction. Its replacement will
freeze the full current source and rerun the complete network and USB recipes;
no earlier result is substituted for those current-source gates.


The current controller-only browser rerun passes in
`/private/tmp/issue473-p213-controller-browser-test.log`, with actual Chrome 157,
PHP templates, local HTTP/WebSocket services, and the current compiled controller.
Setup keeps Fleet available, local-output controls unavailable, unsaved entries
retained, and zero configuration writes. Operation has no fabricated local
output. Light/dark desktop/mobile captures are under
`/private/tmp/issue473-p213-controller-captures/`; current desktop Setup and mobile
Operation were visually rechecked against the incumbent design. The corresponding
recovery rerun's read-only mobile capture was also rechecked. No UI source or
operator procedure changed for the input-boundary correction.

The final paired run is now
`/private/tmp/issue473-linux-pair/runner-p213-input-final.log`, with
`p213-input-final-source-manifest.json`; it includes the corrected controller
reader and refusing writer. All previous cancelled snapshots remain excluded
from the completion gate. The latest code/contract/persistence review identified
no additional actionable defect in the implemented boundaries. Phase-wide remote
Pi enrollment, authenticated management, ownership-transfer recovery, and their
production negative tests remain unqualified under P2-01. This is not a clean
phase-wide assessment or a phase-completion claim.


The final snapshot's generated contract, component/controller/production-main
checks, and all three full network targets pass. Its independent USB test then
failed before READY: the container invocation omitted `exec` on its private
`/tmp`, where the actual Pico USB driver was built. `Driver` could not execute
that process, so the production USB negotiation correctly failed. This is an
environment/runner defect, not waived USB evidence; the exact failure is retained
in `p213-input-final-independent-usb-noexec-failure.log` and the original runner
log. No production timeout, validator, or USB protocol was weakened.

The continuation uses `--tmpfs /tmp:rw,exec,mode=1777`, verifies every frozen
Pi/Pico source hash and HEAD before starting, rebuilds both actual Pico drivers,
and reruns the USB client/management, independent management pair, USB UI pair,
and all remaining profile checks. It uses exactly the same manifest and pinned
Mbed TLS source, with no product-source change. Its log is
`/private/tmp/issue473-linux-pair/runner-p213-input-final-resume.log`. Network
results and successful remaining checks must be considered together; a failed or
skipped resumed USB check still prevents acceptance.


### Current source-pair network/USB results

Source revisions plus the phase's uncommitted changes:

| Repository/source | Revision and state |
| --- | --- |
| Pi | `c48a00dbbd6d9d65014c56f286ace074df9240e9`, feature branch `codex/issue-473-device-owned-scheduling`, tested dirty application/UI/component/generated/test changes |
| Pico edit candidate | `05cc90d67442c536f1cf7bee74a94914ad032c23`, `codex/issue-473-phase2-paired-candidate` in `/private/tmp/issue473-pico-candidate`, tested dirty schema/generator/handlers/driver/assets |
| Mbed TLS | clean `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`, explicitly mounted read-only |
| Docs | `a06c1d5590614a4794684aac794c468bec36b7cf`, separate dirty feature branch; not part of protocol execution |

The frozen physical source roots are
`/work/issue473-frozen/p213-input-final-1794849114803025253/{pi,pico}`. The manifest
records SHA-256 for each source file. Before resuming USB, both source HEADs and
every frozen file hash matched; a separate host comparison found zero changed
Pi source/UI/script/workflow or Pico source paths since freezing. The ordinary
Pico checkout remains clean and is not the edited candidate. No source has been
delivered by commit, push, merge, or CI for phase 2.

From the frozen Pi `src` root, the final network commands all PASS:

```sh
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  PICO_SOURCE=/work/issue473-frozen/p213-input-final-1794849114803025253/pico \
  MBEDTLS_SOURCE=/mbedtls wtp-network-runtime-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  PICO_SOURCE=/work/issue473-frozen/p213-input-final-1794849114803025253/pico \
  MBEDTLS_SOURCE=/mbedtls wtp-network-interop-test
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  PICO_SOURCE=/work/issue473-frozen/p213-input-final-1794849114803025253/pico \
  MBEDTLS_SOURCE=/mbedtls wtp-config-interop-test
```

This includes current closed contracts, real TLS/crypto and serialization,
configuration/resources, independent management transports, and the actual
controller-only HTTP service with two Pico services. It does not establish the
held automatic remote Pi pairing contract.

The resumed full USB commands all PASS:

```sh
cmake -S /work/issue473-frozen/p213-input-final-1794849114803025253/pico \
  -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF \
  -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4
make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  PICO_SOURCE=/work/issue473-frozen/p213-input-final-1794849114803025253/pico \
  MBEDTLS_SOURCE=/mbedtls wtp-usb-pair-client-build wtp-usb-management-test
node ../WsprryPi-UI/tests/pico_management_usb_pair_test.js \
  /work/issue473-frozen/p213-input-final-1794849114803025253/pi/src/build/bin/pico_management_usb_pair_client \
  /tmp/pico-usb/usb_management_driver
node ../WsprryPi-UI/tests/wtp_usb_settings_test.js
env WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js \
  /work/issue473-frozen/p213-input-final-1794849114803025253/pi/src/build/wtp-network/wtp_usb_pair_client \
  /tmp/pico-usb/usb_management_driver
```

`guarded-mode-change-persistence-test` also passes in the explicit
`BACKENDS=si5351,simulated ANCILLARY_GPIO=0` profile. Exact commands for all steps
are printed in the two runner logs and implemented in
`run_p213_input_final.py` / `resume_p213_input_final.py` beside them. Failed and
cancelled earlier runs are retained as diagnostic history, never counted as
successful required checks. The final default-profile check remains pending
until its actual result is recorded.


## Final assessment of this continuation

**P2-13 is implemented and validated. Phase 2 remains ⏳; phase 3 remains ▧.**
The final default-profile command passes, and the resumed runner exits 0 with
`ALL REQUESTED FROZEN CANDIDATE PAIR STEPS PASS`. Final frozen Pi/Pico source
hash verification passes. All required network and USB checks now have successful
results for this exact source pair. No failed, cancelled, skipped, or stale
result is substituted for a required successful check.

The final Linux default-profile command (hardware access disabled by the
runner) is:

```sh
make -j4 SUDO= pi-managed-main-test wtp-managed-output-test \
  pi-managed-startup-test pi-managed-host-test pi-managed-host-runtime-test \
  pi-private-network-persistence-test pi-managed-store-test \
  pi-management-service-test pi-management-journal-test
```

The separate explicit simulated-only offline-WTP actual-main test also passes;
the production GPIO-capable pre-parse privilege gate is preserved. These are
software checks, not installation, service-lifecycle, physical USB, timing,
GPIO/I2C, or RF qualification. No physical Pi or Pico was used. No CI run was
triggered for the uncommitted phase; there is no current-phase CI URL.

### Adversarial loop disposition

1. Reproduced predictable temporary-file publication, added the approved generic
   writer boundary, and bound Pi publication to exact live committed-journal
   proof. Fixed the two compile/test issues and checked complete transaction
   images before the first final-path write. Component and production-path
   negative tests pass.
2. Reassessed the other role and found the controller-only ordinary input reopen.
   Applied the already approved protected reader and refused INI writes in that
   role. Both actual-main and browser regressions pass.
3. Reassessed source selection, profile boundaries, and validation itself. Fixed
   the GPIO-capable WTP-fixture profile selection and the container's no-exec
   temporary directory. Reran affected checks successfully on verified unchanged
   candidate sources; no product safety or validation boundary was relaxed.
4. Performed a further source/contract/persistence/workflow/documentation and
   evidence assessment after those corrections. No additional actionable defect
   was identified in the implemented boundaries. The full phase is still not
   complete: automatic remote Pi pairing and its dependent remote-management,
   transfer/revocation, and recovery acceptance remain unimplemented/unqualified.
   The review does not waive that known gap.

**Exact next unfinished step:** qualify an acceptable provider for the approved
RFC 9382/P-256 automatic pairing contract, then implement and validate automatic
pairing/ownership changes and authenticated remote Pi management end to end.
OpenSSL/Mbed TLS remain in use. BoringSSL remains held behind the user's
maintenance/packaging gates and a separately reviewed wire amendment; there is
no manual-certificate, hand-written-PAKE, execution-credential, or unauthenticated
management workaround. No further approval is pending for P2-10 through P2-13.

### Documentation Impact

- Updated: this phase record, the phase-indicator link, and the independent
  `src/INI-Handler/README.md` callback/trust/durability description. The approved
  phase's Docs changes remain in
  `docs/User_Interface/Setup/Transmitter/index.md`,
  `docs/Advanced_Operations/ini_configuration/transmitter_backends.md`, and
  `docs/Command_Line_Operations/wspr_options.md` in WsprryPi-Docs; their recorded
  Sphinx build and desktop/mobile render checks pass.
- Considered but unchanged: the existing normal Transmitter screenshot, Pico
  explicit reconciliation guidance, WTP wire contract, and current TLS-library
  documentation. P2-13 introduces no operator procedure or new TLS dependency;
  existing protected-recovery guidance remains accurate. Impeccable/browser
  review evidence is recorded above.
- Still required: automatic remote Pi pairing/ownership-transfer and remote
  management operator/security guidance, alongside the blocked implementation.
  No future behavior is presented as available operator functionality.

Git boundaries at completion of this continuation: Pi has 191 changed entries,
Pico edit candidate 15, and Docs 3, all unstaged on their feature branches. The
ordinary Pico checkout is clean and untouched. Pi's unrelated untracked SQLite
prompt is preserved. Modified reusable component: `src/INI-Handler`; first-party
UI component: `WsprryPi-UI` (earlier phase changes retained, no new UI source change
for P2-13). No files were staged, committed, pushed, merged, deployed, or installed.


## Amended phase-2 closeout (2026-11-16)

**✅ Phase 2 is complete against the explicitly amended gate.** The user approved
moving new Pi pairing/TLS and dependent controller-to-remote-Pi management to a
later milestone, then instructed completion of phase 2. That milestone now belongs
to phase 6 acceptance, before phase 7, with deferred remote editor/migration checks
carried explicitly. Phase 3 and all later execution remain separately authorized;
none has started. The eventual security design is unchanged and unimplemented.

### Requirement-to-evidence assessment

| Amended phase-2 requirement | Implemented behavior and evidence |
| --- | --- |
| Local Pi management | Production context/settings/status, complete validated saves, bound Stop, durable replay, revision/identity fences; native service/store and actual-main tests pass |
| Independent Pico management | Per-target authenticated contexts, existing resource parity, no selected-backend/claim/job side effects; full current-source network and USB production pairs pass, including the two-Pico controller HTTP fixture |
| Explicit controller-only role | No local transmitter initialization, station requirement, output or automatic failure fallback; Mac actual-main/controller browser and Linux fixtures pass |
| Startup and durable recovery | Owned bounded no-follow input/publication, complete WAL decisions, initialized-history holds, retained first-initialization proof, saved enable/boot policy, protected recovery under Off, and pending-draft/reply preservation; component/parent/main/browser checks pass |
| Existing security and limits | Current Pico authentication/TLS, strict Control/1 validation, Host/Origin/proxy/CSRF policy, transport bounds, redaction, and unresolved-operation fencing remain enforced; negative and full paired checks pass |
| Deferred remote Pi remains unavailable | Added actual-main/controller-only HTTP negatives reject remote context/settings/status/receipts/Stop and Pi pairing paths even with a valid browser session; host configuration, catalog and nested history remain byte-for-byte unchanged |
| Required evidence | Both full network and USB suites, Linux default profile, strict standalone INI component, focused native tests, browser/Impeccable reviews, and Docs build/render evidence pass for the stated source pair and additive closeout tests |
| Documentation and future gate | Authoritative plan, execution rules, phase prompts/register/list, and deferred security milestone agree on scope; operator guide states the current remote-Pi limitation without promising future functionality |

The new tests use artificial valid remote identifiers and, on the native Pi,
a complete saved candidate, real binding fields, valid browser credentials and
revision. Refusal precedes persistence. Native absent pairing routes return 404;
controller-only role guards return their existing 409 `controller_only` refusal.
An initial test mistakenly expected 403 for the latter; the test was corrected
to the source-defined rejection, without changing production behavior. The final
file snapshots include nested management directories, not only the INI leaf.

### Current closeout validation

From the Pi repository, these commands ran sequentially to retain the actual
production singleton boundary; both PASS:

```sh
make -C src controller-only-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
make -C src pi-managed-main-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
```

Logs: `/private/tmp/issue473-amended-controller-refusal-final.log` and
`/private/tmp/issue473-amended-native-refusal-final.log`. Coverage includes both
controller launcher and production main, Enforced and Off native recovery, the
new remote-route negatives, existing local Stop/replay, no defaults becoming
confirmed saved state, and no RF. Earlier parallel focused runs also pass, but
the sequential logs are the final closeout evidence.

The prior complete network/USB and Linux-profile results above remain applicable:
there is no application, UI, shared schema, build recipe, or Pico source change
since their successful immutable snapshot. The only source-tree deltas are the
two additive Python negative-test scripts just executed. The comparison checks
both file sets and SHA-256 bytes with the same build/runtime/skill exclusions as
the original runner, and records current test hashes in
`/private/tmp/issue473-amended-phase2-source-comparison.json`. Repeating unchanged
full interface suites for a planning/docs/test-only amendment is unnecessary;
no new runtime interface is being qualified by mocks or stale evidence.

The source revisions remain Pi
`c48a00dbbd6d9d65014c56f286ace074df9240e9` plus the phase's dirty changes and Pico
`05cc90d67442c536f1cf7bee74a94914ad032c23` plus its isolated candidate changes.
The ordinary Pico checkout is untouched; the edited companion exists only on
`codex/issue-473-phase2-paired-candidate`, not delivered on `devel` or `main`.
Docs remains a separate feature-branch review/Git boundary. No current-phase CI
was triggered; source-level checks are not CI, delivery, installation, physical
USB, service lifecycle, GPIO/I2C, timing, or RF qualification.

In WsprryPi-Docs:

```sh
source docs/.venv/bin/activate
python -m sphinx -b html docs docs/_build/html
```

PASS, no warnings: `/private/tmp/issue473-amended-phase2-docs-build.log`.
`node /private/tmp/issue473-amended-controller-docs-render.js` also passes against
actual Chrome 157. Both final 1280px/390px captures were visually inspected under
Impeccable: readable text, preserved Read the Docs navigation, and no horizontal
overflow. Evidence: `/private/tmp/issue473-amended-controller-docs-{1280,390}.png`
and its renderer log. This amendment makes no application UI change; the earlier
current-source desktop/mobile light/dark application reviews remain applicable.

### Repeated adversarial assessment

The first amendment review found and corrected stale phase-3 adapter references,
D02's phase-owner row, phase-1 historical startup wording, the phase-2 progress
heading/gate, and duplicated operator limitation text. It also made the mandatory
phase-6/phase-7 boundary explicit in the authoritative plan, shared execution
rules, phases 3–8, and the phase list. The missing remote implementation is now
an explicit approved deferral with complete acceptance obligations, not a hidden
phase-2 completion waiver or removed feature.

The next full assessment checked the amended prompt requirement by requirement,
actual routing/role/service guards, complete durable-state snapshots, source-pair
identity, all required successful validation, provider/transport boundaries,
future predecessor gates, operator wording/rendering, and Git/hardware scope.
No further actionable defect or unmet requirement remains in the amended
phase-2 scope. The deferred milestone stays open and must pass before phase 7;
phase 8 cannot waive it. No further approval is pending to close phase 2.

### Documentation Impact

- Updated: authoritative plan, phase list/prompts/shared rules, phase-1 sequencing
  annotations and D02 owner, this evidence record, and the new
  [deferred Pi security milestone](deferred-pi-security.md). Updated the Docs CLI
  guide to explain that remote Pi settings/pairing/ownership transfer are currently
  unavailable and that each Pi's own UI remains the local settings interface.
- Considered but unchanged: existing WTP/Pico TLS and wire/security contracts,
  normal application screenshots, and prior Pico/recovery guides. Production
  behavior is unchanged by this closeout; those remain accurate. The INI component
  README and tests from P2-13 remain validated and unchanged.
- Still required: automatic Pi pairing/TLS, ownership/credential lifecycle,
  authenticated remote-management/editor/migration implementation, tests, and
  concomitant operator/security guidance at the end-of-phase-6 milestone. They
  are not advertised as available or omitted from the overall issue acceptance.

All existing dirty work and the unrelated SQLite prompt are preserved. Current
closeout modifies planning/operator documentation and two parent Python test
scripts only. The phase's earlier reusable-component boundary is `src/INI-Handler`
and first-party UI boundary is `WsprryPi-UI`. No runtime/component/companion code
was modified for the amendment. All work remains unstaged on feature branches;
no commit, push, PR, merge, deployment, service/hardware operation, or phase-3
implementation occurred.


## Feature-branch delivery authorization (2026-11-16)

After phase-2 acceptance, the user explicitly authorized commits and pushes to
feature branches and monitoring CI if enabled for them. This supersedes the
no-Git-action execution restriction for this delivery only; no merge, PR,
release, installation, hardware operation, or phase-3 execution is authorized.

The reviewed Pico companion is committed as
`ef5b80924bee9d3d483d8f2fcd0c7f52a3538ea8` on
`codex/issue-473-phase2-paired-candidate` and pushed to its origin feature ref.
The operator Docs changes are committed separately as
`9d41898acf3aa62f9c79665ef6fd0dbc30184127` on
`codex/issue-473-device-owned-scheduling`. Neither update is on `devel`/`main`.
Pi contract provenance now pins that exact clean Pico commit with
`source_uncommitted: false`; generated/source parity and source-selection checks
pass. Content comparison against the successful paired-test manifest finds no
Pico or production Pi/UI/schema/build change. The only later source-tree deltas
are the already tested additive Python refusal tests and this provenance pin.
No full-suite result is being attributed to changed production code.

The workflow push filters exclude these feature branches in all three
repositories, and no open pull request exists for them. No CI configuration is
changed or workflow manually dispatched for delivery; final GitHub run lookup
is reported with the pushed commits. Existing successful local validation,
visual reviews, documentation checks, and hardware limitations remain as above.
The unrelated SQLite prompt and local skill/runtime artifacts are excluded from
the approved parent commit. The ordinary Pico checkout remains untouched.
