# Drive-current mappings across WsprryPi and WsprryPico: implementation plan

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478). Execution instructions: [implementation prompt](legacy-gpio-drive-label-implementation-prompt.md).

## Objective and authorized scope

This planning handoff covers issue #478, this plan, and its implementation prompt. Preserve unrelated working-tree changes. The next implementation task is the bounded Pi companion slice required for Pico PR #30. Pi 4/Pi 400/CM4 label correction, any Pi Test Tone UI or proxy, HP 437B adapter work, and wider drive-mapping work are separate slices. Sections 1–7 retain the wider roadmap. Implementation, related commits and pushes, merges, deployment, and hardware tests require separate authorization.

## Nominal mapping reference; wider rows remain deferred

| Output/platform | Legal nominal drive currents |
| --- | --- |
| Si5351 in WsprryPi and WsprryPico | 2, 4, 6, or 8 mA |
| Pi 1, Pi 2, Pi 3, Zero, and Zero 2 W direct GPIO | 2, 4, 6, 8, 10, 12, 14, or 16 mA |
| Pi 4, Pi 400, and CM4 direct GPIO | 1, 2, 3, 4, 5, 6, 7, or 8 mA |
| Pi 5 and other supported RP1 platforms | 2, 4, 8, or 12 mA |
| WsprryPico direct RP2350 GPIO | 2, 4, 8, or 12 mA |

These are nominal drive-current settings, not frequencies, calibrated RF power, measured pad current, or WSPR reported-power metadata. RF amplitude and power depend on frequency, output circuit, and load. Do not derive new dBm estimates from nominal current.

## Pico producer handoff — 2026-11-24

Both Pi companion suites select the frozen, clean WsprryPico commit [`8df9ea0565d819083dd9d884aced954d45ea54f9`](https://github.com/WsprryPi/WsprryPico/commit/8df9ea0565d819083dd9d884aced954d45ea54f9). It was the verified head of the open draft Pico PR targeting `devel` from `codex/issue-478-pico-drive-current` on 2026-11-24. [Pico PR #30](https://github.com/WsprryPi/WsprryPico/pull/30)

The Pico handoff consolidates gray-line research into the standalone guide, removes the superseded plan, and updates qualification guidance for native execution following the retirement of external WTP jobs and for the current Si5351 profile. It retains prior test evidence, qualification limits, and recovery instructions. The Pico handoff reports four local prompts retired, a clean working tree, and passing documentation checks and six RF-calculation tests. Hardware qualification remains at the previously recorded scope. [Gray-line research](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/standalone.md#gray-line-research-and-measurement-limits) · [Native RF qualification](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/pico-rf-qualification.md)

The subsequent Pico documentation commits remove superseded instructions and task history. `aed2462f` corrects standalone configuration guidance and restores RF measurement safeguards. `8df9ea05` updates seven Markdown files, including the network guide's instructions for current authenticated management. The commits preserve evidence and recorded hashes, qualification limits, recovery instructions, and recovery tools. The Pico handoff reports passing checks for both commits. The Pico checkout was clean at the recheck. [Qualification and configuration update](https://github.com/WsprryPi/WsprryPico/commit/aed2462f9934dbe664d6541b294be79d404ce0f5) · [Network guidance update](https://github.com/WsprryPi/WsprryPico/commit/8df9ea0565d819083dd9d884aced954d45ea54f9)

Native Test Tone Review and Start admit the exact 144,490,500 Hz request on the ordinary `qrp-labs-rev6-25mhz-clk0` Si5351 profile with default experimental-frequency permissions. The exact-point rule leaves neighboring frequencies, other profiles, and other job sources under the general frequency policy. The historical conducted evidence at 144,490,500 Hz used 8 mA.

### Status

- ✅ Pico producer, including exact 144,490,500 Hz native Test Tone admission, and Pico-local validation.
- ✅ Settings-preserving Pico C install and HP adapter USB Review of all 56 points.
- ✅ Pico C conducted HF Test Tone checks at all four nominal Si5351 drives.
- ▧ Pi companion schema, strict validators, generated assets, consumers, and paired-test updates.
- ▧ Production network and USB source-pair suites against explicitly selected current Pi and Pico sources.
- ▧ Pi 4/Pi 400/CM4 nominal label correction and wider mapping verification.
- ▧ Full Si5351 band/mode qualification at 2, 4, and 6 mA, calibrated power, and independent fault-proof cutoff.

The authoritative delivered contracts are [Station-independent GPIO drive](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/gpio-drive.md#station-independent-settings), [Configuration updates](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/configuration-updates.md#drive-without-station-setup), [Saved progress](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/saved-progress.md), [Finite Test Tone](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/protocol/Native-Execution.md#authenticated-finite-test-tone), the [current closed Control/1 schema](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/protocol/control-1.schema.json), and [GPIO drive save/restart/readback](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/gpio-drive.md). Use these consolidated guides; the completed station-independent-drive execution prompt was deleted and must not be linked or recreated. See the [Si5351 engineering guide](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/si5351.md) for execution profile, register evidence, and qualification limits.

### Completed producer behavior to consume and preserve

Ordinary Si5351 execution accepts nominal 2, 4, 6, and 8 mA under profile `qrp-labs-rev6-25mhz-clk0`. In the [current Control/1 schema](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/protocol/control-1.schema.json), `ordinary_profile.drive_ma` is `[2,4,6,8]`, replacing scalar `8`. Test Tone Review and Start require the boot-engine assertion. Si5351 supports 10–120,000 ms and reports fresh, execution-epoch-bound drive-register evidence in `synthesis_readback`, with null `pad_readback`. Direct GPIO retains 2, 4, 8, and 12 mA and a 1 ms minimum, with SDK `pad_readback` and null `synthesis_readback`. Native and engine limits can lower the advertised maximum. Si5351 finite execution supports exact-job Stop and confirms shutdown through register readback.

Authenticated Control/1 implements finite Review, Start, identified Status, and exact-job Stop through native execution, ownership, frequency/correction/filter, hardware, and clock admission. Engineering HTTPS, authenticated Consumer-Radio, and local USB reach the same coordinator under their existing authority rules. Public observer reads and plaintext consumer HTTP grant no Test Tone authority. The optional `test_tone` capability's availability is not Start admission. The Pi gateway route subset was intentionally not expanded; advertising the capability grants no Pi proxy, UI, or remote execution feature.

| Operation | Delivered resource | Binding |
| --- | --- | --- |
| Review | `POST /api/control/v1/test-tone/review` | Authenticated current device/boot; pure assessment, no write precondition |
| Start | `POST /api/control/v1/test-tone/start` | Review's current `assessment_revision` in `If-Match`; unique request ID and asserted boot engine and drive |
| Status | `GET /api/control/v1/test-tone/status` | Explicit device/boot and exactly one request-ID or job-ID query selector; same authenticated authority generation |
| Stop | `POST /api/control/v1/test-tone/stop` | Exact job and its `control_revision` in `If-Match` |

Use exact closed requests and responses from the delivered schema. Status IDs are 32 lowercase hexadecimal characters; duplicate/unknown query keys, percent encoding, alternate target spellings, and ambiguous selectors refuse. Review projects post-restart drives with ready, restart-required, and specific refusal results without writes, reservation, hardware preparation, or GPIO programming. Absolute integer-Hz carriers receive no WSPR offset or dial substitution; 5,366,200 Hz remains exactly that nominal carrier. Invalid durations refuse without clamping. Native RF timing ends independently of polling, sessions, or ACK delivery, including beyond the existing 60-second native lease.

Receipts are volatile and boot-scoped: up to eight settled receipts for up to one hour, with earlier capacity eviction and active/unresolved-fault work pinned separately. Admission consumes a nonreused boot-local sequence without a journal write; retained identical replay returns the original job, while changed content or original revision conflicts. Missing receipts are unavailable evidence, not proof that nothing ran. Never automatically retry Start after a lost reply; reconcile the identified request/job. Consumer reconciliation binds to stable profile/credential generation; engineering, field, and USB authority scopes remain distinct. Exact Stop uses job-specific revision and native identity, cannot stop a successor, and reports inactive output only on native confirmation. Unresolved shutdown retains fault/recovery inhibition and blocks Start beyond receipt/lease expiry; no Test Tone operation clears it.

For direct GPIO, retain nominal drive values 2/4/8/12 mA and the 12 mA new-installation default. Missing stored `gpio_drive_ma` retains its defined 4 mA decoding/serialization behavior. Saving and explicit restart/readback remain unchanged. Saved drive null means no persisted RF/settings configuration, not an incomplete WSPR station; after a verified drive save it is non-null even without a station; immutable boot drive remains a separate validated selection. Job-attributable SDK readback is copied per execution epoch with its pin, initialization stage, and monotonic timestamp. Global cached pad readback can be null before execution and is not a substitute for job evidence or measured electrical current. Numerical NCO realization/quantization bounds do not qualify RF frequency or power.

Drive saves require no callsign, locator, WSPR dial, or station commissioning. The scoped GPIO drive PUT preserves authoritative settings server-side. Healthy absent settings can initialize from validated factory hardware defaults and the current network profile; invalid or unhealthy state never falls back. A drive-only configuration merge and saved-progress drive Apply use the same authority. Drive Apply requires a saved drive draft, applies that saved revision/domain, returns current progress readback and its global ETag, and leaves unrelated drafts inert; then read GPIO drive to verify. Local USB settings require no Wi-Fi credentials. Do not reconstruct settings from redacted configuration, resend existing station details, or invent a station to save drive.

The current Config read representation permits both-empty callsign/locator as absent identity with valid reported power and WSPR disabled. Partial or malformed identity still refuses; Config existence is not WSPR readiness. This adds no station clearing and does not weaken valid-station writes or mixed-patch validation. Preserve dormant-station/profile authority, credentials, other settings, STOP, schedules, and recovery duties. The shared installer preserves these settings and requires linked image support before overwriting such saved state with another image. No version increment, historical control view, or storage-format change was added.

An otherwise unconfigured device may independently admit a tone on its valid existing boot drive when native rules pass. Expected Start drive is an assertion, not per-tone programming. Both native schedule types must be saved disabled for tone admission; Test Tone never disables them automatically. HP 437B Automation is the client project. Pico owns finite execution. The future companion slice updates consumers and fixtures with existing Pi route authority. Generic upload, scheduling, per-tone overrides, and the HP adapter are separate work.

### Future bench instructions; separate authorization required

Retain the original boot-active nominal drive and saved state before changes. Explicitly disable both native schedules under the campaign's authorization, verify idle/unowned/inactive output, save each selected drive through the scoped authority, restart explicitly when required, rediscover current device/boot and capabilities, and verify immutable boot drive before Review/Start. An initial save of an already-active 12 mA value is not itself a hardware change; actual changes retain sticky A/B/A restart inhibition. Use one finite tone at a time, reconcile lost replies by identity, verify that job’s engine-specific readback, and confirm native inactivity before the next point or save. Stop the group on unresolved shutdown or unverified drive. Restore the original nominal drive afterward through save/restart/rediscovery/readback without enabling schedules. If originally unsaved, this restoration persists a drive value rather than restoring null; it does not authorize erasing settings. Retain recovery obligations and separately approved RF path, frequencies, duration, stop procedure, and meter acceptance criteria.

### Current Pico software and conducted evidence

The historical Pico run for implementation commit `b0edc0dd` passed 226 Pico host tests, the ordinary linked-image check, and the stack guard. A settings-preserving installation on Pico C was verified. [Pico PR CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45426499466) · [Pico exact-source CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45426556062)

The HP adapter submitted all 56 points through USB Review with default permissions. The 14 ready and 42 restart-required results included all four drives at 144,490,500 Hz. At that carrier, 8 mA was ready and 2, 4, and 6 mA required restart. Review left settings and revision unchanged. This is Pico USB admission evidence. The local record is `/Users/lbussy/GitHub/WsprryPico/build/si5351-2m-handoffs/hp-56-review-evidence.json`.

The [historical 85-cell and 51-frame band/mode matrix](https://github.com/WsprryPi/WsprryPico/blob/8df9ea0565d819083dd9d884aced954d45ea54f9/engineering/development/si5351.md#conducted-mode-acceptance) records conducted results at 8 mA, including the 2 m point. Earlier Pico C HF checks used firmware built from `946dbb427be9e86376f1fd6f38d34a45a5065d35` and covered 10 ms completion, exact Stop of a running 120-second request, and full 120-second completion at 14,097,100 Hz for each of 2, 4, 6, and 8 mA. All 12 checks confirmed inactive output. The recorded final state was idle and unowned with 8 mA saved and boot-active, no restart pending, and both native schedules disabled. The capture record is `/Users/lbussy/GitHub/WsprryPico/build/si5351-test-tone-handoff-evidence.json`. Full band/mode qualification at 2, 4, and 6 mA, calibrated RF power, absolute timing, and an independent fault-proof cutoff remain open.

### Historical direct-GPIO handoff — 2026-11-23

At `9ea181daf3cb38c531c96f3cf5bb697804af81d8`, [Pico CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45312874379) and [Read the Docs](https://wsprrypico--30.org.readthedocs.build/en/30/) passed. Local evidence includes 225 host tests, 182 installer cases, real-browser station-free drive save/read/review/Apply, and normal/inhibited firmware plus RF driver link checks. The exact-count CI policy was corrected for the new installer case without relaxing enforcement. These earlier results cover the station-independent direct-GPIO slice.

| Historical installation observation | Recorded value |
| --- | --- |
| Host / USB serial | `wpicoc.local` / `1545B6EFBF78028C` |
| Device / observed postboot ID | `c2a3a2a314c948b50a8562a1aeb69669` / `a83bbeffb1f80839617c7d8626fb61a1` |
| Running source readback | `9ea181daf3cb` |
| Installed UF2 SHA-256 | `a9c1d9fc14fe27daececdb344a8abcc310d46552afbb7e1d5248fd8af09f0f85` |
| Version / bundle provenance | `0.0.0-devel`; two pre-existing untracked prompts label the developer bundle dirty; tracked implementation sources match the pushed commit |
| Shared USB installer | `ok=true`, `stage=complete`, `preserved=true`, `recovery_required=false`; verified after reboot |
| Local exact installed-image bundle | `/Users/lbussy/GitHub/WsprryPico/build/station-independent/developer-bundle` |
| USB-host exact installed-image bundle | `/home/pi/.local/share/wsprrypico-deploy/station-independent-drive-20261123/developer-bundle` |
| Postboot state | `pio-dma-gp2`, GP2; saved 12 mA; boot-active 12 mA; SDK pad readback null before execution; output inactive and unowned |

Saved configuration matched the preflight snapshot exactly, including complete existing station details. USB and network identities match. The listed bundles identify the historical direct-GPIO image. The boot ID is an observation and must be rediscovered before operations. These are delivered installation observations, not fresh live-device checks in this documentation task.

The 52 postboot dry Reviews across 13 representative carriers and four drives yielded 13 ready and 39 requires_restart, with configuration and revision unchanged. These carriers are fixtures, not the approved HP campaign list. No tone Start, drive save, or meter operation was performed during deployment verification. These dry Reviews established no physical RF evidence.

Exact local commands/results, both tested source revisions and companion working-state evidence, CI, and installation results are retained in `/Users/lbussy/GitHub/WsprryPico/build/station-independent/evidence/validation.json`. The installation record above predates the current Si5351 image.

### Verified baseline and open pair checks

At the 2026-11-24 recheck, WsprryPi `devel` was `e7d44eaae25d763af97fde4776382443098d3e28`. The intervening Pi commits since the original review at `99d5f6c4` touched only these planning docs. The Pi base commit recorded clean Pico `devel` commit `7c09ff0fb15bb421f9010fb6faa6868f61b7a1a4`, which was still the live Pico `devel` head. The Pi base vendor schema and that Pico `devel` schema had the same SHA-256. The frozen Pico PR schema differs from the Pi base vendor copy. It is byte-identical at the pinned head, `aed2462f`, the prior pin `0d8f30b6`, the earlier reviewed PR head `064319a0`, and implementation commit `b0edc0dd`.

| Control/1 schema source | SHA-256 |
| --- | --- |
| Pi base vendor copy and then-current Pico `devel` | `09f561a31a46eceb961b9dd495f98091a4ff18e71707c532004a42956f5472e1` |
| Pinned Pico PR head, earlier reviewed head, and implementation commit | `5a5f47a7909b3dbc530bfcfda8ffdd8eb4ac824ff526eb0ea56d53d13aeee2f0` |

At the pre-implementation recheck, no Pi companion PR or successful production pair run against the pinned Pico head was found. The recent green Pi macOS controller runs skipped both paired jobs. [Xcode 27 run](https://github.com/WsprryPi/WsprryPi/actions/runs/45441599007) · [macOS 26 Intel run](https://github.com/WsprryPi/WsprryPi/actions/runs/45441589588) · [Pinned-head Pico-only CI](https://github.com/WsprryPi/WsprryPico/actions/runs/45462062405)

Historical attempts used Pico `9ea181daf3cb38c531c96f3cf5bb697804af81d8` (the tested uncommitted candidate was subsequently committed and verified by CI) and Pi `06282dd8834c44c18320fc70aa8819977d496c60` plus its then-uncommitted state, retained in the evidence JSON with diff SHA-256 `24a59ad78270847af9d46bc0f8526eb3b1e39ebff60bf2048bc5714604ba0d8c`. That snapshot is not a claim that today's dirty tree is identical.

- The explicit Pico source list in `src/tests/network/CMakeLists.txt` omitted Test Tone and ARM admission sources in the historical attempt. The earlier build reported undefined Test Tone and `assess_arm` references.
- The earlier production USB pair rejected capabilities as `invalid_remote_response`, status 502, `mutation_attempted=false`. Some settings and component checks passed in that attempt. The complete USB suite failed.
- The runtime check needed retained SQLite headers/libraries supplied in an isolated container. Its retry then failed because that Python lacks `TarFile.extractall(filter=...)` required by the companion recovery-image builder. This is a test-environment dependency failure, not evidence of a Pico RF defect.

### Bounded Pi companion acceptance

Use the [implementation prompt](legacy-gpio-drive-label-implementation-prompt.md) for the authorized execution sequence.

1. Refresh the checked-in Control/1 schema, provenance, generated C++ metadata, and shared browser assets with `scripts/generate_control_contract.py` from a clean checkout of the pinned Pico SHA. Verify the generated result and provenance. Preserve the single current prerelease Control/1 contract and independent WTP, transport, authentication, and storage versions.
2. Update strict Pi capability and operations-descriptor validation, including optional `test_tone`, and refresh the shared browser validator. Preserve the Pi gateway and consumer-session allowlists and deny Test Tone Status query forwarding. In `src/wtp_integration/configuration.cpp`, validate qualified Si5351 boot drive against membership in the exact `[2, 4, 6, 8]` array for `qrp-labs-rev6-25mhz-clk0`; retain exact checks for other profile fields. Accept current station-absent, WSPR-disabled saved settings through drive save, explicit restart, and readback while preserving strict station-write validation. Cover malformed or superseded documents, invalid limits, and device, boot, profile, or revision mismatches. Verify refusal before mutation.
3. Add the production Pico Test Tone and ARM admission sources and their dependencies to `src/tests/network/CMakeLists.txt`. Run both complete production recipes in section 4 against the same exact Pi and Pico candidates using the checked-in dependency pins. Confirm the network run clears the prior link, SQLite, and Python environment failures. Confirm that the production USB pair accepts valid capabilities and rejects malformed ones before writes. Both suites must pass. Record the exact source SHAs, any tested working-tree changes, commands, results, and CI links.
4. Update affected Pi engineering/contract documentation and review operator-documentation impact during implementation. Obtain separate scope for any sibling documentation edits.

### Pi-led merge and deployment sequence

Open the Pi companion PR first and have it drive both production pair jobs against the pinned Pico SHA. Keep that SHA reachable from a retained Pico ref for Pi CI. If either candidate changes, update the exact pair record and rerun both suites, even if the schema is byte-identical. Once both suites pass for the same exact source pair and both PRs are approved, obtain merge authorization, merge the Pi companion into `devel` first, then merge Pico PR #30 into `devel` in the same coordinated window. Deploy the matched pair together under separate deployment authorization.

## Deferred wider mapping roadmap — not authorized by this handoff

The following retained roadmap preserves pending research and validation context. Its imperative wording describes work only if separately authorized; it grants no cross-repository authority and does not override the bounded label or companion-integration slices above. Si5351, RP1/provider corrections, Pico edits, and broad matrix/UI changes require explicit scope before execution.

## 1. Establish current evidence and ownership

Read applicable instructions, including WsprryPico's README, CONTRACT, and architecture before implementation and the operator documentation repository's instructions. Inspect branches, revisions, staged and unstaged changes, untracked files, nested instructions, affected component ownership, and whitespace errors in every affected repository. Preserve all existing work. Historical baselines are WsprryPi `devel` at `51257fdb` before the planning commit and WsprryPico `codex/issue-473-device-owned-scheduling` at `05cc90d6`; neither is an instruction to check out stale sources.

For each matrix row, record the advertised domain, UI options, configuration key and stored representation, defaults, parsing and validation, serialization, runtime consumption, register or SDK encoding, actual output selected, tests, and documentation. Include WsprryPico standalone management and WsprryPi's management of Pico over both network and USB. Inspect generated assets and strict companion validators, not only handwritten code.

Verify Pi generation detection using actual model strings and processor fallback, including Pi 400, CM4, Zero, Zero 2 W, and supported RP1 models. Unknown or malformed identity must not display a guessed current or grant hardware capability. Do not add a second model parser in JavaScript to mask producer defects.

Verify register encodings, nominal-current units, and relevant board/SoC differences against primary device documentation and the pinned SDK/provider definitions. Record the source and revision used; UI tables and tests copied from the same implementation are not independent hardware-contract evidence. Report a conflict with the requested matrix before choosing an incompatible interpretation.

The earlier review found a fixed 2–16 mA legacy UI label table, correct-looking Si5351 and RP1 encodings, and correct-looking Pico direct GPIO encoding. These are leads, not proof of complete end-to-end correctness. WsprryPico's ordinary Si5351 profile now admits all four drive values on the specified hardware profile. Broader engineering configurations and conducted band/mode qualification have separate requirements. If satisfying intended availability requires qualification changes or physical evidence, identify the exact remaining decision or evidence rather than excluding Pico or Si5351 from the work.

## 2. Correct each defective lifecycle at its owner

Retain valid saved representations, defaults, and selected settings unless a separately reviewed compatibility change is necessary. In particular, legacy `GPIO.Power Level` remains code 0–7: generations 1–3 map code `n` to `2 × (n + 1)` mA, while generation 4 maps it to `n + 1` mA. Code 7 therefore remains code 7 while displaying 16 mA on Pi 3 and 8 mA on Pi 4. Pi Si5351 levels 1–4 must reach 2/4/6/8 mA. Pico and RP1 mA fields must not be confused with those indices.

Fix defaults, parser/validator domains, internal types, serialization, persistence, runtime selection, backend register or SDK programming, provider admission, and output selection wherever necessary to satisfy the matrix. Reuse current contracts when sufficient. If metadata or an API/schema change is necessary, implement it with coordinated producers, consumers, descriptors, strict validation, generated assets, and tests; new fields are permitted when justified by this objective. Do not force a display-only solution when a backend correction is needed.

For reusable component changes, identify the defect, explain why the correction belongs there, preserve public boundaries and extraction potential, and run standalone and parent integration checks. Any component mapping correction requires explicit authorization for that owner; unrelated refactoring, platform ports, dependency modernization, driver redesign, general cleanup, and unrelated RF work remain outside scope. Escalate material unresolved migrations, compatibility changes, or qualification-policy decisions before choosing new behavior.

## 3. Align both applications' interfaces

Use Impeccable during UI implementation. Correct the Pi-local, Pico standalone, and Pi-managed Pico controls wherever mismatched. Preserve established layout, separate backend control visibility, and existing explicit read/save/apply/restart semantics. Saved selection, boot-active setting, cached pad readback, and measured electrical behavior must remain distinct. Unreachable devices must not acquire an observed default.

Use accurate mA labels and matching accessible values; preserve native controls and stored semantics. Unknown identity displays a raw valid level with nominal drive unavailable, and invalid values do not produce fabricated currents. Clear stale labels after identity changes. Suppress legacy secondary dBm estimates on generation 4 and unknown identity; do not add current-derived estimates elsewhere. Preserve unrelated earlier-model presentation rather than recalibrating RF power.

Exercise initialization, delayed metadata, reload, reconnect, backend switching, target or boot changes, drafts, default reset, keyboard operation, and error recovery. Rendering must not add configuration mutations or overwrite drafts. Deliberate edits retain existing autosave or explicit-save policy. A lost mutation response must not cause a hidden retry, restart, or fallback. Do not broaden into a new management workflow.

## 4. Coordinate shared contracts and both carriers

Apply this gate to the future bounded Pi slice above. The delivered Pico Test Tone producer is complete independently; these remaining paired checks do not require recreating it. Preserve the delivered Pi gateway route subset. Advertising or parsing `test_tone` does not authorize a Pi proxy, UI, or remote execution feature.

For any capability, limits/units, descriptor, API/schema, or protocol-facing change, follow the current Pi/Pico completion gate in both repositories. Inspect every producer and affected companion consumer before editing. Evolve the single current control contract according to authoritative Pico `control-policy.json`, retaining prerelease major/namespace 1 until the explicit stable transition. Keep WTP, transport/authentication, and storage versions independent. Do not add historical views, permissive version ranges, validation relaxation, or hidden downgrade/carrier fallback.

Validate shape before access; reject missing/extra fields, wrong types, unsupported versions or values, inconsistent domains, stale identities, boots, revisions, and profiles before mutation. Coordinate generated assets, paired adapters, runtime serialization, and documentation together.

For this cross-product implementation, require both production-path hardware-free suites on the intended current source pair, even if a particular path proves unchanged. Use WsprryPi `.github/workflows/debian-non-hardware.yml` recipes for `wtp-network-loopback` and `wtp-usb-management-pair`. Network coverage includes `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`. USB coverage includes the actual Pico `usb_management_driver`, `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and `WsprryPi-UI/tests/wtp_usb_pair_test.js`. Use recursive Mbed TLS source at the workflow pin `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` for the network recipe. Run each recipe’s strict generator check against the selected Pico source and retain the production-path checks. Record both revisions, explicit source paths, tested working-tree inputs, dependencies, exact commands and results, and relevant CI links. Retained fixture names do not authorize reintroducing retired WTP execution.

Mocks, standalone builds, old CI results, or stale companion sources do not replace these suites. Failed, skipped, cancelled, or timed-out checks remain unresolved. A companion mismatch that blocks the changed contract is part of the integration work. Distinguish local paired-source evidence from delivery on intended paired refs; do not claim release or branch delivery that has not occurred.

## 5. Validate the whole matrix

Cover every legal value in every row, invalid intermediate and boundary values, index-versus-mA confusion, defaults, saved-value round trips, reload, runtime/backend encoding, correct output register/pin, preservation of unrelated register bits where applicable, provider refusal, and error handling. Use independent expected mappings and fake register/SDK adapters or existing backend seams, not copied production formulas or real hardware. Include no-write assertions for rejected settings and presentation-only refreshes, and preserve meaningful backend safety and cleanup assertions.

Run affected component standalone tests, parent regression checks, Pico host tests, firmware build checks where affected, and both paired suites. Inspect launchers for safety. Use checked-in Makefile/CMake/test infrastructure rather than invented commands. On macOS use `make semantics-test-portable SUDO=` from Pi `src` when relevant; on Linux with libgpiod C++ headers use `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. Portable coverage is a subset, not evidence of full physical-backend semantics. Follow canonical Linux CI recipes when host tests cannot cover the changed backend.

For Pi UI coverage, run focused new tests, `node tests/conditional_transmit_gpio_integration_test.js`, `node tests/rp1_route_ui_test.js`, `node tests/native_host_ui_test.js`, and `npm run test:unit` from `WsprryPi-UI`, plus applicable real-page browser tests. Select Pico tests from its current engineering guide and registrations, including drive, Si5351, schema, and browser coverage. Register focused tests without overwriting existing package or build changes.

Render affected Pi and Pico pages at desktop/mobile widths in light/dark themes. Inspect known/unknown identities, every affected backend, disabled/pending/error states, and visible/accessibility agreement. Preserve screenshots and logs outside tracked source paths and record their locations. Do not use running appliances as fixture endpoints. Required dependency or environment blockers must be reported; do not change pins, weaken checks, or claim completion to avoid them. Physical operation requires separate explicit setup authorization.

## 6. Update documentation in the same implementation

If the wider roadmap is separately authorized with cross-repository documentation scope, update affected operator pages in `/Users/lbussy/GitHub/WsprryPi-Docs` after verifying its location and instructions, plus relevant Pi and Pico engineering/contract documentation. Keep code edits in their owning repositories. The current handoff grants no sibling-documentation edits.

Start with operator `docs/User_Interface/Setup/Transmitter/index.md`, `docs/Advanced_Operations/rf_electrical.md`, the transmitter-backend configuration guide, RP1 pages, and Pico installation/drive/Si5351 pages. Review Pi `docs/wtp-gpio-drive.md` and Pico `engineering/development/gpio-drive.md`, Si5351 guides, descriptors, and contracts for stale defaults, units, supported domains, activation, or qualification statements. Update only affected content, and replace screenshots only when materially inaccurate. Document nominal current, stored-code semantics where useful, reported-power independence, saved versus active/readback states, and qualification limits accurately. Do not copy regression diaries into operator prose.

Keep paragraphs on one source line, preserve directives and tables, and use Oxford commas. Build operator docs with their documented virtual environment and `python -m sphinx -b html docs docs/_build/html`; use the Pico documentation repository's own documented checks. Review affected rendered documentation with Impeccable at desktop/mobile widths and verify links. Report warnings and failures. No publication or release is included.

## 7. Assess, fix, and report the separately authorized wider roadmap

Perform an adversarial assessment across the entire matrix, both products, backend encodings, consumers/carriers, persistence, identities, output safety, profiles, generated assets, tests, UI, docs, and repository boundaries. Fix in-scope findings and rerun affected checks, then perform another full assessment. Repeat until no actionable in-scope findings remain; required unavailable evidence remains a blocker even if source review is clear. Do not turn correct mappings into unnecessary edits merely to populate a diff.

Completion requires verified/corrected matrix rows with evidence, necessary backend and contract changes, passing affected standalone and parent checks and both paired suites, reviewed rendered interfaces, and built/reviewed updated documentation. Distinguish software validation from physical pad, timing, service, installation, and RF qualification. No physical qualification is claimed without its own evidence.

Report behavior and verified-unchanged paths, all modified repositories/components, source revisions and uncommitted changes tested, exact commands/results and CI links where applicable, visual evidence, Documentation Impact, remaining blockers, and branch/working-tree/Git state per repository. Preserve unrelated work and keep Git boundaries separate. The 2026-11-24 handoff records status in the issue and planning artifacts.

## Bounded label-slice acceptance and Documentation Impact

The Pi 4/Pi 400/CM4 label slice corrects visible and accessible nominal current only, using stored `GPIO.Power Level` code 0–7 unchanged (`code + 1` mA for generation 4; `2 × (code + 1)` mA for earlier models). Preserve defaults, slider position, drafts, dirty state, payloads, backend visibility, runtime behavior, and hardware encodings. Unknown identity must not fabricate a current. Suppress generation-4/unknown legacy dBm estimates without deriving replacement power estimates or changing reported WSPR power. Refresh rendering without synthetic persistence events. A suspected backend defect is a finding for separate scope, not permission to expand this label task.

A future label implementation must use Impeccable, focused UI/source regressions, and rendered desktop/mobile light/dark inspection, including delayed/changed/unknown identity, keyboard operation, resets, errors, and no extra rendering-induced writes. Shared contract changes, if proposed, require separate scope and the complete pair gate above.

Updated here: issue #478, this plan, and its prompt. Considered but unchanged: delivered Pico guides and schema, Pi `docs/wtp-gpio-drive.md`, and operator `WsprryPi-Docs/docs/User_Interface/Setup/Transmitter/index.md` and `docs/Advanced_Operations/rf_electrical.md`. Future authorized implementation must review affected nominal labels/units, saved-versus-active/readback semantics, station-independent settings, and explicit restart instructions at their owners; sibling edits/build/rendering need explicit authorization. No operator behavior changed in this documentation task, so no operator-page edit or UI rendering is required here.

Assess both complete documents adversarially for stale producer assumptions, scope expansion, contradictory acceptance, settings/station confusion, lost-reply safety, source/branch availability, evidence limits, deleted links, and all unresolved pair blockers. Fix actionable documentation findings and repeat full assessment until none remain. Report completed Pico producer separately from remaining Pi dependencies; documentation completion cannot close the software integration gate.
