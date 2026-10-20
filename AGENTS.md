# WsprryPi Repository Instructions

These instructions apply to the entire WsprryPi repository unless a more specific nested `AGENTS.md` overrides them.

## Working Principles

- Inspect the current repository, branch, working-tree status, affected component
  paths, and relevant implementation contracts before acting.
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

## Repository and Work-Harness Cleanup

- For an explicit request to clean up WsprryPi, follow the
  [reusable cleanup instructions](docs/development/repository-cleanup-prompt.md).
- Present an exact DELETE / REWRITE / RETAIN / HOLD inventory before changes.
  Include project-owned tracked, untracked and ignored artifacts, Mac work areas,
  and the Raspberry Pi hosts authorized for the request. Verify physical paths,
  active use, dependencies and recovery duties; preserve user work and shared tools.
- Describe the current product in direct, impersonal language. Operator prose
  explains capabilities, use and meaningful limitations. Keep required technical
  detail in engineering guides; remove development diaries and future promises.
- Delete completed, superseded and abandoned execution prompts, temporary
  handoffs and obsolete evidence after consolidating needed current information.
  Repair references; do not create an archive or permanent cleanup report.
- Keep handoffs needed for selected unfinished work and genuinely reusable
  instructions. The release punchlist is the selected work authority; preserve
  its priorities and acceptance gates. Active release/research plans may retain
  phase/step organization; remove that framing from ordinary documentation.
- Preserve component boundaries, current behavioral tests/fixtures, source
  provenance, dependencies, live settings and required rollback/recovery assets.
  Keep permission-limited or ambiguous items on hold and continue independent work.
- Cleanup does not authorize hardware operation, service/configuration changes,
  sibling-repository edits, checkout synchronization, Git-history changes,
  staging, commits, pushes or publication. Separate explicit requests govern them.
- Reading or maintaining the instructions does not invoke cleanup.

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

### Authorized Installation Monitoring

When the user explicitly authorizes an installation, keep its completion marker
outside the Git checkout. Before starting, always remove any stale marker from
the invoking user's home directory. Create the marker only if the installer
succeeds:

```sh
rm -f ~/finished
sudo ./scripts/install.sh && touch ~/finished
```

For an installation running in an existing terminal session, monitor
`~/finished`; never create or monitor `finished` relative to the repository
working directory. The marker belongs to the invoking command, not to
`scripts/install.sh`.

## Hardware-Free Simulated Backend

WsprryPi provides a canonical hardware-free simulated transmission backend for
ordinary Debian development, automated tests, and CI. Read
`docs/simulated-backend.md` before planning or running application-level
transmission tests that do not require physical hardware.

- Prefer the explicit `--backend simulated` selection when exercising the real
  application planning, scheduling, cancellation, status, failure, and cleanup
  paths without GPIO, MMIO, mailbox, DMA, I2C, transmitter device nodes, or RF.
- Use `.github/workflows/debian-non-hardware.yml` as the checked-in reference
  for the canonical unprivileged Debian validation suite.
- Simulation must never be selected automatically because hardware is absent or
  initialization fails, and it must not weaken safety policy for physical
  backends.
- Timing-mode selection, trace-path overrides, and lifecycle fault injection are
  transient typed C++ test/developer APIs, not production CLI, INI, environment,
  or UI controls. Additional shell-facing controls require a separately reviewed
  developer-harness use case.
- Simulator evidence qualifies software contracts only. It does not qualify
  Raspberry Pi timing, GPIO, MMIO, DMA, mailbox, I2C electrical behavior,
  Si5351 output, installation, services, RF output, frequency accuracy, or a
  physical transmitter chain.

## Raspberry Pi Kernel Build Worker

Maintainer kernel work can use the Debian ARM64 build worker prepared outside
the WsprryPi repositories. Agents working on kernel source, configuration,
builds, staging, or deployment planning must read this document before
operating the worker:

- `docs/raspberry-pi-kernel-build-worker-setup.md` is the canonical guide.
  It defines VM requirements, package installation, workspace creation, exact
  source pinning, target-config import and normalization, stock validation,
  staging, checksums, acceptance criteria, repeat runs, cleanup, and the
  validated safety boundary.
The worker contract is build-and-stage only. Keep kernel source, out-of-tree
build output, staged artifacts, and WsprryPi repositories separate. Never
install a built kernel into the worker's `/boot` or `/lib/modules`. Target-Pi
access, copying, installation, boot configuration, `depmod`, reboot, service
operation, GPIO, transmission, and RF activity require separate explicit
authorization. A successful build or manifest verification is not target
hardware qualification.

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

Operator documentation lives in the separate sibling Git repository
`../Wsprry_Pi_Docs`.

- Inspect and follow `Wsprry_Pi_Docs/AGENTS.md` before working there.
- Do not write to that repository without explicit cross-repository authorization.
- Preserve its current branch and working tree, including all existing user changes.
- Build, render, and verify documentation using that repository's documented workflow.
- Use Impeccable to review affected rendered UI documentation. Replace screenshots only when they are materially inaccurate.
- Keep application/UI changes and `Wsprry_Pi_Docs` changes as separate review,
  commit, and push boundaries.
- If cross-repository documentation changes are not authorized, review and report the required operator-documentation follow-up without modifying that repository.

## Component Policy

WsprryPi tracks its application and reusable components as ordinary content in
one Git repository:

- `WsprryPi-UI` contains the first-party application web interface.
- Named components under `src/` provide libraries and supporting functionality
  used by the C++ application.

Treat every component as a coherent source and test boundary inside the parent
repository. Preserve its named root, internal hierarchy, public interfaces,
README, attribution, standalone build or test entry points, and extraction
potential where present.

### Initial Inspection

Before building, testing, or modifying code, inspect the parent repository and
the affected component paths:

```sh
git status --short --branch
git diff --check
git diff --cached --check
```

Review both staged and unstaged changes. Confirm that affected component files
are ordinary parent-repository content and inspect applicable nested
instructions before acting. Existing changes anywhere in the repository belong
to the user and must be preserved.

### Root UI Component

`WsprryPi-UI` is the editable first-party web UI.

For UI work:

- Follow the mandatory Impeccable workflow.
- Inspect the complete parent working tree and the existing UI source state.
- Make UI source changes inside `WsprryPi-UI`.
- Do not replace missing UI sources with copies from an installed web root,
  generated output, or another checkout.
- Coordinate UI behavior with the parent repository's configuration,
  validation, persistence, websocket, scheduling, and runtime contracts.
- Run UI-specific tests from `WsprryPi-UI` and applicable integration or
  source-regression tests from the parent repository.
- Review UI changes as an explicit component portion of the parent diff.

### `src/` Components

Components under `src/` retain strong independent boundaries even though they
are tracked by the parent repository.

- Do not modify a component merely to work around a parent integration problem.
- Do not apply formatting, refactoring, warning cleanup, or modernization as
  incidental work.
- If a required fix belongs in a component, identify the affected component,
  suspected defect, why a parent-level fix would be inappropriate, proposed
  change, and validation.
- Obtain approval before broadening a parent-only task into component changes.
- Keep component changes independently buildable and testable where supported.
- Preserve reusable components such as `LCBLog` and `WSPR-Reference` without
  introducing dependencies on WsprryPi application internals.

Never conceal a component modification inside an otherwise parent-only change.

### Dirty Component Paths

Existing changes inside a component path belong to the user just like changes
elsewhere in the parent repository.

- Do not reset, clean, switch, checkout, stash, rebase, overwrite, or discard
  them.
- Work around unrelated changes when safe.
- If requested work overlaps existing changes, inspect them and continue only
  when the task clearly authorizes that scope.
- Otherwise stop and ask for direction.

### Commit and Push Boundaries

Do not commit or push unless explicitly requested.

When an authorized change includes a component:

1. Review the component portion of the parent diff.
2. Run the component's relevant standalone tests where available.
3. Run applicable parent integration tests.
4. Review the complete staged parent diff.
5. Commit the component and integration changes in the parent repository at a
   boundary appropriate to the approved task.
6. Push only the parent branch and remote the user authorized.

Any future extraction or publication workflow requires separate authorization.

### Validation and Reporting

Build and test the ordinary component trees tracked by the parent repository.

In the completion report, state:

- parent repository branch and working-tree state;
- every component path modified;
- standalone component tests run;
- integration tests run in the parent repository;
- validation not run and why; and
- whether a parent commit or push occurred.

Do not describe the repository as clean when a component path contains staged,
unstaged, or untracked changes.

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

Choose the semantics profile explicitly for the build host:

| Host/toolchain | Command | Coverage |
| --- | --- | --- |
| Linux with the libgpiod C++ development headers | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=` | Full semantics coverage for the default physical and simulated backend profile, with hardware access disabled. |
| macOS, another non-Linux host, or Linux without the libgpiod C++ development headers | `make semantics-test-portable SUDO=` | Portable simulated-only coverage with `BACKENDS=simulated ANCILLARY_GPIO=0`. |

`semantics-test-portable` is an explicit subset, not an automatic fallback and
not evidence that the full suite passed. It excludes the backend-specific
runtime-semantics and cleanup-lifecycle executables whose assertions require
physical backend capabilities. Neither profile qualifies physical hardware,
GPIO timing, installation, services, or RF output.

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
