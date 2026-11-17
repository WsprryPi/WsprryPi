# Phase 6.2: Pi security and remote-management gate

Status: sequencing amendment approved by the user on 2026-11-16. The user split
phase 6 into recurrence integration (6.1) and this security gate
(6.2) on 2026-11-16. Neither subphase has started; each requires separate execution
authority. Use the [phase 6.2 execution prompt](06-2-pi-security.md).

The [implementation plan](phase-6-2-implementation-plan.md) expands this gate
without authorizing implementation or changing the approved contracts.

## Scope and placement

Keep all eight phases. Phase 2 completes the local Pi service, existing Pico
management, explicit controller-only role, protected persistence/recovery, and
readiness foundations. New controller-to-remote-Pi management, automatic Pi
pairing, its new TLS transport, and the dependent lifecycle/acceptance work move
to mandatory phase 6.2, after recurrence integration in phase 6.1 and before
phase 7 may start. Phases 3–6 may implement independent local Pi and existing
Pico behavior when separately authorized. Phase 6.1 has its own recurrence acceptance gate. Overall
phase 6 cannot be marked complete until both 6.1 and 6.2, including deferred remote
checks from phases 3 and 5, pass. Phase 8 cannot waive this requirement.

This supersedes only implementation order and affected acceptance gates. D02,
D07, the phase-1 pairing workflow, and normative security/storage contracts remain
the intended design. Existing WTP, Plain LAN execution, Pico authentication/TLS,
and other current transports are unchanged. OpenSSL/Mbed TLS remain selected;
BoringSSL remains held behind its maintenance/packaging gates and any separately
approved pairing-wire amendment. No dependency or wire amendment is approved here.

## Enforced boundary before completion

- Do not implement or expose controller-to-remote-Pi enrollment, configuration,
  actions, credential renewal, or ownership transfer through an unsecured path.
  No trust-on-first-use, accept-any-certificate, manual certificates, shared
  execution credentials, or automatic fallback substitutes for pairing.
- A target Pi's own existing web UI and its protected local management API remain
  available to eligible LAN/loopback browser clients under their existing Host,
  Origin, proxy-client, CSRF, and safety policy. This is distinct from the deferred
  cross-target controller adapter; it is not a new network-access prohibition.
- Only the owning local Pi and verified existing Pico contexts are eligible for
  new target-management operations. A remote Pi identifier must not route into
  the host's settings, Stop, receipts, or recovery. Existing WTP execution profiles
  may remain usable; they confer no new settings-management authority.
- Phase 3 migrates eligible local/Pico controls only. Discovery/selection cannot
  enroll a remote Pi, create an edit context, or grant write capability. Do not
  advertise unfinished pairing or remote-Pi controls as available.
- Phase 5 still enforces target-local interlocks on every enabled path. A remote
  Pi migration requiring the deferred authenticated channel remains unavailable;
  keep its configuration disabled and preserve all uncertainty/retirement evidence.
- Nothing here permits early recurrence enablement, reduced validation, relaxed
  origin/CSRF checks, or cleanup of authority/recovery records.

## Pairing flows to consider at this gate

Recorded at the user's request on 2026-11-16. Review these proposed flows when
designing and implementing this milestone; recording them does not authorize
implementation or supersede the approved security contracts above.

- **Direct USB Pico:** A Pico connected by USB to the managing device should
  automatically negotiate pairing.
- **IP-connected Pi or Pico:** Allow the target to generate a pairing code for
  entry on the managing device to negotiate pairing.
- **Pico connected by USB to a managed Pi:** The attached Pico should automatically
  negotiate pairing through that Pi.

User clarification (2026-11-16): when managing a Pi, use that existing authenticated
connection to negotiate management of attached Picos automatically. No separate
delegation switch, child-scope approval, or additional operator pairing step is
required. Preserve device/context validation and existing resource/safety checks.
This clarification does not approve the other pending research recommendations.

Define the backend authorization and identity binding for management negotiated
through the Pi, including key ownership and replacement/revocation effects,
without adding a separate operator grant workflow. Define the trust basis
for automatic USB negotiation rather than assuming USB presence alone grants
management authority. Review any required Pico contract changes with the affected
producers and consumers under explicit companion-repository authority.

## Required work and acceptance

1. Qualify a maintained provider for the approved RFC 9382/P-256 exchange, including
   platform support, security evidence, licensing/provenance, upgrades, and seamless
   packaging. Any substantive contract change requires separate review.
2. Implement target-local temporary-code Pair/Replace/Remove UI, device-only key
   generation/storage, mutual confirmation, TLS 1.3 PSK-DHE, automatic reconnect,
   renewal/rotation, revocation, and crash/lost-reply recovery. Users never manage
   certificates, keys, fingerprints, or credential files.
3. Implement bounded authenticated remote-Pi adapters using the approved identity,
   authorization, profile/boot/revision, resource, routing, redirect, and replay
   contracts. Remote edits persist only on that target and never claim RF.
4. Finish deferred remote-Pi phase-3 editor/target-switching and phase-5 retirement
   integration through production authenticated paths. Keep all dependent paths
   unavailable until their applicable security and authority gates pass.
5. Test zero-local-output controllers and at least two independent targets:
   authentication failure, redaction, malformed/stale documents, revocation,
   ownership replacement with the old manager offline, reboot/reconnect, storage
   loss, concurrent operations, uncertain replies, bounded retries, proxy/redirect
   refusal, and no cross-target writes or authority changes. No discovery-based
   grant or silent trust reset is permitted.
6. Update owning contracts, shared consumers/assets, and operator/security docs
   concomitantly; run both complete current-source network and USB suites and the
   required production browser workflows. Repeat adversarial review until no
   actionable issue or required evidence gap remains. Record exact source pair,
   dirty changes, commands/results, and limits before declaring this gate passed.

Successful phase-2 local/Pico tests do not satisfy this milestone. Software
fixtures cannot qualify physical hardware, installation, services, timing, or RF.
