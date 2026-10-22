# WsprryPi Repository Instructions

These instructions apply to the entire WsprryPi repository unless a more specific nested `AGENTS.md` overrides them.

## Working Principles

- Inspect the current repository, branch, working-tree status, submodule status, and relevant implementation contracts before acting.
- Keep work tightly within the user-approved scope.
- Preserve all existing changes. A dirty working tree belongs to the user unless explicitly stated otherwise.
- Do not discard, overwrite, reset, stage, commit, amend, rebase, push, or create a pull request unless the user explicitly requests that action.
- Do not broaden a focused feature, fix, documentation task, or investigation into adjacent cleanup or refactoring.
- Prefer the smallest maintainable change that satisfies the requested behavior.
- Distinguish clearly among:
  - implemented behavior
  - planned or proposed behavior
  - explicit non-goals
  - unresolved decisions
  - validation still required
- Never describe work as complete unless the required implementation, tests, documentation review, and applicable runtime validation have actually been completed.

## Request Boundaries

- For research, review, diagnosis, planning, or report-only requests:
  - Inspect relevant files and evidence.
  - Report findings and recommendations.
  - Do not modify files or operational state.
- For implementation requests:
  - Make only the approved changes.
  - Add or update appropriate tests.
  - Review documentation impact.
  - Run safe, relevant validation.
- If a task is divided into approved slices, implement only the current slice. Do not silently begin later slices.
- If an ambiguity would materially change behavior, compatibility, persistence, hardware operation, or user workflow, stop and ask for direction.

## Raspberry Pi and Hardware Safety

WsprryPi controls radio-transmission and Raspberry Pi hardware.

Unless the user explicitly authorizes live hardware operation, do not:

- start a transmission
- generate a test tone
- key transmitter GPIO
- change GPIO state
- exercise attached transmitter hardware
- install or replace the running binary
- stop, start, enable, disable, or restart system services
- change system configuration
- reboot or shut down the Raspberry Pi
- run installation or removal targets
- use `sudo` for a mutating action

Treat compilation, non-hardware unit tests, source inspection, and configuration validation as distinct from live-device qualification.

Never claim that hardware, RF output, GPIO timing, service lifecycle, or installation behavior has been validated solely because source-level tests passed.

Before any authorized live transmission, confirm the exact frequency, mode, duration, output path, attached hardware, and stopping procedure.

## User Interface Work: Impeccable Is Required

Any task affecting a user interface must use the Impeccable skill.

UI work includes, but is not limited to:

- HTML, PHP templates, CSS, or JavaScript presentation
- forms, settings, controls, navigation, dialogs, and status feedback
- responsive or mobile behavior
- accessibility
- wording, labels, hints, validation messages, and operator workflow
- screenshots or documentation depicting the UI

For every UI task:

1. Read and follow the Impeccable skill before making UI changes.
2. Inspect the existing design language and interaction patterns.
3. Preserve established visual and behavioral consistency unless a redesign is explicitly requested.
4. Use Impeccable during design or implementation, not merely as a final mention.
5. Render and inspect the resulting interface at applicable desktop and mobile sizes.
6. Address relevant Impeccable findings or explain why a finding is not applicable.
7. Report the UI workflow exercised and the visual evidence reviewed.

If the Impeccable skill is missing, unavailable, or cannot be used, stop before making UI changes and tell the user what is unavailable. Do not silently substitute another visual-review workflow.

Do not commit local skill/runtime artifacts such as `.agents/`, `.impeccable/`, `.claude/`, or `.codex/` unless the user explicitly requests them and they are intended repository content.

## Operator Documentation Repository

Operator documentation lives in the separate sibling Git repository `../Wsprry_Pi_Docs`; it is not a submodule of `WsprryPi`.

- Inspect and follow `Wsprry_Pi_Docs/AGENTS.md` before working there.
- Do not write to that repository without explicit cross-repository authorization.
- Preserve its current branch and working tree, including all existing user changes.
- Build, render, and verify documentation using that repository's documented workflow.
- Use Impeccable to review affected rendered UI documentation. Replace screenshots only when they are materially inaccurate.
- Keep application, `WsprryPi-UI` submodule, and `Wsprry_Pi_Docs` changes as separate review, commit, and push boundaries.
- If cross-repository documentation changes are not authorized, review and report the required operator-documentation follow-up without modifying that repository.

## Submodule Policy

WsprryPi uses Git submodules in two distinct roles:

- `WsprryPi-UI` at the repository root contains the first-party application web interface.
- Submodules under `src/` provide libraries and supporting components used by the C++ application.

Treat every submodule as a separate Git repository with its own branch, working tree, history, tests, and commit boundary.

### Initial Inspection

Before building, testing, or modifying code, inspect the parent repository and all submodules:

```sh
git status --short --branch
git submodule status --recursive
git submodule foreach --recursive 'git status --short --branch'
```

Interpret submodule status carefully:

- A leading `-` means the submodule is not initialized.
- A leading `+` means it is checked out at a commit different from the parent repository’s recorded commit.
- A dirty submodule contains local changes that must be preserved.
- A detached `HEAD` is common for a checked-out submodule and must not be mistaken for an error or permission to discard work.

Report unexpected, dirty, uninitialized, or mismatched submodule state before making changes that could affect it.

### Initialization

When required for inspection, building, or testing, initialize submodules at the commits recorded by the parent repository:

```sh
git submodule update --init --recursive
```

Do not use any of the following unless the user explicitly requests a dependency update:

```sh
git submodule update --remote
git submodule foreach git pull
git submodule foreach git reset --hard
```

Do not automatically move a submodule to its latest upstream branch. The parent repository’s recorded commit is the authoritative dependency version.

Do not change submodule URLs, branches, or `.gitmodules` configuration unless that is explicitly in scope.

### Root UI Submodule

`WsprryPi-UI` is the editable first-party web UI.

For UI work:

- Follow the mandatory Impeccable workflow.
- Verify that `WsprryPi-UI` is initialized at the commit recorded by the parent repository.
- Inspect the UI submodule’s branch and working-tree status separately.
- Make UI source changes inside `WsprryPi-UI`.
- Do not replace missing UI sources with copies from an installed web root, generated output, or another checkout.
- Coordinate UI behavior with the parent repository’s configuration, validation, persistence, websocket, scheduling, and runtime contracts.
- Run UI-specific tests in the UI repository and applicable integration or source-regression tests in the parent repository.
- Treat the UI commit and the parent repository’s updated submodule pointer as separate reviewable changes.

Do not update the parent repository’s `WsprryPi-UI` pointer until the intended UI commit exists and has been reviewed.

### `src/` Dependency Submodules

Submodules under `src/` are dependencies and must be treated as read-only by default.

- Do not modify a `src/` submodule merely to work around a parent-repository problem.
- Do not update a dependency revision as incidental cleanup.
- Do not apply formatting, refactoring, warning cleanup, or modernization inside dependency submodules unless explicitly requested.
- If a required fix appears to belong in a dependency, report:
  - the affected submodule
  - its current recorded commit
  - the suspected defect
  - why a parent-level fix would be inappropriate
  - the proposed dependency change and validation
- Obtain approval before editing the dependency submodule.
- Keep dependency changes independently buildable and testable where its repository supports that.
- Update the parent repository’s submodule pointer only after the dependency change has been reviewed and committed in the dependency repository.

Never conceal a dependency modification inside an otherwise parent-only change.

### Dirty Submodules

Existing submodule changes belong to the user.

- Do not reset, clean, switch, checkout, stash, rebase, or overwrite a dirty submodule.
- Do not run recursive commands that could mutate every submodule.
- Work around unrelated dirty submodules when safe.
- If the requested work overlaps a dirty submodule, inspect the existing changes and continue from them only when the task clearly authorizes that scope.
- Otherwise stop and ask for direction.

### Commit and Push Ordering

Do not commit or push unless explicitly requested.

When an authorized change includes a submodule:

1. Review the submodule diff.
2. Run the submodule’s relevant tests.
3. Commit the submodule change in that submodule repository.
4. Ensure the submodule commit is available on its intended remote before publishing a parent commit that references it.
5. Review the parent repository diff, including the exact old and new submodule commit IDs.
6. Commit the parent repository’s pointer update separately or as an explicitly reviewed part of the parent change.
7. Push only the repositories the user authorized.

Never push a parent commit whose submodule pointer refers only to an unpushed local commit. That would leave other checkouts unable to initialize the recorded revision.

### Validation and Reporting

Build and test against the exact submodule commits recorded by the parent repository unless the approved task intentionally changes them.

In the completion report, state:

- parent repository branch and working-tree state
- initialized and uninitialized submodules
- dirty or mismatched submodules
- every submodule modified
- old and new submodule commit IDs, if pointers changed
- tests run inside each affected submodule
- integration tests run in the parent repository
- whether submodule commits and parent-pointer commits were created or pushed

Do not describe the parent repository as clean when one of its submodules is dirty or checked out at an unexpected commit.

## Configuration and Compatibility

- Inspect the full configuration lifecycle before changing settings:
  - defaults
  - file or JSON parsing
  - validation
  - internal configuration structures
  - web form population
  - autosave or persistence
  - serialization
  - scheduling and runtime consumption
  - tests and documentation
- Preserve compatibility with existing valid configurations unless a migration is explicitly approved.
- Do not silently replace or normalize a user’s custom value merely because the UI introduces a preset.
- Keep displayed units and persisted semantics accurate. Distinguish seconds, hertz, PPM, minutes, and dot-length multipliers.
- Keep QRSS, FSKCW, and DFCW shared timing behavior synchronized where the existing configuration contract requires it.
- Ensure disabled or derived controls still produce an intentional and valid persisted configuration.
- Treat previews and transient UI selections separately from persisted settings unless the user explicitly approves immediate persistence.

## Operator Workflow

- Evaluate changes from the operator’s point of view, not only from the underlying data model.
- Put action feedback near the control that triggered it.
- Make disabled, derived, pending, invalid, and saved states visually and semantically clear.
- Do not let a preview or exploratory action silently persist settings.
- Preserve user-entered drafts when validation fails unless the approved behavior says otherwise.
- Make defaults discoverable without obscuring advanced control.
- Avoid exposing internal implementation terminology when a clear operator-facing term exists.

## Build and Test Discipline

Use the repository’s existing Makefile targets and test infrastructure.

Run tests from `src` unless the project’s current instructions say otherwise.

A typical non-hardware regression command is:

```sh
cd src
make semantics-test
```

Use focused targets when they cover the changed behavior, including as applicable:

```sh
make guarded-mode-change-persistence-test
make band-gpio-default-convergence-test
make band-gpio-rotation-test
make selector-shutdown-cleanup-test
make monitor-file-regression-test
make gpio-line-resolver-test
make non-wspr-repeat-policy-test
```

For UI changes, include the existing UI/source regression coverage and perform the Impeccable visual workflow.

Do not run hardware-dependent, installation, service-management, or transmission targets without explicit authorization.

Do not assume a test command is safe merely because its target name contains `test`; inspect what the target executes first.

When reporting validation:

- State exactly which commands ran.
- State whether each command passed, failed, or was skipped.
- Separate automated tests from visual review and real-hardware qualification.
- Do not conceal warnings, intentional skips, environment limitations, or untested behavior.
- Run a whitespace/error check on the final diff when appropriate.

## Documentation Requirements

Review documentation impact for every user-visible or operator-facing change.

Changes involving configuration, modes, timing, frequencies, scheduling, GPIO, transmitter backends, installation, services, logging, maintenance, or UI workflow should be presumed to require documentation review.

Before declaring work complete:

1. Identify the affected operator behavior.
2. Locate the corresponding documentation in the Wsprry Pi documentation repository when available.
3. Update documentation in the appropriate repository when that work is in scope.
4. Keep current behavior, future plans, and known limitations clearly separated.
5. Do not include internal bug-fix narration or regression-test details in user documentation unless they materially help the operator.

If documentation changes are outside the approved scope, report the exact documentation follow-up rather than silently omitting it.

Every implementation summary must include a `Documentation Impact` section that lists:

- documentation updated
- documentation considered but unchanged
- documentation still required
- why no documentation change was necessary, if applicable

## Completion Report

Lead with the outcome and provide evidence.

For implementation work, report:

- behavior implemented
- important files changed
- tests and checks run
- UI and Impeccable review performed, when applicable
- hardware or runtime qualification performed
- validation that remains outstanding
- documentation impact
- working-tree status
- whether anything was committed or pushed

If work remains incomplete, state the exact next unfinished step.

Never claim release readiness, installation success, service correctness, hardware correctness, or transmission correctness without the corresponding evidence.
