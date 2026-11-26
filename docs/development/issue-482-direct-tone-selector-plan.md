# Direct test tone without an inferred band selector

Status: proposed implementation plan for [issue #482](https://github.com/WsprryPi/WsprryPi/issues/482), TODO item 11. No implementation, transmission, installation, service operation, commit, or push is authorized by this document.

## Outcome and bounds

Allow an explicitly admitted numeric direct CLI Test Tone at 13,560,000 Hz when no selector is requested, without requiring an amateur-band lookup to succeed. Preserve exact carrier semantics and every existing frequency, backend, output, ownership, startup-inhibition, and cleanup check. A successful software request is not measured RF output or qualification of the 22 m band.

For an outside-catalog carrier, select a GPIO only through the existing explicit per-frequency `@GPIO[H|L]` suffix. With no suffix, select none; do not borrow a configured amateur-band GPIO, choose the nearest band, or invent a 22 m amateur-band entry. Existing unrelated band mappings may remain configured and must not be altered. An explicit selector still undergoes the existing syntax, range, build-capability, pin-ownership, preparation, polarity, and teardown rules.

This is a parent-application direct CLI orchestration fix. It does not add a browser/API frequency option, alter managed Test Tone, change recurring schedules, add I²C filter control, modify drive strength or frequency correction, extend backend frequency capabilities, change the amateur-band catalog, or relax either outside-band override. It does not change Pico, shared control schemas, advertised capabilities, storage formats, or reusable transmitter components. UI work and operator-documentation repository writes require their applicable workflows and authorization if subsequently needed.

## Evidence and entry inspection

The issue records a Pi 4 Model B, BCM2711 GPIO backend, GPIO4, and `4.0.0-rc.1+70259739` refusing the numeric carrier before RF starts because selector band lookup fails. Its bench command is evidence only and must not be executed as a planning or software-validation step.

Planning inspection on 2026-11-25 used local `devel` at `e7d44eaae25d763af97fde4776382443098d3e28`, with extensive uncommitted controller-port work. This checkout is behind the already published TODO update; source inspection describes this working tree, not the current remote release. Relevant dirty files include `src/scheduling.cpp`, `src/test_tone_selector_plan.cpp`, and `src/Makefile`. Preserve all existing work and re-read these files and their diffs at implementation entry. Do not reset, pull over, rebase, or copy a stale scheduler over the active work.

Inspect the actual candidate's root and nested instructions, staged/unstaged changes, source revision, selected build profile, and the following paths before implementation:

- `src/arg_parser.cpp`: `parse_frequency_entry_token()`, `set_direct_tone_startup_request()`, numeric/range validation, and pin-conflict admission. Numeric and unit-suffixed requests retain the exact RF carrier; band/preset requests retain their existing fixed WSPR placement.
- `src/arg_parser_runtime.cpp`, the production startup caller, and `src/scheduling.cpp`: trace startup through `start_direct_tone_execution()`, policy and inhibition checks, selector preparation, request commit, and asynchronous dispatch.
- `src/scheduling.cpp` and `src/scheduling_internal.hpp`: `prepare_band_gpio_for_frequency_or_log()`, `apply_band_gpio_resolution()`, `stop_active_transmission_selectors()`, `BandGPIOResolution`, and `commit_band_gpio_snapshot_to_request()`. The helper also serves non-Tone scheduling, so a global unknown-band relaxation would broaden this fix.
- `src/tests/dial_frequency_semantics_test.cpp`: existing direct-tone policy rejection, start-invocation/commit checks, and outside-band explicit-selector coverage. The current 150 MHz rejection without overrides remains valid and must not be replaced by a permissive test.
- `src/tests/test_tone_selector_plan_test.cpp`, `src/tests/test_tone_lifecycle_test.cpp`, selector rotation/shutdown tests, `src/Makefile`, `docs/simulated-backend.md`, and the checked-in Debian non-hardware workflow.

Current behavior evaluates GPIO band policy before selector work. The unknown-band helper branch already accepts an explicit selector; without one, it returns `Failed`. Known-band branches support explicit selection, configured fallback where permitted, or `Inactive`. The request snapshot clears selector fields unless preparation is `Prepared`. The separate `plan_test_tone_selector()` accepts a known `HamBand`; widening that web-oriented planner is not necessary for this CLI defect.

## Required behavior matrix

| Direct CLI request | Required result |
| --- | --- |
| Numeric 13,560,000 Hz, no suffix, existing GPIO admission satisfied | Reach request commit and dispatch once with the exact RF target and selector disabled. |
| Same carrier, enabled mappings for unrelated amateur bands, no suffix | Same inactive-selector result; preserve the mappings without selecting any of them. |
| Same carrier with valid explicit suffix | Retain the explicit GPIO and polarity and existing prepare-before-commit ordering. |
| Same carrier with either required GPIO override absent | Refuse under existing frequency policy before selector preparation, commit, or dispatch. Test neither flag, each flag alone, and both flags. |
| Invalid suffix, unavailable ancillary GPIO, reserved/conflicting GPIO, or selector preparation failure | Refuse at the existing admission/preparation boundary; do not commit or start the tone. |
| Known amateur-band numeric carrier or band/preset token | Preserve exact numeric versus band-center semantics and existing explicit/fallback/no-selector resolution. |
| Existing startup/output/ownership inhibition or unsupported backend frequency | Continue to refuse; selector absence grants no new output authority or backend capability. |
| Outside-band request in other modes or managed/browser Test Tone | Preserve existing behavior; no inherited relaxation from this CLI change. |

Use existing policy results for each backend/profile. The dual-override matrix above describes the reported GPIO case, not a new universal backend policy. Preserve existing RP1 direct-operation confirmation and replay admission. Do not make simulation an automatic fallback or use a simulator pass as proof of physical GPIO feasibility.

## Implementation design

1. Add a narrow internal selector-resolution policy for the direct CLI Tone call, with all other callers retaining the present strict unknown-band behavior. An internal enum or equivalent typed policy may distinguish the direct-tone no-selector allowance; do not add a public CLI, INI, JSON, environment, or protocol setting. Prefer this bounded change over duplicating selector ownership and teardown logic in a new subsystem.
2. Keep frequency policy and existing direct-tone confirmation/inhibition in their current order before any new success path. Evaluate the actual RF carrier using the existing policy. Do not implement an early `true` return that bypasses request construction, ownership checks, or ordinary commit/dispatch.
3. In the unknown-band case, preserve the existing explicit-selector branch. Only for the direct-tone policy with `selector_gpio == kSelectorGpioUnset`, resolve an inactive selector with `band_known = false`, `selector_enabled = false`, and an empty GPIO configuration. Do not access the configured band array using a default/sentinel band. Known-band resolution and fallback remain unchanged.
4. Apply the existing selector teardown/release lifecycle before publishing an inactive result so a preceding explicit selector cannot remain selected. Honor returned failures where the current API reports them. Do not strengthen software observations into a claim of physical deassertion: the current teardown logs a failed `setBandState(false)` but can still return success. A broader teardown safety redesign is outside this fix; retain the existing inhibit/error behavior, test the available failure contract, and report any independently reproducible safety defect explicitly instead of masking it.
5. Commit the existing selector-disabled snapshot and ordinary Tone request, then invoke the normal dispatch once. The disabled snapshot's internal sentinel must never be used to infer an amateur band, drive a GPIO, or claim band identity. Retain the existing explicit-selector snapshot representation without adding fields or changing component interfaces.
6. Keep diagnostics accurate: a permitted selector-free direct tone must not emit the fatal unknown-band selector message. A debug-level explanation may say that no selector was requested; do not suppress genuine policy, explicit preparation, cleanup, or output failures and do not change ordinary logging levels broadly.

No reusable component changes are planned. If inspection shows the fix requires a component interface, shared control contract, or companion change, stop that expansion for a reviewed scope decision. Do not edit a component to accommodate a parent-only test fixture. Contract-facing expansion would also invoke the coordinated producer/consumer and both-carrier integration requirements in repository instructions.

## Regression and validation plan

Write a regression that fails on the inspected behavior before the fix, using the production direct CLI parser and direct-tone start function. Reuse the existing scheduler execution suppression, typed start invoker, request/route snapshots, GPIO test mode, and Pi-generation override fixtures. Restore every fixture and transient startup request between cases. GPIO test mode and execution suppression must be active before any selector setup or start call; inspect target recipes and fixture initialization before running them. Do not run the `make test-tone` target.

The acceptance assertions must exercise behavior, not just match source text or count calls:

- Parse `13560000` and equivalent `13.560MHz` as exactly 13,560,000 Hz. With BCM2711 policy and both overrides enabled, start successfully, commit Tone at that exact carrier without WSPR offset/random placement, publish selector disabled, and observe one suppressed dispatch. Assert the controller/request route matches the existing direct-tone contract, not just a nonempty request.
- Use `13560000@17H` and `13560000@17L` through the production parser. Check explicit GPIO/polarity in the committed snapshot, preparation before dispatch, no amateur-band inference, and normal selector cleanup. These are software fixture GPIO numbers, not authorization to wire or operate GPIO17.
- Cover no overrides and each single override, malformed/out-of-range suffixes, ancillary-disabled profile rejection, pin conflicts including RF GPIO and enabled reserved functions, and injected explicit preparation failure. Assert rejection before commit/dispatch at the appropriate boundary. A parser-rejected request must not leave a startup request behind in a fresh fixture.
- Seed a preceding active selector using test-only GPIO operations, then start the allowed selector-free tone. Verify the previous active selection is cleared/released under the existing lifecycle, no new selector is asserted, and the new request cannot inherit the prior GPIO/polarity. Exercise teardown's reported failure path where supported; document its current reporting limits separately from physical behavior.
- Retain ordinary amateur-band explicit-selector and allowed configured-fallback behavior, no-selector behavior when fallback is disabled, existing 150 MHz refusal without overrides, and the existing admitted outside-band explicit-selector case. Keep non-Tone callers strict. Check an existing startup inhibit prevents the new selector-free case from committing/dispatching.
- Verify that direct-tone startup remains transient and does not change saved configuration or configured band mappings. Preserve existing cancellation, final selector cleanup, and managed Test Tone restoration coverage.

The suppressed-dispatch regression proves parsing, admission, selector resolution, and request commitment only. Preserve the existing compiler/backend frequency-validation tests and include a hardware-disabled downstream request/preparation assertion for this exact carrier if the existing software fixture supports it without constructing physical hardware. A rejected downstream request must retain its normal cleanup/error result. Do not remove backend validation or advertise synthesis success simply because the scheduler start hook was reached. Physical backend initialization and measured RF remain outside software evidence.

Extend the existing parent semantics fixture or add a focused parent direct-tone selector fixture if needed to isolate this matrix. If adding a focused test/target, name it explicitly in the implementation report and wire it into the appropriate existing non-hardware CI recipe. Do not claim `wspr-tone-regression-test` alone covers direct CLI Tone: it checks WSPR symbol expansion. Compiling physical backend code for the audited Linux suite is allowed; initializing hardware or enabling physical outputs is not. Do not run the normal GPIO application startup or `apply_runtime_config_side_effects()` as a substitute for the suppressed parent fixture: those paths select backends and initialize runtime outputs.

On an authorized Linux software runner with libgpiod development headers, run from `src` with hardware access disabled and no privilege elevation:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make gpio-band-policy-test band-gpio-rotation-test selector-shutdown-cleanup-test test-tone-lifecycle-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make test-tone-request-test test-tone-frequency-plan-test test-tone-selector-plan-test SUDO=
```

Run any newly added focused target under the same audited safety conditions. Avoid repeating checks already run successfully as prerequisites unless new changes or failures justify a rerun. Inspect the candidate Makefile first because it has unrelated active edits. Record actual commands and results rather than assuming these targets retain their inspected recipes.

On macOS, `make semantics-test-portable SUDO=` provides its documented simulated-only subset. It does not execute the full physical-backend semantics matrix and cannot replace the required Linux direct-tone/ancillary-enabled evidence. If the Linux runner is unavailable, record that gate as outstanding. An explicit `--backend simulated` application exercise may add software lifecycle evidence after reading its contract, but an unbounded simulated CLI tone is unnecessary and does not reproduce BCM2711 policy by itself. No hardware test, build, or application execution is required during this planning task.

Before reporting implementation complete, inspect the final parent diff, run `git diff --check`, record source revision and pre-existing dirty changes, and confirm only approved paths were changed. This direct CLI-only change does not require a new Pi/Pico schema or companion implementation; if that boundary changes, apply the repository's full network and USB contract gates instead of waiving them.

## Documentation Impact

- Update parent `docs/band-and-wspr-presets.md` during implementation with the direct CLI selector rule: numeric carriers remain exact, outside-catalog carriers are admitted only under existing policy, and an explicit per-frequency suffix is required when a selector is wanted. Explain that selector absence does not establish filtering or output qualification. Keep prose operational and avoid copying the issue's privileged bench command as a generally safe example.
- Operator follow-up is required in `WsprryPi-Docs/docs/Command_Line_Operations/service_test_controls.md` and `transmitter_backends.md`: distinguish selector-free numeric tone admission from explicit selector syntax and existing dual overrides. Review `quick_start.md` for conflicting examples without expanding into unrelated corrections. Read that repository's instructions before any work there; no sibling writes are authorized by this plan request. If later execution lacks that authorization, report these exact follow-ups as outstanding and do not claim operator documentation is complete.
- No browser UI, Pico guide, public API, configuration schema, band-catalog, or drive-current documentation change is expected. If implementation changes UI behavior or UI-depicting documentation, use the mandatory Impeccable workflow and obtain any necessary scope before proceeding.

## Execution order and completion gates

1. Reinspect current source/diffs and record the implementation baseline. Confirm issue scope still matches the candidate and isolate the new edits from unrelated controller work without discarding it.
2. Add and demonstrate the failing hardware-free regression, implement the bounded selector policy, and run focused acceptance cases. Reassess policy ordering, unknown-band handling, explicit selector ownership, previous-selector release, and failure-before-dispatch.
3. Run required Linux regression coverage and applicable portable checks, update authorized documentation, then adversarially assess the complete change. Fix actionable in-scope findings, rerun affected checks, and repeat a full assessment until no actionable finding remains. Unavailable validation and documentation remain named gates, not a clean completion claim.
4. Report behavior, modified parent paths/components, tests passed/failed/skipped, documentation updated/unchanged/outstanding, and exact Git state. Distinguish implemented software behavior from separately unperformed installation, service, physical GPIO, electrical/filter, timing, frequency-accuracy, and RF qualification. Do not commit, push, or change the issue's state without a separate instruction.

A later optional conducted check needs explicit live-operation authorization and confirmation of device, candidate binary, exact 13.560 MHz carrier, mode, finite duration, RF/load/filter path, ancillary wiring, service handling, and stop/restore procedure. It is not a prerequisite for calling this source fix software-validated, and software validation does not close that physical qualification gate.

## Decisions and assessment closure

The issue provides the required behavior; no additional product decision is needed for the bounded parent CLI fix. Linux software-runner availability and operator-documentation authorization are execution gates. Component/protocol expansion, a broader teardown redesign, or live-device work requires a separate decision if evidence makes it necessary.

Planning assessments must cover scope, exact-frequency preservation, policy admission, known-band fallback, explicit selector validation, stale-selector ownership, build-profile differences, documentation authorization, dirty-source preservation, and evidence limits. A plan review establishes a coherent work specification, not implementation or qualification.

Assessment pass 1 closed ambiguity about downstream evidence and host profiles: a suppressed scheduler hook is not backend acceptance, portable validation does not replace the GPIO matrix, and the WSPR symbol-expansion test is not direct-tone coverage. Full assessment pass 2 clarified that physical-backend compilation is compatible with inhibited tests and explicitly excluded normal hardware-initializing startup from the regression harness. Full assessment pass 3 rechecked the behavior matrix, call sites, policy/cleanup ordering, scope, documentation boundaries, and completion gates after those corrections and found no further actionable planning findings. Implementation and runtime evidence remain outstanding.
