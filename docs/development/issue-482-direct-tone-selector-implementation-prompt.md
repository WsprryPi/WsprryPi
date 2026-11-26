# Implement issue #482: selector-free direct CLI Test Tone

Use this prompt only after a separate instruction authorizes implementation. Creating, reading, or reviewing it does not start implementation or authorize Git publication, installation, services, trust/configuration changes, GPIO, or RF operation.

Implement [issue #482](https://github.com/WsprryPi/WsprryPi/issues/482), TODO item 11, according to the complete [implementation plan](issue-482-direct-tone-selector-plan.md). The plan defines scope, behavior, evidence limits, documentation, and acceptance. This prompt operationalizes those requirements without expanding them. If the issue, plan, candidate source, or execution authorization materially disagree, identify the exact conflict before dependent edits; do not silently choose broader behavior.

## Deliverable and authority

Deliver the smallest maintainable parent-application fix allowing an admitted numeric direct CLI Test Tone at exactly 13,560,000 Hz without a band-selector GPIO. A missing amateur-band match must not make a selector-free direct tone fail. Outside-catalog selector use requires the existing explicit per-frequency `@GPIO[H|L]` suffix. Preserve configured amateur-band mappings without selecting one for an unknown band. Keep 22 m outside the amateur-band catalog.

Preserve all existing frequency/backend/output admission, both required GPIO outside-band overrides (`--allow-unqualified-frequency` and `--allow-non-amateur-frequency`), pin ownership, RP1 confirmation/replay rules, startup inhibition, cancellation, and cleanup. Numeric and unit-suffixed tones retain exact RF semantics; band/preset tones retain their existing fixed WSPR placement. Do not move the carrier to make a backend accept it. A scheduler success is not measured output or physical qualification.

Do not add UI/API settings, alter managed Test Tone, scheduling, band catalogs, drive strength, correction, I²C selection, storage, capabilities, shared schemas, or Pico behavior. Do not widen reusable components or the web-oriented known-band tone planner. Parent scheduler source, narrowly necessary internal declarations, parent regression fixtures/build wiring, and relevant parent engineering documentation are the expected edit boundary. A new component/protocol/UI requirement needs a reviewed scope decision and its mandatory validation; do not absorb it silently.

## Orient before edits

1. Read actual root and nested repository instructions. Record branch, HEAD, staged/unstaged changes, relevant untracked files, and `git diff --check` / `git diff --cached --check`. The plan's historical source hash is provenance, not the source to reset or test automatically. The planning checkout contains substantial unrelated controller-port work, including overlapping scheduler and Makefile changes. Preserve all of it. Do not switch, reset, stash, rebase, synchronize, stage, commit, push, or publish without the applicable instruction.
2. Trace production CLI parsing and admission through startup to `start_direct_tone_execution()`. Inspect `parse_frequency_entry_token()`, `set_direct_tone_startup_request()`, policy/profile evaluation, configuration/pin-conflict validation, and `arg_parser_runtime.cpp`. Normal startup and `apply_runtime_config_side_effects()` may select physical backends or initialize outputs; read those paths without executing them.
3. Inspect every caller/declaration of `prepare_band_gpio_for_frequency_or_log()`, the selector resolution/apply/teardown code, `BandGPIOResolution`, `commit_band_gpio_snapshot_to_request()`, and direct-tone request commit/dispatch. Inspect the existing direct-tone policy/explicit-selector tests in `src/tests/dial_frequency_semantics_test.cpp`, lifecycle and rotation/shutdown fixtures, `src/Makefile`, `docs/simulated-backend.md`, and the current Debian non-hardware CI recipe. Confirm which fixtures suppress execution and enable GPIO test mode before using them.
4. Establish the actual acceptance baseline and safe Linux runner/profile. Existing external toolchains, runner access, operator-documentation repository writes, live hardware, and deployment remain limited to the execution authorization. Continue independent authorized work if a gate is unavailable, but report that gate accurately.

## Implement in this order

1. Add a behavior regression reproducing the failure through the production CLI parser/admission and parent direct-tone execution function under test-only suppression. Demonstrate the selector-free admitted 13.560 MHz case fails before the fix for the expected reason. Do not reproduce the issue with its privileged conducted-bench command. If the current candidate already passes, investigate and report whether the fix is present; do not manufacture redundant production changes.
2. Introduce a bounded internal selector-resolution policy allowing an unknown-band inactive result only for the direct CLI Tone call. Use a typed policy or equivalent internal approach, keep other callers strict, and update all necessary declarations/call sites coherently. Do not use a new external option or a global unknown-band success condition.
3. Keep existing actual-RF policy, direct-operation confirmation, and inhibition before the new success path. Preserve the unknown-band explicit-selector branch and all known-band explicit/fallback/no-selector behavior. Only an unknown band with no explicit selector under the direct-tone policy resolves to selector disabled, empty GPIO configuration, and unknown band. Never index the band table using a sentinel or infer a replacement band.
4. Clear/release a previous active selector through the existing lifecycle before publishing the inactive result. Honor reported failure returns; do not ignore a newly encountered failure to reach success. The existing teardown can log a failed physical deassert attempt while returning success: preserve its established contract without claiming physical deassertion. Report any independently reproducible safety defect and hold the affected success claim for a scope decision; do not broaden this fix into a teardown redesign or hide an observed unsafe transition.
5. Commit the ordinary selector-disabled Tone snapshot and route, then dispatch once through the existing path. No early success may bypass request construction, ownership/inhibition, snapshot clearing, or dispatch. Keep genuine policy/preparation/output/cleanup errors; an admitted selector-free request must no longer emit the fatal unknown-band selector error. Limit any new explanation to the existing diagnostic style.

## Required acceptance cases

Use temporary state and the production parser/admission plus parent execution seams. Initialize execution suppression, GPIO test mode, and the intended BCM2711 profile before any selector setup; restore overrides, start hooks, transient requests, snapshots, routes, and selector state between cases. Parser helpers alone do not prove full pin-conflict/configuration admission. Match each negative case to its real rejection boundary.

| Case | Evidence required |
| --- | --- |
| `13560000` and `13.560MHz`, both GPIO overrides, no suffix | Exact 13,560,000 Hz Tone snapshot, no offset/random placement, selector disabled, correct committed route, one suppressed dispatch. |
| Same request with unrelated enabled amateur-band mappings | No mapping selected or changed; no default-band lookup; same successful inactive result. |
| `13560000@17H` and `13560000@17L` | Existing explicit GPIO/polarity retained; preparation before commit/dispatch; normal cleanup. GPIO17 is fixture data only. |
| Neither override, each override alone | Existing BCM2711 outside-band refusal before selector preparation, commit, and dispatch. Both flags are necessary for this GPIO case; do not invent a universal backend rule. |
| Bad syntax/range, ancillary-disabled build, RF/reserved-pin conflict | Existing appropriate parser/configuration refusal; no committed/started request; fresh failed parser fixture has no startup request. |
| Explicit preparation failure or existing startup inhibit | No new request commit/dispatch; established failure cleanup and error preserved. |
| Prior active explicit selector followed by selector-free tone | Existing active selection cleared/released; no newly asserted selector or inherited GPIO/polarity. Exercise supported teardown failure reporting without claiming electrical evidence. |
| Ordinary amateur numeric/band/preset tone | Numeric versus band-center frequency semantics, explicit selection, permitted configured fallback, and fallback-disabled inactive behavior preserved. |
| Existing outside-band negatives and positives | Retain 150 MHz rejection without overrides and the existing admitted explicit-selector case; do not rewrite them as permissive cases. |
| Other scheduler callers, managed Test Tone, persistence | Existing strict behavior, cancellation/restoration, and final cleanup retained; no saved settings or band-map changes. |

A suppressed start hook proves scheduler behavior, not downstream synthesis. Preserve the existing compiler/backend frequency-validation coverage; add an exact-carrier downstream software assertion if the existing fixture supports it without hardware initialization. Unsupported frequencies must still fail through their existing downstream path and cleanup; do not require a downstream rejection to occur before a scheduler commit when that is not the production contract. Report lack of such exact-carrier evidence explicitly; never disable a backend check or claim RF success from a mocked dispatch.

Extend the existing parent semantics fixture or add a focused parent fixture when needed for safe isolation. Identify a new target as new and include it in the appropriate existing non-hardware CI recipe. Do not count `wspr-tone-regression-test` as direct-tone coverage: it checks WSPR symbol expansion. Compiling physical-backend code is allowed for inhibited software testing; starting physical outputs is not.

## Run validation

Inspect the actual recipes and fixture setup first. On an authorized Linux runner with libgpiod development headers, run from `src` with hardware access disabled and no `sudo`:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make gpio-band-policy-test band-gpio-rotation-test selector-shutdown-cleanup-test test-tone-lifecycle-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make test-tone-request-test test-tone-frequency-plan-test test-tone-selector-plan-test SUDO=
```

Run any new focused acceptance target under the same conditions. Avoid rerunning successful prerequisite checks unless changes or failures justify it. On macOS use `make semantics-test-portable SUDO=` for its stated simulated-only subset; it does not replace the full Linux BCM2711/ancillary-enabled regression. An unavailable Linux runner leaves required validation outstanding. Do not run `make test-tone`, installation/removal targets, real services, normal GPIO startup, or the issue's bench command. Simulation must be explicitly selected and is neither a physical-backend fallback nor GPIO/RF qualification.

No shared control contract or companion change is expected. If implementation crosses that boundary, stop for the needed scope decision and apply all coordinated producer/consumer, strict validation, generated-asset, and actual network/USB integration gates in repository instructions. A parent-only unit test cannot waive them.

## Documentation and closure

Update parent `docs/band-and-wspr-presets.md` with exact numeric tone semantics, selector-free outside-catalog admission under existing policy, and explicit suffix selection when desired. Distinguish requested output from filtering or physical qualification. Keep paragraph/list prose on single physical lines. Do not present the privileged issue reproduction as an unrestricted operator example.

Operator-documentation follow-up belongs in `WsprryPi-Docs/docs/Command_Line_Operations/service_test_controls.md` and `transmitter_backends.md`; review `quick_start.md` for conflicting examples. Write there only under explicit cross-repository authorization, after reading its instructions and preserving its dirty work; use its required build/link/render workflow. If unauthorized or unvalidated, list the exact follow-up as outstanding. No UI changes are planned; any newly authorized UI or UI-depicting documentation work must follow Impeccable and the required visual review.

Adversarially assess the full change against the plan and every table row, including retained callers, policy order, stale-selector state, failure-before-dispatch, build profiles, unrelated dirty work, and documentation. Fix actionable in-scope findings, rerun affected validation, then perform another full assessment. Repeat until a full pass finds no further actionable findings. Missing evidence, blocked documentation, or observed safety failures remain explicit gates, not a clean completion claim.

Finish with `git diff --check` and a review separating task changes from pre-existing changes. Report behavior, modified files/components, exact candidate revision and dirty-state qualifications, exact commands and passed/failed/skipped results, final assessment findings, remaining gates, and a Documentation Impact section listing updated, unchanged, and outstanding documentation. State explicitly whether any commit/push occurred. Without separate authorization, leave changes unstaged and do not change the GitHub issue state.

Installation, service operation, physical USB/GPIO, electrical/filter behavior, timing, frequency accuracy, and RF remain unqualified. A later conducted check requires separate authorization and confirmation of the device, binary, exact carrier, finite duration, output/load/filter path, wiring, stopping procedure, and restoration. Do not begin it to fill a software evidence gap.
