# The roadmap

This roadmap covers the shared frequency contract for WsprryPi and WsprryPico. Work starts from Pi `devel` on `codex/shared-frequency-contract` and coordinates the affected Pico and operator-documentation sources. Stage 0 inspection and Stage 1 implementation, validation and corrective review are complete. Stage 2 implementation, local hardware-free validation, documentation and corrective review are complete. Stage 3 implementation, local hardware-free validation, documentation and corrective review are complete. Stages 4 and 5 are not started.

## Agreed contract and scope

- A WSPR band selection represents a USB dial frequency in both CLI and UI. Resolve the effective preset, profile and per-band preference consistently before calculating RF targets.
- For WSPR, use `C = D + 1500 + r` and `Fk = C + (k - 1.5) * 1.46484375 Hz`, where `D` is dial frequency, `C` is RF center, `r` is random placement and `k` is 0 through 3. The fixed 1,500 Hz placement is the project convention.
- Pico standalone uses the same dial-to-tone calculation with `r = 0`. Do not add random placement to Pico.
- Web and CLI Test Tone band selections target the WSPR RF center without random placement. Numeric CLI and custom RF Test Tone inputs target the entered carrier directly. A Test Tone emits one steady carrier.
- CW controls accept numeric frequencies. QRSS uses carrier frequency; FSKCW uses space frequency with mark at `space + shift`; DFCW uses dot frequency with dash at `dot + shift`. QRSS and DFCW gaps have RF off; preserve FSKCW space behavior and the existing timing contracts.
- Fleet WSPR `frequency_hz` represents an absolute RF center. Preserve its numeric meaning and correct the labels and help.
- WTP event frequencies are absolute RF targets. Receiving paths do not add a WSPR offset, consult standalone frequency settings or apply Pi calibration again. Preserve explicit-frequency Morse APIs, including either supported DFCW orientation.
- Preserve existing valid Pi configuration values, custom preferences and unrelated settings. Preserve independent settings during ordinary merge, replace and partial-save operations.
- Pico is unreleased and Pi 4.0 is a closed beta. Add no migration layers, prerelease compatibility shims, compatibility-only schema versions, legacy CLI switches or deprecation framework for this work.
- Pico interoperability tests accept the selected source checkout, including uncommitted changes. Remove Pico exact-revision and clean-checkout admission gates and obsolete prerelease snapshot plumbing. Source path, revision and dirty state are informational diagnostics. Keep the separate MbedTLS dependency pin outside this change.
- Pico consumer documentation belongs under `docs/`; engineering guidance, protocols, schemas and vectors belong under `engineering/`. Update references to the current layout without duplicate documents or old-path fallbacks.
- Keep calibration ownership, correction signs, oscillator models, backend synthesis behavior and the corrected GPIO Test Tone behavior intact. Distinguish requested targets, backend realization and measured RF.
- Implement only the stage explicitly authorized next. Do not stage, commit, push, install, operate hardware or change services without the applicable authorization. Use an identified Linux environment for Pi C++ validation; do not retry local macOS C++ compilation.

## Stage 1 Pi terminology CLI alignment and test fixtures

### Status

Stage 1 implementation, hardware-free validation, documentation and corrective review are complete. Implementation commits `7b57af1e7d9f2687524c371436ffa50ff35d3ebd` and `e677056bd8258d4fdcd8f559f82c09b237244733` were published on the feature branch. All six jobs passed for `e677056` in [Non-hardware validation run 40050920404](https://github.com/WsprryPi/WsprryPi/actions/runs/40050920404): six-profile GCC 13 release builds, strict I2C, the cloud macOS simulated profile, Linux semantics/UI/hardware-access audit, current-source network interoperability and the actual USB producer/client/endpoint pair. The final accompanying preservation coverage also passed the complete local configuration-interoperability target. At Stage 1 closeout, Stages 2–5 were unstarted.

- Pi implementation: `codex/shared-frequency-contract`, based on `f19e9556bc4dbc336b24c6ba1f5943a1b92bc751`; UI and C++ components are tracked directly on this branch, with no submodule pointer changes.
- Operator documentation: `WsprryPi-Docs`, `codex/shared-frequency-contract`, based on `a6b2ba0ea8802454b7e464b7f48daa2cfdbd6034`, published as `6ea880fb90861301641d0b487cb16eb7e3b059c6`. Nine pages and three materially outdated screenshots were updated; Sphinx built without warnings. The documentation feature branch has no applicable CI run; its documented local build and rendered-page review provide the validation evidence.
- Selected Pico input: `/Users/lbussy/GitHub/WsprryPico`, `devel`, `31ac24c6e43f363fa87a3f6513dba8ff2b768033`, clean and read-only. Its standalone setting remains an RF base frequency. Test fixtures explicitly select that setting at runtime without changing Pico.
- MbedTLS input: `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`; the independent dependency pin and clean-source policy are unchanged.
- Linux validation: isolated Ubuntu 24.04 ARM64 Docker container, GCC 13.3.0 and libgpiod C++ 1.6.3. `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` was set throughout. No physical devices, installations, firmware, services or target configuration were operated; no local macOS C++ compilation was attempted.

The following commands passed from the corresponding Linux Pi `src` directory, with no skipped required cases:

```sh
export WSPRRYPI_DISABLE_HARDWARE_ACCESS=1
make test-tone-request-test test-tone-frequency-plan-test test-tone-selector-plan-test test-tone-response-test test-tone-lifecycle-test legacy-tone-frequency-test SUDO=
make band-lookup-correlation-test wspr-band-catalog-test wspr-preset-catalog-response-test SUDO=
make wspr-tone-regression-test qrss-execution-regression-test wtp-plan-test wtp-fleet-test wtp-config-test wtp-api-test SUDO=
make wtp-config-interop-test wtp-network-interop-test SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/mbedtls
make semantics-test SUDO=
```

`/pico` is the read-only selected Pico checkout above; `/mbedtls` is the separately pinned dependency. The full semantics suite also passed under `strace`; `scripts/ci/assert-no-transmitter-hardware.sh` reported no prohibited transmitter hardware access. Missing `/proc/device-tree/model` and compatible-file diagnostics are expected on the Linux container and do not establish hardware qualification. Make also reports jobserver resets where component recipes explicitly select `-j18`, `-j2` or `-j4`; those notices did not skip or fail tests. CI package setup reports absent manual-page alternatives in its slim Debian image; these are package-install notices, not skipped test cases.

The production USB endpoint was built with `cmake -S /pico -B /tmp/stage1-usb-host -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` and `cmake --build /tmp/stage1-usb-host --target usb_management_driver -j4`. `make wtp-usb-pair-client-build wtp-usb-management-test SUDO=`, `node WsprryPi-UI/tests/wtp_usb_settings_test.js` and `node WsprryPi-UI/tests/wtp_usb_pair_test.js src/build/wtp-network/wtp_usb_pair_client /tmp/stage1-usb-host/usb_management_driver` passed through the actual host-only producer/client/endpoint path.

`python3 -B scripts/tests/pico_source_selection_test.py /pico /mbedtls` passed. It copies current selected inputs into a disposable checkout, edits a compiled production endpoint source, verifies the edit executes in both client and network fixtures, then verifies missing source and real compiler errors fail. The original Pico checkout remains unchanged. Current-layout Consumer-Radio and Consumer-Recovery vectors and protected-request, identity, stale-revision, atomic-persistence and unrelated-setting tests passed through production handlers. Current consumer station/AP tests also exercise complete PUT, merge and replace writes, preserving a non-default standalone RF base and the full unrelated configuration while rejecting wrong targets, stale revisions and failed persistence.

`npm run test:unit`, `npm run test:browser` and `node tests/wtp_pi_fleet_ui_integration_test.js` passed from the Linux UI directory. Impeccable review exercised WSPR and CW controls, band and custom Test Tone, invalid input, previews, accepted status, mode changes, draft retention and keyboard recovery. Evidence covers 18 Test Tone workflows, 29 modal screenshots, five console pages, four modes, two themes, 1440/1024/390-pixel layouts, 200% text and Fleet desktop/mobile views. Error contrast is at least 5.75:1. The mechanical detector's three color advisories refer to unchanged runtime-message colors; the pre-existing design-sidecar drift was left outside scope.

Operator documentation passed `source docs/.venv/bin/activate` followed by `python -m sphinx -b html docs docs/_build/html`. All nine affected pages were rendered at 1440×1000 and 390×844; image loading and horizontal bounds passed. Operations frequency panels and the CW control screenshot were refreshed from mocked application fixtures. INI/API keys and frequency persistence semantics were reviewed and retained. No additional Stage 1 operator-documentation changes remain.

Adversarial review and corrective reassessment covered the complete diff and evidence. Repairs closed hidden invalid repeated or superseded CLI tone arguments, partial numeric/CW parsing, numeric-form preservation, current Pico source closure, runtime frequency setup and replacement-write coverage, shared fixture-port concurrency, dark-theme validation contrast, screenshot viewport/chrome fidelity, all four Fleet tone targets and stale CW calibration instructions. The fresh final source assessment found no unresolved actionable Stage 1 defect. Firmware integrity, credentials, target identity, WTP absolute targets, MbedTLS policy, scheduled WSPR placement, selectors, timing and gaps remain intact. Historical release manifests and adapted-source provenance remain attribution records, not checkout admission gates.

Application/UI and operator-documentation commits and normal feature-branch pushes remain separate. The implementation and documentation remote tips were verified after publication. Every subsequent feature push receives the same applicable CI, including the final preservation-test and roadmap commit; completion is reported only after that submitted commit's results return. Hardware, RF, service and installation qualification remain outside this stage.

### Work

- Clarify WSPR dial, RF center, tone 0, CW frequency references, direct carrier targets and random offset in configuration controls, previews, status, logs, CLI help and affected developer contracts. Keep previews distinct from committed status.
- Change CLI band-selected Test Tone so `--test-tone 20m` follows the UI WSPR-band calculation. Keep explicit numeric Test Tone inputs as direct RF. Use shared frequency resolution and prevent argument-order-dependent results wherever profile and preference settings apply.
- Keep CW inputs numeric and retain band correlation for output limits and filter selection.
- Correct Fleet WSPR labels and help to RF center without changing saved values or execution semantics. Use mode-appropriate labels for its shared frequency control.
- Remove Pico exact-SHA and clean-working-tree enforcement from `src/tests/network/CMakeLists.txt`. Update Makefile and CI selection to use the supplied Pico checkout locally and Pico `devel` by default in CI, with an explicit ref override when needed.
- Remove obsolete Pico pin/profile machinery used solely for prerelease snapshots, retaining useful behavioral cases as current-contract tests. Do not replace the gate with a commit prerequisite or another approval mechanism.
- Correct affected fixture and CI references to the current Pico layout, including `engineering/protocol/Consumer-Radio-v1-vectors.json`.
- Update affected Pi operator documentation in `/Users/lbussy/GitHub/WsprryPi-Docs`. Describe current behavior directly; omit migration and historical bug narratives.

### Acceptance

- With the standard `20m` preset and random placement disabled, scheduled WSPR has dial 14,095,600 Hz and RF center 14,097,100 Hz. CLI and web band-selected Test Tone each target one carrier at 14,097,100 Hz. Numeric Test Tone input `14095600` targets exactly 14,095,600 Hz.
- Tests cover aliases, effective presets and preferences, numeric and unit-suffixed inputs, argument ordering, CW rejection of band aliases, unchanged configuration round trips and Fleet center semantics.
- Interoperability fixtures accept an edited Pico checkout, consume current-layout vectors and still fail for missing required sources or genuine build and contract errors.
- Run affected Test Tone, band-catalog, frequency-semantics, Fleet, WTP and UI tests. Apply Impeccable and inspect desktop and mobile fixture views for affected controls and status.

## Stage 2 Pico standalone WSPR dial frequency

### Status

Stage 2 implementation, local hardware-free validation, operator documentation and adversarial corrective review are complete. Coordinated feature pushes select Pico `codex/shared-frequency-contract` for both Pi network and USB jobs, record the actual selected revision and retain explicit `pico_ref` override precedence. Other contexts keep the normal `devel` default. Submitted-commit CI is checked separately from the local evidence below. At Stage 2 closeout, Stages 3 through 5 were unstarted.

Standalone input and persistence use whole-Hz WSPR USB dial `wspr_dial_frequency_hz`. One checked integer-nHz calculation constructs the four nominal RF targets around dial +1500 Hz, with no random placement, and both frames retain those targets. Dial `14095600` produces center `14097100 Hz` and exact targets `14097097802734375`, `14097099267578125`, `14097100732421875`, `14097102197265625` nHz. The dedicated resource retains `frequency_hz` and schema version 1. Application views 1–4, the strict Pi adapter and management display use only `schedule_dial_frequency_nhz`, with the existing nontransmitting `"0"` sentinel; standalone status retains null for unset. Generic resources redact and preserve the independent dial.

All 148 registered Pico host tests passed without skips, with firmware disabled, tests enabled and the separately pinned MbedTLS input supplied. Focused numerical/API tests cover 132/138/150 MHz engine limits, every RF tone’s policy admission, strict lexical parsing versus schema integer domains, single/pair scheduler events, nondefault persistence, first API and Console saves, STOP, identity, ABA revisions, storage faults and incompatible latest journal rejection. Seven offline pair-analysis tests and the WTP contract validator passed. The shipped-assets browser integration passed desktop/mobile, native keyboard save, invalid/policy-rejected drafts, read/save/discard, stale revision recovery, wrong target, simulated boot identity review, 200% text and independent journal inspection after persistence failure. Generated operator hashes and actual browser-generated whole-Hz writes were checked.

Pi’s requested WTP configuration, API, plan, Fleet, configuration/network interoperability, host-only USB producer/client/endpoint, Test Tone frequency/response and WSPR tone targets passed. The full Linux `semantics-test SUDO=` suite passed with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`. UI unit and browser suites passed; management resource rendering used responses emitted through actual Pi/Pico adapters, including unset dial, both Pi themes and desktop/mobile layouts. The edited-source proof executed an instrumented production Pico endpoint in both client and network fixtures, then confirmed missing source and genuine compiler errors fail. Its fixture port is kept free from other interoperability runs.

Pi C++ validation used isolated Ubuntu 24.04 ARM64 with GCC 13.3.0 and libgpiod C++ 1.6.3. Pico host validation used isolated Debian 13 ARM64 with GCC 14.2.0, locked Node dependencies, Python schema/NumPy support and MbedTLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Normal and inhibited firmware linked in isolated Ubuntu 24.04 x86 Linux using the repository-pinned Arm GNU 15.3.1 archive, SDK `079c6f39023649b154152db30f1d781e884879bc` and picotool `6f6458d792b93685a11423b244a585eaa99eafcf`. Application/inhibition, asset, stack, heap, RF renderer and reserved-journal image checks passed, and UF2 application payloads matched their ELF load segments. The SDK’s source timestamp update and tests’ source-local output use disposable writable source copies; the real checkouts and retained outputs are preserved. No local macOS C++ compilation, flashing, live USB, GPIO, transmission, installation or target service operation occurred.

Pico consumer and engineering documentation, Console examples, installer preservation assertions and the offline tool’s explicit `--dial-hz` input use the current reference. Pico’s strict Sphinx build, documentation checker and image-delivery test passed; the Pi operator Sphinx build passed. Seven affected pages were reviewed at desktop/mobile widths with all images loading and no page overflow. The materially outdated standalone label screenshot was refreshed from the shipped UI. Stage 1 Test Tone and Fleet center semantics, CW direction, Pi random-placement lifetime, calibration, ownership and scheduler lifecycle remain outside this change.

Adversarial review closed inconsistent first Console selection enablement, unchanged-but-blocked dial preservation through generic replacement, old tone-0 fixture expectations, decoder finite-input protection, stale developer terminology and screenshot/feedback mismatches. Corrective test edits were compiled and rechecked, including strict rejection of old and dual recurrence members and inspection of committed journal values after failed saves. The fresh full coordinated assessment found no unresolved actionable Stage 2 issue. Software and firmware evidence does not establish RF, target timing, physical USB, installation or service qualification.

### Work

- Replace the persisted and internal standalone frequency member with `wspr_dial_frequency_hz`. Calculate the RF center and four tones during preparation, exactly once, using the agreed formula and no random placement.
- Keep the independent `/api/v1/standalone-frequency` resource and its generic `frequency_hz` member. Define the value as whole-Hz WSPR dial frequency throughout capability descriptions, validation, readback and browser controls.
- Rename the recurrence projection to `schedule_dial_frequency_nhz` across Pico status, existing application schema views, Pi's strict parser, management display and all affected fixtures.
- Update configuration parsing, serialization, scheduler pair snapshots, generic-update preservation, Console, browser drafts, installer-preservation coverage and affected offline analysis tools together. Preserve independent and unrelated settings through ordinary updates.
- Validate every resulting RF tone against the actual selected engine limits and frequency policy, with checked arithmetic and exact nHz handling. Keep unset frequency intentional and schedule enabling separate from frequency saving.
- Regenerate shipped operator assets through the existing build workflow and verify their hashes. Update Pico consumer instructions under `docs/` and API, schema and developer guidance under `engineering/`.
- Use existing strict handling of incompatible saved configuration. Add no old-base reader, base-to-dial migration, dual semantics or new reset workflow.

### Acceptance

- For dial 14,095,600 Hz, require the independent exact tone targets 14,097,097.802734375; 14,097,099.267578125; 14,097,100.732421875; and 14,097,102.197265625 Hz.
- Cover unset and valid values, fractions and suffix rejection, overflow, engine boundaries, frequency policy, exact serialization and both frames of a message pair.
- Cover wrong target, stale revision, failed validation, failed persistence, draft retention, generic merge/replace preservation and unchanged unrelated settings. Failed saves leave committed configuration intact.
- Exercise actual browser-generated requests, Pi adapters and Pico handlers together against the selected edited sources. Remote WTP jobs retain their absolute event frequencies regardless of standalone settings.
- Run affected Pico host, schema, browser, Console, installer-preservation and asset-integrity tests, plus Pi configuration and interoperability tests. Review desktop and mobile controls with Impeccable.

## Stage 3 Pico CW references and shift direction

### Status

Stage 3 implementation, hardware-free validation, operator/API documentation and adversarial corrective review are complete. QRSS enters carrier, FSKCW enters space and DFCW enters dot. The two-frequency browser modes add a positive shift. For B = 14096900 Hz and s = 5 Hz, QRSS carrier is 14096900 Hz; FSKCW space is 14096900 Hz and mark is 14096905 Hz; DFCW dot is 14096900 Hz and dash is 14096905 Hz. Exact B and B+s values are 14096900000000000 and 14096905000000000 nHz. The unchanged explicit API and encoder retain either distinct positive DFCW orientation. Stages 4 and 5 remain unstarted; submitted-commit CI is assessed separately from this local evidence.

The browser shares exact BigInt frequency construction between preview and submission, checks uint64 addition and each advertised engine range, ignores the disabled QRSS shift without discarding it, and preserves raw frequency, shift, message and timing drafts. Local invalid requests reach neither LOAD_MESSAGE nor ARM. Server policy, ownership, clock admission, allow_frequency_adjustment and calibration ownership remain authoritative. The decoder, Morse encoder, standalone WSPR dial/snapshots, Test Tone/Fleet semantics, timing defaults, repeats and raw absolute job behavior are unchanged.

All 157 applicable Linux host tests passed without skips with separately pinned MbedTLS and the optional NumPy/Harness analysis dependencies present. After formatting the changed C++ test lines, all eight focused browser, network, core, RF-stream, consumer-session, frequency-policy and operator-asset checks passed again. The WTP validator passed 23 schema, seven raw-JSON, one framing and eight transition cases. Source hashes matched all 865 selected files in both isolated build environments; edited sources were accepted before commits without clean-checkout or source-revision gates.

The actual-browser integration serves generated app.min.js through compiled production handlers and exercises consumer and engineering forms with native Tab/Enter submission. Submitted requests, decoded/accepted compiled jobs and independently specified events agree. The repeated "A E" vector has 12 events and duration 85000001000 ns for QRSS/FSKCW or 73000001000 ns for DFCW. Separate "AE" checks cover character gaps. Exact fractional nHz, both explicit DFCW orientations, invalid/equal/reversed API requests, raw job files, finite tones, local validation, server policy rejection, preserved drafts, offline/login recovery, unchanged journals, named previews and accessible labels passed. Repeat gaps stay RF-off in every mode and the final tail stays 1000 ns RF-off.

The following commands ran in isolated Linux environments; host/browser commands use /work/stage3-source as the Pico working directory. The retained Chromium wrapper only supplies the container's required --no-sandbox flag. The Harness path supplies existing offline analysis modules, not hardware access.

```sh
cmake -S /work/stage3-source -B /work/stage3-host-writable -DCMAKE_BUILD_TYPE=Debug -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DWSPRRY_PICO_TEST_MBEDTLS_PATH=/sdk-input/lib/mbedtls -DWSPRRY_PICO_HARNESS_PYTHON=/usr/bin/python3
cmake --build /work/stage3-host-writable --parallel 4
PYTHONPATH=/work/stage3-harness-src NODE_OPTIONS=--experimental-websocket ctest --test-dir /work/stage3-host-writable --output-on-failure --timeout 240
python3 -B scripts/validate_wtp_contract.py
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/message_frequency_ui_integration.mjs /work/stage3-host-writable/consumer_bootstrap_driver /work/stage3-cw-ui-reviewed
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/stage3-host-writable/consumer_bootstrap_driver /work/stage3-standalone-ui
```

Normal WsprryPico and WsprryPico-Inhibited firmware cross-builds passed with SDK 2.3.1 at 079c6f39023649b154152db30f1d781e884879bc, Arm GNU 15.3.1 and picotool 2.3.0. The normal image uses the 138 MHz RF application and the inhibited image uses its simulator. Application identity, BOOTSEL topology, flash/journal separation, stack/heap guards, allocator/pool hooks and applicable RF-worker checks passed. Both UF2 payloads match their linked ELFs. The selected source snapshot is writable because the pinned SDK/picotool build touches CMakeLists.txt; repository sources remain read-only mounts. The Arm tools are on PATH for post-link checkers. Picotool reports its intentional build without USB support. Neither image was flashed.

```sh
cmake -S /work/stage3-source -B /work/stage3-firmware -G Ninja -DCMAKE_BUILD_TYPE=Release -DWSPRRY_PICO_BUILD_FIRMWARE=ON -DWSPRRY_PICO_BUILD_TESTS=OFF -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=/sdk -DPICO_TOOLCHAIN_PATH=/work/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi -DPICOTOOL_FETCH_FROM_GIT_PATH=/work/stage3-firmware/_deps -DPICOTOOL_FORCE_FETCH_FROM_GIT=ON -DPICOTOOL_GIT_REPOSITORY_URL=/picotool/picotool-src
PATH=/work/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi/bin:$PATH cmake --build /work/stage3-firmware --target WsprryPico WsprryPico-Inhibited --parallel 4
```

Pi plan/frequency-vector, QRSS execution, Fleet, configuration interoperability and network interoperability tests passed against the edited Pico sources. The retained standalone MbedTLS checkout at /work/mbedtls has the same pinned 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20 revision; the original bind-mounted submodule's Git indirection is not usable inside that container. These commands ran from the Pi src directory with hardware access disabled:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-plan-test qrss-execution-regression-test wtp-fleet-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-config-interop-test wtp-network-interop-test SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/work/mbedtls
```

Impeccable review inspected the existing design and final desktop/mobile form, preview, keyboard focus, invalid input, refusal and 200% text views. The intentionally light-only theme remains light under a dark preference. The mechanical detector ran with degraded parser availability and reported nine preexisting design-sidecar token advisories; no unrelated sidecar or design changes were made. Manual contrast checks include 8.14:1 for action errors, 5.94:1 for help, 12.35:1 for body text and 6.92:1 for primary buttons. Rendered operator instructions passed desktop/mobile review without horizontal overflow.

Adversarial assessment closed stale accepted-job feedback beside an edited draft and dimmed server-refusal feedback inside a disabled fieldset. Editing now clears the old action result; action status remains outside disabled controls with explicit regression assertions and reviewed screenshots. Fixture corrections model an idle boot with saved settings, observe the correct consumer/engineering instance, retain unique terminal job identities and use the production frequency-policy engine. Initial read-only-output, missing analysis-module, Arm-tool PATH and MbedTLS Git-indirection failures were corrected in the isolated environments without weakening tests or source/dependency policy. The final complete assessment found no unresolved actionable Stage 3 issue.

Documentation impact: Pico docs/operate.md and engineering/browser-api.md describe the implemented references, positive shift, direct RF inputs, named previews, validation and explicit API capability. Pico README, existing screenshots and Pi operator instructions in Install/pico.md, User_Interface/Setup/Signal_Setup/cw.md, Command_Line_Operations/cw_modes.md and User_Interface/Setup/Transmitter/index.md were considered and remain accurate. No Pi operator-documentation edit or empty commit is needed. The project-local strict Sphinx build, documentation/link checker and image-delivery test passed; no documentation was deployed. No further Stage 3 documentation is required.

```sh
docs/.venv/bin/python -m sphinx -E -a -n -W --keep-going -b html docs docs/_build/html
docs/.venv/bin/python scripts/check_documentation.py
docs/.venv/bin/python tests/test_image_delivery.py
```

Feature pushes retain the Stage 2 coordinated Pico branch selection and explicit pico_ref override precedence. The Pico publication workflow's main/devel/tag restrictions remain unchanged; no arbitrary feature publication or release metadata was introduced. Software, browser and firmware-build evidence does not establish physical RF, target timing, USB hardware, installation or service qualification. No live device, service, GPIO or RF operation was performed.

### Work

- Present mode-specific numeric inputs: carrier for QRSS, space for FSKCW and dot for DFCW, with a positive shift for the two-frequency modes.
- Construct FSKCW mark as `space + shift` and DFCW dash as `dot + shift`. Update labels, help, preview, validation and generated operator assets together.
- Preserve the absolute-frequency message API and encoder behavior, including explicit DFCW jobs with either supported orientation. Add no reverse-direction control solely for compatibility.
- Update the affected consumer operating instructions and engineering API examples without changing shared timing, repeats or RF-off gap behavior.

### Acceptance

- Verify actual browser-generated payloads and compiled events for QRSS, FSKCW and DFCW, including frequencies, shift direction, equal DFCW element durations, repeats and gap behavior.
- Retain behavioral tests for explicit-frequency API orientation and bounds. Review mode changes, validation, preview and responsive controls with Impeccable at desktop and mobile sizes.

## Stage 4 Pi WSPR message pair placement

### Work

- Snapshot the selected random placement with the prepared WSPR message plan and reuse it for both frames. Draw a new placement for the next independent plan.
- Carry placement through scheduler preparation, copy, commit and reset paths. Handle cancellation, completion, skipped entries, configuration reload and failed preparation consistently.
- Add a deterministic test seam without a production seed flag or new configuration setting. Preserve the existing random range and disabled behavior.
- Update WSPR scheduling and random-offset documentation to match the implemented plan lifetime.

### Acceptance

- Force `+37 Hz` and verify identical placement in both frames; force `-21 Hz` for the next independent plan and verify a new draw.
- Cover disabled randomization, single-frame plans, pair completion, cancellation, reload, skipped entries and preparation failure. Verify no stale placement leaks into a new plan or Test Tone.
- Run focused scheduler and frequency tests, then applicable semantics and backend conversion regressions with hardware access inhibited.

## Stage 5 Integrated validation and documentation closeout

### Work

- Run the relevant Pi and Pico host suites and current-contract interoperability tests against the actual selected sources, including uncommitted changes. Report source identities without requiring commits or pin updates.
- Verify the independent WSPR arithmetic across ordinary Pi scheduling, Pico standalone and absolute WTP events. Check backend realization separately from requested targets and measured RF.
- Run Pi UI unit and browser coverage, Pico browser and asset checks, and Impeccable review of affected desktop and mobile workflows. Preserve drafts and distinguish preview, saved, pending and committed states.
- Review Pi operator documentation and Pico consumer documentation for consistent current terminology and examples. Keep engineering contracts under the engineering layout, repair affected links and build the documentation using each repository's documented workflow.
- Run the broad applicable Linux semantics suite after focused checks pass. Record failures, skips and environment limitations without treating host or simulated results as hardware or RF qualification.

### Acceptance

- Every supported input path has one documented meaning, and tests establish the resulting request and execution-plan frequencies without omitted or repeated conversion.
- No prerelease migration layer, historical Pico checkout gate, obsolete document-path fallback or CLI/UI band exception remains in the affected scope.
- Review the complete diffs, generated assets, documentation output and working-tree state. Report tests, visual evidence, documentation impact and remaining validation separately for each repository.
- Hardware, RF, installation and service qualification require an explicitly authorized finite campaign; they are not established by these software checks.
