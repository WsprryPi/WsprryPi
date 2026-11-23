# Drive-current mappings across WsprryPi and WsprryPico: implementation plan

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478). Execution instructions: [implementation prompt](legacy-gpio-drive-label-implementation-prompt.md).

## Objective and authorized scope

Verify and correct nominal RF-output drive-current selection throughout WsprryPi and WsprryPico, including direct GPIO, Si5351, RP1, necessary backend/provider changes, both local and companion interfaces, persistence, and documentation. This supersedes the earlier UI-only scope. An already-correct mapping requires verification and suitable regression evidence, not gratuitous code changes.

This revision updates planning documents and the issue only. Actual implementation begins when the implementation prompt is invoked. That implementation includes coordinated edits in WsprryPi, WsprryPico, and the operator documentation repository. A demonstrably necessary RP1 provider correction is included, subject to inspecting its owning repository and instructions. Keep each repository's branch, working tree, validation, and Git boundary separate. Do not infer permission for implementation, commits in companion repositories, deployment, firmware loading, services, GPIO operation, or RF tests from publication of this plan.

## Required nominal mappings

| Output/platform | Legal nominal drive currents |
| --- | --- |
| Si5351 in WsprryPi and WsprryPico | 2, 4, 6, or 8 mA |
| Pi 1, Pi 2, Pi 3, Zero, and Zero 2 W direct GPIO | 2, 4, 6, 8, 10, 12, 14, or 16 mA |
| Pi 4, Pi 400, and CM4 direct GPIO | 1, 2, 3, 4, 5, 6, 7, or 8 mA |
| Pi 5 and other supported RP1 platforms | 2, 4, 8, or 12 mA |
| WsprryPico direct RP2350 GPIO | 2, 4, 8, or 12 mA |

These are nominal drive-current settings, not frequencies, calibrated RF power, measured pad current, or WSPR reported-power metadata. RF amplitude and power depend on frequency, output circuit, and load. Do not derive new dBm estimates from nominal current.

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

Report behavior and verified-unchanged paths, all modified repositories/components, source revisions and uncommitted changes tested, exact commands/results and CI links where applicable, visual evidence, Documentation Impact, remaining blockers, and branch/working-tree/Git state per repository. Preserve unrelated work and keep Git boundaries separate. The current task publishes revised planning artifacts only; it does not implement this work.
