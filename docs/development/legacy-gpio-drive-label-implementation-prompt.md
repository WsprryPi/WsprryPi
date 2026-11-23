# Legacy GPIO drive-label correction: implementation prompt

Tracking: [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478). Read the [implementation plan](legacy-gpio-drive-label-implementation-plan.md) before implementation.

Implement the narrowly scoped WsprryPi UI correction described below. Follow the current repository instructions and preserve all existing work. This prompt authorizes the specified implementation and hardware-free validation when explicitly invoked; its presence in the repository does not authorize execution. Do not stage, commit, push, publish, deploy, install, operate services, or exercise hardware.

## Outcome

Correct the nominal drive-current presentation for the legacy GPIO slider on Pi 4, Pi 400, and CM4. Preserve the stored pad code and hardware behavior. The existing `GPIO.Power Level` setting remains an integer from 0 through 7. It is not a value in mA.

Use this acceptance matrix for valid stored code `n`:

| Platform | Existing platform generation | Nominal current displayed |
| --- | --- | --- |
| Pi 1, Pi 2, Pi 3, Zero, and Zero 2 W | 1, 2, or 3 | `2 × (n + 1) mA` |
| Pi 4, Pi 400, and CM4 | 4 | `(n + 1) mA` |
| Missing, malformed, or unrecognized platform identity | Unknown | Raw level and an explicit indication that nominal drive is unavailable |
| Pi 5 and other supported RP1 platforms | Existing RP1 path | Preserve the separate 2, 4, 8, or 12 mA control |

For example, code 0 displays 1 mA on Pi 4 and 2 mA on Pi 3; code 7 displays 8 mA on Pi 4 and 16 mA on Pi 3. A UI update must never change code 7 to another code to preserve the old displayed current.

## Scope and non-goals

- Change only the legacy GPIO label calculation, its necessary refresh and accessibility behavior, and focused tests and test registration.
- Preserve the existing slider range, step, default code 7, setting name, serialization, validation, persistence, autosave policy, draft handling, backend selection, output eligibility, and hardware programming.
- Preserve the existing layout and design language. Do not redesign the settings page, rename unrelated controls, or introduce new workflows.
- Preserve earlier-model current labels and their existing secondary dBm presentation. Do not carry those dBm estimates into generation 4 or unknown-platform labels, and do not calculate replacement dBm estimates from nominal current.
- Do not change Si5351 mappings, RP1 drive selection, WsprryPico, Pico qualification profiles, WSPR reported-power metadata, RF-power calculations, or transmitter components.
- Do not add configuration keys, API fields, capability descriptors, schema versions, model-selection controls, migrations, dependencies, or a new platform-detection subsystem.
- Do not edit the sibling operator-documentation repository. Review and report its required follow-up only.
- Do not fix unrelated findings or preexisting test failures as incidental work.

## Inspect before editing

1. Read the applicable `AGENTS.md` files, inspect the current branch and complete working tree, and review staged and unstaged changes. Run `git diff --check` and `git diff --cached --check`. Confirm affected UI files are ordinary parent-repository content.
2. Treat all existing changes as user work. The planning baseline was WsprryPi `devel` at `51257fdb`, with substantial existing changes, including `WsprryPi-UI/package.json`. This is historical context, not an instruction to check out that revision or overwrite current files.
3. Read and apply the Impeccable skill before UI work. Follow its context-loading instructions and read its craft-floor guidance immediately before editing. Do not commit skill or runtime artifacts.
4. Inspect `updateGpioPowerLabel()`, configuration population, draft restoration, default reset, backend switching, and platform refresh. Inspect `syncGpioDriveControls()` and existing RP1 and Si5351 behavior as regression boundaries.
5. Trace `get_raspberry_pi_generation()` in `src/version.cpp`, the existing public platform serialization, and `site.js` population of `window.WSPRRYPI_PLATFORM.raspberryPiGeneration`. Confirm the intended named models are classified appropriately, including Pi 400, CM4, Zero, and Zero 2 W. Account for actual model strings and processor fallback; do not infer that a board's marketing name equals its processor generation.
6. Reconfirm that legacy `GPIO.Power Level` is passed unchanged as the 0–7 pad code to the transmitter. This is source verification, not physical pad or RF qualification.
7. Inspect existing test launchers before running them. Identify their fixtures, side effects, prerequisites, and output paths.

If platform classification or backend encoding prevents the stated behavior, stop that dependent implementation, report the exact evidence, and ask for direction before changing detection, backend code, or public contracts. Do not conceal the problem with a JavaScript model-name parser or a guessed fallback. Preserve independent completed work.

## Implementation

The expected primary file is `WsprryPi-UI/data/index.js`. Modify `WsprryPi-UI/data/site.js` only if the existing metadata lifecycle requires an explicit label refresh. Modify `WsprryPi-UI/data/views/config.php` only if necessary for the affected control's accessible presentation. Keep tests within `WsprryPi-UI/tests` and make only additive, focused test-registration changes to the existing package scripts. Do not create a generic formatting framework.

Use a small, testable formatter or equivalent focused logic driven by the existing platform generation and selected pad code. Reuse the established platform-data contract; do not accept arbitrary truthy values or coerce blank, null, or malformed identities into a known generation. A valid slider value arrives as a string, so distinguish legitimate integer strings from invalid or empty inputs without changing configuration validation.

For recognized generations, display the matrix's nominal current. For unknown identity, preserve the raw valid code and use neutral wording such as `Level 7 — nominal drive unavailable`. For invalid codes, show an unavailable state without inventing a current or silently repairing the setting. A later identity refresh must clear a stale current label when identity becomes unknown. Unknown identity must not newly enable, disable, or expose hardware controls; preserve existing availability policy.

Keep visible and accessible values consistent. Set the slider's `aria-valuetext`, or use the established equivalent, so keyboard and assistive-technology users receive the same nominal-current meaning. Preserve the native range control and its 0–7 value semantics. Do not describe nominal current as measured current or calibrated RF power.

Ensure the presentation refreshes after platform metadata and the selected code are available on initial load, configuration reload, reconnect, draft restoration, default reset, and backend switching. Reuse existing refresh calls where sufficient. If another call is needed, invoke the presentation function directly; do not dispatch input/change events solely to refresh labels. Label-only updates must not alter slider position, saved payloads, drafts, dirty state, or network mutation counts.

Distinguish presentation refreshes from intentional operator edits. Resetting defaults or changing the selected backend may already change configuration under existing policy; preserve that policy. Tests must compare the same workflow before and after the correction, isolating any extra writes caused by label rendering rather than forbidding the workflow's legitimate writes.

Preserve separate RP1 control visibility and enablement, Si5351 labels, and controller-only behavior. A hidden legacy control must not affect active RP1 or Si5351 configuration.

## Tests and validation

Add focused tests following the existing UI infrastructure. Exercise production formatter/rendering code rather than a duplicate implementation embedded in tests. Browser coverage must load the real application assets through local fixtures, not a standalone mock page.

Cover these acceptance cases:

- Every valid code 0–7 for generations 1, 2, 3, and 4, including exact endpoint and default labels.
- Pi 400 and CM4 classification through the existing producer-to-UI metadata path. Supply evidence for the named earlier-model classifications too. Browser fixtures alone do not prove native model detection; use existing native test seams or source-path evidence where appropriate. Do not add a production override or duplicate detector solely for testing.
- Missing platform object, missing generation, unknown/unsupported generation, malformed identity, and invalid code inputs. No fabricated current, stale current, or fallback to the earlier-model mapping.
- Initial load before metadata, metadata arrival after the slider value, configuration reload/reconnect, known-to-unknown identity refresh, draft restoration, default reset, and backend switching.
- Generation 4 and unknown labels have no inherited dBm estimate; earlier-model presentation is preserved.
- Visible and accessible values agree, including keyboard changes.
- Serializing an unchanged selection preserves its original code. Label-only lifecycle refreshes send zero additional mutation requests and preserve unsaved drafts and dirty state. Deliberate user changes retain the existing autosave behavior.
- RP1 and Si5351 controls retain their current mappings, visibility, and enablement; controller-only hosts gain no local transmission capability.

From `WsprryPi-UI`, run the new focused tests and the following existing commands after inspecting their launchers:

```sh
node tests/conditional_transmit_gpio_integration_test.js
node tests/rp1_route_ui_test.js
node tests/native_host_ui_test.js
npm run test:unit
```

Run the applicable existing browser coverage for configuration loading and backend switching, registering any new browser test in the established runner. Record the exact commands selected and their results. Report unavailable prerequisites and unrelated failures accurately; do not weaken assertions or claim a skipped check passed.

Use the existing installed test tooling and local fixture endpoints. Do not add dependencies, download a browser or SDK, connect to a physical Pi or Pico, or invoke a running appliance as a validation workaround. If a prerequisite is unavailable, report the exact blocked check and continue independent in-scope work. If an existing test launcher exercises hardware or services, do not run it; report why that named check remains unperformed and use only a demonstrably hardware-free equivalent for additional evidence.

Use Impeccable to inspect the actual rendered control at desktop and mobile sizes, in light and dark themes. Review an earlier-model state, generation 4, and unknown identity, including keyboard operation and the accessible value. Capture visual evidence outside tracked source paths, and record the viewport sizes, states, and evidence locations. Do not substitute DOM-only assertions for rendered inspection.

No application transmission, installation, service, hardware, or RF validation is authorized. This task does not require a C++ semantics build when production C++ remains unchanged. Hardware-free fixture results qualify software presentation and persistence behavior only.

The planned correction consumes existing platform metadata and changes presentation only; it does not change advertised limits, wire units, schemas, shared control semantics, or companion interfaces. Therefore, the coordinated Pi/Pico network and USB integration gate is not triggered by this scope. If implementation would cross that boundary, stop for scope direction; any authorized expansion must satisfy the full repository gate with the intended source pair.

## Documentation impact

The available operator repository at planning time was `/Users/lbussy/GitHub/WsprryPi-Docs`; the `Wsprry_Pi_Docs` spelling in the application instructions did not exist. Verify the current location and read its instructions before inspecting it. Do not write there under this prompt.

Report the exact follow-up for `docs/User_Interface/Setup/Transmitter/index.md`, under `GPIO Power Level`, which presents the earlier-model mapping generally, and `docs/Advanced_Operations/rf_electrical.md`, which gives a general approximate 16 mA maximum. The follow-up should distinguish nominal current by model, explain the retained 0–7 pad code where useful, and avoid deriving calibrated RF power from drive current. Review affected screenshots and recommend replacement only if materially inaccurate. Do not broaden into a full electrical-documentation rewrite.

The implementation report must include a `Documentation Impact` section listing documentation updated, documentation considered but unchanged, and documentation still required. Cross-repository follow-up remains outstanding until separately authorized and validated. Do not describe the whole effort as complete while required implementation checks or documentation work remain unresolved; distinguish a validated UI correction from the outstanding documentation boundary.

## Adversarial review and completion

After implementing, assess the entire change against this prompt. Challenge model classification, boundary values, malformed and changing metadata, accessible labels, dBm handling, persistence compatibility, incidental autosaves, initialization ordering, hidden-backend interference, test realism, dirty-tree preservation, and documentation claims. Fix in-scope issues, rerun affected checks, and then perform another full assessment. Repeat until no actionable in-scope issues remain. Report blockers and out-of-scope findings rather than silently expanding scope or asserting uncertainty has disappeared.

State the material assessment findings and their resolutions in the final report. An unresolved required check remains unresolved even when no additional source defect is found. Do not add permanent review diaries or extra planning artifacts to the repository.

Review the complete affected component diff, the focused package-script changes, and any new files. Run a final whitespace check and inspect staged, unstaged, and untracked state. Ensure no unrelated existing changes were overwritten or included in the implementation's claimed scope.

Report the behavior implemented, important files changed, exact automated commands and results, Impeccable workflow and visual evidence, documentation impact, remaining validation or blockers, branch and working-tree state, and whether anything was staged, committed, or pushed. There must be no staging, commit, or push under this prompt. State explicitly that no physical pad, RF, installation, or service qualification was performed.
