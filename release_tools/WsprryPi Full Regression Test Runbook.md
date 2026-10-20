# WsprryPi Full Regression Test Runbook

## Purpose

This document defines the complete live regression review for a WsprryPi development host. When an operator says:

> Run regression tests in `release_tools/WsprryPi Full Regression Test Runbook.md`.

Codex must execute this runbook from beginning to end, subject to the authorization gates below. The review covers repository integrity, build and deployment state, service health, functional UI behavior, responsive rendering, configuration persistence, live attenuated RF, GPIO behavior, safety guards, logging accuracy, restoration, and final health.

The runbook is a test procedure, not standing authorization. Never infer permission for live RF, hardware changes, installation, service management, identity substitution, reboot, or shutdown merely because the operator named this document.

## Required capabilities

- Use the **Impeccable** skill for the UI/UX and responsive-rendering review.
- Use **Computer Use** or the available browser-control skill to exercise the rendered UI as an operator would.
- Use SSH for repository, build, service, configuration, GPIO, and journal verification on the target host.
- Follow the repository `AGENTS.md` and any more specific instructions encountered during the run.

If any required capability is unavailable, stop before live testing and report the missing capability.

## Authorization gate — ask before starting

Before making any operational change or starting the regression sequence, inspect only enough read-only state to identify the target and fill in the prompt below. Then ask the operator to authorize the proposed test exactly. Do not compile, deploy, change configuration, restart services, alter station identity, manipulate GPIO, transmit, reboot, or shut down until the operator responds explicitly.

Use this prompt, replacing bracketed fields with discovered or proposed values:

> I am ready to run the full WsprryPi regression suite on `[host]`, branch `[branch]`, at `[commit]`.
>
> Please explicitly authorize all scopes you want included:
>
> 1. Build and deployment: compile with `[job count]`, install a rebuilt binary if the installed version is stale, copy the current UI with `sudo ./scripts/copy_ui.py` only if the served UI is stale, and restart `wsprrypi` when required.
> 2. Temporary configuration and identity: back up the current configuration, temporarily use test identity `[callsign] / [locator]`, exercise configuration persistence and invalid-input guards, then restore the original configuration exactly.
> 3. Attenuated RF: transmit WSPR on `[band/frequency]` for at most `[duration]`, generate a test tone at `[frequency]` for at most `[duration]`, using `[backend/output path]`, with the transmitter and any amplifier connected to a dummy load or adequate attenuation. The stop procedure is `[UI Stop/End control plus service-level fallback]`.
> 4. GPIO and attached hardware: exercise transmit output `[pin]`, band selector `[pin and polarity]`, LED `[pin and polarity]`, amplifier key `[pin and polarity]`, and shutdown input `[pin and polarity]`. Confirm that these assignments match the connected test hardware or dummy loads.
> 5. Power controls: open and cancel reboot/shutdown confirmation dialogs. A real reboot or shutdown is excluded unless you separately authorize it; either action interrupts testing and requires post-boot or post-power-on verification.
>
> Reply with explicit authorization and any substitutions or exclusions. Testing will not start until you do.

Treat omissions as not authorized. If the operator authorizes only part of the suite, run the safe/read-only and authorized portions, mark the excluded checks **Not run — authorization not granted**, and do not claim the full suite passed.

If a different frequency, backend, pin, polarity, identity, duration, or physical load becomes necessary after authorization, stop and obtain amended authorization before using it. Never treat a statement that RF is attenuated as authorization to change identity or GPIO, and never treat permission to use GPIO as proof of safe physical wiring.

## Safety limits

- Prefer a bounded transmission or tone and keep a positive stop method available throughout.
- Do not leave the transmit gate enabled after a test.
- Do not leave a test tone running while navigating away or after losing controller connectivity.
- Capture the original configuration before the first change and verify its checksum after restoration.
- Use safe inactive output levels during setup and cleanup.
- Do not apply a real shutdown edge merely to test the configured input. Verify the input configuration and inactive level unless a physical shutdown test is separately authorized.
- A real reboot or shutdown requires a second, immediate confirmation because it can interrupt the remaining tests. After a reboot, wait for SSH and the controller to return and verify health. After shutdown, completion remains pending until the operator restores power and the host is verified.
- Stop immediately for unexpected RF, an asserted output that will not release, loss of the documented stop path, unsafe temperature or power behavior, corrupted configuration, or unexplained service instability.

## Test records and issue storage

Create a timestamped Markdown issue list in local Codex storage outside every product repository. Do not store screenshots, logs, credentials, temporary configuration, or regression findings in the repository unless the operator separately asks for that.

Record for every issue:

- severity and concise title;
- page, control, viewport, or hardware path;
- exact reproduction steps;
- expected and observed behavior;
- supporting screenshot, UI state, API readback, pin reading, or journal evidence;
- whether it is confirmed, intermittent, an observation, or blocked;
- whether the regression baseline is known.

Keep temporary remote evidence under `/tmp` or another non-repository temporary location. Do not expose secrets in the report.

## 1. Read-only preflight

1. Resolve the requested host and confirm SSH access with the intended account.
2. Inspect the parent branch, commit, upstream divergence, working tree, retained
   component inventory, and repository organization:

   ```bash
   git branch --show-current
   git rev-parse HEAD
   git status --short --branch
   git rev-list --left-right --count HEAD...@{upstream}
   test -f docs/components/README.md
   for component in WsprryPi-UI src/Band-Lookup src/Chipset-Offsets \
       src/INI-Handler src/LCBLog src/Mailbox \
       src/MonitorFile src/PPM-Manager src/Signal-Handler src/Singleton \
       src/WSPR-Transmitter src/WSPR-Reference; do
       test -d "$component" || { printf 'Missing component: %s\n' "$component" >&2; exit 1; }
       test -z "$(find "$component" -name .git -print -quit)" || {
           printf 'Nested Git administration under: %s\n' "$component" >&2
           exit 1
       }
   done
   git ls-files -s | awk '$1 == "160000" { found=1; print > "/dev/stderr" } END { exit found }'
   ```

3. Stop and report any unexpected dirty, divergent, missing-component, or
   nested-Git state. Do not reset, clean, stash, switch, pull, or
   overwrite it.
4. Record the running service state, installed application version, served UI build identity when available, failed systemd units, and recent warning/error journal entries.
5. Inspect current configuration without changing it. Record the station identity, mode, transmit gate, backend, GPIO assignments and polarities, boot policy, LED, amplifier, and shutdown settings.
6. Determine whether the installed binary or served UI is stale relative to the clean checkout.
7. Choose a normal four-core Pi build limit of `-j3` unless observed resource pressure requires less.
8. Present the completed authorization prompt and wait for the operator's reply.

## 2. Preserve the baseline

After authorization:

1. Copy the active configuration to a uniquely named file outside the repository.
2. Record its ownership, permissions, and SHA-256 checksum.
3. Capture an API/UI configuration snapshot when available and record its checksum.
4. Record the initial service state and relevant GPIO directions and levels.
5. Keep the backup until final restoration has been verified.

## 3. Build, deploy, and service alignment

1. Inspect the relevant build targets before running them.
2. Run the repository's safe automated regression coverage, including applicable semantic, persistence, GPIO cleanup, monitor-file, resolver, selector, repeat-policy, and UI/source tests.
3. If the installed binary is stale and deployment was authorized, perform a clean release build using the approved job limit, install it through the repository workflow, and verify the reported version matches the checkout.
4. If and only if the served UI is stale and UI deployment was authorized, run:

   ```bash
   sudo ./scripts/copy_ui.py
   ```

5. Restart the service only when required and authorized. Verify a clean startup, controller connection, and no new warning/error entries.
6. Record every build, test, install, UI-copy, and service command with pass/fail status. Do not conceal warnings or skipped tests.

## 4. Functional UI review

Exercise the application through the rendered UI, not solely through direct APIs.

1. Visit each primary navigation destination, including Operation, Setup, Maintenance, Logs, spots or reporting views, and About where present.
2. Verify controller-connected, ready/paused/transmitting, validation, saved/invalid, success, error, disabled, loading, fallback, empty, and confirmation states that can be reached safely.
3. Exercise ordinary form controls, save/load round trips, refresh persistence, modal open/cancel behavior, navigation, and reconnect behavior.
4. Confirm UI values agree with configuration/API readback and the journal.
5. Verify update status against the actual installed and checkout versions. Do not report an update solely because version comparison is unavailable.
6. Verify downloader or data-source fallback behavior and distinguish a working fallback from an undiagnosed primary-source failure.
7. Check browser console output for product-origin errors. Separate browser-extension or environment noise from application defects.
8. Specifically test whether an invalid field blocks or falsely appears to accept changes to unrelated settings.

## 5. Impeccable responsive and accessibility review

Use Impeccable to critique the live UI's hierarchy, clarity, spacing, typography, color, responsive behavior, control states, operator feedback, and accessibility. At minimum inspect:

| Viewport | Purpose |
| --- | --- |
| 1440 × 900 | Full desktop layout |
| 1280 × 800 | Compact desktop |
| 1024 × 768 | Tablet/desktop transition |
| 768 × 1024 | Portrait tablet |
| 390 × 844 | Common phone |
| 320 × 568 | Narrow phone stress case |

At every applicable viewport, inspect:

- masthead, title, controller status, navigation wrapping, and active-page indication;
- form labels, inputs, validation, buttons, cards, tables, and dialogs;
- clipping, overlap, horizontal overflow, ellipsis, awkward wrapping, and unreachable content;
- fixed or sticky elements competing with content;
- long log entries, timestamps, service names, and error text;
- focus indication, keyboard reachability, semantic labeling, touch-target size, and contrast;
- loading, disabled, pending, saved, invalid, transmitting, and stopped states.

Capture evidence for material findings. Do not change UI source during a regression-review run unless the operator opens a separate implementation scope.

## 6. Configuration and guard-path checks

Using the authorized temporary identity and hardware assignments:

1. Verify identity save/load and plan rendering for the intended WSPR identity type.
2. Configure LED, amplifier, band selector, and shutdown input one at a time, confirming UI display, persisted readback, and inactive pin level.
3. Test invalid amplifier and band-selector pin values. They must fail safely or normalize to disabled without arming an output.
4. Test relevant enable-on-boot values and restore the original selection.
5. Confirm temporary settings do not silently persist when validation fails.
6. Confirm disabled controls produce an intentional valid configuration.
7. Check that UI, configuration, runtime state, and journal wording agree.

## 7. Authorized live WSPR workflow

1. Restate the authorized identity, band, RF frequency, backend/output pin, load or attenuation, maximum duration, and stop procedure immediately before transmission.
2. Enable the persistent transmit gate through the UI.
3. Confirm the UI shows the expected WSPR plan and waiting/ready state.
4. Observe the start at the scheduled window. Record the actual RF frequency, start time, plan, and relevant journal line.
5. Disable the persistent transmit gate while a transmission is active. Verify this prevents future transmissions but does not falsely claim the active transmission has stopped.
6. Use the explicit **Stop** control to end the active transmission.
7. Verify the UI returns to paused, logs record cancellation and elapsed time accurately, and all related outputs return inactive.
8. If the primary stop control fails, use the authorized service-level fallback, stop testing, and record a high-severity issue.

## 8. Authorized test-tone and GPIO workflow

1. Restate the authorized tone frequency, maximum duration, selector pin/polarity, LED pin/polarity, amplifier pin/polarity, backend/output path, dummy load or attenuation, and stop procedure.
2. Start the test tone from Maintenance.
3. Verify the UI reports the requested RF frequency and selector assignment.
4. While the tone is active, sample and record:
   - band selector direction and asserted level;
   - transmit LED direction and asserted level;
   - amplifier key direction and asserted level;
   - shutdown input direction, pull, and inactive level;
   - pertinent journal start and frequency lines.
5. End the tone through the UI.
6. Verify the journal reports the end and elapsed duration accurately.
7. Sample the pins again. Confirm selector, LED, and amplifier are inactive and the shutdown input remains safely inactive.
8. Treat any output that remains asserted as a stop condition and high-severity defect.

## 9. Power-control UI

1. Open the reboot confirmation dialog and verify its title, warning, cancel path, and affirmative action are visible at desktop and phone widths. Cancel it.
2. Open and similarly verify the shutdown confirmation dialog. Cancel it.
3. Do not choose the affirmative action unless the operator separately and immediately authorizes the real power action.
4. If a reboot is authorized, execute it only after other tests are safely stopped and configuration is recoverable. Verify SSH, service health, controller connection, configuration persistence, and logs after return.
5. If a shutdown is authorized, state that final verification will remain incomplete until power is restored. Do not call the suite complete while the host is offline or unverified.

## 10. Restoration and cleanup

1. Ensure there is no active transmission or test tone and the persistent transmit gate is off.
2. Restore the original configuration with its original owner and permissions.
3. Verify its SHA-256 checksum exactly matches the saved baseline.
4. Restart the service only if required to load the restored configuration and only within the authorization granted.
5. Verify the restored identity, mode, backend, boot policy, transmit gate, LED, amplifier, shutdown, selector, and GPIO values through runtime readback.
6. Confirm all tested output pins are at safe inactive levels.
7. Verify service active state, zero unexpected failed units, controller connectivity, current version, and no new warning/error journal entries.
8. Recheck parent status, upstream divergence, component presence, component
   documentation, and the absence of nested Git administration.
   The test must not leave repository changes.
9. Remove temporary sensitive material when safe, but retain the local issue list and the baseline backup until restoration is conclusively verified.

## 11. Completion report

Lead with the overall outcome and link the local issue list. Report:

- target host, branch, parent commit, UI source identity when available,
  installed version, and upstream divergence;
- complete parent cleanliness and retained-component integrity;
- automated tests and their results;
- whether a rebuild, install, UI copy, or service restart occurred;
- pages, workflows, states, and viewport matrix reviewed;
- Impeccable findings and browser-console results;
- each live RF event with mode, actual frequency, duration, backend/output path, and stop result;
- GPIO assignments, polarities, asserted/inactive observations, and cleanup result;
- logging accuracy and any primary/fallback data-source behavior;
- baseline restoration checksum and final service health;
- tests skipped, authorization exclusions, limitations, and remaining qualification;
- confirmation that no regression artifacts or unintended changes were left in any repository;
- whether anything was committed or pushed.

Use **Passed** only for checks supported by direct evidence. Use **Not run**, **Blocked**, or **Observation** where appropriate. A partial authorized run is not a full-suite pass.

## Expected completion criteria

The full suite is complete only when:

- every authorized check above has a recorded result;
- active RF and tones are stopped;
- persistent transmit is disabled unless it was enabled in the captured baseline;
- tested outputs are inactive;
- original configuration is restored and checksum-verified;
- the service and UI controller are healthy;
- logs contain no unexplained new warnings or errors;
- the parent working tree and all ordinary component content retain their
  pre-test state;
- findings are saved outside the repository;
- any deliberately excluded destructive power action is clearly reported as not run.
