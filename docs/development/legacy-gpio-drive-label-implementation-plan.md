# Legacy GPIO drive-label correction: implementation plan

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478).

Correct WsprryPi's nominal drive-current labels on Pi 4, Pi 400, and CM4 while preserving stored settings and hardware behavior. Follow the [implementation prompt](legacy-gpio-drive-label-implementation-prompt.md) when implementation is explicitly authorized. Saving or publishing this plan does not authorize implementation.

## Scope

The implementation is limited to the legacy GPIO slider's current labels, necessary refresh and accessibility behavior, and focused tests and test registration. WsprryPico, Si5351 mappings, RP1 drive selection, transmitter components, RF-power calculations, and shared control contracts remain unchanged. No configuration migration, new platform fields, detection subsystem, dependencies, installation, services, or hardware operation are included.

The stored `GPIO.Power Level` remains a pad code from 0 through 7, with default code 7. It is not a persisted current in mA. Loading the corrected interface must not reinterpret or migrate a saved selection.

## Acceptance matrix

| Platform | Existing generation | Nominal current for code `n` |
| --- | --- | --- |
| Pi 1, Pi 2, Pi 3, Zero, and Zero 2 W | 1, 2, or 3 | `2 × (n + 1) mA` |
| Pi 4, Pi 400, and CM4 | 4 | `(n + 1) mA` |
| Missing, malformed, or unrecognized identity | Unknown | Raw valid level with nominal drive unavailable |
| Pi 5 and other supported RP1 platforms | Existing RP1 path | Existing separate 2, 4, 8, or 12 mA control |

Code 0 must display 1 mA on Pi 4 and 2 mA on Pi 3. Code 7 must display 8 mA on Pi 4 and 16 mA on Pi 3. Invalid codes must not produce a fabricated current or silently repair the setting. Si5351 retains its existing 2, 4, 6, and 8 mA mapping.

## 1. Establish the implementation baseline

Read current repository and nested instructions, inspect the branch and complete working tree, review staged and unstaged changes, and run both diff whitespace checks. Preserve all existing work, including existing modifications to package scripts. The planning baseline was `devel` at `51257fdb` with substantial existing changes; recheck current state rather than checking out that revision.

Confirm affected UI files are ordinary parent-repository content. Do not switch branches, stage, commit, push, or modify companion repositories during implementation unless separately authorized. The publication of these planning documents is a separate authorized action.

## 2. Verify the existing contracts

Trace `get_raspberry_pi_generation()` in `src/version.cpp` through public configuration serialization and `WsprryPi-UI/data/site.js` to `window.WSPRRYPI_PLATFORM.raspberryPiGeneration`. Verify actual model strings and processor fallback for Pi 400, CM4, Zero, Zero 2 W, and the other listed families. Do not introduce another model parser in JavaScript.

Confirm that `GPIO.Power Level` remains a 0–7 pad code passed unchanged to the backend. If a detection or backend-encoding defect prevents the matrix, stop dependent work and report the evidence before expanding scope. Do not conceal a defect with a guessed identity or backend workaround. Source inspection does not qualify electrical behavior.

## 3. Correct the presentation

Use a small, testable formatter or equivalent focused logic in `WsprryPi-UI/data/index.js`, where `updateGpioPowerLabel()` currently uses a fixed table. Consume the existing platform generation and selected code. Distinguish valid integer slider strings from empty or invalid inputs without changing configuration validation. Do not coerce malformed identity into a known generation.

Preserve slider range, step, default, stored key, serialized values, validation, hardware programming, and layout. Display the matrix's current for known generations. For unknown identity, use neutral wording such as `Level 7 — nominal drive unavailable`, and clear stale current labels after identity changes. Preserve existing hardware availability and control visibility policy.

Do not reuse the existing secondary dBm estimates for generation 4 or unknown identity, and do not derive replacement RF-power estimates from nominal current. Preserve the earlier-model current labels and secondary presentation to keep the correction focused. Keep visible and accessible values consistent, using `aria-valuetext` or the established equivalent without changing native range semantics.

Modify `site.js` only if an explicit metadata refresh call is necessary, and `data/views/config.php` only if needed for the affected control's accessible presentation. No generic formatting framework or unrelated UI cleanup is included.

## 4. Preserve configuration lifecycle behavior

Verify initialization, platform metadata arrival, configuration reload, reconnect, backend switching, draft restoration, default reset, and keyboard changes. Refresh presentation after identity and selection are available. Reuse existing calls where sufficient; invoke rendering directly when another refresh is necessary rather than dispatching synthetic input/change events.

Label-only refreshes must preserve slider position, drafts, dirty state, payloads, and network mutation counts. Intentional user edits, default resets, and backend changes retain their existing persistence policy. Test for additional writes caused by rendering rather than incorrectly forbidding legitimate workflow writes. Hidden legacy controls must not affect RP1, Si5351, or controller-only operation.

## 5. Add focused regression coverage

Exercise production formatting and rendering code, not a copied test implementation. Use the existing real-page browser fixtures. Register focused tests in existing package scripts without overwriting unrelated work.

- Check every code 0–7 for generations 1, 2, 3, and 4, including default code 7.
- Verify named-model classification through the existing producer-to-UI path. Browser fixture identities alone do not prove native detection; use existing native seams or explicit source-path evidence without adding production overrides.
- Check missing, malformed, unsupported, and changing identity, invalid codes, delayed metadata, and known-to-unknown transitions.
- Check configuration reload, reconnect, draft restoration, reset, backend switching, visible labels, accessible values, and keyboard operation.
- Check that generation 4 and unknown identity show no inherited dBm estimate and that earlier-model presentation is preserved.
- Check unchanged serialization, preserved drafts and dirty state, zero added mutations from rendering, and existing deliberate-edit autosave behavior.
- Check RP1 and Si5351 mapping, visibility, and enablement, and the absence of newly enabled local transmission on controller-only hosts.

## 6. Validate software and rendered behavior

Inspect test launchers before execution and use installed tooling and local hardware-free fixtures. Do not download a browser or SDK, add dependencies, use a physical Pi or Pico, or connect to a running appliance as a workaround. Report blocked prerequisites and unsafe launchers without treating skipped checks as passes.

From `WsprryPi-UI`, run the new focused tests and these existing checks when their inspected launchers are safe:

```sh
node tests/conditional_transmit_gpio_integration_test.js
node tests/rp1_route_ui_test.js
node tests/native_host_ui_test.js
npm run test:unit
```

Run applicable existing browser coverage for configuration loading and backend switching. Record exact commands and outcomes. Do not repair unrelated failures or weaken assertions. Production C++ is unchanged, so a C++ semantics build is not required for this scope.

Use Impeccable during implementation, including required context and craft guidance, and inspect the actual rendered control at desktop and mobile widths in light and dark themes. Review earlier-model, generation 4, and unknown-identity states, keyboard interaction, and accessible values. Record viewport sizes and visual evidence outside tracked source paths. DOM assertions do not replace rendered inspection.

This display-only correction does not change advertisements, wire units, limits, schemas, shared control semantics, or companion interfaces, so paired Pi/Pico network and USB integration suites are not required by this scope. If implementation would cross that boundary, stop for direction; any authorized expansion must satisfy the repository's full coordinated-contract gate.

## Documentation Impact

The available operator repository during planning was `/Users/lbussy/GitHub/WsprryPi-Docs`; the `Wsprry_Pi_Docs` spelling in application instructions did not exist. Verify the current location and read its instructions before inspection. Cross-repository edits are not authorized by this implementation plan.

Required follow-up is `docs/User_Interface/Setup/Transmitter/index.md`, under `GPIO Power Level`, which presents the earlier-model mapping generally, and `docs/Advanced_Operations/rf_electrical.md`, which gives a general approximate 16 mA maximum. Distinguish model-specific nominal currents and retained 0–7 code semantics without deriving calibrated RF power. Review affected screenshots and recommend replacement only if materially inaccurate. Do not broaden this into an electrical-documentation rewrite.

Report documentation updated, considered but unchanged, and still required. A validated UI correction and outstanding operator-documentation work must remain distinct; do not claim the whole effort complete while required work remains unresolved.

## Assessment and completion gates

Assess the implemented change for mapping correctness, identity assumptions, invalid and changing data, accessible labels, dBm handling, persistence compatibility, incidental autosaves, initialization ordering, hidden-backend interference, test realism, scope, existing work preservation, and documentation claims. Fix in-scope issues, rerun affected checks, and perform another full assessment. Repeat until no actionable in-scope issues remain. Unavailable validation remains unresolved regardless of source-review results.

Accept implementation only after the mapping matrix passes, saved codes remain unchanged, rendering adds no mutations, backend controls retain behavior, and required software and rendered checks pass. Review the component diff, package-script edits, and new files; run final whitespace checks and inspect staged, unstaged, and untracked state. No installation, service, physical pad, timing, transmission, or RF qualification is implied.

Report behavior, changed files, exact test results, visual evidence, documentation impact, remaining blockers, branch and working-tree state, and staging/commit/push status. Stop at this bounded implementation; broader changes need separate direction.

## Plan assessment record

The first adversarial review identified missing identity verification, unknown-platform behavior, and Pi 4 dBm handling. The plan was revised to verify the existing metadata path, use neutral unknown-state labels, and suppress unsupported estimates. The second full review identified autosave risks, lifecycle-test gaps, and ambiguous documentation and protocol boundaries; the plan was revised to isolate rendering from persistence, cover defaults and drafts, identify exact documentation follow-up, and define the contract-change stop condition. The final review found no further plan-level issues. Implementation evidence remains required.
