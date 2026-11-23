# Drive-current mappings across WsprryPi and WsprryPico: implementation prompt

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478). Read the [implementation plan](legacy-gpio-drive-label-implementation-plan.md) and current repository instructions before implementation.

## Task and authority

When this prompt is explicitly invoked, verify and correct RF-output drive-current mappings end to end across WsprryPi and WsprryPico, including direct GPIO, Si5351, RP1, necessary backend/provider changes, configuration and management interfaces, persistence, tests, and documentation. The previous UI-only scope and exclusions are superseded. Do not omit Pico, Si5351, RP1, necessary backend changes, or documentation on the grounds that they were previously excluded. Verify already-correct paths and retain them when no change is needed.

This prompt's creation or publication does not itself invoke implementation. Implementation includes necessary coordinated source and documentation edits in WsprryPi, WsprryPico, and WsprryPi-Docs, and a necessary RP1 provider correction in its owning repository if evidence places the defect there. Follow each repository's instructions and preserve each Git boundary. Do not stage, commit, push, publish, deploy, load firmware, operate services, reboot, change live configuration/GPIO, or transmit unless separately authorized. Broad backend rewrites, platform ports, dependency upgrades, unrelated cleanup, and RF-power recalibration are outside scope.

## Required behavior

| Output/platform | Legal nominal drive currents |
| --- | --- |
| Si5351 in both products | 2, 4, 6, or 8 mA |
| Pi 1, Pi 2, Pi 3, Zero, and Zero 2 W GPIO | 2, 4, 6, 8, 10, 12, 14, or 16 mA |
| Pi 4, Pi 400, and CM4 GPIO | 1, 2, 3, 4, 5, 6, 7, or 8 mA |
| Pi 5 and other supported RP1 platforms | 2, 4, 8, or 12 mA |
| Pico RP2350 direct GPIO | 2, 4, 8, or 12 mA |

Drive current is nominal electrical selection, not frequency, measured pad current, calibrated RF output, or reported WSPR dBm. Preserve valid saved values, defaults, and existing representation wherever possible. In particular, Pi legacy `GPIO.Power Level` stays code 0–7: earlier models display `2 × (code + 1)` mA and generation 4 displays `code + 1` mA. Default code 7 therefore means 16 mA on Pi 3 and 8 mA on Pi 4 without changing the stored code. Pi Si5351 level 1–4 must map to 2/4/6/8 mA; do not confuse indices with fields already stored in mA. Do not silently normalize custom settings or invent a migration. Raise material unresolved compatibility or migration decisions before choosing behavior.

## Inspect and establish a per-row evidence map

Inspect each repository's branch, revision, complete staged/unstaged/untracked state, applicable nested instructions, component ownership, and whitespace checks before builds or edits. Preserve all existing changes. Read Pico README, CONTRACT, and engineering architecture; read operator-doc instructions. Historical revisions in the plan are context, not checkout instructions. Avoid blanket staging, resets, stashes, synchronization, or overwrites.

For every matrix row, trace legal advertised values and defaults through controls, parsing/validation, internal configuration, saved serialization, runtime consumption, register or SDK encoding, and output selection. Include Pico standalone and Pi-managed Pico paths over network and USB, strict descriptors and validators, generated assets, saved drafts, boot-active state, and pad observation. Record evidence before declaring a row correct.

Inspect the known legacy UI table in `WsprryPi-UI/data/index.js`, platform population in `data/site.js`, public configuration serialization, `src/version.cpp`, and legacy/RP1/Si5351 backends in `src/WSPR-Transmitter`. Inspect Pico `src/standalone`, `src/network`, `src/hardware/si5351*`, `src/rf/si5351*`, and `src/rf/pico` with their tests. Follow actual current interfaces rather than assuming those paths exhaust the implementation.

Verify Pi 400, CM4, Zero, Zero 2 W, and supported RP1 identities through current model and processor detection. Fix a necessary detection defect at its owner; do not mask it with a duplicate browser model parser. Missing, malformed, stale, or unsupported identity must not produce guessed current or grant hardware eligibility.

Check register encodings and units against primary device documentation and pinned SDK/provider definitions, recording their sources and revisions. Do not use the UI table or tests derived from production code as independent evidence of hardware encoding. If authoritative evidence conflicts with the required matrix, report that conflict before implementing an incompatible interpretation.

Pico's ordinary qualified Si5351 profile was restricted to 8 mA during the initial inspection. Verify current behavior and distinguish legal register values, accepted configuration, engineering execution, ordinary qualified execution, and physical evidence. Keep existing safeguards unless an evidence-backed change is justified. If intended availability cannot be achieved without qualification-policy decisions or physical evidence, report the exact blocker and obtain that direction; do not silently exclude those values or claim qualified operation.

## Implement necessary corrections at each owner

Correct defective domains, defaults, validation, persistence, serialization, internal units, runtime selection, backend register/SDK programming, provider admission, and output targeting. Explain each component defect and why the correction belongs there, preserve reusable boundaries, and run component and parent tests. Necessary mapping-related backend/component changes are authorized by this scope; incidental cleanup is not.

Prefer existing metadata and interfaces. New API/schema fields are allowed if demonstrably necessary for the mapping objective, with all producers and consumers updated together. Do not force a display-only fix for an encoding or runtime defect, and do not change correct paths solely to create edits. Keep source, generated assets, and documentation consistent.

Align Pi-local, Pico standalone, and Pi-managed Pico controls with their actual supported domains and activation rules. Use Impeccable before UI changes, including required context and craft guidance, and preserve the established design. Match visible and accessible mA values. Show raw valid level plus nominal drive unavailable for unknown identity; invalid inputs must not fabricate current or silently repair settings. Clear stale labels on identity/boot/target changes.

Suppress legacy current-table dBm estimates on generation 4 and unknown identity; retain unrelated earlier-model presentation. Do not calculate replacements from current. Preserve reported-power metadata, independent backend visibility, controller-only limits, output admission, and explicit read/save/apply/restart workflows. Saved, boot-active, cached readback, and measured electrical state are different observations; unavailable members must not display observed defaults.

Refresh labels after identity and selection are available, using direct rendering calls rather than synthetic events that trigger persistence. Preserve drafts, slider position, dirty state, and payloads on label-only updates. Intentional user edits, resets, and backend changes retain their legitimate write behavior; tests must isolate extra rendering-induced writes. Lost replies must not trigger automatic retries, restarts, or hidden fallback.

## Shared contracts and paired integration

Read and follow both repositories' current coordinated capability/protocol gate for any descriptor, advertised limit/unit, API/schema, or protocol-facing change. Validate shape before field access and reject extra/missing fields, wrong types, unsupported versions/values, inconsistent descriptors, and stale device/boot/revision/profile bindings before writes. Update runtime consumers, serialization, generated assets, paired adapters, and docs in the same task.

Follow authoritative Pico `control-policy.json`: retain the single current prerelease control major, identifier, and namespace at 1 until the explicit stable transition; do not introduce historical views, loose version ranges, downgrade/carrier fallback, or weaker validation. Keep WTP, transport/authentication, and storage formats independent. Existing fixture names do not authorize restoring retired WTP execution.

Require both production network and USB hardware-free integration suites for this cross-product task, even if some mappings remain unchanged. Use checked-in WsprryPi `.github/workflows/debian-non-hardware.yml` recipes for `wtp-network-loopback` and `wtp-usb-management-pair`: network `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`; USB actual Pico `usb_management_driver`, `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and `WsprryPi-UI/tests/wtp_usb_pair_test.js`. Explicitly select the intended current Pico/Pi sources and pinned dependencies, and record revisions plus tested uncommitted changes.

Do not substitute mock-only tests, stale refs, prior CI, or standalone builds. Failed/cancelled/timed-out/skipped required checks remain blockers. Resolve companion mismatches affecting the changed contract as integration work, and distinguish local candidate validation from companion availability on intended paired refs. Do not claim delivery on devel/main without that evidence.

## Validation

Inspect launchers before execution. Use installed tools and checked-in build/test recipes, safe local fixtures, and pinned dependencies. Do not use a running appliance, physical device, installer, or service as a validation workaround; report environment blockers without changing pins or weakening assertions. Physical testing requires separate explicit setup authorization.

Test all legal values for every matrix row and negative boundaries/intermediate values; defaults; stored round trips; index-versus-mA errors; identity classification and changes; actual runtime output/register selection; encoding and unrelated-bit preservation where applicable; provider refusal; rejected-write invariants; and activation, pending-restart, fault, and readback semantics. Use production code with independent expected mappings and fake register/SDK adapters or existing test seams. Do not copy the implementation formula into tests or assert unsupported electrical measurements.

Run affected standalone component tests, parent regression checks, Pico host tests, firmware builds when affected, and both paired suites. Choose Pi semantics explicitly from `src`: on macOS or unsupported Linux hosts use `make semantics-test-portable SUDO=`; on Linux with libgpiod C++ headers use `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. Portable coverage is a subset and cannot qualify a changed physical backend. Use canonical Linux CI coverage where needed and document remaining limits.

From Pi `WsprryPi-UI`, run new focused tests, `node tests/conditional_transmit_gpio_integration_test.js`, `node tests/rp1_route_ui_test.js`, `node tests/native_host_ui_test.js`, `npm run test:unit`, and affected real-page browser checks. Select Pico drive, Si5351, schema, and browser tests from its current documented registrations. Register new focused coverage without overwriting existing test scripts or user changes. Record exact commands/results and meaningful warnings/skips.

Exercise initialization, delayed metadata, reload/reconnect, known-to-unknown identity, target/boot changes, backend switching, default reset, draft restoration, keyboard changes, and errors. Assert visible/accessibility agreement, unchanged serialized selection, no extra rendering-induced mutations, retained drafts/dirty state, and intended writes for deliberate edits. Preserve RP1/Si5351 visibility and controller-only restrictions.

Inspect actual rendered Pi and Pico affected interfaces with Impeccable at desktop/mobile sizes in light/dark themes. Cover every affected backend and unknown/disabled/pending/error states. Preserve screenshots and logs outside tracked sources and record viewports and evidence paths. DOM assertions do not replace rendered inspection. Hardware-free evidence does not qualify electrical behavior, timing, installation, service lifecycle, frequency accuracy, or RF output.

## Documentation is included

Update affected operator documentation in `/Users/lbussy/GitHub/WsprryPi-Docs` after verifying its actual location and instructions. The application instruction's `Wsprry_Pi_Docs` spelling was absent during planning. Cross-repository documentation changes are authorized for this implementation; do not defer them as outside scope. Preserve each repository's branch and user changes and edit only its owned content.

Review operator `docs/User_Interface/Setup/Transmitter/index.md`, `docs/Advanced_Operations/rf_electrical.md`, transmitter-backend configuration pages, RP1 pages, and Pico installation/drive/Si5351 pages. Review Pi `docs/wtp-gpio-drive.md`, Pico `engineering/development/gpio-drive.md`, and affected Si5351 and contract guides. Correct affected values, defaults, units, activation steps, qualification statements, and saved/active/readback distinctions. Replace screenshots only if materially inaccurate. Do not undertake an unrelated rewrite, RF-power recalibration, or publication.

Keep prose paragraphs on one physical source line, use Oxford commas, and preserve code blocks/directives/tables. Build operator docs through its documented virtual environment with `python -m sphinx -b html docs docs/_build/html`; run Pico's documented docs checks. Inspect affected rendered documentation with Impeccable at desktop/mobile sizes and verify links. Resolve in-scope errors and record unrelated warnings or blocked prerequisites.

## Adversarial assessment and completion

Assess the full cross-product matrix after implementation, including backend code, generated assets, strict validators, both carriers, bindings, persistence, qualification boundaries, output safety, UI/accessibility, documentation, test realism, and scope. Fix actionable in-scope findings, rerun affected checks, then perform another full assessment. Repeat until no further actionable findings remain. Unavailable required validation remains a blocker; a clean source review does not erase it. Report out-of-scope discoveries without expanding into unrelated work.

Review each complete affected repository/component diff and new files, run final whitespace checks, and inspect staged/unstaged/untracked state. Preserve unrelated changes. Do not stage, commit, push, publish, install, or operate hardware under this prompt without separate authorization.

Report implemented behavior and verified-unchanged mappings, modified repositories/components and source revisions, tested uncommitted changes, standalone and parent tests, both paired suite results and applicable CI links, UI workflows and visual evidence, hardware/runtime evidence actually obtained, remaining blockers, and Git state per repository. Include a Documentation Impact section listing updates, considered-but-unchanged material, required builds/rendering, and any unresolved documentation work. Do not declare completion until necessary code/backend/contract work, required integration, UI review, and documentation updates/checks are complete. Keep software evidence distinct from unperformed physical qualification.
