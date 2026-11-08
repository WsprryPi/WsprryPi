# Increase WsprryPi Fleet Capacity to 32 Devices

## Objective

Work on `devel` and increase WsprryPi Fleet capacity from eight to **32 independently scheduled remote WsprryPi/WsprryPico devices**, alongside the controller Pi’s existing local schedule.

Implement the coordinated runtime, persistence, API, UI, test, and documentation changes. Collapse the production Fleet capacity limit into **one authoritative constant**. Perform repeated adversarial reviews, resolve findings, validate the final changes, then commit, push, and report.

This is an execution prompt. Follow the standing authority included below.

## Required Outcome

When complete:

- Fleet supports zero through 32 independently scheduled remote devices, including mixed Pi and Pico targets.
- The controller’s local schedule remains independent and does not consume a remote Fleet slot.
- The 32nd assignment succeeds; a 33rd assignment is rejected before persistent changes or remote mutations.
- Existing valid assignment files continue to load without losing or changing saved settings, schedules, identities, enabled states, or recovery barriers.
- Saved fleets containing 9–32 assignments survive restart and retain the existing reconciliation requirements.
- Runtime admission, persistent validation, advertised capacity, UI behavior, and operator documentation agree.
- Exactly one production constant defines the numeric Fleet capacity. Other production layers derive their behavior from it.
- Existing ownership, identity exclusion, scheduling, frequency policy, stop, recovery, takeover, and cleanup safeguards remain effective.
- Both required production network and USB integration suites pass against the intended current source pair.
- Final changes pass another adversarial assessment after issues have been closed.
- Validated changes are committed and pushed to the configured `devel` branches, subject to normal Git safeguards and repository workflow.

## 1. Inspect and Establish the Working Baseline

Read applicable `AGENTS.md` instructions in every affected repository and nested component before acting.

Inspect:

- Repository physical paths, remotes, current branches, and working-tree state.
- Staged, unstaged, and untracked changes.
- Current local and remote `devel` revisions and divergence.
- Relevant implementation contracts, tests, CI recipes, and documentation.

Run the required initial status and diff checks. Preserve all existing user work.

The previous investigation occurred on `research/standard-feld-design`; its findings are investigation pointers, not proof of what exists on `devel`. Inspect the actual `devel` implementation before selecting changes. Do not merge the research branch or import unrelated work.

Use `devel` for this task. If the current checkout cannot safely switch because of existing work, use an isolated checkout or worktree based on the intended `devel` revision. Do not discard, stash, overwrite, or accidentally commit unrelated changes.

Resolve the actual documentation repository path from the configured checkout and remote. Earlier instructions have used differing sibling directory names; do not create a replacement repository or assume a name identifies the correct checkout.

Cross-repository inspection, required implementation, documentation updates, and validated commits and pushes are authorized for WsprryPi, WsprryPi-Docs, and WsprryPico. Keep their Git boundaries separate.

## 2. Trace the Complete Capacity Contract

Locate every production enforcement point and every assumption tied to eight Fleet outputs.

Investigation pointers from the earlier checkout include:

- `src/wtp_integration/fleet_store.cpp`
- `src/wtp_integration/fleet_runtime.cpp`
- `src/wtp_integration/fleet_runtime.hpp`
- `src/wtp_integration/target_runtime.hpp`
- `WsprryPi-UI/data/fleet-schedules.js`
- `WsprryPi-UI/data/views/fleet-schedules.php`
- `src/tests/wtp_fleet_test.cpp`
- `WsprryPi-UI/tests/wtp_pi_fleet_ui_integration_test.js`
- `docs/wtp-fleet.md`
- `docs/wtp-pi-operation.md`

Trace:

- Assignment and worker-context limits, including retained contexts awaiting cleanup.
- Storage validation, loading, saving, file-size limits, and startup recovery.
- API capacity advertisement and strict response validation.
- Browser admission, labels, feedback, rendering, and test fixtures.
- Shared schemas, generated assets, companion clients, and adapters where applicable.
- Catalog capacity and discovery limits, keeping these distinct from active Fleet capacity.
- Logging or other unrelated eight-device limits, which must remain unchanged unless demonstrably required by this task.

Do not perform a blanket replacement of numeric eight values. Protocol replay capacities, event limits, logging limits, and unrelated bounds have separate meanings.

## 3. Define One Authoritative Production Constant

Introduce one appropriately named constant, such as `kMaximumFleetOutputs`, with value `32`, in an existing suitable shared location or a small focused header.

Use it for:

- Persistent assignment-count validation.
- Assignment admission.
- Worker-context admission.
- API capacity advertisement.
- Other production Fleet-capacity checks discovered during inspection.

The UI must consume the server-advertised capacity rather than introduce another numeric production limit. Validate the advertised value before using it; an absent or malformed capacity must not enable unbounded assignment creation or silently substitute an obsolete limit.

Derive displayed capacity wording from the same advertised value where practical. Avoid requiring a separate hardcoded UI update whenever the server constant changes.

Tests may explicitly assert the required capacity of 32 and exercise the 32/33 boundary. Such independent expectations are useful regression checks and are not additional production authorities. Documentation may state the supported capacity.

Preserve the existing meaning of retained unresolved worker contexts. Raising assignment capacity must not allow repeated removal, takeover, or failed cleanup to accumulate unlimited contexts or bypass identity exclusion.

## 4. Preserve Persistence and Recovery Semantics

Review the full assignment-file lifecycle.

The earlier checkout had a 128 KiB read limit. Determine whether maximum valid 32-device state fits within the actual current limits, including:

- Maximum permitted strings and settings.
- Fully populated schedules.
- Recovery state and retained removal notices.
- Pretty-printed serialized output.
- Any associated request or response limits.

Use bounded worst-case calculations and executable validation where appropriate. Ensure the application cannot successfully save a valid file that it later refuses to reload because of its own size limits. Make the smallest necessary correction if the increased capacity exposes that problem.

Keep storage and control/API versions independent. Increasing a count limit must not automatically trigger a WTP wire-version change or an unrelated storage-format redesign.

Preserve:

- Atomic replacement and durability behavior.
- File ownership and permission checks.
- Strict document validation before field access.
- Revision and concurrency safeguards.
- Consumed-slot tracking.
- In-flight barriers.
- Paused and unresolved states.
- No automatic replay or rearming after uncertain outcomes or restart.

Assess rollback implications: an older eight-device implementation may reject state containing more than eight assignments. Document the supported operational consequence clearly. Do not silently prune assignments, normalize away user state, or add historical compatibility branches.

## 5. Assess Capacity Under Realistic Software Load

Inspect the current worker, connection, and scheduling architecture before concluding that changing a bound is sufficient.

The previous checkout used a worker thread and runtime context per output, shared persistent writes, and a process-wide bound of one outstanding DNS lookup. Verify current behavior and evaluate:

- CPU, memory, thread count, and open descriptors.
- Connection establishment and simultaneous hostname resolution.
- Job preparation and arming deadlines when many outputs share a UTC boundary.
- Serialized persistence and status operations.
- Slow, unreachable, disconnected, and restarted targets.
- API responsiveness and browser polling.
- Startup with 32 saved assignments.
- Shutdown and cleanup when several targets are unavailable.

Use the current architecture unless evidence demonstrates that a focused change is necessary. Avoid an unrelated scheduler or transport rewrite.

Keep bounded resources and failure isolation. A failing target must not cause healthy targets to miss deadlines, lose recovery state, or become uncontrollable.

Do not claim a hardware capacity qualification from host fixtures. State the tested host, workload, source revisions, resource observations, and limits of the evidence.

## 6. Coordinate API and Companion Contracts

Inspect the producer and all affected consumers before modifying any advertised capability, schema, descriptor, or protocol-facing interface.

Where applicable:

- Update authoritative schemas, strict validators, runtime consumers, serialization, generated assets, documentation, and paired adapters together.
- Maintain one current settings/control major.
- Follow the current prerelease control-policy rules.
- Keep WTP, transport/authentication, storage, and shared control contracts independent.
- Preserve exact closed validation and refusal before writes.
- Do not add arbitrary version ranges, hidden downgrade paths, relaxed validators, stale-peer substitutions, or historical control views.

A Fleet capacity increase does not inherently require Pico firmware changes or a new WTP version. Make companion changes only when inspection shows they are necessary. Record the source pair used for validation even if one repository remains unchanged.

## 7. Implement and Review the UI With Impeccable

Read and apply the required Impeccable skill before UI changes.

Preserve existing design language and interaction patterns. Ensure:

- Capacity feedback agrees with the server.
- Adding the 32nd assignment works.
- Adding a 33rd is unavailable in the UI and refused by the server independently.
- Refreshing or receiving updated state changes available capacity correctly.
- Removed but unresolved contexts produce accurate feedback.
- Long lists remain usable on desktop and mobile.
- Actions remain associated with the correct device as rows change.
- Names, long status messages, failure states, and controls remain readable.
- Existing keyboard, focus, confirmation, draft-preservation, and accessibility behavior survives.

Render and inspect the actual resulting interface at applicable desktop and mobile sizes. Review a full 32-device list and representative failure/recovery states. Address relevant Impeccable findings and report the visual evidence and workflows exercised.

## 8. Add Meaningful Tests

Use existing test infrastructure and focused targets. Inspect each target before execution.

Cover at least:

1. Zero assignments and existing valid eight-device state.
2. Successful admission and persistence of 32 unique assignments.
3. Refusal of a 33rd assignment before persistent changes or remote mutations.
4. Restart and reload with 9–32 assignments.
5. Worst-case valid serialized state and relevant size boundaries.
6. Duplicate identity and self-target refusal at the larger capacity.
7. Retained unresolved contexts and capacity reclamation after successful cleanup.
8. Independent scheduling for 32 targets sharing a start boundary.
9. A mixture of healthy, slow, disconnected, and restarted targets.
10. Pause, stop, removal, takeover, reconciliation, and shutdown at capacity.
11. Preservation of consumed slots and in-flight barriers after interruption.
12. Correct advertised capacity and browser use of that value.
13. Malformed or missing advertised capacity.
14. Desktop and mobile workflows with 32 rows.

Use deterministic host fixtures and the existing simulated backend where appropriate. Read `docs/simulated-backend.md` before application-level simulated transmission tests. Select simulation explicitly.

For capacity evidence, exercise the actual production Fleet coordinator, persistence, scheduling, and session paths against an appropriate multi-endpoint fixture. Do not treat a test that merely constructs 32 JSON rows as proof of concurrent scheduling capacity.

Run the required current-source pair suites using the checked-in `.github/workflows/debian-non-hardware.yml` recipes:

- `wtp-network-loopback`, including `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`.
- `wtp-usb-management-pair`, including the actual Pico `usb_management_driver`, `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and `WsprryPi-UI/tests/wtp_usb_pair_test.js`.

Select candidate sources and pinned dependencies explicitly. Record source revisions and tested uncommitted changes.

Both suites must pass for the intended source pair. Failed, skipped, cancelled, or timed-out required checks remain unresolved.

Run relevant standalone component tests if any component is modified, parent integration tests, UI/source regressions, and final diff checks. Choose the documented full or portable semantics profile explicitly for the host; do not describe portable coverage as full coverage.

Physical testing is available under the standing authority but is not required merely to manufacture evidence for 32 physical devices. Use hardware only where it resolves a specific validation need, and distinguish physical evidence from software simulation.

## 9. Update Documentation

Update current engineering and operator documentation in the appropriate repositories.

Document:

- Support for up to 32 independently scheduled remote Pi/Pico devices.
- Independence of the controller’s local schedule.
- The distinction between saved catalog profiles and active Fleet assignments.
- Any relevant retained-context or reconciliation capacity behavior.
- Persistence and restart behavior.
- Rollback limitations when saved fleets exceed the old limit.
- Any measured capacity constraints or meaningful operational limitations.

Locate and correct stale eight-output statements and examples within scope. Preserve unrelated eight-device limits.

Describe current operator behavior directly. Keep internal implementation details and regression evidence in engineering documentation when useful.

Build, render, and verify documentation using its repository workflow. Use Impeccable for affected rendered UI documentation. Replace screenshots only if materially inaccurate.

## 10. Perform Repeated Adversarial Reviews

After the initial implementation and validation, perform an adversarial review of the complete candidate change.

Challenge assumptions about:

- Remaining duplicated limits or stale UI assumptions.
- Assignment-count versus worker-context accounting.
- API and schema agreement.
- Worst-case file size and save/reload symmetry.
- Deadline contention, DNS contention, shared locks, and persistence serialization.
- Failure isolation and shutdown latency.
- Identity exclusion, takeover, removal, and unresolved cleanup.
- Crash recovery, uncertain outcomes, and accidental replay.
- Browser behavior at capacity.
- Documentation accuracy.
- Whether tests exercise production paths and actually prove their claims.
- Whether candidate source selection and integration evidence are correct.

Record actionable findings and resolve them. Add or adjust meaningful regression coverage where needed. Rerun affected validation.

Then run another adversarial assessment of the corrected result. Repeat this cycle until no unresolved actionable findings remain within scope and all required validation passes.

Do not close findings merely because the implementation builds or a test was weakened. Do not omit failing checks or substitute stale green CI.

## 11. Commit, Push, and Verify

Once the implementation, documentation, required validation, and final adversarial assessment are complete:

- Review the complete final diff in each affected repository.
- Confirm unrelated user work is excluded.
- Run final whitespace/error checks.
- Stage and commit only task changes.
- Keep separate repository commit boundaries.
- Push validated commits to the configured `devel` remotes under normal project safeguards.
- Never force-push shared history or bypass branch protections.
- If protections require a pull request, follow that workflow and report the exact delivery state; do not claim changes are on remote `devel` until they are.
- Inspect relevant CI for the submitted revisions, resolve failures, and rerun as needed.
- Verify the final remote commit identities.

Do not create release tags or publish releases for this task.

If a code or documentation correction is made after a passing assessment, rerun the checks and review affected by that correction before declaring completion.

## 12. Final Report

Lead with the implemented outcome and provide concise supporting evidence.

Include:

- The final supported capacity and where the single authoritative constant lives.
- Runtime, persistence, API, and UI behavior changed.
- Every modified repository and component path.
- Exact validation commands and pass/fail/skip results.
- The 32-device workload and resource evidence.
- Both paired integration-suite results, source revisions, and relevant CI links.
- Impeccable review, desktop/mobile workflows, and visual evidence.
- Adversarial findings, resolutions, and the final reassessment result.
- Hardware or deployment activity, if any, and what remains unqualified.
- A **Documentation Impact** section listing updates, documentation considered but unchanged, and outstanding follow-up.
- Final branches, working-tree states, commits, push results, and remote delivery state.
- Any remaining blockers and the exact next unfinished step.

Never claim completion while required checks remain unresolved.

---

# Wsprry Development Session — Standing Execution Authority

## 1. General Authority

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

## 2. Authorized Repositories

You have full development and Git operational authority over:

* WsprryPi
* WsprryPi-Docs
* WsprryPico

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

## 3. Docker and Container Infrastructure

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

## 4. Authorized Raspberry Pi Hosts

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

## 5. Authorized Raspberry Pi Pico Devices

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

## 6. RF Transmission Authority

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

## 7. SDR Authority — wspr5

You have full operational authority over the SDR connected to wspr5. Authorized operations include:

* Start, stop, and configure SDR software.
* Select frequencies, bandwidths, gains, and sample rates.
* Record and analyze IQ data.
* Capture spectra, waterfalls, and diagnostic measurements.
* Measure frequency error, stability, modulation, and timing.
* Execute automated receiver tests.
* Integrate SDR measurements into development and CI test workflows.
* Restart SDR-related services and processes as necessary.

## 8. GPSDO Authority — wspr5

You have authority to operate and configure the GPS-disciplined oscillator associated with wspr5. Authorized operations include:

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

## 9. Computer, Browser, and Developer Tools

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

## 10. Autonomous Execution and Recovery

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

## 11. When Human Confirmation Is Required

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

## 12. Communication Expectations

Operate with minimal interruption.

* Do not request permission for routine authorized operations.
* Do not repeatedly seek reassurance that a chosen implementation is acceptable.
* Do not pause after each intermediate step.
* Do not convert progress reports into approval gates.
* Provide concise progress updates for significant milestones.
* Surface material blockers, safety concerns, or irreversible decisions promptly.
* At completion, provide a concise summary of work performed, verification evidence, Git state, deployed devices, and outstanding issues.

Default operating principle: If the action is necessary for the assigned task, within the explicitly authorized scope, technically reversible or reasonably safe, and not prohibited by a higher-priority policy, proceed without asking.
