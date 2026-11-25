# Issue #478 Pi companion implementation prompt

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478). Read the [owning implementation plan](legacy-gpio-drive-label-implementation-plan.md) and current repository instructions before any future implementation.

## Task and authority

The next implementation task is the bounded Pi companion slice required for Pico PR #30. Pi 4/Pi 400/CM4 label correction, any Pi Test Tone UI or proxy, HP 437B adapter work, and wider drive-mapping work are separate slices. Begin the companion work only after explicit authorization. Preserve unrelated staged, unstaged, untracked, and ignored work. Implementation commits and pushes, PR creation, merges, sibling edits, deployment, and hardware tests require their own authorization.

## Delivered Pico source and contracts

Both Pi companion suites select the frozen, clean WsprryPico commit [`8df9ea0565d819083dd9d884aced954d45ea54f9`](https://github.com/WsprryPi/WsprryPico/commit/8df9ea0565d819083dd9d884aced954d45ea54f9). It was the verified head of the open draft Pico PR targeting `devel` from `codex/issue-478-pico-drive-current` on 2026-11-24. [Pico PR #30](https://github.com/WsprryPi/WsprryPico/pull/30)

The Pico handoff consolidates gray-line research into the standalone guide, removes the superseded plan, and updates qualification guidance for native execution following the retirement of external WTP jobs and for the current Si5351 profile. It retains prior test evidence, qualification limits, and recovery instructions. The Pico handoff reports four local prompts retired, a clean working tree, and passing documentation checks and six RF-calculation tests. Hardware qualification remains at the previously recorded scope. [Gray-line research](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/standalone.md#gray-line-research-and-measurement-limits) · [Native RF qualification](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/pico-rf-qualification.md)

The subsequent Pico documentation commits remove superseded instructions and task history. `aed2462f` corrects standalone configuration guidance and restores RF measurement safeguards. `8df9ea05` updates seven Markdown files, including the network guide's instructions for current authenticated management. The commits preserve evidence and recorded hashes, qualification limits, recovery instructions, and recovery tools. The Pico handoff reports passing checks for both commits. The Pico checkout was clean at the recheck. [Qualification and configuration update](https://github.com/WsprryPi/WsprryPico/commit/aed2462f9934dbe664d6541b294be79d404ce0f5) · [Network guidance update](https://github.com/WsprryPi/WsprryPico/commit/8df9ea0565d819083dd9d884aced954d45ea54f9)

Native Test Tone Review and Start admit the exact 144,490,500 Hz request on the ordinary `qrp-labs-rev6-25mhz-clk0` Si5351 profile with default experimental-frequency permissions. The exact-point rule leaves neighboring frequencies, other profiles, and other job sources under the general frequency policy. The historical conducted evidence at 144,490,500 Hz used 8 mA.

Read these exact-source contracts, not the deleted completed station-independent-drive execution prompt; do not link or recreate that prompt:

- [Station-independent GPIO drive](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/gpio-drive.md#station-independent-settings).
- [Authenticated finite Test Tone](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/protocol/Native-Execution.md#authenticated-finite-test-tone).
- [Current closed Control/1 schema](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/protocol/control-1.schema.json).
- [Drive without station setup](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/configuration-updates.md#drive-without-station-setup).
- [Saved progress](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/saved-progress.md).

The public producer implements POST `review`, POST `start`, GET identified `status`, and POST exact-job `stop` under `/api/control/v1/test-tone/` through authenticated Control/1 and native execution. Review is pure assessment and may project a post-restart drive without writes, reservations, or hardware preparation. Absolute integer-Hz carriers receive no WSPR offset or saved-dial substitution; 5,366,200 Hz means exactly that nominal carrier. Invalid durations refuse without clamping.

Ordinary Si5351 execution accepts nominal 2, 4, 6, and 8 mA under profile `qrp-labs-rev6-25mhz-clk0`. In the [current Control/1 schema](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/protocol/control-1.schema.json), `ordinary_profile.drive_ma` is `[2,4,6,8]`, replacing scalar `8`. Test Tone Review and Start require the boot-engine assertion. Si5351 supports 10–120,000 ms and reports fresh, execution-epoch-bound drive-register evidence in `synthesis_readback`, with null `pad_readback`. Direct GPIO retains 2, 4, 8, and 12 mA and a 1 ms minimum, with SDK `pad_readback` and null `synthesis_readback`. Native and engine limits can lower the advertised maximum. Si5351 finite execution supports exact-job Stop and confirms shutdown through register readback.

Start binds current device/boot, authenticated authority generation, Review's current `assessment_revision` through `If-Match`, unique request ID, and expected immutable boot drive. It retains native ownership, RF, clock, storage, schedule, fault, and recovery admission. Status requires both identities and exactly one request/job selector with the delivered closed bounded query syntax. Stop uses exact job identity and that job's `control_revision`; it cannot stop a successor. The native finite deadline survives session/ACK/polling loss and the ordinary 60-second native lease.

Retain volatile boot-scoped receipts (eight settled, up to one hour, earlier capacity eviction, active/unresolved-fault pinning), nonreused admission sequence, and stable authenticated profile/credential-generation reconciliation. A missing receipt does not prove nothing ran. Never automatically retry Start after a lost reply or infer shutdown from a missing Stop response; reconcile the identified request/job. Keep engineering, field, consumer, and USB authority scopes separate. Require native confirmed inactivity; unresolved shutdown retains fault/ownership/recovery inhibition beyond receipt or lease expiry. Direct GPIO per-job SDK observations carry execution epoch, RF pin, initialization stage, and monotonic timestamp. Use the evidence from the identified job and engine. Numerical realization and register observations provide software and register evidence.

## Station-independent drive semantics to preserve

Pico direct GPIO remains 2/4/8/12 mA, default 12 mA for new installations; existing missing-field 4 mA decoding/serialization remains unchanged. Saved drive, immutable boot drive, nullable cached SDK readback, and job-attributable SDK evidence are distinct. Saved drive null means no persisted RF/settings configuration, not merely incomplete station commissioning. After a verified drive save it is non-null even without a station.

The existing scoped GPIO drive PUT preserves authoritative settings server-side. Healthy absent settings initialize from validated factory hardware defaults and the current network profile; invalid/unhealthy saved state never falls back. A drive-only merge and explicit saved-progress drive Apply use the same authority. Drive Apply requires a saved drive draft and saved revision/domain, returns current progress readback and global ETag, and leaves unrelated drafts inert; read GPIO drive afterward to verify. No callsign, locator, WSPR dial, or station commissioning is required. Local USB settings require no Wi-Fi credentials. Existing complete station details need not be resent or cleared. Do not reconstruct saved settings from redacted Config.

Current Config readback permits both-empty callsign/locator as absent identity with valid reported power and WSPR disabled. Partial or malformed identity still refuses. This does not add station clearing, weaken station writes or mixed-patch validation, or make Config existence mean WSPR readiness. Preserve dormant-station/profile authority, credentials, other settings, STOP, schedules, and recovery obligations. The installer preserves station-independent settings and requires linked image support before overwriting such saved state. No storage-format or firmware version increment is needed.

A lost save reply leaves the result unknown: read authoritative state and review before another explicit save; do not automatically retry or restart. Save does not program the pad. Actual drive changes require explicit restart and retain sticky A/B/A inhibition and boot readback. The first save of the already-active 12 mA selection is not itself a hardware change. Expected Start drive is an assertion, not per-tone programming. An otherwise admissible tone on a valid existing boot drive requires no invented station metadata. Both native schedule types must be saved disabled; Test Tone never changes them automatically.

For a separately authorized HP 437B Automation campaign, retain the original boot-active nominal drive and saved state, explicitly disable both native schedules, save each drive, restart when required, rediscover, and verify boot drive. Review/Start only one finite tone at a time, verify its engine-specific readback, and confirm native inactivity before the next point or save. Reconcile lost replies; halt on unresolved shutdown or unverified drive. Restore the original nominal drive through the same sequence without enabling schedules. If originally unsaved, restoration persists that drive rather than restoring null; it does not authorize erasing settings. The campaign still needs its own frequencies, RF path, duration, stopping procedure, meter setup, and acceptance criteria. HP adapter delivery and meter operation are separate work.

## Delivery evidence and limits

The historical Pico run for implementation commit `b0edc0dd` passed 226 Pico host tests, the ordinary linked-image check, and the stack guard. A settings-preserving installation on Pico C was verified. [Pico PR CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45426499466) · [Pico exact-source CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45426556062)

The HP adapter submitted all 56 points through USB Review with default permissions. The 14 ready and 42 restart-required results included all four drives at 144,490,500 Hz. At that carrier, 8 mA was ready and 2, 4, and 6 mA required restart. Review left settings and revision unchanged. These observations establish Pico USB admission for the reviewed points.

The [historical 85-cell and 51-frame band/mode matrix](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/si5351.md#conducted-mode-acceptance) records conducted results at 8 mA, including the 2 m point. Earlier Pico C HF checks used firmware built from `946dbb427be9e86376f1fd6f38d34a45a5065d35` and covered 10 ms completion, exact Stop of a running 120-second request, and full 120-second completion at 14,097,100 Hz for each of 2, 4, 6, and 8 mA. All 12 checks confirmed inactive output. The recorded final state was idle and unowned with 8 mA saved and boot-active, no restart pending, and both native schedules disabled. The capture record is `/Users/lbussy/GitHub/WsprryPico/build/si5351-test-tone-handoff-evidence.json`. Full band/mode qualification at 2, 4, and 6 mA, calibrated RF power, absolute timing, and an independent fault-proof cutoff remain open.

For the prior direct-GPIO work at `9ea181daf3cb38c531c96f3cf5bb697804af81d8`, [Pico CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45312874379) and [Read the Docs](https://wsprrypico--30.org.readthedocs.build/en/30/) passed. The [plan](legacy-gpio-drive-label-implementation-plan.md#historical-direct-gpio-handoff--2026-11-23) retains the earlier installation record, 225 host tests, 182 installer cases, and 52 dry Reviews. Those records apply to the earlier image.

## Status

- ✅ Pico producer, including exact 144,490,500 Hz native Test Tone admission, and Pico-local validation.
- ✅ Settings-preserving Pico C install and HP adapter USB Review of all 56 points.
- ✅ Pico C conducted HF Test Tone checks at all four nominal Si5351 drives.
- ▧ Pi companion schema, strict validators, generated assets, consumers, and paired-test updates.
- ▧ Production network and USB source-pair suites against explicitly selected current Pi and Pico sources.
- ▧ Pi 4/Pi 400/CM4 nominal label correction and wider mapping verification.
- ▧ Full Si5351 band/mode qualification at 2, 4, and 6 mA, calibrated power, and independent fault-proof cutoff.

## Verified baseline and open pair checks

At the 2026-11-24 recheck, WsprryPi `devel` was `e7d44eaae25d763af97fde4776382443098d3e28`. The intervening Pi commits since the original review at `99d5f6c4` touched only these planning docs. The Pi base commit recorded clean Pico `devel` commit `7c09ff0fb15bb421f9010fb6faa6868f61b7a1a4`, which was still the live Pico `devel` head. The Pi base vendor schema and that Pico `devel` schema had the same SHA-256. The frozen Pico PR schema differs from the Pi base vendor copy. It is byte-identical at the pinned head, `aed2462f`, the prior pin `0d8f30b6`, the earlier reviewed PR head `064319a0`, and implementation commit `b0edc0dd`.

| Control/1 schema source | SHA-256 |
| --- | --- |
| Pi base vendor copy and then-current Pico `devel` | `09f561a31a46eceb961b9dd495f98091a4ff18e71707c532004a42956f5472e1` |
| Pinned Pico PR head, earlier reviewed head, and implementation commit | `5a5f47a7909b3dbc530bfcfda8ffdd8eb4ac824ff526eb0ea56d53d13aeee2f0` |

At the pre-implementation recheck, no Pi companion PR or successful production pair run against the pinned Pico head was found. The recent green Pi macOS controller runs skipped both paired jobs. [Xcode 27 run](https://github.com/WsprryPi/WsprryPi/actions/runs/45441599007) · [macOS 26 Intel run](https://github.com/WsprryPi/WsprryPi/actions/runs/45441589588) · [Pinned-head Pico-only CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45462062405)

The plan retains the earlier source records. The network fixture previously failed to link Test Tone and `assess_arm`; the runtime environment also lacked required SQLite development files and then used a Python without `TarFile.extractall(filter=...)`. The production USB pair previously rejected capabilities as `invalid_remote_response` (HTTP 502, `mutation_attempted=false`).

## 1. Refresh the shared contract

Inspect applicable repository instructions, branch and working-tree state, and affected contract owners. From the Pi checkout, run `python3 -B scripts/generate_control_contract.py --source <clean-pico-checkout>` against a clean checkout of the pinned Pico commit above. Refresh the vendored Control/1 schema, provenance, generated C++ metadata, and shared browser assets. Verify exact source selection, clean provenance, file hashes, canonical digest, and lifecycle policy with `python3 -B scripts/generate_control_contract.py --source <clean-pico-checkout> --check`.

Keep the one current, closed prerelease contract at major 1, `Wsprry-Control/1`, `/api/control/v1/`, and `urn:wsprry:control:1`. Preserve independently versioned WTP, transport, authentication, and storage formats. Use exact current-contract negotiation and keep the existing refusal of superseded views, downgrade attempts, and carrier fallback.

## 2. Update the affected Pi consumers

Update strict Pi capability negotiation, operations-descriptor validation, runtime readers, serialization, and paired adapters where the new producer contract affects them. Validate the optional `test_tone` capability when present, including engine limits, readback nullability, and availability versus admission. Strictly validate Pico's expanded operations descriptor. Preserve the `pi_management` and `pi_consumer_session` allowlists. Exercise gateway denial of Test Tone routes, including denial of Test Tone Status query forwarding. Use appropriate direct production fixtures for producer lifecycle or reconciliation coverage.

In `src/wtp_integration/configuration.cpp`, replace scalar equality for qualified Si5351 `boot_active.drive_ma` with membership in the validated `ordinary_profile.drive_ma` array `[2, 4, 6, 8]` for `qrp-labs-rev6-25mhz-clk0`. Keep exact validation of the profile and its other qualification and boot fields. Refresh the shared browser validator from the pinned source and verify the affected Pi browser consumers accept the same contract.

Accept a current closed saved-settings document with both callsign and locator empty, valid reported power, and WSPR disabled. Exercise drive save, explicit restart, and readback without inventing station metadata or changing unrelated saved settings. Preserve nullable saved drive semantics and strict station-write validation and mixed-patch validation. Cover partial or malformed identity and absent identity with WSPR enabled as negative cases.

Validate document shape before accessing fields. Cover the valid current contract over both carriers and refusal before mutation for malformed or superseded documents, inconsistent advertised/returned schemas, missing or extra fields, wrong types, invalid limits or units, unsupported versions or carriers, and mismatched device, boot, profile, authority, or revision. Verify unchanged authoritative state after refused requests.

## 3. Repair and run both production pairs

Update `src/tests/network/CMakeLists.txt` with the production Pico Test Tone and ARM admission sources and their dependencies. Use the selected Pico implementation and retain the existing production Pi client/coordinator, Pico handlers, crypto, serialization, framing/ACK, and journals. Confirm the complete network run clears the prior link failures and runs with SQLite headers/libraries and a Python that supports the recovery-image builder's `TarFile.extractall(filter=...)`.

Run both complete recipes in `.github/workflows/debian-non-hardware.yml` against the same exact Pi and Pico candidates using the checked-in dependency pins. Use the committed Pico SHA in Pi contract provenance for PR CI; verify the selected checkout in both jobs and run each recipe's strict generator check.

- `wtp-network-loopback` includes `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`. Use recursive Mbed TLS at the checked-in pin `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.
- `wtp-usb-management-pair` builds the actual Pico `usb_management_driver`, runs `wtp-usb-pair-client-build` and `wtp-usb-management-test`, and runs `WsprryPi-UI/tests/wtp_usb_pair_test.js` through the production pair. Confirm valid capabilities are accepted and malformed capabilities are refused before writes.

Run affected contract, consumer, browser, and adapter regressions as well. Record both exact source SHAs, any tested working-tree changes, dependency pins, exact commands and results, and CI links. Both complete production suites must pass for the intended pair. Failed, cancelled, timed-out, or skipped jobs leave the gate open.

## Pi-led merge and deployment sequence

Open the Pi companion PR first and have it drive both production pair jobs against the pinned Pico SHA. Keep that SHA reachable from a retained Pico ref for Pi CI. If either candidate changes, update the exact pair record and rerun both suites, even if the schema is byte-identical. Once both suites pass for the same exact source pair and both PRs are approved, obtain merge authorization, merge the Pi companion into `devel` first, then merge Pico PR #30 into `devel` in the same coordinated window. Deploy the matched pair together under separate deployment authorization.

## Separate Pi label slice reference

Keep Pi 4/Pi 400/CM4 work limited to nominal drive labels. Stored `GPIO.Power Level` stays code 0–7: generation 4 displays `code + 1` mA; earlier models display `2 × (code + 1)` mA. Code 7 remains 7 and displays 8 mA on Pi 4 versus 16 mA on Pi 3. Preserve defaults, stored values, payloads, hardware programming, runtime behavior, other backends, reported-power metadata, and unrelated presentation. No Si5351, RP1/provider, or Pico implementation is implied.

Inspect the existing generation/model authority and affected UI sources before edits; do not add a duplicate browser model parser. Unknown identity must not fabricate nominal current. Suppress generation-4/unknown legacy dBm estimates without calculating replacement power from current. Render labels only after identity/selection are available, without synthetic events or extra persistence. Preserve slider position, drafts, dirty state, and intended writes from deliberate user edits. Report suspected backend defects for separate scope.

Use mandatory Impeccable before future UI edits. Run focused UI/source regressions and affected existing UI tests, and inspect rendered desktop/mobile light/dark states. Cover initialization, delayed/changed/unknown identity, target/boot changes, reset, backend switching, keyboard operation, errors, accessibility agreement, unchanged serialized selection, and no rendering-induced writes. Do not perform UI implementation or rendering as part of this documentation update.

## Documentation, adversarial review, and reporting

Future authorized implementation must update affected Pi engineering/contract documentation and review operator impact. Relevant existing pages are Pi `docs/wtp-gpio-drive.md`, operator `WsprryPi-Docs/docs/User_Interface/Setup/Transmitter/index.md` and `docs/Advanced_Operations/rf_electrical.md`, and the delivered Pico guides above. Verify actual repository location and instructions before work. Sibling documentation edits require explicit authorization; report exact follow-up if absent. Keep label units, nominal-current versus reported-power distinctions, station independence, saved, boot-active, and engine-specific readback evidence, and restart semantics accurate. Use each documentation owner's build/render workflow and Impeccable for affected rendered UI documentation when those changes are authorized. This planning update changes no operator behavior or UI.

Assess both full documents adversarially for stale source/install evidence, producer recreation, station-save prerequisites, unsafe restoration, scope/authority conflicts, consumer completeness, gateway expansion, strict validation, pair dependency closure, branch availability, and overclaimed evidence. Fix actionable documentation findings and repeat the full assessment until none remain. For future implementation, repeat the same assess/fix/full-assess cycle across its code, tests, and documentation; unavailable required validation remains a blocker.

Run final whitespace checks and inspect staged/unstaged/untracked state. Report the completed Pico producer separately from remaining Pi label and integration dependencies. Include modified files/components, checks/results, unperformed runtime/hardware qualification, remaining blockers, a Documentation Impact section, branch/working-tree state, and any authorized commit/push. Do not claim software integration, physical RF, installation, service, timing, or release readiness from documentation review or hardware-free tests.
