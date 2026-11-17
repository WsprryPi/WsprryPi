# Phase 6.2: researched recommendations for approval

Status: all five decisions proposed, awaiting user approval. Researched
2026-11-16. This records recommendations, not implementation authority or provider
qualification. No approved normative contract is amended by this document alone.
See the [implementation plan](phase-6-2-implementation-plan.md).

| Decision | Recommendation | Approval effect |
| --- | --- | --- |
| D62-01 | Qualify Botan 3.14.1 for PAKE only and explicitly amend unreleased Pi pairing to SPAKE2+; retain OpenSSL/Mbed TLS for existing TLS roles. | Allows the specified protocol amendment and candidate qualification, not automatic production acceptance. |
| D62-02 | Reuse USB-local Pico management, automatically negotiate attached Pico management through the managed Pi connection, and retain existing network Pico login. | The attached-Pico workflow follows the user's clarification; the other proposed Pico dispositions still await approval. |
| D62-03 | Keep the phase-5 assignment digest and use the context ETag only for current HTTP preconditions. | Clarifies retirement revision semantics; does not reinterpret old receipts. |
| D62-04 | Use a durable target-issued resume grant, consumed only by a currently paired old controller. | Defines remote rollback; revoked retirement-only credentials cannot resume schedules. |
| D62-05 | Keep target-owned authenticated history and explicit completeness; use a separate retirement-only TLS purpose for callbacks. | Defines registration/reachability and the honest eligibility boundary under unchanged WTP. |

## D62-01 — PAKE provider and narrow wire amendment

### Evidence

The approved RFC 9382 SPAKE2 suite must not be confused with RFC 9383 SPAKE2+.
They have different registration, transcript, and confirmation behavior; a provider
for one is not a drop-in implementation of the other.
[RFC 9382](https://www.rfc-editor.org/rfc/rfc9382.html),
[RFC 9383](https://www.rfc-editor.org/rfc/rfc9383.html).

The bounded candidate review did not establish a production-qualified provider
for the exact existing RFC 9382 profile:

| Candidate | Research finding and disposition |
| --- | --- |
| BoringSSL | Upstream discourages general third-party dependency and does not promise API/ABI stability. Keep held/rejected under the existing maintenance/packaging rule; do not reopen adoption merely for PAKE. [Upstream policy](https://boringssl.googlesource.com/boringssl/+/HEAD/README.md). |
| RustCrypto `spake2` | The documented implementation supplies an Ed25519 group and warns that it has not received independent security/correctness audit. It is not the approved P-256 provider. [Provider documentation](https://docs.rs/spake2/latest/spake2/). |
| `pakery` | Claims RFC 9382 vectors, but its own security disclaimer says it has not been independently audited. Its Rust toolchain/FFI and release integration would also require new packaging work. No evidence found in this review closes the security/packaging gate. [Upstream README](https://github.com/djx-y-z/pakery/blob/main/README.md). |
| `backkem/spake2_cpp` | Exact RFC/P-256 candidate, but its README explicitly warns of no formal security review/audit; repository activity inspected was two commits, latest push 2025-05-29. Do not promote vector success to security/maintenance qualification. [Upstream README](https://github.com/backkem/spake2_cpp). |
| Mbed TLS EC-JPAKE | Different protocol; its documented TLS exchange is experimental/disabled by default. It is not a conforming replacement for the saved SPAKE2 exchange. [Configuration documentation](https://mbed-tls.readthedocs.io/projects/api/en/v3.6.3/api/file/mbedtls__config_8h/). |

Botan supplies an RFC 9383 SPAKE2+ API. Its release history records PAKE introduction
in 3.13.0 and later validation fixes; the reviewed current release is 3.14.1,
2026-11-06, commit `01af88c039fcfaedc418b1af4d6d3a139674e591`. The source is
BSD-2-Clause. Linux aarch64 is tier 1, while macOS and Linux arm32 are tier 2;
that does not establish ARMv6 or this application's package compatibility.
[API](https://botan.randombit.net/handbook/api_ref/spake2p.html),
[release notes](https://botan.randombit.net/news.html),
[exact license](https://raw.githubusercontent.com/randombit/botan/3.14.1/license.txt),
[platform policy](https://botan.randombit.net/handbook/support.html).

### Recommendation

Approve Botan as the **candidate for qualification**, for PAKE only. Use a pinned,
unmodified, minimized static build with its official module/dependency selection;
exclude Botan TLS, X.509, HTTP, and unrelated algorithms. Retain existing OpenSSL
Pi TLS and Mbed TLS Pico TLS. Do not ship two competing TLS stacks or copy/fork
individual cryptographic implementation files. Upstream supports minimized builds;
actual size, loading, compiler, and packaging compatibility remain untested here.
[Build documentation](https://botan.randombit.net/handbook/building.html).

Approve this narrow unreleased `Wsprry-Pair/1` amendment, retaining version 1 and
one exact profile, with no SPAKE2/SPAKE2+ negotiation or fallback:

- Use RFC 9383 P-256/SHA-256/HKDF/HMAC, controller as prover and target as verifier.
  Target computes the ephemeral registration record from its own temporary code;
  no registration record or credential is supplied by an operator or transmitted
  for provisioning.
- Bind the existing IDs, boots, generation, action, pair/window IDs and nonces,
  plus fixed suite `SPAKE2+-P256-SHA256-HKDF-HMAC`, into canonical context C.
  Bind both nonempty
  role identities and C through the provider's standard transcript/context API.
- Preserve the 32-MiB scrypt budget and N=32768,r=8,p=1. For the new profile,
  derive 96 bytes from RFC 9383's length-framed code/prover/verifier input using
  SHA256(J(C)) as salt; split into big-endian 48-byte halves and use provider scalar reduction
  for w0/w1. Reject zero and consume a failed attempt. Use the supported prehashed
  API; do not adopt its different default Argon2 profile or hand-roll group math.
- Keep the existing hello/challenge route family and add the fixed suite binding
  to its closed schema. Exchange returns the verifier share and confirmation;
  the controller verifies it before sending prover confirmation. The target
  verifies that confirmation before commit eligibility. The confirmation reply
  is an acknowledgement, not a second invented PAKE confirmation. Never use the
  provider's skip-confirmation API.
- Derive the existing purpose-separated transport/commit/retirement keys from the
  confirmed shared secret using `Wsprry-Pair/1/spake2plus/tls-psk`,
  `Wsprry-Pair/1/spake2plus/commit`, and `Wsprry-Pair/1/spake2plus/retirement`.
  Preserve
  TLS PSK-DHE, commit/readback, code/window/failure/deadline limits, and ownership
  behavior. Update the normative profile, closed schemas, vectors, and consumers
  together after approval; do not run either profile under ambiguous wire semantics.

Approval is conditional on all existing qualification gates. Require official
vectors plus application-profile vectors, reviewable PAKE/security evidence,
negative/fault tests, and ordinary build/package/runtime tests for every existing
supported Pi architecture and production Mac role. No security audit of this new
Botan feature or package success is claimed from documentation research.

Maintain one source pin/checksum and notices; check upstream advisories before
every release and on a documented monthly maintainer review. Rehearse an upstream
patch update across the existing matrix without local crypto patches or operator
intervention. Failure on maintenance, security, or seamless packaging rejects this
candidate for pairing and holds the affected path; there is no automatic alternate
library, algorithm, or manual-credential fallback.

Tradeoff: one additional PAKE dependency and an explicit prerelease wire amendment,
instead of owning a small unaudited PAKE implementation. Existing TLS and WTP do
not change. If the user rejects the amendment, retain RFC 9382 and hold provider
selection; this review has not qualified a conforming implementation for it.

## D62-02 — Pico USB, IP, and management through a Pi

The existing Pico USB contract already treats a selected physical device as local
administrative authority through OS permissions, exclusive access, physical-parent/
serial/role checks, and device/boot/session bindings. It explicitly does not call
those identifiers cryptographic proof, confer transmitter ownership, or let a
Pico network request select the internal USB principal. Existing network Pico
management has its own consumer login/engineering TLS paths.
[USB contract](https://github.com/WsprryPi/WsprryPico/blob/f7c4fad280c811755b2e1619ae90f438eb06d49b/engineering/protocol/USB-Management.md),
[current Pico contract](https://github.com/WsprryPi/WsprryPico/blob/f7c4fad280c811755b2e1619ae90f438eb06d49b/CONTRACT.md).

Approve this explicit disposition of the three proposals:

1. **Direct USB Pico:** Automatically negotiate the existing USB management
   context for a locally selected/enrolled USB device after OS/exclusive-open and
   exact physical/session checks. No code, certificates, or new persistent PAKE
   bond. Label the connection USB access, not cryptographically paired. Discovery
   alone does not enroll arbitrary newly attached devices or authorize writes.
2. **IP-connected Pico:** Keep the existing password-based consumer login or
   already-supported engineering transport. Do not add Pi-style IP-code PAKE to
   Pico in 6.2. This is an explicit scope disposition of the recorded proposal,
   not a claim the proposed IP-code Pico flow exists. Pi IP-code pairing remains
   included. Existing Pico features and authentication are not removed.
3. **Pico attached to managed Pi:** Use the existing authenticated Pi management
   connection to automatically discover attached Picos and negotiate their
   supported management contexts through that Pi. The Pi performs the physical
   USB/session checks as its local administrator. No separate delegation switch,
   child-scope approval, pairing prompt, or operator-managed credentials are
   required. This is the user's clarified workflow, not a new optional grant.
   Negotiation and presentation do not save settings, enable schedules, change
   output selection, or dispatch work. The operator selects the Pico to edit in
   the existing Device workflow after its context is verified.

Use the existing target-bound Pico resource route/payloads. Add an exact typed
gateway binding to the current Control/1 context: parent Pi ID/pair authorization,
child ID/boot, parent and child profile/connection generations, and resource scope.
The controller connects only to the pinned Pi pair; the Pi dispatches only the
reviewed configuration/status/action resource allowlist to its bound USB context.
No raw network/USB forwarding, WTP dispatch, firmware updater, arbitrary device
path, or automatic direct-IP fallback is added by this management path.

The existing Pi management authorization governs this path; the physical Pico
does not acquire a second network manager or exported secret. After normal Pi
Pair/Replace completes, automatically negotiate fresh attached-Pico contexts for
that manager without a second approval. Remove/revocation closes queued/new
access through the Pi. Recheck the Pi authorization immediately before USB dispatch. A USB request already
handed off may commit: retain its operation/reply and report uncertainty rather
than pretend revocation undid it. Reboot/hotplug invalidates contexts and requires
fresh negotiation/readback, never automatic mutation replay. Accepted jobs and
Pico settings remain unchanged by access revocation.

This is trust in the Pi as gateway, not end-to-end cryptographic identification of
the Pico. A compromised trusted Pi/OS administrator is outside that boundary.
Pi Remove/Replace revokes the old manager's Pi path only; it does not rotate the
Pico's independent password or revoke independently obtained Pico network/USB
access. Do not present this as transferring every Pico access path to a new owner.
Materialize the gateway schema and affected companion validators under fresh Pico
authority; direct Pico network requests still cannot request USB-local authority.

Tradeoff: automatic management negotiation over the connection already used for
the Pi, with no extra operator grant workflow. The separate proposal to exclude
a new Pico network PAKE protocol from 6.2 still awaits approval.

## D62-03 — Distinguish assignment identity from HTTP concurrency

Approve the existing phase-5 immutable assignment revision as the retirement
proof's `assignment_revision`. It is SHA-256 of the existing deterministic row
serialization after excluding `enabled`, `in_flight`, and `last_start_ns`. Preserve
that exact semantic projection; no opportunistic canonicalization change or
reinterpretation of retained records. A schedule, endpoint, or other semantic row
change changes the digest; pausing or recording a launch does not.

Keep the public context ETag separately for browser `If-Match`: it covers the
whole public representation, including controller incarnation and current
assignment revisions. This is consistent with HTTP strong preconditions; it is
not a permanent identifier for one assignment.
[HTTP preconditions](https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.1).

Retirement binds the persistent assignment ID, frozen assignment digest,
transition, target/controller IDs, and target authority generation. Preserve the
original browser ETag only for exact request replay; a later unrelated assignment
edit must not invalidate the genuine stored retirement proof. Keep the controller
book's CAS generation distinct from the target authority generation carried by
the machine retirement message. They are different counters, not interchangeable
values despite similar existing field names.

Amend the older normative sentence calling the proof digest the old controller's
store ETag. No change to WTP, assignment-v1 layout, or existing proof identity is
required. Test unrelated-row edits, volatile flags, actual semantic edits,
recreated assignment IDs, exact replay, and generation-role substitution.

## D62-04 — Resume only under a live pair and durable target grant

Approve a target-issued, one-use resume authorization over the **current paired
TLS connection**, rather than enlarging retirement-only key permissions. If the
old controller's management access was revoked, first use ordinary target-local
Pair/Replace to authorize it again, then explicitly request rollback. Pairing
again by itself still cannot resume anything. An offline old controller leaves
rollback pending. This may require an extra ordinary code-pairing step when
management ownership changed; it requires no files or certificates.

Recommended state sequence:

1. Under existing current-binding/revision checks, verify native scheduling
   disabled, its disable/readback receipt, output safe, and all accepted/unknown
   work reconciled. Confirm the selected old controller is currently paired and
   the exact old assignment still matches its retained retirement receipt.
2. Atomically persist a rollback reservation and frozen resume grant in the
   target's existing authority/settings/operation transaction. Hold both native
   and target-local legacy admission before making the grant available. Record
   the selected remote execution owner explicitly; a generic `legacy`/`rollback`
   state must never reopen the target's own old scheduler. The grant has no timeout
   that can reopen native admission.
3. The old controller reads the grant through paired TLS, verifies its identity,
   current pair/generation, current target/controller context, exact retired
   assignment/digest/receipt, and disabled target revision. Commit its resumed
   authority disposition, assignment after-image, anchor, and exact resume receipt
   under one local WAL decision within the six-image bound. Publish dispatch
   permission only after verified durable publication. Do not delete the retired
   record or add a general clear-fence operation.
4. Return the retained result through paired TLS. Lost replies resolve the same
   operation through readback. Target boot/context changes before consumption
   require fresh explicit review; already committed consumption replays only its
   historical receipt, never another resume.
5. The target remains disabled and reserved while delivery/outcome is unknown.
   Once acknowledged, it records legacy authority for that exact remote controller
   and assignment, keeping target-local recurrence disabled. A later return to
   native scheduling needs a fresh higher-generation
   retirement of the resumed assignment; the old retirement receipt cannot be reused.

Bind the grant to a previously committed disabled snapshot and its existing
disable receipt, plus the disabled candidate digest. Its disabled revision is
that historical snapshot's revision, not the composite revision produced by
publishing the grant itself; do not create a self-referential digest. Verify the
same disabled candidate through reservation and consumption, and hold conflicting
candidate/enable mutations while the grant outcome is unresolved. Safe Stop and
access revocation remain independent. No reservation reset on reboot or timeout.

Keep at most one outstanding resume grant for the target and retain its unresolved
receipt under the existing bounds. Resumption is a new explicitly authorized
disposition of the old assignment, not deletion of its retirement. Old retirement
proofs cannot qualify subsequent native enablement after that disposition.

Proposed machine routes on the target's paired listener:

| Route | Closed semantics |
| --- | --- |
| `GET /api/pair/v1/resume` | Read only: return the exact pending grant to its designated current paired controller, or a typed absent/unknown result. Never creates or consumes a grant. |
| `POST /api/pair/v1/resume/ack` | Accept only the retained local resume/refusal receipt for that grant. Unknown is not a definite refusal. Never grants a different assignment or repeats resume. |
| Existing `/api/pair/v1/operations/{operation_id}` | Recover the exact operation under its current authenticated scope; no mutation on lookup. |

The closed grant binds contract/schema, operation ID, pair ID/authorization
generation, target ID/boot, controller ID/incarnation, assignment ID/digest,
retirement transition/receipt digest, target authority generation, disabled
settings revision/receipt digest, disabled candidate digest, and grant digest.
Grant digest covers the canonical grant without its own digest field. The acknowledgement binds the
same operation/pair/target/controller/assignment, grant digest, definite
resumed/refused outcome, and durable controller generation/receipt digest.
Use existing ID/Digest/Generation types and bounded current version-1 schemas.
The target-side user action remains the approved migration/rollback route with
its complete binding and revision checks. These machine routes are not browser
commands and never accept cookies or retirement-only proofs as resume authority.
An acknowledged refusal must prove no resume decision committed; otherwise retain
unknown state and the target hold. Neither access revocation nor new pairing can
cancel an ambiguously delivered grant or remove that hold.

Tradeoff: a current pair is required for rollback, rather than adding another
retained credential class, signing-key infrastructure, or reverse management grant.
Explicit rollback may eventually transmit under the selected saved legacy
settings, subject to existing WTP/Transmit/ownership rules; the UI must say so.

## D62-05 — Target-owned inventory, authenticated registration, honest limits

Approve target-owned history of **known first-party saved recurrence authorities**,
not a claim that unchanged WTP identifies every possible LAN client or its future
intent. The existing D07 boundary expressly preserves fresh Plain-LAN claims when
local Transmit permits them. No registration requirement is added to WTP or to
ordinary manual/one-shot work.

Record verified authority metadata within the existing recurrence-authority owner
image, with a coordinated exact current storage schema, rather than adding a
seventh WAL image. Retain existing closed controller rows and their identities;
add bounded private provenance/completeness/endpoint metadata under that owner.
At most 32 authority entries and retained uncertainty remain subject to current
bounds; full storage holds, never prunes unresolved authority to manufacture space.

Each registered entry binds installation/history, target/controller IDs,
persistent assignment ID and D62-03 digest, pair namespace/generation, observed
controller-book generation, verified retirement state, and approved endpoint
hints. Registration is obtained from the owning controller's real store/book over
the current paired connection. The target validates peer identity, transaction
scope, and exact assignment evidence, then acknowledges a durable record before
using it for migration. No browser-supplied controller list proves completeness.
The local Pi's own legacy row stays a direct in-process authority transition.

Use a closed authenticated registration command such as
`POST /api/pair/v1/authorities/register`; bind its operation, pair, target,
controller, exact source-book generation, assignment rows/digests, and typed peer
endpoint hint. It may register/update that peer's own evidence only; it cannot
remove another peer, attest global completeness, enable recurrence, or create a
new Fleet assignment. Receipt/state readback uses the existing operation lookup.
Legacy dispatch without a pair keeps its existing behavior but gains no migration
eligibility from being reachable. Registration is not a new WTP admission handshake.

Completeness rules:

- A positive target-owned installation boundary can establish the initial scoped
  inventory; it is not evidence the physical hardware has never been used.
- Preserve the ledger through manager replacement, updates, ordinary reset, and
  controller removal. Imported matching evidence can resolve specific known gaps;
  pairing one controller or importing all rows from it cannot prove no other
  historical controller existed.
- Unknown preexisting history, missing established records, unmatched evidence,
  or an unverified old peer remains unclassified/held or migration pending.
  Do not offer a browser checkbox asserting that no other controllers exist,
  erase history, scan credentials, or treat elapsed time as proof.
- An installation whose legacy history cannot be proved may remain in its existing
  valid legacy mode; it is ineligible for the affected native migration. This is
  a supported safe outcome, not a reason to fabricate automatic success. Fully
  erased or administrator-rolled-back history has no stronger attestation than
  the already approved installation model.

Controller callbacks used for existing retirement requests are nominated only
through authenticated registration and checked against the current controller
identity. Validate and pin private addresses per connection, prohibit self/
metadata/public/redirect/proxy/arbitrary-path targets, and enforce existing
deadlines. Hints from discovery may locate the same verified peer but never
replace its identity. Offline/changed/unverified peers remain pending. The shared
retirement verifier authorizes only the exact retirement/status path, not reverse
settings access. To reconcile a revoked peer without retaining its normal TLS
credential, approve a dedicated retirement-only TLS purpose on the same bounded
31418 listener, not an unencrypted callback or normal-management fallback:

- Use the same TLS 1.3 PSK-DHE, cipher/group/Finished requirements and shared
  connection/deadline budget, with ALPN `wsprry-retirement/1`.
- Derive a 32-byte ephemeral transport PSK from the retained retirement verifier
  using HKDF-SHA256. Salt is SHA256 of the canonical fixed-purpose context
  containing target/controller IDs, pair ID, authorization generation, and
  client-role target/server-role controller. Info is
  `Wsprry-Pair/1/retirement-tls-psk`; identity is
  `Wsprry-Retirement/1/{pair_id}/{authorization_generation}`. Store no additional
  long-lived transport key. Standard PSK guidance calls for identity and purpose
  separation; these exact application bindings are the proposed amendment.
  [External PSK guidance](https://www.rfc-editor.org/rfc/rfc9257.html).
- Permit only the two existing retirement/status POST messages under that
  principal, still checking their body MACs, pair, IDs, transition, and generation.
  Never accept settings, Stop, pairing/rotation, registration, resume, or normal
  management ALPN with this purpose. Reject cross-purpose identities/keys before
  dispatch; do not scan credentials or try another mode after failure.
- Retain only the approved retirement verifier/receipts through reconciliation;
  normal revoked TLS secrets remain erased. Close these scoped sessions when
  their recovery authority is reclaimed. Public HTTP bootstrap remains confined
  to its existing pairing window and cannot serve retirement mutations.

This is an explicit additional transport-purpose amendment for the already
required retirement proof, using the existing TLS library and port. It does not
restore a revoked manager or add a WTP carrier. Resumption instead uses D62-04's
controller-initiated current **normal management** TLS connection. Materialize
the exact purpose context/identity, route inventory, and validators together on
approval. Tests must reject every management/resume command under retirement-only
credentials, including after reboot and ordinary-owner replacement.

Test ledger retention, partial imports, dishonest completeness claims, missing
history, replacement with an offline old manager, poisoned callback hints,
controller reincarnation, full capacity, recreated assignments, and manual WTP
claims that remain independent. Overall guarantees cover cooperating first-party
authority records and current runtime ownership; they do not permanently fence
an arbitrary fresh Plain-LAN WTP client.

## Approval, validation, and documentation boundaries

All five decisions remain **proposed**. Approval adopts their specific behavior
and listed prerelease contract amendments; it does not certify Botan, bypass
phase 6.1, authorize Pico edits, implement code, or authorize Git/hardware actions.
After approval, update owning normative definitions, one-current schemas,
generated consumers, storage owners, operator guidance, and negative vectors
together during authorized implementation. Existing numeric/resource bounds,
WTP, code-free USB-local operation, and independent management/RF ownership remain.

Research performed: primary upstream API/release/license/platform/build/security
documentation; exact public upstream tag/commit and maintenance metadata; current
Pi retirement source and Pico USB/network contracts. No provider build, audit,
package rehearsal, performance measurement, hardware operation, or application
qualification occurred. Documentation promises are not test evidence.

Adversarial research review checked protocol-versus-library mismatch, unsupported
packaging claims, accidental Pico trust elevation, stale ETags, retirement-only
key escalation, lost resume acknowledgement, historical receipt reuse, inventory
completeness claims, and callback SSRF. Follow-up review fixed the target-local
legacy-admission hazard, specified historical disabled-snapshot binding to avoid
a recursive grant digest, kept one unresolved grant bounded, and specified
retirement-only TLS to avoid plaintext callbacks or revoked normal-key reuse.
The recommendations retain their gates and
safe pending states. Required implementation evidence remains
outstanding; this is a decision proposal, not a completed security milestone.

Final full reassessment found no further actionable recommendation defects after
those corrections. All five approval states remain pending; provider security/
packaging qualification and implementation tests are deliberately not claimed.
Documentation Impact: this decision record and the linked implementation plan
are updated; operator guides, firmware, application code, dependencies, and phase
indicators are unchanged. No implementation, staging, commit, or push occurred.
