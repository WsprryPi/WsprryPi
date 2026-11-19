# Phase 6.2 execution prompt: automatic security and remote management

Status: saved implementation instructions, prepared 2026-11-16. Preparation and review do not execute the phase or qualify a provider. Execute this prompt only when the user separately authorizes phase 6.2 of issue #473. Stop before phase 7.

## Read first and establish authority

Read the repository and applicable nested AGENTS.md instructions, then:

- [Shared execution rules](execution-rules.md).
- [Authoritative implementation plan](../device-owned-solar-scheduling-plan.md).
- [Phase 6.2 security gate](deferred-pi-security.md).
- [Comprehensive 6.2 implementation plan](phase-6-2-implementation-plan.md).
- Approved [phase-1 decisions](phase-1-decisions.md), [pairing workflow](phase-1-pairing.md), and [normative contracts](phase-1-contract-details.md).
- [Researched D62-01–D62-05 recommendations](phase-6-2-decisions.md), distinguishing actual approvals from proposals. Approved contracts take precedence until an explicitly accepted amendment is recorded.
- Accepted phases 1–5 and [phase 6.1](06-device-recurrence.md) evidence, checked against the intended current sources, not merely their checklist marks.

Inspect each repository's branch, full working tree, staged/unstaged changes, component ownership, and applicable instructions. Run `git status --short --branch`, `git diff --check`, and `git diff --cached --check`. Record actual revisions and preserve all existing changes, including planning edits and unrelated user files. Use feature branches; do not reset, stash, overwrite, synchronize other checkouts, or silently substitute a different companion source.

Missing phase-6.1 acceptance blocks dependent 6.2 implementation. Report the exact prerequisite and continue only independently authorized inspection/planning; do not begin 6.1 as a workaround. Verify authorized concomitant WsprryPi-Docs scope and follow its instructions. Required Pico changes need explicit phase-6.2 companion authority; earlier phase-specific permission is insufficient. Identify any necessary reusable-component change and obtain its separate scope approval before modifying it. Do not describe an incomplete source pair as delivered.

Do not stage, commit, push, amend, rebase, merge, create PRs, publish, or post external messages unless separately requested for this execution. This prompt provides no live-device, installation, service, reboot, GPIO/I2C, transmission, RF, or mutating-sudo authority. Software fixture actions are separate from those operations. Do not start phase 7 or unrelated cleanup/refactoring.

## Required outcome and scope

Deliver the approved automatic Pi pairing and lifecycle, authenticated remote Pi management, deferred existing target editors, and verified remote-authority handover/rollback. Users manage no certificates, keys, fingerprints, credential paths, imports, or downloads. Target-local Pair/Replace/Remove/Pair again are ordinary UI workflows. Management replacement must not require the old manager online and is independent of scheduling retirement.

The user's attached-Pico requirement is settled: **when managing a Pi, use that existing authenticated connection to automatically discover attached Picos and negotiate their supported management contexts through the Pi.** Do not add a separate delegation switch, default-off grant, per-Pico child-scope approval, second pairing prompt, or operator credential provisioning. Validate the Pi and Pico identities, USB physical/session binding, current authorization, resource capability, and safety. Negotiation is not a settings save, backend/output change, schedule enable, WTP claim, or job dispatch. Present verified contexts through the existing Device workflow; do not equate discovery alone with authority.

Preserve WTP bytes, transport/authentication, claim/admission rules, existing Pico authentication/TLS, and phase-6.1 recurrence semantics. Do not build a parallel scheduler, controller-driven remote solar dispatch, new Pico scheduling, new human login, Internet management, multiple simultaneous managers, or phase-7 Scheduling UI. Pairing never changes Transmit, boot preference, output ownership, settings, schedules, or accepted work. Explicit Stop and migration/rollback retain only their approved runtime effects and checks; access revocation is not an RF stop.

## Decision gates: do not manufacture approval

Resolve protocol/workflow/persistence ambiguities with concrete proposals before affected implementation. Continue independent authorized work while awaiting answers. Record only approvals actually supplied; preparing or invoking this prompt does not approve every research recommendation.

| Decision | Execution rule |
| --- | --- |
| D62-01: provider/profile | Current approved exchange remains RFC 9382 SPAKE2/P-256 unless its amendment is approved. Botan 3.14.1 as PAKE-only with RFC 9383 SPAKE2+ is a candidate proposal, not a qualified provider or drop-in substitute. Apply its exact amended profile only after approval and coordinated contract updates. Retain OpenSSL/Mbed TLS for their existing TLS roles. BoringSSL stays held behind its gates and separate amendment authority. |
| D62-02: Pico dispositions | Automatic management negotiation through a managed Pi is required without an extra operator grant. Direct-USB details and the recommendation to retain existing network Pico login instead of new IP-code PAKE remain proposed until explicitly resolved. Do not silently exclude a requested flow or add a new Pico authentication protocol. |
| D62-03: revision identity | Resolve the old-store-ETag wording versus phase-5 assignment digest and context ETag before machine retirement integration. Never reinterpret retained proofs or confuse controller-book and target-authority generations. |
| D62-04: remote rollback | Resolve the resume command, authentication, durable disabled-target proof, and retained-fence semantics before implementing resume. Current-pair, one-use grants are proposed; retirement-only credentials must not gain resume privilege by implication. |
| D62-05: authority history | Resolve target-owned registration, completeness, peer reachability, and any additional retirement-only TLS purpose. An unknown old authority stays pending. A proposed new ALPN/key purpose is not already approved. |

A rejected/unavailable provider does not permit homemade PAKE, manual credentials, insecure fallback, a second hidden protocol view, or a completion claim. Present qualification evidence and seek disposition under the approved omission/redesign rule; do not unilaterally waive a gate or remove the entire feature.

## Implementation sequence and acceptance per slice

Follow the implementation plan's ordered work packages. For each slice identify its owning files/interfaces, approved behavior, failure states, tests, and exit criteria before edits. Keep unqualified production paths unavailable while testing the actual implementation through isolated typed fixtures.

### 1. Provider, platform, and packaging qualification

Qualify the exact approved PAKE/profile using maintained upstream sources, official vectors, provenance/licenses/notices, reviewable security evidence, advisory/release history, and a documented dependency-update process. Correct vectors alone do not establish security or maintenance qualification.

Demonstrate ordinary builds and packaged runtime loading across the existing supported Linux architecture/toolchain matrix and production macOS roles, plus a routine upstream patch-update rehearsal. Use existing recipes, explicit pins, and isolated package roots; no operator dependency repair or new manual files. Measure memory/size/deadline fit. Reject a candidate that fails required security, regular maintenance, or seamless packaging. Do not replace existing TLS stacks, import a different PAKE suite, or fork cryptographic arithmetic to bypass failure. Check supported roles/carriers separately per host. Managing a remote Pi from a Mac does not require inventing a new Mac physical USB carrier. Do not silently drop an approved role or expand platform/carrier scope to resolve a genuine gap; report it for narrow disposition before the affected implementation.

### 2. Coordinated contracts, principals, and durable ownership

Inspect producers and every affected consumer before changing interfaces. Materialize approved missing pairing/management/retirement/gateway shapes in their owning schemas; update descriptors, advertisements, generators/assets, strict C++/JS validators, serializers, runtime consumption, and paired adapters together. Keep Control/1 major/identifier/namespace at 1 before its reviewed stable-release transition. Keep WTP, pairing, carrier, and storage formats independently owned; no version bump merely because implementation begins, hidden downgrade, or historical control view.

Use typed browser/local/machine/retirement principals, not a fabricated local boolean or cookie to authorize a machine request. Bind pair, peer/self IDs, current generation/epoch, role, resource, and operation. Validate closed shape, UTF-8/encoding, bounds, identity, boot, profile/connection/capability revision, and unsupported versions/carriers before access, secret work, or writes.

Reuse the existing Pi transaction and retirement owners. Document lock order and revocation-versus-commit linearization; hold no admission/configuration lock across DNS, network waits, or PAKE derivation. Recheck current authorization under the mutation commit lock. Principal-scoped exact replay must not create another write or grant access after revocation.

Establish authoritative pairing history; absence/corruption after initialization is recovery, never an unpaired blank slate. Use private 0700/0600 bounded no-follow storage, file/directory sync, and crash-consistent complete decisions. Preserve existing WAL meanings/version 1 and six-image bound. Account for old/pending secret-bearing images without deleting unresolved evidence. Preserve bounded 256 terminal/32 unresolved operation receipts and at most 32 retained revoked recovery records; unresolved records are not evictable and capacity holds safely. Pairing recovery must not erase recurrence authority. Pairing-only faults hold management, not autonomous recurrence; existing scheduling-storage safety remains.

### 3. Pairing, TLS, and automatic lifecycle

Implement the exact currently approved exchange, code alphabet/normalization, transcript, password mapping, domain-separated derivation, confirmations, and operation shapes. Do not copy an incompatible provider's defaults or skip peer confirmation. Preserve the eight-symbol/40-bit code, 120-second monotonic window, five-failure budget, one reserved attempt/target, 30-second exchange, 4096-byte/ depth-8 messages, and serialized 32-MiB scrypt derivation plus bounded overhead, unless a specific changed bound is separately approved. Do not conflate exchange and normal-management deadlines. Reads/reconnects never reset budgets/open windows.

Show the code only to its originating target-local browser session. Send a code entered on the controller only to that controller's protected backend; no public bootstrap code field, persistent browser storage, URL, logs, or support-bundle leak. Stage the controller's private pending pair before target commit. Target commit is the authorization linearization point. Show Paired only after authenticated durable target readback; unknown/cancelled browser results cannot restore an old manager or undo a committed replacement.

Use the approved separate Pi TLS listener/client and exact PSK identity, ALPN, TLS 1.3 PSK-DHE, fresh P-256, cipher, Finished, authorization/epoch, no-0-RTT and resumption restrictions. Public bootstrap cannot become an HTTP management fallback. Integrate startup/recovery/bounded shutdown in transmitter and controller-only Linux/macOS roles without initializing hardware for a zero-output controller. No silent port, identity, carrier, or credential fallback. Management listener/provider/port failures must leave the new management path unavailable without changing transmitter role, overwriting configuration, or stopping otherwise valid autonomous recurrence. Preserve distinct existing configuration/output-recovery holds; do not mask their failures as pairing errors.

Implement automatic reconnect and at-most-two-authorized-epoch renewal after 30 days accumulated monotonic service time or 2^20 authenticated connections, whichever comes first; include unclean-restart renewal before mutation, exact lost-reply recovery, durable Remove/Replace revocation, and Pair again storage recovery. Retired/previous epochs authorize only their exact approved recovery scopes. Offline time must not require credential maintenance. Manager replacement closes old sessions/fences old writes without changing accepted jobs or recurrence.

### 4. Remote Pi and attached-Pico management

Route local Pi requests to their owner and remote requests only through the exact authenticated target adapter. A remote ID must never fall into host settings, Stop, initialization, receipts, or recovery. Keep all browser LAN/loopback, Host, Origin, proxy-client, request-intent, CSRF/session, and safety checks. New remote management is unavailable under the saved insecure-disabled policy; preserve the independently protected local recovery exception.

Apply approved private-address/DNS/self/metadata/redirect/proxy refusal, pin the resolved address per connection, and allow no arbitrary URL/path/device selectors. Enforce DNS 3 s, connect 3 s, handshake 10 s, I/O 8 s, and 24 s whole normal operation deadlines; one mutation per target; four concurrent remote connections per controller and four live target connections including handshakes; 64 profiles; 32 unresolved operations; 10 requests/s/client with burst 20; observations at most 1 Hz. The connection budget is shared across any approved management and retirement purposes, not multiplied per purpose or attached Pico. Bound queues and response sizes; trickled I/O cannot renew an absolute deadline. Preserve original operation bindings on uncertainty; read back before an explicit exact user-operation retry, never blind mutation retry.

Keep approved pairing/rotation recovery automatic: after authenticated readback, the protocol state machine may continue only its exact durably prepared operation under the approved context/epoch rules. It cannot create a new operation ID, rebase an unknown request, or treat unknown as permission to repeat an effect. Do not require an operator to repair keys or manually finish normal rotation.

For Picos attached to a managed Pi, automatically negotiate through the existing Pi connection as required above. The Pi owns physical USB access and backend context construction; remote input cannot select an internal USB authority token or arbitrary device path. Preserve exact parent/child identity, boot, profile, connection, authorization, and resource bindings. Reuse approved typed resource methods/payloads and real existing Pico handlers; negotiate actual capabilities. No raw tunnel, new WTP dispatch path, or unapproved firmware-management scope.

Do not insert an Add/enroll/grant click as a prerequisite for this automatic attached-Pico negotiation. Maintain any necessary private identity/session/ recovery metadata through its owning transaction without silently saving operator settings, enabling schedules, switching output/carrier, or replacing Pico owner credentials. Existing public workflow guards remain separate from this trusted backend negotiation; direct network callers cannot select the USB principal.

Normal Pi replacement automatically negotiates fresh attached-Pico contexts for the new manager, with no second approval step. Revocation closes new/queued access through the old Pi connection; recheck immediately before dispatch. A request already handed to USB can have committed: preserve its evidence and recover by readback rather than pretending revocation undid it. Hotplug/reboot invalidates contexts, never automatically replays mutations. Do not claim Pi revocation also revokes independently held Pico network/USB access or rotates Pico passwords.

Expose supported existing editors through the shared Device workflow, preserving per-target drafts and frozen accepted-write recovery across switching/restarts. Do not add phase-7 Scheduling UI or persist a preview/selection/negotiation.

### 5. Authenticated authority inventory, handover, and rollback

Implement only the approved disposition of D62-03–D62-05. Preserve phase-5 assignment IDs, immutable intent, durable dispatch fences, accepted complete-job draining, exact receipts, and original uncertainty. Inventory comes from verified target/controller history, not discovery, pairing replacement, an empty catalog, a caller's claimed controller list, elapsed time, or a browser completeness checkbox. Unknown/unreachable/unverifiable old authorities keep migration pending; normal management replacement may still succeed independently.

Authenticate retirement against exact peer/pair/generation, target, assignment, revision, and transition. Retirement-only credentials authorize retirement/recovery only. A separately proposed TLS purpose/ALPN must be explicitly approved and isolated from management/Stop/rotation/registration/resume; never retain revoked normal keys or add a plaintext callback as a shortcut. Use the same resolver, resource, and deadline safeguards for peer callbacks.

Do not confuse a pre-publication enqueue fence with completed durable retirement. Stage target settings disabled only after verified retirement and job reconciliation, then verify persistence. Preserve all missing/corrupt-history and direct API/ startup/reload enablement fences. Do not add registration requirements to ordinary WTP/manual work or promise permanent exclusion of a fresh Plain-LAN claimant.

Rollback must bind an explicitly selected old assignment, a verified disabled candidate/receipt, and reconciled output. If a resume grant is approved, persist the target hold before releasing it and keep both native and target-local legacy recurrence closed while remote resume is pending. A generic legacy/rollback state must not revive the Pi's own old scheduler. Bind proofs to an already committed disabled snapshot, not a self-referential grant/publication digest. Publish old controller permission only after its complete durable decision/readback; retain tombstones and unresolved receipts. Unknown delivery/reboot/revocation cannot reset the target hold. Returning to native requires fresh retirement of any resumed generation, not reuse of an earlier retired receipt. No generic clear-fence API.

## Required validation and documentation

Read `docs/simulated-backend.md`. Use isolated processes/private journals, explicit `--backend simulated` for real application fixtures, and disabled hardware access. Inspect every test target before running it. Add meaningful coverage through the actual new Pi PAKE/TLS client/listener, parser, authorization, store/WAL, and runtime; existing Pico carrier tests alone do not prove Pi pairing works.

Use typed fixture-only resolver/clock/fault seams for isolated socket/endpoint tests. Do not relax production private-address/self/loopback policy, add production bypass controls, or operate live LAN/USB devices to make fixtures pass. Keep actual production crypto, framing, handlers, and journals in the acceptance path.

At minimum cover a controller with no local output, two independent targets, Picos attached to a managed Pi through software USB endpoint fixtures, and an old manager. Test official/profile crypto vectors, malformed/tampered exchanges, guessing/rate/capacity/deadline exhaustion, secret redaction, wrong bindings, SSRF/rebinding/redirect/proxy rejection, and no writes/contact on applicable refusals. Inject fresh-process crashes and lost replies before/after every durable boundary; cover replacement racing writes/rotation, old-manager-offline replacement, storage loss/corruption/fullness, reboot/hotplug, renewal and key-purpose isolation.

Exercise actual protected browser Pair/Replace/Remove/Pair again, target switching, late results, and draft retention. Demonstrate attached-Pico negotiation uses the managed Pi connection without a delegation switch/second pairing step, cannot cross devices, and cannot mutate from stale parent/child context. Test revocation before dispatch versus already accepted USB work, and independent Pico access. Exercise the whole controller -> paired Pi TLS -> production Pico USB management client -> intended candidate `usb_management_driver` -> actual Pico handler chain, including framing, delivery ACK, identity/revision checks, and journals. A mocked gateway response or separately green endpoint test cannot replace that evidence.

Exercise actual authenticated multi-process retirement/rollback, unknown inventory, restored/recreated assignments, stale receipts/generations, lost resume acknowledgement, reboot during holds, and direct enable bypass. Verify target-local recurrence stays closed during remote rollback and cannot overlap the selected old controller. Regress unchanged WTP claims, manual/one-shot/Test Tone permissions, and accepted work. Verify phase-6.1 autonomous recurrence through browser/controller loss, management revocation, and offline renewal without changing its semantics. Include management-port/provider startup failure while valid recurrence continues and verify no silent role switch or saved-configuration replacement.

Run both complete checked-in Debian workflow recipes with the actual intended Pi/Pico sources and explicitly pinned dependencies:

- `wtp-network-loopback`, including `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`.
- `wtp-usb-management-pair`, including building the actual Pico `usb_management_driver`, `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and `WsprryPi-UI/tests/wtp_usb_pair_test.js`, plus every other required recipe stage.

Run applicable focused management/Fleet/storage/recovery and new security tests, UI npm/source regressions, production Mac role fixtures, and host-appropriate semantics from `src`. On Linux with libgpiod C++ headers use `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. On macOS or without those headers use `make semantics-test-portable SUDO=`; report its intentional subset, never as full semantics. Run standalone component tests where changed. No skipped, failed, cancelled, timed-out, stale-source, or mock-only required carrier result satisfies acceptance. Pin/hash the final tested pair and generated provenance; rerun affected gates after fixes and explain any metadata-only difference.

Use Impeccable before UI implementation; preserve `WsprryPi-UI/DESIGN.md` patterns. Review actual desktop/mobile, light/dark, keyboard/focus, disabled/loading/error/ expired/revoked/unknown states and near-action feedback. Review rendered UI docs. Update owning engineering contracts, dependency notices/build/package guidance, redaction/support-bundle rules, and authorized operator Setup/Transmitter/ Maintenance/API/troubleshooting guidance with implemented behavior only. Build Docs using its activated `docs/.venv` and documented Sphinx command; inspect warnings, links, screenshots, and renders. Use isolated packaging fixtures, not a live installer/service operation. No placeholder or promised follow-up substitutes for required documentation or companion integration.

## Assessment loop, phase indicator, and final report

Update the phase indicator to in progress only when authorized execution actually starts with prerequisites satisfied; preparing this prompt does not start 6.2. Before acceptance, assess the complete actual diff and all owning boundaries against this prompt, approved contracts, plan, and requirement-to-evidence map. List concrete defects and missing evidence. Fix authorized issues, rerun affected checks, then perform another full assessment. Repeat until no known actionable issue or required evidence gap remains. Do not turn a failed gate into a waiver.

Mark 6.2 complete only after its entire applicable gate passes for the intended source pair, including provider qualification, automatically negotiated attached Picos, all approved remote editor/handover obligations, required carrier/browser/ platform/packaging tests, and documentation. Overall phase 6 requires both 6.1 and 6.2; stop before phase 7. If blocked, identify the exact next unfinished action and approval/dependency/evidence needed; do not claim completion.

Report implemented versus proposed/blocked behavior, decision dispositions, provider/security/maintenance/packaging evidence, important files and changed components, exact revisions and tested dirty changes, commands/results and relevant CI links, UI/Impeccable workflows and captures, Documentation Impact (updated/considered unchanged/still required), unrun checks and why, and every repository's branch/working-tree/Git delivery state. State whether anything was committed/pushed and only do so when separately requested. Software fixtures do not qualify physical USB, GPIO timing, RF, installation, or services.

## Prompt-preparation assessment record

This records review of these instructions on 2026-11-16, not execution evidence.

1. First adversarial assessment checked scope/authority, approval precedence, automatic attached-Pico intent, recovery, resource bounds, and platform limits. Fixed retry wording that could require manual completion of normal rotation; distinguished internal session/recovery metadata from operator settings; prohibited an extra Add/enroll/grant step; made limits and per-host carrier boundaries explicit.
2. Full reassessment checked every required work path, failure transition, production boundary, and acceptance gate. Added management startup-failure isolation from valid recurrence, protected fixture-policy boundaries, and mandatory end-to-end Pi TLS/gateway/Pico USB handler evidence.
3. Final full reassessment checked the corrected prompt against the approved contracts, implementation plan, proposed decision register, user clarification, phase prerequisites, repository/component authority, and reporting gates. No further known actionable prompt issues remained. Pending decisions and provider/implementation evidence are still execution gates, not waived findings.

Preparation checks: local document links resolve and `git diff --check` passes. Only this saved prompt changed in this preparation task; existing planning and unrelated user changes were preserved. No implementation, dependency, UI, sibling repository, or phase-indicator changes, application tests, hardware operations, staging, commits, or pushes occurred. Operator documentation was considered but unchanged because no product behavior changed; its required implementation-time updates and validation are specified above.
