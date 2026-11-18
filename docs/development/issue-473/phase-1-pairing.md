# Proposed automatic Pi management pairing

Status: D02 workflow/behavior approved by the user on 2026-11-15; not
implemented. Exact protocol/schema details are fixed by the
[normative supplement](phase-1-contract-details.md); production implementation
and security validation remain required. Replaces manual provisioning in D02.
User constraints recorded 2026-11-15: no certificate/file/fingerprint workflow,
keys generated and stored on devices, automatic credential lifecycle, ownership
replacement through ordinary UI, and no change to existing WTP contracts.

Implementation sequencing amendment (approved 2026-11-16): new Pi pairing/TLS
and controller-to-remote-Pi management are deferred to the
[phase-6.2 security milestone](deferred-pi-security.md). These approved
design contracts remain unchanged; phase 2 no longer claims their implementation.
Existing WTP/Pico TLS and the requirement for an automatic certificate-free
operator workflow remain unchanged.

## Operator workflow

1. Open the intended target Pi's own UI and choose **Pair a controller**. The
   target shows a temporary eight-character code, grouped for readability.
2. In the controller's Fleet view, select the discovered Pi, choose **Pair**, and
   enter that code. Discovery alone never grants management access.
3. The devices authenticate the exchange, create and durably store their own
   pairing material, verify the resulting connection, and show **Paired**.
   No file uploads, shell commands, fingerprints, browser certificate prompts,
   or certificate terminology appear in the operator workflow.

Target UI access is the existing trusted operator interface; pairing does not
establish a new human-login system. A hostile network that can alter that UI or
steal the displayed code is outside that inherited browser-access trust boundary.
The code is never sent in plaintext over the discovery/bootstrap connection.

Fields and feedback use incumbent Bootstrap/bench-instrument patterns. Keep code
entry, expiry, progress, and Retry beside Pair; preserve unrelated drafts. Support
paste, keyboard submission, accessible labels, a textual expiry indication, and
one live status region. Selecting a target never pairs or changes backend/output.
No new screen is rendered or visually qualified in this design-only phase.

## Ownership changes

The target lists its current management controller by friendly name and stable
identity, with **Replace controller** and **Remove controller** actions. These
manage access, not WTP claims or recurrence ownership.

- Replace starts the same short pairing flow after explicit target-local approval.
  The old manager stays effective until the new pairing is ready to commit.
- At target commit, bump the durable management-authorization generation, install
  the new pair, revoke the old pair, and invalidate old sessions atomically.
  Old requests recheck generation under the mutation lock before commit.
- The old controller need not be online or cooperate. On its next connection it
  sees **Access removed**; it cannot restore itself through retry, discovery, an
  old receipt, or a reboot. If reachable, it receives an immediate notification.
- Remove commits a revocation tombstone and closes management sessions. The
  target's autonomous schedule/settings remain intact. A later manager uses Pair.
- RF ownership changing through existing Transmit, takeover, WTP claim/release,
  job completion, or recovery does not revoke pairing or require pairing again.
  Pairing, replacement, and removal never acquire RF ownership or change Transmit,
  boot preference, schedule enablement, backend, or accepted jobs.
- If pairing material is lost through storage replacement or reset, recovery is
  **Pair again** in the target UI. No credential repair/import is exposed. A reset
  that cannot preserve identity is a replacement device, never silent trust in
  the same hostname. Safe local UI access remains a prerequisite; no network
  protocol can safely infer ownership from an unauthenticated discovery name.

There is one controlling manager per target in this proposal. Additional browsers
can use that controller's existing UI. Multi-controller management sharing is not
included and must not be implemented implicitly.

## Device-only key establishment and transport

This is a new, independently versioned Pi management-authentication contract,
not a WTP transport or job-protocol extension. No new requirements are imposed on
existing WTP clients, Plain LAN, Pico pairing, or engineering TLS profiles.

The user approved D62-01 on 2026-11-17: use RFC 9383 SPAKE2+ with mutual
confirmation and the single P-256/SHA-256/HKDF/HMAC profile. The controller is
the prover; the target derives its own ephemeral registration record from its
temporary code. No registration record is provisioned by an operator or exported
for provisioning. Botan 3.14.1 is approved for PAKE-only qualification, not yet
qualified or accepted for production. Existing OpenSSL/Mbed TLS roles remain.
The [approved amendment](phase-6-2-decisions.md#d62-01--pake-provider-and-narrow-wire-amendment)
and normative supplement define the exact 96-byte scrypt mapping, provider scalar
reduction, context/role bindings, confirmations, and purpose-separated keys.
There is one current version-1 profile and no SPAKE2/SPAKE2+ fallback.

Bind the complete authenticated exchange to protocol/version/roles, both full
stable device IDs, both boot IDs, target authorization generation, random pairing
ID, both nonces, and intended action (pair or replace). Derive independent keys
with protocol-specific HKDF labels; never reuse WTP/Pico credentials or the code.
The normative supplement fixes the bootstrap/rotation encoding and password
mapping; production implementations must pass its required conformance tests.
No custom replacement for PAKE or undocumented transcript field is permitted.

Derive a 256-bit external PSK for the new management connection. Use TLS 1.3
PSK with fresh (EC)DHE (`psk_dhe_ke`), mutual Finished verification, and no 0-RTT,
PSK-only mode, anonymous mode, or downgrade. This avoids certificates entirely
for this new path; neither browsers nor administrators manage them. Existing
WTP TLS remains unchanged. See [TLS 1.3, RFC 8446 §4.2.9](https://www.rfc-editor.org/rfc/rfc8446.html#section-4.2.9).

Pair-specific key scope binds target/controller identity, pair ID, authorization
generation, and management service/ALPN. Use a new parent-owned management
transport, not altered credential semantics in the existing WTP TLS adapter.
A new machine endpoint is proposed on port 31418; discovery advertises only its
availability, never secrets. Existing browser APIs proxy through the controller
backend. Browsers cannot directly use or retrieve the management PSK.

## Bounded pairing and persistent lifecycle

- A target-local guarded UI action creates one CSPRNG code from eight symbols of
  an unambiguous 32-symbol alphabet (40 bits). It expires after 120 monotonic
  seconds. One active attempt per target; at most five failures per window.
  Exhaustion closes the window until the target operator explicitly starts again.
  PAKE session responses are bounded to 4096 bytes; whole exchange deadline 30 s.
- Public bootstrap accepts only the fixed pairing exchange during an open window.
  It cannot open windows, disclose codes, write settings, expose controller names,
  grant privileges from discovery, or reset rate limits on reconnect. A successful
  code is consumed once. Pairing windows are lost on reboot and never auto-open.
- Target-local start/replace/remove use the existing protected Host/Origin and
  mutation-intent checks plus the proposed session CSRF checks. Initial pairing
  grants one fixed manager role: read/configure/migrate/stop, with the existing
  runtime safety and ownership checks still enforced. Browser sessions do not
  become transferable machine credentials.
- Both ends persist private regular files owned by the service user in 0700/0600
  storage, no symlinks or browser-selected paths; file and directory sync are
  required. Exact record binds pair ID, peer/self IDs, generation, key epoch,
  granted role, and private key material. Secrets/code/PAKE messages are redacted
  from ordinary logs, support bundles, URLs, browser storage, and receipts.
- Stage a pending pair on the controller before asking the target to commit.
  Target commit is the authorization linearization point. A lost reply is
  resolved by reconnecting with the pending key and reading the matching durable
  receipt. The controller only shows Paired after target confirmation. Pending
  or cancelled UI states never imply that the old manager is still authorized.
- Target replacement is generation-fenced and revocation-durable before success
  is returned. If the new controller dies after target commit, the target retains
  that outcome; another ordinary Replace flow repairs access without restoring
  the revoked manager. Storage ambiguity holds management writes and reports
  **Pairing needs attention**; it does not disable autonomous recurrence.
- Reconnect and key renewal are automatic. Rotate the transport PSK after 30 days
  of accumulated monotonic service time or 2^20 authenticated connections, whichever
  comes first, using a new authenticated ephemeral exchange and two durable epochs.
  Pending next epoch is recovery-only until committed. One previous epoch may
  retrieve/finish the matching rotation receipt, but cannot mutate configuration;
  retire it after acknowledgement. Offline time does not expire the current pair
  or force users to repair credentials. Failed rotation recovers the current
  unrevoked pair; it never revives an old owner/generation.
- Address changes use ordinary discovery/direct profile resolution followed by
  cryptographic peer verification against the existing pair. Names/IPs are hints,
  never authority. Unexpected device identity requires Pair again. No automatic
  fallback to another target, Plain LAN management, or existing WTP credentials.

Cryptographic implementation/library conformance to the now-specified wire
contract, official vectors, and production-path crash/revocation tests are required
before calling this design implemented. They are not existing repository features.
No manual credential fallback is allowed if any part proves unavailable; remove
this new remote-management path or redesign it within these UI constraints.

## Validation and limits

Required: successful pairing, no key material in browser/logs, wrong/expired code,
role/ID substitution, transcript tampering, online guessing, duplicate commits,
lost replies at both commits, reboot at every durable boundary, unavailable old
owner, replacement racing old writes, stale sessions/tickets/receipts, key rotation
through disconnection, revoked-epoch refusal, changed IP, lost local storage, and
feature-off/backward WTP interoperability. UI reviews must cover desktop/mobile,
both themes, keyboard, expired/busy/error/revoked states, and an old controller
coming back online after replacement.

Management replacement can automatically revoke management access because the
target owns that key/generation. It cannot selectively revoke an old Plain LAN
WTP scheduler: that existing protocol has a shared principal. Pairing is not proof
of legacy scheduler retirement. The earlier D07 proposal to close that lane is
withdrawn under the unchanged-WTP requirement. D07 now has approved automatic
retirement/reconciliation semantics, with offline or unknown old authority leaving
migration pending; do not add manual certificate work, silently change WTP
admission, or claim pairing alone resolves unknown accepted RF work.
