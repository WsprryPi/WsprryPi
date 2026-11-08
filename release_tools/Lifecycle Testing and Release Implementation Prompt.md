# Implement lifecycle testing and release orchestration in WsprryPi Qualification Harness

## Objective and execution project

Execute this implementation task from the **WsprryPi-Qualification-Harness** project. Build a maintained lifecycle-testing and release-orchestration capability that reuses the existing Pi/RP1 lifecycle tests, adds unattended Pico lifecycle automation, supports independent component and complete-system operations, and minimizes expensive CI runs. Update the qualification harness project documentation as part of the implementation.

This document is an implementation prompt, not evidence that any capability already exists. Apply the Standing Execution Authority reproduced below when executing the assigned implementation task. It authorizes covered development, Git, CI, host, device, and diagnostic work without repeated confirmation. Resolve the exact component, device, versions, destructive actions, publication actions, and final state before an operational run; resolution is an engineering preflight, not an additional approval gate for already authorized work. The current task of writing or revising this prompt does not start its implementation or operational runs.

The harness owns orchestration, reusable scenarios, evidence, progress, comparisons, recovery coordination, and reporting. Product repositories retain their production responsibilities. For necessary product changes, apply the supplied standing authority and any task-specific cross-repository authority, and follow the owning repository's instructions. Identify changes early; seek additional authority only for actions not already covered and continue independent harness work while it is pending. Do not copy installers into the harness or introduce production dependencies on the harness.

## Applying the standing execution authority

The appendix contains the user-supplied Standing Execution Authority, updated at the user's request to include WsprryPi-Qualification-Harness and RP1-GPIO-CLK-DKMS with the same development and Git permissions. It supplements existing authorization for the assigned task; apply it directly rather than converting routine implementation, testing, service operation, firmware deployment, CI troubleshooting, or covered Git operations into approval requests. Continue through intermediate failures, select reasonable diagnostics independently, and provide concise progress updates.

The authority explicitly names WsprryPi, WsprryPi-Docs, WsprryPico, WsprryPi-Qualification-Harness, and RP1-GPIO-CLK-DKMS; hosts wspr5, wspr4, wspr2, and wspr1; and Pico A, Pico B, and Pico C. It also covers related Docker infrastructure, development/browser tools, and the SDR and GPSDO associated with wspr5, subject to its stated limits. Prefer wspr5 for the lifecycle bench unless the selected test requires another authorized target. Identify the intended physical Pico before programming it. Do not treat general availability of other hosts or devices as an instruction to use them unnecessarily.

All five named repositories have the same development and Git permissions under the standing authority. The harness remains the execution project. Necessary cross-repository work within the assigned task is covered without a redundant approval request; preserve each repository's ownership, existing work, instructions, and separate Git boundaries.

The authority does not erase the task's scope, existing user work, unique data, protected Git history, branch protections, technical permission boundaries, or publication selection. Covered commit/push operations may proceed when consistent with the assigned execution task and repository workflow. Release publication must be part of the selected operation; the implementation of release tooling is not by itself an instruction to release products. Movement of an existing tag that overwrites remote history still requires the separate specific authority described in the supplied grant or an applicable explicit user instruction.

Lifecycle tests remain RF disabled by design. If a separately selected test requires RF, apply the supplied standing RF authority: verify current physical termination or reliable current physical-path evidence, resolve the intended transmitter/frequency/mode/duration/output and stop procedure, then proceed within that configuration without asking again for an already authorized transmission. Never infer termination from old documentation or transmit into an antenna under this grant. Preserve the GPSDO recovery requirements and unrelated services, credentials, persistent data, and containers.

## Orient before changing anything

1. Read the harness `AGENTS.md`, `CONTRACT.md`, `docs/AGENT_OPERATIONS.md`, `docs/CURRENT_WORKFLOWS.md`, `docs/CAPABILITY_MATRIX.md`, `docs/OPERATOR_SECURITY.md`, relevant development guides, schemas, CLI help, implementation, and tests. Current source and supported CLI behavior take precedence over historical notes.
2. Inspect branch, remote, staged and unstaged changes, untracked files, nested instructions, and applicable contracts in every affected repository. Preserve existing work. The harness has previously contained unrelated changes in its adapters, instructions, helper documentation, and tests; inventory the actual current state rather than assuming those changes belong to this task.
3. Discover actual repository roots and authorities. Expected siblings include WsprryPi, WsprryPico, RP1-GPIO-CLK-DKMS, and WsprryPi-Docs; older aliases and directory names may be stale. Do not silently switch a Pico candidate branch to devel or synchronize unrelated checkouts.
4. Work on the selected development bases, using an isolated `codex/` worktree when necessary and permitted. Record the intended source pair and any tested uncommitted changes. Never reset, clean, stash, stage, commit, push, or retag existing work without current authorization.
5. Inspect the release punchlist and current release machinery as selected scope and acceptance authorities. Do not change priorities, mark entries complete, or publish merely because this prompt describes release commands.

## Required entry points

Provide a coherent CLI with the following capabilities. Choose exact command spelling to fit the existing CLI and document it; the following names describe required behavior rather than an imposed parser design.

| Operation | Required scope |
| --- | --- |
| Test Pico | Firmware installation, same-image reinstall, supported upgrade, settings preservation, reset/erase, recovery, and final-state checks |
| Test Pi | Application/bootstrap/configuration/service lifecycle with companion mutation explicitly selected or excluded |
| Test RP1 | Standalone provider package, real preflight, DKMS/runtime, replacement, removal, and recovery |
| Test system | Coordinated Pi/RP1/Pico lifecycle and compatibility without publication |
| Release Pico | Resolve candidate, necessary tests and canonical CI, tag/publication, and consumer acceptance |
| Release Pi | Resolve application plus fixed dependency identities, necessary tests and CI, tag/assets, and default-installer acceptance |
| Release RP1 | Resolve package and tooling identities, native lifecycle tests and canonical CI, tag/publication, and consumer acceptance |
| Release system | Release necessary dependencies, update Pi selections, validate/release Pi, and perform complete public acceptance |
| Plan/preflight | Read-only resolved plan, prerequisites, effects, authorization gaps, test selection, CI reuse/rebuild reasons, and final states |
| Status/report/resume/recover | Durable progress and results, bounded continuation, and owning-component recovery after interruption or partial publication |

Independent means only selected components are changed or published. It does not waive compatibility checks against fixed companions. A default Pi 5 installation includes RP1; distinguish that normal integration profile from a Pi-isolated profile. If the public installer cannot isolate a component, report that scope accurately rather than hiding the mutation.

Do not use the RF `complete-test` entry point as a shortcut for destructive lifecycle operations. Lifecycle runs default to RF disabled and require no receiver, RF-path configuration, or transmission. Existing RF qualification and its result semantics remain intact and independently selected.

## Reuse the existing implementation and evidence

Start by inventorying and mapping the following coverage to maintained harness scenarios. Port reusable orchestration and assertions; parameterize prototype assumptions. Add new tests only for uncovered behavior or the new orchestration contracts.

- The eight-step public live runner and evidence were retained under `/private/tmp/wspr5-beta5-rerun-20261107.fjt_qb6x/`, including `run.py`, `snapshot.py`, `absence-template.py`, `paths.json`, `results.json`, `source-identities.json`, command records, logs, and snapshots. An earlier implementation is under `/private/tmp/wsprrypi-installer-fix-20261107.xDwqr2/public-live/`. Locate them if available and inspect before reuse. These temporary directories are migration inputs, never maintained runtime dependencies; if absent, recover equivalent source and tests from the owning repositories and report the missing historical evidence.
- Pi coverage includes `scripts/tests/installer_dependency_test.sh`, bootstrap and dry-run purity tests, RP1 helper/shell tests, ownership tests, shared picotool tests, service recovery, precompiled selection, and INI migration tests. Inspect their real scope and external mocks before claiming coverage.
- RP1 coverage includes `tests/check_provider_lifecycle.py` and `tests/provider_package_removal_linux.py`. Reuse the real package/preflight/native installation test instead of adding another mock-only replacement test. Inspect provider lifecycle, module, and package-operation contracts.
- Pico coverage includes the shared USB installer and its plan/transaction/recovery tests, guarded software BOOTSEL implementation, image references, settings/control tests, and canonical software-build verification/publication workflow.
- Reuse harness process supervision, progress JSONL, SSH trust, typed capability adapters, result validation, and evidence facilities where their contracts fit. Extend contracts explicitly where lifecycle requirements differ.

Remove hardcoded wspr5 identities, temporary paths, historic executable hashes, and fixed module-file counts from maintained logic. Derive expected inventory from the supported package/kernel policy. Keep named bench profiles for convenient repeatable use, with explicit device identity checks before mutation.

## Run model, ownership, evidence, and cleanup

Resolve a schema-validated plan before execution. Record selected components and scenarios, exact sources and dirty-state digests, old/new tags and commit identities, executable/package/UF2 hashes and embedded identities, dependency/toolchain/profile inputs, target OS/kernel/board/USB identity, transport, ownership scope, settings fixtures, deadlines, required checks, CI policy, and per-component final states. Store credentials separately and redact secrets from reports.

Use durable run directories outside source control and outside disposable installer or managed product trees. A maintained runner must not rely on OS temporary directories for recovery-critical state. Preserve verified baseline/candidate bundles, transaction receipts, logs, checkpoints, and recovery assets until the corresponding recovery duties are closed. Do not commit host archives, authorization receipts, target-specific bundles, or evidence into the harness repository.

Define lifecycle-specific result semantics and schemas without changing existing RF meanings. Distinguish pass, expected refusal, unexpected failure, blocked prerequisite, abort, inconclusive result, and cleanup failure. An intentionally injected fault followed by verified supported recovery may pass its scenario. Skipped or unattempted required checks do not pass. Cleanup failure prevents a successful overall result.

Use explicit phases and durable checkpoints around irreversible effects. Record effect intent and observed results; after interruption, inspect the actual target state before choosing a supported continuation. Bound processes, disconnect waits, USB re-enumeration, service readiness, and retries. Hold component/host/device ownership appropriately, prevent concurrent controllers, and retain existing SSH verification and privilege boundaries.

Define exact expected-retained artifacts and their ownership. Preserve user data, foreign files, shared tools, OS packages, GPS configuration, and independently owned Pico assets. Do not exempt whole directories to conceal residue. Never delete product receipts, journals, or unknown files merely to make a test pass. Keep harness inputs/controller state outside trees that uninstall removes.

## Preserve and strengthen the Pi eight-step matrix

Implement this existing sequence as a parameterized maintained scenario:

1. Install through the actual public default devel streamed installer.
2. Uninstall through its supported public streamed entry point.
3. Independently audit complete removal within the declared ownership scope.
4. Install a verified earlier application build as an upgrade baseline, using a local verified bundle if necessary.
5. Reinstall that identical baseline idempotently through the supported installer path.
6. Run the default public devel installer to upgrade to the selected current published build, with no special rollback preparation.
7. Uninstall through the supported public streamed entry point.
8. Repeat the independent exhaustive absence audit.

Exercise the actual public stdin/bootstrap path; substituting a checked-out script does not prove that path. Resolve and verify public redirects/channels, explicit devel raw URL, tagged scripts, checkout installation, and released/local/source-build selections according to their supported contracts. Use focused profiles rather than multiplying every path into every expensive live run.

Before each authorized installation, remove the invoking user's stale `~/finished`; create it only when the installer succeeds. Never place or monitor it relative to the checkout. The harness may remove its own baseline input and marker during cleanup; it may not manually purge product residue between steps.

After installation, verify executable hash and embedded version, RP1 package/source/tooling identity, required services active and enabled, actual UI/API/config readiness, and neutral transmitter state. Require transmission disabled, no unintended selected route, and the supported never-transmit-on-boot setting. A service being active alone does not prove application readiness or safety.

After removal, independently check package status, DKMS registrations, kernel modules and source/build artifacts, runtime state/device nodes, owned systemd units and enabled links, product listeners/routes, generated web configuration, dedicated groups, installed sources/helpers/configuration/cache/logs, and product temporary artifacts. Include justified searches of boot and runtime locations; report exactly what was inspected. Verify package-manager and Apache health, restoration/preservation of the stock site, and disappearance of product endpoints. Compare shared boot configuration and shared-tool hashes where the scenario promises preservation.

Keep standalone Pico installer files, caches/state, and shared picotool when independently owned. Do not claim all-system removal while retaining an unexplained owned artifact. Distinguish intentional retained data, externally owned data, and actual defects.

### Nondefault settings and retention

Seed valid distinctive nondefault settings through supported production interfaces, keeping RF disabled. Compare canonical settings semantics and runtime/API readback after identical reinstall, restart, and upgrade. Identify fields expected to change, including versions, boot/session identities, and migration metadata; verify new fields receive documented defaults. Do not accept wholesale default regeneration as settings preservation.

Test pre-existing operator files, attributable backups, fleet data, modified owned code, symlinks, and foreign files according to production ownership rules. Verify preservation or refusal as specified, not a blanket deletion policy. Separate settings retention from package/source residue assertions.

## RP1 lifecycle and real transitions

Support standalone RP1 scenarios and the same provider through Pi's production lifecycle caller. Exercise real preflight and verified candidate package inputs at both boundaries. Cover identical reinstall without unnecessary rebuild, same-version changed verified package/source replacement, compatible real package-version increase, application-only upgrade retaining/revalidating RP1, and final uninstall.

Verify that ordinary forward replacement works without the outgoing package file or an outgoing download. Preserve meaningful checks for unknown files, modified ownership evidence, conflicting authorities, unsupported versions, and explicit downgrade policy. Do not require operators to prepare old rollback packages for a normal supported upgrade.

Reuse the native Linux package test for genuine APT/dpkg/DKMS/preflight execution, partial-removal recovery, and foreign-file refusal. Run destructive fixture combinations in disposable environments; select bounded physical cases separately. A synthetic version fixture is fixture evidence, not a genuine published version upgrade. Never manipulate public tags solely to manufacture a test baseline.

Assert old DKMS registrations, obsolete owned module/source artifacts, and terminal dedicated state disappear when promised. Verify group ownership rules and concurrent lifecycle locking. Retain necessary recovery proof through partial cleanup, and ensure a predecessor removal during upgrade does not perform terminal retirement prematurely. Do not claim atomic host rollback when shared package/configuration changes can remain.

## Pico unattended lifecycle automation

Keep settings/provisioning reset, software BOOTSEL, and full flash erase as distinct operations with explicit effects. Inspect the current supported board geometry rather than assuming all Pico-family boards have identical flash. On the supported Pico 2 W, the full erase contract currently covers the entire 4 MiB flash, blank verification, and remaining in ROM BOOTSEL; ROM/OTP/hardware identity are outside flash erase.

Reuse guarded production USB maintenance and the shared installer. Compatible upgrades already use software BOOTSEL. The existing erase transaction has historically admitted a safe device and waited for manual BOOTSEL rather than requesting it; inspect current behavior and implement the missing host orchestration if still required. Prefer the existing guarded maintenance transition over a new wire command.

Persist the exact transaction before requesting reset. Enforce idle/unowned/safe state, correct device/boot/revision binding, and ACK/reset ordering. Follow re-enumeration using verified serial/physical topology and ROM identity. Handle a lost ACK by inspecting state, not blindly repeating a destructive operation. Never flash whichever USB device appears first.

Acquire and verify candidate and recovery inputs before erasing. Preserve reserved settings flash before boot where required, program and verify through the production installer, use its required reboot path, and honor postboot verification hold. Verify actual firmware identity and saved semantics before acknowledging completion. Do not bypass transaction/image-reference ownership with ad hoc picotool commands.

Provide two primary tracks:

- **Fresh-state track:** safe software BOOTSEL, complete erase and blank verification, installation from the verified bundle, fresh defaults/no old records, new distinctive settings through production interfaces, restart, and readback. End in the selected installed or blank-in-BOOTSEL state.
- **Preserved-state track:** verify the actual baseline, seed distinctive settings, identical-image reinstall, compatible candidate upgrade, preserved old values, documented defaults for new fields, newly supported settings round trip, and recovery. Do not erase this track's state to make preservation appear successful.

Test changed bytes under the same published semantic version explicitly. Inspect the shared installer's existing local-image reference support and equal-version public-image assumptions. Resolve actual old and new verified bundles independently; version equality does not prove byte identity. Preserve truthful baseline provenance even if a public tag moves. Do not edit receipts, substitute current bytes for old-image proof, weaken validation, or introduce hidden downgrade behavior.

Test standalone Pico and Pi-managed USB coordination, including releasing/reacquiring the device and preserving identity across controller handoff. Use synthetic settings and private credential backups. A missing Pico must be explicitly optional for a Pi/RP1 profile; it blocks required Pico/system coverage. Unsupported or unresponsive firmware may require physical recovery; report that boundary honestly. Normal supported successful cycles must require no BOOTSEL button press. Validate one bounded physical cycle before repeated endurance testing.

## Failure and recovery scenarios

Extend existing fault coverage rather than replacing it. Include unavailable downloads, checksum mismatch, missing/malformed helpers, real preflight refusal, service startup failure, disk-full/unwritable paths, stale/concurrent ownership and held locks, foreign files, interrupted shared picotool relocation, interrupted terminal cleanup, handled termination, uncatchable process loss, USB disconnect, and interrupted erase/program/postboot verification.

For each scenario, establish a known baseline, introduce one identified fault at a controlled boundary, verify the expected refusal/failure and preserved obligations, remove only the introduced fault, retry the owning production/recovery operation, and independently verify the final state. No manual deletion of product recovery evidence or bespoke target repair may turn a failing supported path green. Keep most injected failures offline or in disposable native environments; record exactly which physical cases were exercised.

## Component release and coordinated release

Build a resolved release plan with independently identified firmware, application, provider package, provider tooling, shared Pico installer, dependency selections, and published artifacts. Tags, tooling source, paired test source, embedded build source, and executable source can differ legitimately; record each accurately. A CI artifact is not a publicly consumable release.

An independent release validates the selected component and necessary fixed-companion compatibility, uses its canonical release machinery, publishes only the selected scope, and checks real consumer resolution. Do not advance Pi solely because Pico or RP1 was independently released unless that update is selected.

For a system release, resolve suitable dependency releases first and reuse unchanged suitable published artifacts. Release changed Pico/RP1 components as required, verify consumer availability, update Pi's actual authoritative dependency/tooling/channel selections, run required exact-pair checks, prepare/release Pi, then run the full selected lifecycle acceptance against public entry points. Do not publish every dependency just because the operation is called full-system release.

Support new tags and explicitly requested movement of existing tags. Capture prior remote object IDs and use guarded updates against the expected old value. Never force tags implicitly or add mandatory immutable-tag policies contrary to the maintainer's editable-release workflow. A moved source tag must not misrepresent unchanged binaries as having been rebuilt from the new commit.

Promote the exact verified artifact when embedded version/source, board/profile, toolchain, dependencies, and build inputs permit reuse. Rebuild when those identities require it. Keep original build provenance for unchanged executable assets after metadata-only changes. Verify public asset bytes, checksums, embedded identities, redirects/selectors, default devel plan, and actual installed results after publication.

Checkpoint partial publication. If a dependency has published and a later stage fails, report partial delivery and resume from verified public state without unnecessary retagging/rebuilding. Do not claim cross-repository atomic rollback or erase published history as automatic recovery. Lifecycle success alone is not stable-release, RF, timing, or spectral qualification.

## Minimize CI without weakening evidence

1. Implement an inspectable selection plan: changed scopes, affected checks, reusable results, invalidated inputs, required canonical jobs, expected new runs, and reuse/rebuild reasons.
2. Iterate locally with focused tests. Run the complete applicable local acceptance set once on the frozen candidate, then broaden/repeat only for new changes, failures, or unresolved concerns. Use disposable native package tests before costly bench/CI cycles.
3. Freeze exact source/dependency pairs before expensive CI. Key caches and reusable evidence on all relevant source, dirty-state, version-generation, dependency, compiler, board/profile, generator, and test-input identities. A nearby green commit or stale companion is insufficient.
4. Reuse canonical component verification and artifact-promotion mechanisms. Avoid duplicate push/PR/tag/dispatch builds where supported, separate validation from publication, and batch fixes before the next expensive run. Do not trigger CI merely to write planning artifacts.
5. Retain required trust boundaries: Pico canonical verification/publication checks, Pi's checked-in non-hardware recipes, and RP1 native package validation cannot be replaced by an unrelated central green job. The target is the minimum necessary canonical runs per changed candidate, not an unconditional one-run promise.
6. Shared settings/control, descriptors, advertised capabilities, or protocol-facing changes require both actual network and USB integration suites against the intended Pi/Pico sources. Use Pi's checked-in `debian-non-hardware.yml` recipes, including runtime/network/config interop, actual Pico USB management driver, Pi pair client, USB management tests, and production pair JavaScript test. Preserve strict validation and prerelease policy. Failed, cancelled, timed-out, skipped, or stale-source results remain unresolved.
7. At completion, report actual CI runs, reused evidence and validity, avoidable duplicates, public links, and any remaining gates. Never disable required checks, accept arbitrary passing versions, or weaken negative tests to reduce runtime.

## Qualification harness documentation is a required deliverable

Update project documentation together with implementation. Document implemented behavior, supported profiles, exact commands and examples derived from real CLI help, input schemas, prerequisites, platform/transport requirements, ownership and authorization boundaries, settings fixtures, final-state policies, progress viewing, result meanings, evidence placement, public acceptance, CI selection/reuse, and release recovery after partial publication.

Review and update `README.md`, `CONTRACT.md`, `docs/AGENT_OPERATIONS.md`, `docs/CURRENT_WORKFLOWS.md`, `docs/CAPABILITY_MATRIX.md`, relevant `docs/development/` guides, schema/CLI references, and roadmap entries as applicable. Add a coherent lifecycle/release guide rather than scattering contradictory procedures. Explain the separation from RF campaigns and distinguish offline, native-package, physical USB, live Pi/service, and RF evidence. Cross-link the maintained existing lifecycle tests and their owning contracts.

Include worked examples for every independent component, system test, system release, read-only plan, interrupted-run recovery, and final cleanup report. Explain normal no-button Pico operation and the limited conditions requiring physical recovery. Document real limitations and unresolved gates; do not describe proposed capabilities as implemented.

Check links, command examples, schema references, generated help/docs, and any documented workflow automatically where practical. Keep operator material factual and current; retain deep implementation details in engineering guides. Preserve unrelated documentation changes. Do not publish private imported chat context, credentials, or device evidence. Review affected product documentation and report precise follow-ups; changes to the separate Pi operator-documentation repository are covered by the supplied standing authority when necessary for this task and must follow its instructions. UI changes require the owning repository's Impeccable workflow.

## Implementation sequence and validation

First deliver the inventory, gap analysis, ownership map, proposed CLI/schema/result changes, reuse matrix, and CI selection design as reviewable implementation artifacts. Then implement bounded vertical slices: maintained Pi/RP1 scenarios and independent absence checks; distinctive settings and real transition/failure coverage; Pico software-BOOTSEL and transaction/reference gaps; independent/system orchestration and publication planning; final documentation and acceptance. Do not stop after the design when implementation is authorized. Do not silently broaden into an unapproved product repository or live operation.

Add meaningful orchestration tests for plan purity, selection/invalidation, phase/checkpoint transitions, concurrency, resume, irreversible effects, cleanup failure precedence, artifact identity, and partial publication. Exercise real production boundaries with existing integration/native tests; mocked adapters alone cannot prove installer, package, service, or firmware behavior. Preserve the portable Python core on macOS, Linux, and native Windows; isolate systemd, POSIX signals, USB and OS behavior behind capability adapters. Test spaces and Windows path forms, and state any unavailable native platform validation.

Follow the current harness acceptance commands. At the time of authoring these include `python -m ruff format --check .`, `python -m ruff check .`, `python -m mypy src`, `python -m pytest`, `python -m build`, CMake configure/build with `WSPQ_BUILD_SOAPY=OFF` and `WSPQ_BUILD_TESTS=ON`, and CTest. Use current instructions and actual supported toolchain versions. Run relevant owning-repository checks for authorized product changes. Complete required paired suites when their gate applies.

Under the applicable standing or task-specific live authority, rerun the actual eight-step Pi/RP1 sequence, the supported physical Pico fresh/preserve cycles, and the selected system handoff/public acceptance. Record exact source revisions, commands, identities, and final state. If live execution is unavailable, report it as outstanding; do not claim unattended physical behavior from simulations.

## Adversarial review and completion

After implementation, perform an adversarial assessment against the requirements and production code paths. Challenge same-version/different-byte tags, unavailable outgoing packages, public-versus-checkout installer substitution, stale green CI, unintended companion mutations, false settings preservation, missed runtime/boot residue, symlink and foreign ownership, lost reset ACKs, wrong USB identity, competing controllers, interruption at every durable boundary, premature receipt retirement, partial publication, and misleading success or cleanup claims.

Fix actionable in-scope findings, add the smallest meaningful regression coverage, rerun affected validation, and repeat the assessment until no unresolved actionable in-scope finding remains. Clearly list blocked or separately scoped issues instead of hiding them. Review documentation against the final behavior after the last code change.

Report implemented entry points and ownership boundaries; reused versus added coverage; every repository/branch and changed path; exact automated/native/live/paired tests and results; source/artifact identities; CI reuse and new runs; public verification; final device and cleanup states; outstanding limitations; and whether any commit, push, tag movement, or publication occurred. Include a Documentation Impact section listing documents updated, considered unchanged, and still required. Follow current explicit Git/publication authorization; if none exists, leave changes reviewable and report that fact.

## Appendix: user-supplied Standing Execution Authority

The following is the user-supplied authority with the two additional repositories expressly authorized by the user. The remaining authority text is unchanged. Its scope and exceptions apply together with the implementation instructions above and higher-priority instructions.

```text
Wsprry Development Session — Standing Execution Authority

1. General Authority
For this Codex session, you have standing authorization to perform all operations reasonably necessary to complete the assigned task within the systems and repositories listed below.

This authorization supplements permissions already granted. It is intended to minimize unnecessary interruptions, repeated approval requests, and routine clarification questions.

Operate autonomously. Do not request confirmation for actions explicitly authorized here unless a higher-priority policy or technical permission boundary requires it.

* Make reasonable technical decisions independently.
* Investigate, implement, test, debug, correct, and retest without asking permission at each stage.
* Do not ask for permission to inspect files, run commands, access authorized hosts, restart services, or execute tests.
* If an approach fails, investigate and attempt reasonable alternatives.
* Prefer established project conventions and existing architecture.
* Do not repeatedly ask questions already answered in the current session, repository documentation, or prior execution results.
* Continue through intermediate problems without stopping unnecessarily.
* Report significant decisions and results, but do not turn ordinary progress updates into requests for approval.
* This authority applies to the assigned task and its reasonable dependencies, not unrelated projects or operations.

2. Authorized Repositories
You have full development and Git operational authority over:

* WsprryPi
* WsprryPi-Docs
* WsprryPico
* WsprryPi-Qualification-Harness
* RP1-GPIO-CLK-DKMS

Authorized operations include:

* Read, create, modify, rename, and delete project files.
* Refactor code and modify build configurations.
* Install, remove, and update dependencies.
* Build, compile, test, package, and execute software.
* Create and modify branches, commits, tags, and pull requests.
* Fetch, pull, merge, rebase, cherry-pick, and push changes.
* Run, inspect, and troubleshoot CI workflows.
* Review CI logs, resolve failures, and rerun workflows.
* Update project documentation and tests as appropriate.
* Push validated changes to the configured project remotes.

Use normal Git safeguards. Do not overwrite remote history, force-push shared branches, disable branch protections, or delete remote repositories without separate authorization.

3. Docker and Container Infrastructure

You have authority to manage Docker infrastructure related to the authorized projects. This includes:

* Start, stop, restart, create, inspect, modify, and remove containers.
* Build, rebuild, pull, and remove images.
* Create and modify Dockerfiles and Compose configurations.
* Manage networks, volumes, environment variables, and mounts.
* Execute commands inside containers.
* Inspect logs, processes, resource consumption, and network connectivity.
* Replace or recreate containers when needed.
* Install dependencies and modify container configurations.

Preserve persistent data and configurations that are not safely reproducible. Destruction of unrelated containers, persistent volumes containing unique data, or unrelated host resources requires separate authorization.

4. Authorized Raspberry Pi Hosts
You have full operational authority over the following hosts:

* wspr5
* wspr4
* wspr2
* wspr1

You may use SSH and other existing management interfaces to:

* Inspect and modify operating-system and application configurations.
* Install, upgrade, remove, compile, and execute software.
* Start, stop, enable, disable, restart, and modify systemd services.
* Modify service definitions, scripts, configuration files, and scheduled jobs.
* Inspect logs, processes, networking, permissions, and hardware interfaces.
* Update deployed project files and binaries.
* Reboot hosts when necessary.
* Perform hardware and software diagnostics.
* Configure and test GPIO, I2C, SPI, UART, USB, and other supported interfaces.
* Collect diagnostic data and logs.
* Deploy and validate WsprryPi and WsprryPico components.

Treat these systems as an authorized development and testing environment.
Preserve unrelated services, access credentials, and persistent data.

5. Authorized Raspberry Pi Pico Devices
You have authority to control, program, and test:

* Pico A
* Pico B
* Pico C

Authorized operations include:

* Compile and build firmware.
* Install, replace, and update firmware.
* Reset, reboot, and recover devices.
* Enter and exit bootloader modes.
* Use supported USB, serial, SWD, and debugging interfaces.
* Inspect and modify device configuration.
* Test GPIO, PIO, DMA, RF generation, timing, and peripheral interfaces.
* Execute functional, integration, and regression tests.
* Capture and analyze device output.
* Repeat firmware deployment and testing cycles as necessary.

Identify the intended physical device before programming it.

6. RF Transmission Authority
You are authorized to initiate and control RF transmissions using the local WsprryPi and WsprryPico development equipment.

Before enabling transmission, verify that the intended transmitter has a valid, appropriate RF termination.
Permitted test configurations include:

* A suitable attenuator and dummy load.
* A verified attenuation path connected to the SDR.
* Another verified, appropriately terminated conducted RF test configuration.

Do not assume a safe RF path solely from prior documentation, software configuration, or a previous test.
Validate the actual connection or use reliable, current evidence of the physical RF configuration.

Once the RF path is verified, you may:

* Initiate and terminate transmissions.
* Set and adjust test frequencies, tones, and timing.
* Execute repeated transmission tests.
* Measure frequency accuracy, stability, and timing.
* Test calibration and frequency-correction routines.
* Exercise transmit scheduling and control functions.
* Perform unattended test cycles within the verified configuration.

Do not intentionally transmit into an antenna or unverified RF load under this standing authorization.

If the physical RF path cannot be verified, continue all non-transmitting work and request confirmation only when transmission becomes necessary.

7. SDR Authority — wspr5
You have full operational authority over the SDR connected to wspr5. Authorized operations include:

* Start, stop, and configure SDR software.
* Select frequencies, bandwidths, gains, and sample rates.
* Record and analyze IQ data.
* Capture spectra, waterfalls, and diagnostic measurements.
* Measure frequency error, stability, modulation, and timing.
* Execute automated receiver tests.
* Integrate SDR measurements into development and CI test workflows.
* Restart SDR-related services and processes as necessary.

8. GPSDO Authority — wspr5
You have authority to operate and configure the GPS-disciplined oscillator associated with wspr5.  Authorized operations include:

* Inspect and modify supported operating configurations.
* Read status, lock state, and diagnostic measurements.
* Select and configure supported reference outputs.
* Perform reference-frequency measurements.
* Use the GPSDO as a frequency and timing reference.
* Execute calibration, verification, and stability testing.
* Restart device interfaces or supporting services as necessary.
* Collect logs and measurement results.

Preserve known-good configuration settings when practical.

Do not perform irreversible hardware calibration or persistent reference changes without a verified recovery procedure.

9. Computer, Browser, and Developer Tools
You are authorized to use available computer-control and browser capabilities for development, testing, research, and diagnostics. This includes:

* Controlling the integrated browser.
* Using browser developer tools and JavaScript consoles.
* Inspecting network requests, responses, logs, and browser storage.
* Running browser automation.
* Accessing authorized project management and development websites.
* Using HTTP clients, APIs, and command-line network utilities.
* Downloading required documentation, development tools, and dependencies.
* Using web-based development, debugging, and diagnostic interfaces.

Use existing authenticated sessions and credentials where supported and authorized.

Do not disclose credentials or transmit unrelated private information.

10. Autonomous Execution and Recovery
When executing an assigned task:

1. Inspect the relevant state and available documentation.
2. Determine the required implementation and validation steps.
3. Execute the work using the standing authority above.
4. Diagnose and correct failures independently.
5. Repeat build, deployment, and testing cycles as needed.
6. Validate the final result using available tools and hardware.
7. Update relevant documentation and tests.
8. Commit and push validated changes when consistent with the assigned task and established repository workflow.
9. Report the final result, including important findings, changes, validation evidence, and any unresolved problems.

Do not stop simply because an intermediate command, build, test, deployment, or diagnostic attempt fails.

Do not ask which reasonable diagnostic step to attempt next. Select and execute it.

11. When Human Confirmation Is Required
Interrupt the user only when:

* An action exceeds the systems or scope authorized in this document.
* Required credentials, physical access, or permissions are unavailable.
* A higher-priority safety or security policy requires confirmation.
* An action would irreversibly destroy unique or unrelated data.
* A proposed action would overwrite protected Git history or bypass branch protections.
* RF safety cannot be established before a required transmission test.
* An irreversible hardware operation cannot be safely recovered.
* A material product, architecture, or requirements decision cannot reasonably be derived from existing instructions.
* The operation presents a meaningful risk that cannot be mitigated within this authority.

For ordinary engineering ambiguity, choose a reasonable course consistent with project conventions and document the decision.

When a blocker requires confirmation, complete any remaining independent work before asking.

12. Communication Expectations
Operate with minimal interruption.

* Do not request permission for routine authorized operations.
* Do not repeatedly seek reassurance that a chosen implementation is acceptable.
* Do not pause after each intermediate step.
* Do not convert progress reports into approval gates.
* Provide concise progress updates for significant milestones.
* Surface material blockers, safety concerns, or irreversible decisions promptly.
* At completion, provide a concise summary of work performed, verification evidence, Git state, deployed devices, and outstanding issues.

Default operating principle: If the action is necessary for the assigned task, within the explicitly authorized scope, technically reversible or reasonably safe, and not prohibited by a higher-priority policy, proceed without asking.
```
