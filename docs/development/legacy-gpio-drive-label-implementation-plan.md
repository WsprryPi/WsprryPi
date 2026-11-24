# Drive-current mappings across WsprryPi and WsprryPico: implementation plan

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478). Execution instructions: [implementation prompt](legacy-gpio-drive-label-implementation-prompt.md).

## Objective and authorized scope

Verify and correct nominal RF-output drive-current selection throughout WsprryPi and WsprryPico, including direct GPIO, Si5351, RP1, necessary backend/provider changes, both local and companion interfaces, persistence, and documentation. This supersedes the earlier UI-only scope. An already-correct mapping requires verification and suitable regression evidence, not gratuitous code changes.

This revision updates the local planning documents only; it does not publish an issue update. Actual implementation begins when the implementation prompt is invoked. That implementation includes coordinated edits in WsprryPi, WsprryPico, and the operator documentation repository. A demonstrably necessary RP1 provider correction is included, subject to inspecting its owning repository and instructions. Keep each repository's branch, working tree, validation, and Git boundary separate. Do not infer permission for implementation, commits in companion repositories, deployment, firmware loading, services, GPIO operation, or RF tests from publication of this plan.

## Required nominal mappings

| Output/platform | Legal nominal drive currents |
| --- | --- |
| Si5351 in WsprryPi and WsprryPico | 2, 4, 6, or 8 mA |
| Pi 1, Pi 2, Pi 3, Zero, and Zero 2 W direct GPIO | 2, 4, 6, 8, 10, 12, 14, or 16 mA |
| Pi 4, Pi 400, and CM4 direct GPIO | 1, 2, 3, 4, 5, 6, 7, or 8 mA |
| Pi 5 and other supported RP1 platforms | 2, 4, 8, or 12 mA |
| WsprryPico direct RP2350 GPIO | 2, 4, 8, or 12 mA |

These are nominal drive-current settings, not frequencies, calibrated RF power, measured pad current, or WSPR reported-power metadata. RF amplitude and power depend on frequency, output circuit, and load. Do not derive new dBm estimates from nominal current.

## Delivered Pico producer and remaining Pi integration — 2026-11-23 handoff

The Pico finite Test Tone producer is complete independently at `37b90bc069a400f0b1e05b19c7933d4eeed95161`, committed and pushed on `codex/issue-478-pico-drive-current`. The delivery handoff records [PR #30](https://github.com/WsprryPi/WsprryPico/pull/30) as open and draft, not merged to `devel`. The local Pico checkout inspected for this revision matches that commit, and the inspected contract and implementation files have no changes relative to it. Do not recreate the producer, infer a shared merge, or treat an installed image or side branch as `devel` delivery. No WsprryPi modification, version increment, or release publication was part of the Pico delivery.

The authoritative delivered contracts are [Finite Test Tone](https://github.com/WsprryPi/WsprryPico/blob/37b90bc069a400f0b1e05b19c7933d4eeed95161/engineering/protocol/Native-Execution.md#authenticated-finite-test-tone), the [current closed Control/1 schema](https://github.com/WsprryPi/WsprryPico/blob/37b90bc069a400f0b1e05b19c7933d4eeed95161/engineering/protocol/control-1.schema.json), and [GPIO drive save/restart/readback](https://github.com/WsprryPi/WsprryPico/blob/37b90bc069a400f0b1e05b19c7933d4eeed95161/engineering/development/gpio-drive.md). These supersede the previous internal-only `eb36e49ad498-dirty` readiness baseline and the instruction to implement its missing public lifecycle.

### Completed producer behavior to consume and preserve

Authenticated Control/1 implements finite Review, Start, identified Status, and exact-job Stop through native execution, ownership, frequency/correction/filter, hardware, and clock admission. Engineering HTTPS, authenticated Consumer-Radio, and local USB reach the same coordinator under their existing authority rules. Public observer reads and plaintext consumer HTTP grant no Test Tone authority. The optional `test_tone` capability's availability is not Start admission. The Pi gateway route subset was intentionally not expanded; advertising the capability grants no Pi proxy, UI, or remote execution feature.

| Operation | Delivered resource | Binding |
| --- | --- | --- |
| Review | `POST /api/control/v1/test-tone/review` | Authenticated current device/boot; pure assessment, no write precondition |
| Start | `POST /api/control/v1/test-tone/start` | Review's current `assessment_revision` in `If-Match`; unique request ID and expected boot drive |
| Status | `GET /api/control/v1/test-tone/status` | Explicit device/boot and exactly one request-ID or job-ID query selector; same authenticated authority generation |
| Stop | `POST /api/control/v1/test-tone/stop` | Exact job and its `control_revision` in `If-Match` |

Use exact closed requests and responses from the delivered schema. Status IDs are 32 lowercase hexadecimal characters; duplicate/unknown query keys, percent encoding, alternate target spellings, and ambiguous selectors refuse. Review projects post-restart drives with ready, restart-required, and specific refusal results without writes, reservation, hardware preparation, or GPIO programming. Absolute integer-Hz carriers receive no WSPR offset or dial substitution; 5,366,200 Hz remains exactly that nominal carrier. Durations are 1–120,000 whole milliseconds, further bounded by native/direct-engine limits and advertised as the effective `maximum_duration_ms`; invalid durations refuse rather than clamp. Native RF timing ends independently of polling, sessions, or ACK delivery, including beyond the existing 60-second native lease.

Receipts are volatile and boot-scoped: up to eight settled receipts for up to one hour, with earlier capacity eviction and active/unresolved-fault work pinned separately. Admission consumes a nonreused boot-local sequence without a journal write; retained identical replay returns the original job, while changed content or original revision conflicts. Missing receipts are unavailable evidence, not proof that nothing ran. Never automatically retry Start after a lost reply; reconcile the identified request/job. Consumer reconciliation binds to stable profile/credential generation; engineering, field, and USB authority scopes remain distinct. Exact Stop uses job-specific revision and native identity, cannot stop a successor, and reports inactive output only on native confirmation. Unresolved shutdown retains fault/recovery inhibition and blocks Start beyond receipt/lease expiry; no Test Tone operation clears it.

Retain legal drive values 2/4/8/12 mA and the 12 mA new-installation default. Missing stored `gpio_drive_ma` retains its defined 4 mA decoding/serialization behavior. Saving and explicit restart/readback remain unchanged. Saved drive can be null when no complete configuration exists; immutable boot drive remains a separate validated selection. Job-attributable SDK readback is copied per execution epoch with its pin, initialization stage, and monotonic timestamp. Global cached pad readback can be null before execution and is not a substitute for job evidence or measured electrical current. Numerical NCO realization/quantization bounds do not qualify RF frequency or power.

Drive save still requires complete ordinary configuration, including valid station/profile fields for the selected path, healthy storage, and valid hardware settings. An otherwise unconfigured device may independently admit a tone on its valid existing boot drive when native rules pass; do not impose drive-save prerequisites on tone admission or invent a partial-configuration workaround. Expected Start drive is an assertion, not per-tone programming. Both native schedule types must be saved disabled; no automatic schedule changes are permitted. HP 437B Automation is the client project. Generic upload, scheduling, per-tone drive override, Si5351 changes, a new UI, and an HP adapter were not added by this producer.

### Delivered evidence and its limits

Both final Pico CI runs passed: [PR verification](https://github.com/WsprryPi/WsprryPico/actions/runs/45301000199) and [exact-source verification](https://github.com/WsprryPi/WsprryPico/actions/runs/45301028647). The handoff records 243 tests, zero failures, and zero skips in the exact-source audit, and passing Read the Docs. The CI-only adapter fixture link failure was fixed before these final runs. These establish delivered Pico software evidence; they do not complete future Pi integration or physical qualification.

| Installation observation from the handoff | Recorded value |
| --- | --- |
| Host / USB serial | `wpicoc.local` / `1545B6EFBF78028C` |
| Device / observed postboot ID | `c2a3a2a314c948b50a8562a1aeb69669` / `6523e3fa0ed6ada06ad20d1ef6052562` |
| Installed UF2 SHA-256 | `924d5240b68142fafe481895cff99a65f12df36766477f36d8e6785ec619d28d` |
| Version / bundle provenance | `0.0.0-devel`; untracked prompt documents mark the bundle dirty, but implementation sources match the pushed commit |
| Installer result | `ok=true`, `stage=complete`, `recovery_required=false`; image verified and reserved flash/public state preserved |
| Local retained bundle | `/Users/lbussy/GitHub/WsprryPico/build/test-tone/ci-37b90bc0-bundle` |
| USB-host retained bundle | `/home/pi/.local/share/wsprrypico-deploy/test-tone-control-20261123/ci-37b90bc0-bundle` |
| Postboot state | Saved drive null; boot-active drive 12 mA; global SDK pad readback null before execution; output inactive and unowned |

A transient post-enumeration serial permission error was resolved using the exact installer recovery transaction. USB and network postboot identities agree. Retain this bundle as the installed-image reference for a later upgrade, and rediscover the live boot before any separately authorized operation. These are recorded observations, not fresh live-device checks in this planning task.

The 52 authenticated dry Reviews across 13 representative carriers and four drives yielded 13 ready and 39 requires_restart, with configuration and revision unchanged. The carriers are fixtures, not the approved HP campaign list. No Start or RF transmission was performed. Installation/non-transmitting evidence is separate from producer software results and physical RF/HP measurement qualification; none establishes measured pad current, frequency accuracy, timing, or power.

### Remaining explicitly bounded Pi slice and acceptance

This request authorizes only these two Pi planning documents. It does not invoke implementation, authorize Pico edits, merge either project, synchronize checkouts, commit/push, deploy, or operate hardware. The unrelated drive-mapping scope and its future implementation/documentation gates below remain intact. Future Pi integration must be separately invoked as a bounded companion-consumption slice; it must not reopen completed producer work or add Test Tone execution routes/UI to the Pi. Sections 1–7 retain the broader drive-mapping plan; for this bounded slice, apply only affected-consumer, test, and documentation checks. Its acceptance criteria above govern without requiring unrelated matrix corrections, new UI work, sibling edits, or repeated producer implementation.

1. Record the intended source pair and availability. At this planning inspection Pi is `devel` at `d8ce5b2ef073caaf8b44048d8ce24ee1f9982243` with extensive preexisting local changes; Pico is the delivered commit above on its feature branch. Record the exact tested dirty inputs in a future run, inspect branch/merge availability then, and select candidate refs and pinned dependencies explicitly. Do not merge Pico as part of planning or silently replace it with older `devel` sources.
2. Synchronize canonical metadata only when implementation is authorized. Pi `src/wtp_integration/contract/PROVENANCE.json` currently names Pico base `7c09ff0fb15bb421f9010fb6faa6868f61b7a1a4`; its vendored schema has no Test Tone definitions. Its file SHA-256 is `09f561a31a46eceb961b9dd495f98091a4ff18e71707c532004a42956f5472e1`, whereas delivered Pico schema bytes hash to `78de9018d776269d97d0754ed6d034d71746877eeb6fa861768a9026445066d7`. These are file hashes, not substitutes for canonical schema digests. Inspect `scripts/generate_control_contract.py`, `src/wtp_integration/contract/`, generated `src/wtp_integration/control_contract.hpp`, and `WsprryPi-UI/data/control-contract.js`, including all generator-owned inputs/assets, provenance, and policy/digest checks before generating changes. Preserve unrelated edits; do not hand-patch hashes or blindly copy a checkout.
3. Inspect and update strict descriptor/negotiation validators, route/query authorities, shape validation, parsing/serialization, runtime consumption, and paired adapters only as actually affected. In particular inspect `src/wtp_integration/controller_contract.cpp`, companion network/USB consumers, and applicable UI validators/tests. Understand the new descriptor without expanding `pi_management` or `pi_consumer_session` route authority. Reject malformed documents/descriptors, wrong units/limits/types, extra/missing fields, unsupported versions/carriers, and stale identity/boot/revision/profile bindings before writes. Keep nullable saved drive and per-job SDK attribution intact.
4. Keep major 1, `Wsprry-Control/1`, `/api/control/v1/`, `urn:wsprry:control:1`, existing resource/version identifiers, storage formats, and authentication formats fixed. Preserve one current closed schema with no historical views, downgrade fallback, or relaxed validation. Regenerate required definitions/digests through checked-in recipes; do not treat their refresh as a version increment or stable transition.
5. Run affected schema/policy/generator/provenance, consumer, runtime, and adapter regressions plus both actual production network and USB source-pair suites in section 4. Cover capability presence/absence and availability versus admission, unchanged Pi gateway denial (including bounded Status-query non-forwarding), malformed/current-contract negative cases, nullable drive and exact numeric parsing, and production producer lifecycle/reconciliation/evidence through appropriate direct fixtures. Do not grant gateway execution merely to exercise a fixture. Record exact commands/results, candidate revisions and dirty inputs, CI links, and source-selection/environment limitations. Pico's 243-test result, deployment, and 52 dry Reviews do not replace this future Pi gate.
6. Update affected Pi engineering/contract documentation and tests as part of that future authorized slice; review operator-documentation impact and obtain separate scope for any sibling edits. Acceptance requires consistent canonical/vendor/provenance/digests/generated definitions, strict affected consumers, unchanged gateway authority, passing affected tests and both suites for the intended pair, and an explicit branch-delivery report. Required skipped/failed/timed-out/unavailable checks remain Pi integration blockers without invalidating the independently delivered producer.

HP 437B Automation adapter delivery and its physical campaign remain separate work. The public Pico contract is now delivered; the remaining Pi dependency is bounded metadata/consumer integration and its source-pair validation, not producer recreation. No HP adapter, instrument operation, or 52-point physical sweep is authorized here.

## 1. Establish current evidence and ownership

Read applicable instructions, including WsprryPico's README, CONTRACT, and architecture before implementation and the operator documentation repository's instructions. Inspect branches, revisions, staged and unstaged changes, untracked files, nested instructions, affected component ownership, and whitespace errors in every affected repository. Preserve all existing work. Historical baselines are WsprryPi `devel` at `51257fdb` before the planning commit and WsprryPico `codex/issue-473-device-owned-scheduling` at `05cc90d6`; neither is an instruction to check out stale sources.

For each matrix row, record the advertised domain, UI options, configuration key and stored representation, defaults, parsing and validation, serialization, runtime consumption, register or SDK encoding, actual output selected, tests, and documentation. Include WsprryPico standalone management and WsprryPi's management of Pico over both network and USB. Inspect generated assets and strict companion validators, not only handwritten code.

Verify Pi generation detection using actual model strings and processor fallback, including Pi 400, CM4, Zero, Zero 2 W, and supported RP1 models. Unknown or malformed identity must not display a guessed current or grant hardware capability. Do not add a second model parser in JavaScript to mask producer defects.

Verify register encodings, nominal-current units, and relevant board/SoC differences against primary device documentation and the pinned SDK/provider definitions. Record the source and revision used; UI tables and tests copied from the same implementation are not independent hardware-contract evidence. Report a conflict with the requested matrix before choosing an incompatible interpretation.

The earlier review found a fixed 2–16 mA legacy UI label table, correct-looking Si5351 and RP1 encodings, and correct-looking Pico direct GPIO encoding. These are leads, not proof of complete end-to-end correctness. WsprryPico's ordinary qualified Si5351 profile was restricted to 8 mA; distinguish the legal encoding domain from ordinary executable profiles and engineering configurations. Do not silently remove qualification gates or claim all settings are ordinarily executable without evidence. If satisfying intended availability requires qualification changes or physical evidence, identify the exact remaining decision or evidence rather than excluding Pico or Si5351 from the work.

## 2. Correct each defective lifecycle at its owner

Retain valid saved representations, defaults, and selected settings unless a separately reviewed compatibility change is necessary. In particular, legacy `GPIO.Power Level` remains code 0–7: generations 1–3 map code `n` to `2 × (n + 1)` mA, while generation 4 maps it to `n + 1` mA. Code 7 therefore remains code 7 while displaying 16 mA on Pi 3 and 8 mA on Pi 4. Pi Si5351 levels 1–4 must reach 2/4/6/8 mA. Pico and RP1 mA fields must not be confused with those indices.

Fix defaults, parser/validator domains, internal types, serialization, persistence, runtime selection, backend register or SDK programming, provider admission, and output selection wherever necessary to satisfy the matrix. Reuse current contracts when sufficient. If metadata or an API/schema change is necessary, implement it with coordinated producers, consumers, descriptors, strict validation, generated assets, and tests; new fields are permitted when justified by this objective. Do not force a display-only solution when a backend correction is needed.

For reusable component changes, identify the defect, explain why the correction belongs there, preserve public boundaries and extraction potential, and run standalone and parent integration checks. The necessary mapping correction is authorized; unrelated refactoring, platform ports, dependency modernization, driver redesign, general cleanup, or unrelated RF work is not. Escalate material unresolved migrations, compatibility changes, or qualification-policy decisions before choosing new behavior.

## 3. Align both applications' interfaces

Use Impeccable during UI implementation. Correct the Pi-local, Pico standalone, and Pi-managed Pico controls wherever mismatched. Preserve established layout, separate backend control visibility, and existing explicit read/save/apply/restart semantics. Saved selection, boot-active setting, cached pad readback, and measured electrical behavior must remain distinct. Unreachable devices must not acquire an observed default.

Use accurate mA labels and matching accessible values; preserve native controls and stored semantics. Unknown identity displays a raw valid level with nominal drive unavailable, and invalid values do not produce fabricated currents. Clear stale labels after identity changes. Suppress legacy secondary dBm estimates on generation 4 and unknown identity; do not add current-derived estimates elsewhere. Preserve unrelated earlier-model presentation rather than recalibrating RF power.

Exercise initialization, delayed metadata, reload, reconnect, backend switching, target or boot changes, drafts, default reset, keyboard operation, and error recovery. Rendering must not add configuration mutations or overwrite drafts. Deliberate edits retain existing autosave or explicit-save policy. A lost mutation response must not cause a hidden retry, restart, or fallback. Do not broaden into a new management workflow.

## 4. Coordinate shared contracts and both carriers

Apply this gate to the future bounded Pi slice above. The delivered Pico Test Tone producer is complete independently; these remaining paired checks do not require recreating it. Preserve the delivered Pi gateway route subset. Advertising or parsing `test_tone` does not authorize a Pi proxy, UI, or remote execution feature.

For any capability, limits/units, descriptor, API/schema, or protocol-facing change, follow the current Pi/Pico completion gate in both repositories. Inspect every producer and affected companion consumer before editing. Evolve the single current control contract according to authoritative Pico `control-policy.json`, retaining prerelease major/namespace 1 until the explicit stable transition. Keep WTP, transport/authentication, and storage versions independent. Do not add historical views, permissive version ranges, validation relaxation, or hidden downgrade/carrier fallback.

Validate shape before access; reject missing/extra fields, wrong types, unsupported versions or values, inconsistent domains, stale identities, boots, revisions, and profiles before mutation. Coordinate generated assets, paired adapters, runtime serialization, and documentation together.

For this cross-product implementation, require both production-path hardware-free suites on the intended current source pair, even if a particular path proves unchanged. Use WsprryPi `.github/workflows/debian-non-hardware.yml` recipes for `wtp-network-loopback` and `wtp-usb-management-pair`. Network coverage includes `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`. USB coverage includes the actual Pico `usb_management_driver`, `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and `WsprryPi-UI/tests/wtp_usb_pair_test.js`. Record explicit candidate source paths/revisions and pinned dependencies. Retained fixture names do not authorize reintroducing retired WTP execution.

Mocks, standalone builds, old CI results, or stale companion sources do not replace these suites. Failed, skipped, cancelled, or timed-out checks remain unresolved. A companion mismatch that blocks the changed contract is part of the integration work. Distinguish local paired-source evidence from delivery on intended paired refs; do not claim release or branch delivery that has not occurred.

## 5. Validate the whole matrix

Cover every legal value in every row, invalid intermediate and boundary values, index-versus-mA confusion, defaults, saved-value round trips, reload, runtime/backend encoding, correct output register/pin, preservation of unrelated register bits where applicable, provider refusal, and error handling. Use independent expected mappings and fake register/SDK adapters or existing backend seams, not copied production formulas or real hardware. Include no-write assertions for rejected settings and presentation-only refreshes, and preserve meaningful backend safety and cleanup assertions.

Run affected component standalone tests, parent regression checks, Pico host tests, firmware build checks where affected, and both paired suites. Inspect launchers for safety. Use checked-in Makefile/CMake/test infrastructure rather than invented commands. On macOS use `make semantics-test-portable SUDO=` from Pi `src` when relevant; on Linux with libgpiod C++ headers use `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. Portable coverage is a subset, not evidence of full physical-backend semantics. Follow canonical Linux CI recipes when host tests cannot cover the changed backend.

For Pi UI coverage, run focused new tests, `node tests/conditional_transmit_gpio_integration_test.js`, `node tests/rp1_route_ui_test.js`, `node tests/native_host_ui_test.js`, and `npm run test:unit` from `WsprryPi-UI`, plus applicable real-page browser tests. Select Pico tests from its current engineering guide and registrations, including drive, Si5351, schema, and browser coverage. Register focused tests without overwriting existing package or build changes.

Render affected Pi and Pico pages at desktop/mobile widths in light/dark themes. Inspect known/unknown identities, every affected backend, disabled/pending/error states, and visible/accessibility agreement. Preserve screenshots and logs outside tracked source paths and record their locations. Do not use running appliances as fixture endpoints. Required dependency or environment blockers must be reported; do not change pins, weaken checks, or claim completion to avoid them. Physical operation requires separate explicit setup authorization.

## 6. Update documentation in the same implementation

Documentation is included, not deferred as an unauthorized sibling follow-up. Update affected operator pages in `/Users/lbussy/GitHub/WsprryPi-Docs` after verifying its location and instructions, plus relevant Pi and Pico engineering/contract documentation. Keep code edits in their owning repositories; treat the docs repository's rule against editing application files as that repository's boundary, not permission to omit separately authorized application work.

Start with operator `docs/User_Interface/Setup/Transmitter/index.md`, `docs/Advanced_Operations/rf_electrical.md`, the transmitter-backend configuration guide, RP1 pages, and Pico installation/drive/Si5351 pages. Review Pi `docs/wtp-gpio-drive.md` and Pico `engineering/development/gpio-drive.md`, Si5351 guides, descriptors, and contracts for stale defaults, units, supported domains, activation, or qualification statements. Update only affected content, and replace screenshots only when materially inaccurate. Document nominal current, stored-code semantics where useful, reported-power independence, saved versus active/readback states, and qualification limits accurately. Do not copy regression diaries into operator prose.

Keep paragraphs on one source line, preserve directives and tables, and use Oxford commas. Build operator docs with their documented virtual environment and `python -m sphinx -b html docs docs/_build/html`; use the Pico documentation repository's own documented checks. Review affected rendered documentation with Impeccable at desktop/mobile widths and verify links. Report warnings and failures. No publication or release is included.

## 7. Assess, fix, and report

Perform an adversarial assessment across the entire matrix, both products, backend encodings, consumers/carriers, persistence, identities, output safety, profiles, generated assets, tests, UI, docs, and repository boundaries. Fix in-scope findings and rerun affected checks, then perform another full assessment. Repeat until no actionable in-scope findings remain; required unavailable evidence remains a blocker even if source review is clear. Do not turn correct mappings into unnecessary edits merely to populate a diff.

Completion requires verified/corrected matrix rows with evidence, necessary backend and contract changes, passing affected standalone and parent checks and both paired suites, reviewed rendered interfaces, and built/reviewed updated documentation. Distinguish software validation from physical pad, timing, service, installation, and RF qualification. No physical qualification is claimed without its own evidence.

Report behavior and verified-unchanged paths, all modified repositories/components, source revisions and uncommitted changes tested, exact commands/results and CI links where applicable, visual evidence, Documentation Impact, remaining blockers, and branch/working-tree/Git state per repository. Preserve unrelated work and keep Git boundaries separate. The current task updates local planning artifacts only; it does not implement this work or publish an issue update.
