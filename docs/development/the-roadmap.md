# The roadmap

This roadmap covers the shared frequency contract for WsprryPi and WsprryPico. Work starts from Pi `devel` on `codex/shared-frequency-contract` and coordinates the affected Pico and operator-documentation sources. Stage 0 inspection and Stages 1–4 implementation, validation, documentation, corrective review and publication are complete. Stage 5 integrated software validation, corrective review and documentation closeout are complete; exact submitted-tip CI remains the publication acceptance rule described below.

The original Stage 2 scope and its first-use frequency-save corrective follow-up remain complete. The follow-up completed implementation, local hardware-free validation, documentation and adversarial corrective review after Stage 3. Stage 5 revalidates both alongside the complete shared contract. Hardware, measured RF, target timing, installation and service qualification remain separate evidence domains.

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

### Corrective follow-up First-use frequency save

Status: implementation, local hardware-free validation, documentation and adversarial corrective review are complete. This follow-up follows completed Stage 3 and precedes Stage 4; the original Stage 2 scope and Stage 3 remain complete. Pico corrective commit `d48be0fd4aab303838877db4e82906413b933753` is published on `origin/codex/shared-frequency-contract`. The accompanying Pi roadmap commit uses that coordinated branch; submitted-commit CI is assessed separately from this local evidence.

The first-use defect was reproduced in current production-handler fixtures: healthy frequency storage and a null frequency do not establish an effective applied station, and a complete station draft remains unapplied. The original investigation used historical sources and RAM journals; it did not inspect the tester's physical Pico or exact firmware. Its evidence remains at `/Users/lbussy/Codex-Local/2026-10-24/referenced-chatgpt-conversation-this-is-an/outputs/frequency-save-investigation-evidence.zip`.

#### Implemented correction

The editor brackets frequency and saved-progress reads with the existing `station/2` resource. All observations must agree on member identity, boot and configuration revision; saved progress must agree with the authoritative applied station. Draft completeness controls guidance only. No standalone journal is required for an applied consumer overlay. Network-only profiles, including those with a station-like backing record, and unapplied USB-managed profiles remain unavailable for frequency Save. An applied station with an unset frequency can select its first dial.

Browser text is retained while the station prerequisite is missing, through explicit application and reconnect, and after refused or ambiguous saves. Missing/incomplete and complete-draft guidance uses the actual profile's Save and Apply controls. A complete draft points directly to Apply. `not_configured` explains that prerequisite without an ineffective read/save loop. New boot/target observations retain explicit draft adoption. Observed revision changes invalidate readiness. Application refreshes readiness without a page reload; the shared action lock covers both automatic readiness refresh and explicit frequency reads/saves, preventing overlapping consumer requests. Independent consumer-password ETags are excluded from configuration-revision observation.

The backend guard, whole-Hz WSPR dial, exact integer-nHz conversion, four-tone policy, journal format, recurrence projection, both-frame snapshots, absolute WTP targets, Phase 1 semantics and Phase 3 CW behavior are unchanged. The correction introduces no endpoint, schema, station manufacture, implicit draft application, schedule enablement, ownership grant, STOP/inhibit reset, accepted-job retune or browser persistence.

#### Validation and corrective assessment

All 158 applicable Linux host tests passed without internal skips as an ordinary container user, with pinned MbedTLS and the retained NumPy/Harness analysis dependencies. Independent backend coverage confirms `409 not_configured` before configuration persistence for missing and draft-only stations. Named network-only, network-with-backing-record, USB-managed and consumer-overlay fixtures confirm explicit application, unset first selection, journal reload/reboot, disabled schedules and inactive output. Actual committed-frequency reply loss is read back after reauthentication and simulated reboot without replay. The WTP validator passed 23 schema, seven raw-JSON, one framing and eight transition cases. All 866 selected source hashes matched the final edited checkout in the host and firmware environments; source identities remain informational rather than admission gates.

Generated shipped assets ran through `consumer_bootstrap_driver` and production HTTP/session/API/journal handlers. Desktop/mobile browser checks inspected actual integer-Hz requests and independent journal observations for missing station, complete draft, explicit application, first save, refusal, ambiguous connection loss, reload/reboot and explicit new-boot adoption. The configured workflow retained policy, stale-revision, identity and storage-failure coverage. A current-revision test adapter isolates the actual `not_configured` refusal from the separately tested stale-revision guard; it does not bypass the production station guard. Existing consumer and engineering CW integrations retained their exact requests and compiled event assertions.

```sh
cmake -S /work/first-use-source -B /work/first-use-host -DCMAKE_BUILD_TYPE=Debug -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DWSPRRY_PICO_TEST_MBEDTLS_PATH=/sdk-input/lib/mbedtls -DWSPRRY_PICO_HARNESS_PYTHON=/usr/bin/python3
cmake --build /work/first-use-host --parallel 4
PYTHONPATH=/work/stage3-harness-src NODE_OPTIONS=--experimental-websocket ctest --test-dir /work/first-use-host --output-on-failure --timeout 240
python3 -B scripts/validate_wtp_contract.py
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-configured-ui-closed
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-network-doc-final first-use:network
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-usb-ui-final first-use:usb
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-overlay-ui-final first-use:overlay
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-network-backing-ui first-use:network-backing
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/standalone_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-refusal-ui-final first-use:refusal
CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket node tests/message_frequency_ui_integration.mjs /work/first-use-host/consumer_bootstrap_driver /work/first-use-cw-ui
```

Normal and inhibited firmware cross-builds passed with SDK 2.3.1 `079c6f39023649b154152db30f1d781e884879bc`, Arm GNU 15.3.1 and picotool 2.3.0. Applicable identity, topology, flash/journal separation, stack/heap, allocator/pool and RF-worker link/image checks passed. Both UF2 payloads matched their linked ELFs. Picotool's intentional no-USB build warning remains; no firmware was flashed.

```sh
cmake -S /work/first-use-source -B /work/first-use-firmware -G Ninja -DCMAKE_BUILD_TYPE=Release -DWSPRRY_PICO_BUILD_FIRMWARE=ON -DWSPRRY_PICO_BUILD_TESTS=OFF -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=/sdk -DPICO_TOOLCHAIN_PATH=/work/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi -DPICOTOOL_FETCH_FROM_GIT_PATH=/work/first-use-firmware/_deps -DPICOTOOL_FORCE_FETCH_FROM_GIT=ON -DPICOTOOL_GIT_REPOSITORY_URL=/picotool/picotool-src
PATH=/work/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi/bin:$PATH cmake --build /work/first-use-firmware --target WsprryPico WsprryPico-Inhibited --parallel 4
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-config-interop-test wtp-network-interop-test SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/work/mbedtls WTP_NETWORK_BUILD_DIR=build/first-use-network
```

The Pi command runs from the isolated Pi `src` directory against the selected edited Pico sources, with the independently pinned MbedTLS revision `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Configuration, saved-progress, native station/AP, shipped crypto-context and actual TLS loopback interoperability checks passed. An initial copied CMake-cache path failure was repaired with the unused build directory above. The initial root host run's three ordinary-user installer skips were resolved by running the complete suite as the ordinary user; no required skipped case is counted as passing.

Impeccable hardening/clarification and the existing visual language guided implementation. Inspected desktop/mobile screenshots cover missing station, complete draft, applied readiness, first save, local validation, server refusal, ambiguous disconnect, reconnect/identity review, keyboard Tab/Enter and 200% text without horizontal overflow. The frequency status is a polite live region associated with the input. The detector reported no findings on affected files but ran with degraded parser availability, so it is not independent computed-contrast evidence. No unrelated design-sidecar changes were made.

Adversarial review and corrective reassessment closed overlapping post-login/readiness requests in `app.js` by holding the shared action lock until readback completes; production-lifecycle held-promise assertions and the shipped application/reconnect test cover the repair. Review also closed unrelated password-ETag invalidation, repetitive complete-draft guidance, contradictory discard feedback and the outdated station-draft screenshot. Browser/unit coverage, independent journal observations, strict documentation builds and inspected final screenshots cover those repairs. A complete final assessment of the repaired diff and evidence found no unresolved actionable corrective issue. No assertion was weakened to hide a guard failure.

Documentation impact: Pico `README.md`, `docs/configure.md`, `docs/troubleshoot.md`, `engineering/browser-api.md` and the materially inaccurate `station-draft.jpg` were updated. The required order is station details, valid-field save, explicit station application and confirmation, dial save/readback, then separate schedule enablement. Pi operator `Install/pico.md` and transmitter/member-resource instructions were reviewed and remain accurate; no Pi operator-documentation edit or empty commit is required. The strict Sphinx build, local-target checker and image-delivery test passed, and affected rendered pages were inspected at desktop/mobile sizes. No further documentation is required for this correction.

```sh
docs/.venv/bin/python -m sphinx -E -a -n -W --keep-going -b html docs docs/_build/html
docs/.venv/bin/python scripts/check_documentation.py
docs/.venv/bin/python tests/test_image_delivery.py
```

Publication retains the coordinated feature-branch Pico selection and explicit `pico_ref` override precedence. Pico firmware publication restrictions remain unchanged; the feature branch has no applicable automatic Pico publication workflow. No PR, tag, release, hardware deployment, physical USB, service, GPIO or RF operation is part of this correction. No wspr5 or other development-host connection was used. At this corrective follow-up closeout, Stages 4 and 5 were unstarted; its first-use regression remains in Stage 5 acceptance.

## Stage 3 Pico CW references and shift direction

### Status

Stage 3 implementation, hardware-free validation, operator/API documentation and adversarial corrective review are complete. QRSS enters carrier, FSKCW enters space and DFCW enters dot. The two-frequency browser modes add a positive shift. For B = 14096900 Hz and s = 5 Hz, QRSS carrier is 14096900 Hz; FSKCW space is 14096900 Hz and mark is 14096905 Hz; DFCW dot is 14096900 Hz and dash is 14096905 Hz. Exact B and B+s values are 14096900000000000 and 14096905000000000 nHz. The unchanged explicit API and encoder retain either distinct positive DFCW orientation. At Stage 3 closeout, Stages 4 and 5 were unstarted; submitted-commit CI is assessed separately from this local evidence.

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

### Status

Implementation, local hardware-free validation, documentation, three adversarial assessment rounds and feature-branch publication are complete. Pi implementation commit `985bad10c49dd67f5a6d67e4b2ec009819c4140c` and separate operator-documentation commit `1e201c1354721043831222aef5cbd28a3ba9d690` were published on their respective `origin/codex/shared-frequency-contract` branches. At that closeout, the feature checkout and operator-documentation working trees were clean and Pico was unchanged at `d48be0fd4aab303838877db4e82906413b933753`. The original Pi checkout remains unchanged at `436445862680c40de27c85ea2a3768b8ee01d2c5`; Stage 5 below records the subsequent integrated closeout.

Phase 4 submitted-tip CI passed for `7dd1dc58f04a422d674175a1032332234346dc9f` in [Non-hardware validation run 40107024784](https://github.com/WsprryPi/WsprryPi/actions/runs/40107024784). Its six jobs cover six-profile GCC 13 release builds, strict I2C, macOS portable simulation, Linux semantics/UI/hardware-access audits, current-source network interoperability with the edited-source proof, and the actual USB producer/client/Pico endpoint. This closes the previously pending Phase 4 publication gate. The unchanged source-selection rules retain explicit `pico_ref` precedence, select the coordinated Pico feature branch for ordinary feature pushes and retain `devel` elsewhere. The separate documentation branch has no applicable automatic CI workflow.

The scheduler snapshots nominal center and applied random placement with the prepared plan, copies them into candidates and publishes them only after backend preparation accepts the slot request. Both encoded frames retain one grid. Re-entry retains the pair's dial and frequency entry, including before first-frame completion. Central reset clears placement with the plan, including completion, cancellation, skipped entries, mode changes, Test Tone abandonment and process shutdown. Existing deferred reload and inhibited-reload recovery policies are preserved. Each request remains one executable frame. Calibration capture retains its pre-prepare boundary and refreshes independently for the next frame.

`make wspr-plan-placement-test SUDO=` passed 13,488 checks using production scheduler callbacks, committed legacy/controller requests, the production compiler and WTP converter. Dial `14095600 Hz` with `+37 Hz` produces center `14097137 Hz` for both frames; the next independent `-21 Hz` draw produces center `14097079 Hz`. Independent exact binary64 and integer-nHz tone constants verify every symbol and event timeline. The pair takes one draw, the next plan takes one draw, status and continuation take none, disabled placement takes none, zero is valid and a new plan may repeat the previous value. Single/pair transitions, rotation, skips, cancellation, reload, superseded candidates, planning and both-frame backend preparation failures, retry, Test Tone, shutdown, nested/unwound/concurrent overrides and production-provider restoration are covered. GPIO correction changes from `+2.5` to `-3.25 PPM` between frames without changing nominal `+37 Hz` placement. An isolated per-frame-redraw mutation failed on the first repeated preparation with `committed placement: -21.000000`.

Linux validation used task-owned `/work/phase4-pi` in the retained Ubuntu 24.04 ARM64 container, GCC 13.3.0 and libgpiod C++ 1.6.3. Its baseline is Pi `abb477173d16d553d010b0099dfab62a153d62f0` plus the reviewed Phase 4 diff; source hashes matched the Mac feature checkout. No local macOS C++ compilation was attempted. The following commands passed from Linux `src` with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`:

```sh
make wspr-plan-placement-test SUDO=
make wspr-tone-regression-test wtp-plan-test SUDO=
make test-tone-frequency-plan-test test-tone-response-test test-tone-lifecycle-test legacy-tone-frequency-test SUDO=
make qrss-execution-regression-test non-wspr-repeat-policy-test wtp-fleet-test SUDO=
make wtp-scheduler-test wtp-backend-test wtp-status-test SUDO=
make guarded-mode-change-persistence-test band-gpio-default-convergence-test band-gpio-rotation-test selector-shutdown-cleanup-test semantics-make-regression-test SUDO=
strace -f -e trace=file -o /work/phase4-semantics.strace make semantics-test SUDO=
../scripts/ci/assert-no-transmitter-hardware.sh /work/phase4-semantics.strace
make release debug SUDO=
../scripts/ci/verify-simulated-wspr.sh
../scripts/ci/assert-no-transmitter-hardware.sh /tmp/wsprrypi-wspr-simulator.strace
```

Local make invocations used `-j4`. Full semantics, cleanup and both hardware-access audits passed. The full default release/debug profile passed with warnings as errors. Explicit simulated application integration produced two identical 162-event traces with `110.591999892` logical seconds, without waiting through real slots. Expected device-tree absence, injected failures and incomplete legacy-fixture INI default warnings remain visible; no required case was skipped. Initial fixture, dependency and scratch-repository naming failures were corrected and rerun. The band-GPIO convergence fixture now selects its existing Pi 4 test identity explicitly and reads the stock INI relative to `src`.

UI `npm run test:unit` and `npm run test:browser` passed in the retained Debian ARM64 Chromium/PHP container using locked Node dependencies. The random-offset tooltip/help states the plan lifetime; layout and controls are unchanged. Impeccable review inspected the changed help in light/dark themes at 1440×1000 and 390×844, plus existing browser coverage of five pages, four modes, three widths and 200% text. No page overflow or broken images was observed. The existing design sidecar drift was left untouched. The documentation detector used its declared regex fallback because optional HTML parser modules were absent; this is limited scan evidence, separate from rendered review.

Operator documentation in `WsprryPi-Docs` updates WSPR setup, CLI options, INI guidance/default comments and canonical-band arithmetic. Its required virtualenv Sphinx command passed without warnings; four affected pages were inspected at desktop/mobile widths. Existing screenshots were considered and retained because they remain materially accurate. `docs/wtp-scheduling.md` documents the runtime lifetime and exact focused command. No migration or Phase 5 guidance was added.

Assessment 1 closed request publication before backend acceptance, overlapping override lifetimes, first-frame re-entry rotation and failed continuation status identity. Assessment 2 preserved the original calibration-capture boundary and added GPIO correction/placement coverage; it also tightened continuation rollback so a later launch failure cannot relabel an already committed second frame. The complete third assessment reviewed those repairs, state copy/commit/reset paths, tests, numerical constants, absolute WTP conversion, operator help and publication scope and found no unresolved actionable Phase 4 issue. The original Pi checkout and Pico source remain unchanged; no physical device, RF, firmware, installation, target configuration or service operation occurred.

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

### Status

Integrated software, browser, firmware-build and documentation validation and corrective assessment are complete. Initial inspected inputs were Pi `7dd1dc58f04a422d674175a1032332234346dc9f`, Pico `d48be0fd4aab303838877db4e82906413b933753` and operator documentation `1e201c1354721043831222aef5cbd28a3ba9d690`. Reviewed Phase 5 repairs are published as Pi `2ca4c273c3734345473327805754f8d696434745`, Pico `dac9a76e8d5a7bdb22f8db9fe9e0d0e5c63e2350` with documentation-only follow-up `44c4ab4c6bb6da245151d9e749b1a9918122442a`, and operator documentation `cbb4caf224a7d4e3337f5f554d9757886b151897`. These identities document tested sources, not local checkout admission requirements.

All six jobs passed for the reviewed Pi candidate `2ca4c273c3734345473327805754f8d696434745` in [Non-hardware validation run 40112022046](https://github.com/WsprryPi/WsprryPi/actions/runs/40112022046): `ubuntu-gcc13-release`, `strict-i2c-profile`, `non-hardware-validation`, `macos-simulated-profile`, `wtp-network-loopback` and `wtp-usb-management-pair`. Completed network and USB logs both record selected Pico `dac9a76e8d5a7bdb22f8db9fe9e0d0e5c63e2350`; the network log includes the five scheduler-produced receiving-handler assertions and edited-source proof, and all six warning-as-error release profiles executed. The jobs API temporarily retained stale step summaries after success, so completed logs were also inspected.

The final roadmap-only submission must receive the same six executed, passing jobs on its exact submitted tip before overall closeout is reported. The completion report records that commit, run and actual network/USB companion selections; no extra commit is made solely to record its own hash. Coordinated feature-branch selection and explicit `pico_ref` precedence are unchanged. Pico's publication workflow does not apply automatically to this feature branch, and the operator-documentation branch has no applicable automatic CI workflow; their documented local validation provides the evidence. No release metadata, firmware publication or documentation deployment is used to obtain acceptance.

### Integrated acceptance matrix

| Input path | Input meaning and transformation | Persistence and production path | Fresh validation |
| --- | --- | --- | --- |
| Pi WSPR presets, profiles, preferences and numeric forms | USB dial D; C = D + 1500 + r; four tones symmetric about C | Existing INI/JSON values retained; parser → band resolver → scheduler → compiler | Full Linux semantics, catalog/correlation tests, numeric round trips, selector suffixes, rotation and zero skips |
| Pi independent plans and paired slots | One draw per randomized plan; +37 gives center 14097137 for both frames; next −21 gives 14097079 | Candidate copy/commit/reset; one executable frame per slot, overall pair status | 13,489 placement assertions, failure/reload/cancel/skip tests, actual receiving-handler integration |
| Pico standalone WSPR | Whole-Hz USB dial; same exact grid with r = 0 | `wspr_dial_frequency_hz`; scheduler snapshots the four integer-nHz targets across the pair | Standalone/schema/policy/Console/journal tests; actual standalone producer compared with Pi's nonrandom grid |
| Pi band and direct Test Tone | Standard 20m → one carrier at 14097100; numeric 14095600 → exactly 14095600; no random draw | Transient CLI/web requests; effective profile/preferences; preview distinct from committed status | Request/frequency/selector/response/lifecycle/legacy-tone tests; shipped UI preview, acceptance, refusal and recovery |
| Fleet WSPR | `frequency_hz` is already absolute RF center | Assignment persistence → Fleet scheduler → compiler → WTP | Fleet tests and rendered mode-specific assignment controls; no dial conversion or placement |
| WTP and receiving APIs | Events are absolute RF targets | Compiler → WTP serializer → Pico jobs handler/decoder/JobService; independent standalone dial and correction preserved | Five real scheduler slots, all 162 received events each, TLS jobs and USB producer/client/endpoint; injected repeated +1500 conversion fails the independent oracle |
| QRSS, FSKCW and DFCW | Carrier, space or dot B; browser mark/dash B + shift; explicit DFCW API retains either positive orientation | Direct decimal/nHz inputs → message compiler or raw job; no WSPR conversion | Pi execution/repeat regressions; consumer and engineering shipped-browser requests and compiled events; equal DFCW marks, internal/repeat gaps and final RF-off tails |
| First-use frequency selection | Readiness requires authoritative applied station, current target/boot/revision | Station/draft/progress readback → dedicated PUT → atomic journal; first selection disabled | All five profile/refusal variants plus configured workflow; direct 409 guards, keyboard save, reboot/reload, overlays, races, password ETag independence, ambiguous outcomes and storage faults |
| Schemas and independent ownership | Dedicated `frequency_hz` is whole-Hz dial; application views 1–4 use exact `schedule_dial_frequency_nhz` strings | Generic resources redact/preserve dial; dedicated null and recurrence "0" remain intentional | Strict Pi consumer, old/dual-key rejection, PUT/merge/replace/partial preservation, identity/revision/STOP and fault coverage |
| Backend realization and calibration | Configured reference, nominal target, backend realization and measured RF remain distinct | Existing GPIO/RP1/Si5351/remote/simulated ownership and correction signs | GPIO clock/correction/provenance, RP1 planner/backend, Si5351 planner/transition/realization and simulator/controller contracts; no physical RF claim |

For dial `14095600 Hz` with random placement disabled, both products target center `14097100 Hz` and tones `14097097.802734375`, `14097099.267578125`, `14097100.732421875`, `14097102.197265625 Hz`. Independent rational arithmetic gives exact nHz `14097097802734375`, `14097099267578125`, `14097100732421875`, `14097102197265625`. Standalone dial `3568600` and Fleet center `3570100` describe the same nominal placement. CW reference `14096900` plus shift `5` gives `14096905 Hz`. Pi's existing truncated per-symbol timing and Pico's rational cumulative timing remain intentional producer differences; the cross-product assertion compares frequency grids.

### Linux and browser evidence

The task reused the retained `stage2-pi` Ubuntu 24.04 ARM64/GCC 13.3.0/libgpiod C++ 1.6.3 environment, `stage2-frequency` Debian 13 ARM64/GCC 14.2.0/Node 20.19.2 environment and `stage2-firmware` Ubuntu 24.04 x86 Linux cross-build environment. New directories are `/work/phase5-pi`, `/work/phase5-source`, `/work/phase5-host`, `/work/phase5-usb-host` and `/work/phase5-firmware`. Earlier outputs and fixed-path simulator traces were preserved before replacement. The selected Pico checkout is mounted read-only at `/pico`; writable source copies support tests and SDK timestamp operations. MbedTLS remains independently pinned to `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Firmware uses SDK 2.3.1 `079c6f39023649b154152db30f1d781e884879bc`, Arm GNU 15.3.1 and picotool 2.3.0 `6f6458d792b93685a11423b244a585eaa99eafcf`. All 867 selected Pico file hashes matched in both host and firmware environments; the Pi source comparison covered all 1,079 selected files before publication.

These commands ran from Linux `/work/phase5-pi/src`, with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`. All required commands passed; the broad full semantics profile was not replaced by the portable subset:

```sh
make -j4 wspr-plan-placement-test SUDO=
make -j4 test-tone-request-test test-tone-frequency-plan-test test-tone-selector-plan-test test-tone-response-test test-tone-lifecycle-test legacy-tone-frequency-test SUDO=
make -j4 band-lookup-correlation-test wspr-band-catalog-test wspr-preset-catalog-response-test SUDO=
make -j4 wspr-tone-regression-test qrss-execution-regression-test non-wspr-repeat-policy-test SUDO=
make -j4 wtp-protocol-test wtp-plan-test wtp-scheduler-test wtp-backend-test wtp-status-test wtp-application-test wtp-fleet-test wtp-config-test wtp-api-test wtp-usb-test wtp-usb-management-test SUDO=
make -j4 guarded-mode-change-persistence-test band-gpio-default-convergence-test band-gpio-rotation-test selector-shutdown-cleanup-test semantics-make-regression-test SUDO=
strace -f -e trace=file -o /work/phase5-semantics.strace make -j4 semantics-test SUDO=
../scripts/ci/assert-no-transmitter-hardware.sh /work/phase5-semantics.strace
make -j4 release debug SUDO= EXE_NAME=wsprrypi
../scripts/ci/verify-simulated-wspr.sh
../scripts/ci/assert-no-transmitter-hardware.sh /tmp/wsprrypi-wspr-simulator.strace
make -j4 wtp-config-interop-test wtp-network-interop-test SUDO= PICO_SOURCE=/pico MBEDTLS_SOURCE=/work/mbedtls
make -j4 wtp-usb-pair-client-build SUDO=
make -j4 rp1-gpclk-planner-test rp1-gpclk-backend-test rp1-gpclk-transmit-backend-test wtp-pi-si5351-realization-test SUDO=
make -C WSPR-Transmitter/src -j4 si5351-planner-test si5351-transition-test simulated-backend-test simulated-backend-realtime-test transmission-controller-contract-test startup-quiesce-test SUDO=
```

The isolated Git bundle origin gives the scratch checkout a different executable name; the existing `EXE_NAME=wsprrypi` override supplies the simulator script's canonical path. Default release/debug compilation also passed before the canonical-name relink. Full semantics, RP1/backend, component and final scheduler/receiver executions had separate nonempty `strace` file-access traces; the existing no-transmitter-hardware checker passed each. The simulator's own trace passed separately and produced two identical 162-event WSPR traces with `110.591999892` logical seconds. The exact-rational binary64 conversion oracle passed 1,937 vectors. Network tests exercised actual TLS, a changed second loopback IPv4 address, the 60-second scheduled wait, identity/reconnect and lost LOAD/ARM/ABORT handling. Those network/USB runs are integration evidence, not an assertion that every command was traced.

The actual USB endpoint was built with `cmake -S /pico -B /work/phase5-usb-host -DCMAKE_BUILD_TYPE=Debug -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON` and `cmake --build /work/phase5-usb-host --target usb_management_driver --parallel 4`. From the isolated Pi root, `node WsprryPi-UI/tests/wtp_usb_settings_test.js`, `node WsprryPi-UI/tests/wtp_usb_pair_test.js src/build/wtp-network/wtp_usb_pair_client /work/phase5-usb-host/usb_management_driver` and `python3 -B scripts/tests/pico_source_selection_test.py /pico /work/mbedtls` passed. The source-selection proof executes an edited production Pico endpoint in client and network fixtures, then requires missing-source and real compiler-error cases to fail. No clean or committed Pico source admission gate was added.

The Pico host commands below ran from `/work/phase5-source`; CTest and browser integrations ran as ordinary user `1000:1000` with retained Node/Python/schema/NumPy and Harness dependencies. Registration was derived from `ctest -N --show-only=json-v1`: 14 focused checks and all 163 registered tests passed, with TLS/consumer, offline-analysis, TinyUSB, pinned BTstack and both current-source Pi companion suites present and no internal or CTest-level skips. The WTP validator passed 23 schema, seven raw-JSON, one framing and eight transition cases; the decode regression passed seven tests.

```sh
cmake -S . -B /work/phase5-host -DCMAKE_BUILD_TYPE=Debug -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DWSPRRY_PICO_TEST_MBEDTLS_PATH=/sdk-input/lib/mbedtls -DWSPRRY_PICO_HARNESS_PYTHON=/usr/bin/python3 -DWSPRRY_PICO_WSPRRYPI_SOURCE=/pi-input -DWSPRRY_PICO_NETWORK_CLIENT_SOURCE=/pi-input -DWSPRRY_PICO_TEST_TINYUSB_PATH=/work/phase5-sdk-host-deps/tinyusb -DWSPRRY_PICO_BTSTACK_TEST_ROOT=/work/phase5-sdk-host-deps/btstack
cmake --build /work/phase5-host --parallel 4
PYTHONPATH=/work/stage3-harness-src NODE_OPTIONS=--experimental-websocket ctest --test-dir /work/phase5-host --output-on-failure --timeout 240
python3 -B scripts/validate_wtp_contract.py
python3 -B tests/wspr_type23_decode_tests.py
export CHROME_BIN=/work/chrome NODE_OPTIONS=--experimental-websocket
node tests/standalone_frequency_ui_integration.mjs /work/phase5-host/consumer_bootstrap_driver /work/phase5-evidence/configured
for mode in network usb overlay network-backing refusal; do
  node tests/standalone_frequency_ui_integration.mjs /work/phase5-host/consumer_bootstrap_driver /work/phase5-evidence/$mode first-use:$mode
done
node tests/message_frequency_ui_integration.mjs /work/phase5-host/consumer_bootstrap_driver /work/phase5-evidence/cw
```

Normal and inhibited firmware built with the pinned workflow below. Generated-asset integrity, normal/inhibited engine identity, BOOTSEL topology, flash/journal separation, primary/RF-worker stacks, heap/allocator/pool hooks and applicable linked RF-worker image checks passed. Both UF2 application payloads matched their respective linked ELFs; neither was flashed. These validation images were compiled from the precommit edited sources whose 867 file hashes match published Pico `dac9a76e8d5a7bdb22f8db9fe9e0d0e5c63e2350`; their build identity reflects that precommit worktree, not a released firmware artifact. Final Pico `44c4ab4c6bb6da245151d9e749b1a9918122442a` changes only the sanitizer example's companion path in `engineering/development/network-control.md`; all compiled sources and generated assets are identical.

```sh
cmake -S /work/phase5-source -B /work/phase5-firmware -G Ninja -DCMAKE_BUILD_TYPE=Release -DWSPRRY_PICO_BUILD_FIRMWARE=ON -DWSPRRY_PICO_BUILD_TESTS=OFF -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=/sdk -DPICO_TOOLCHAIN_PATH=/work/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi -DPICOTOOL_FETCH_FROM_GIT_PATH=/work/phase5-firmware/_deps -DPICOTOOL_FORCE_FETCH_FROM_GIT=ON -DPICOTOOL_GIT_REPOSITORY_URL=/picotool/picotool-src
PATH=/work/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi/bin:$PATH cmake --build /work/phase5-firmware --target WsprryPico WsprryPico-Inhibited --parallel 4
```

Pi UI `npm run test:unit` and `npm run test:browser` passed in the separate disposable Linux UI copy. The 18 Test Tone workflows and whole-console checks cover five pages, four modes, both themes, 1440/1024/390-pixel layouts, 200% text, visible keyboard focus, accessible descriptions and computed button/state contrast. Fleet assignment and member-resource browser checks passed. The member views use fresh responses emitted through actual Pi/Pico adapters; unset rendering retains the verified "0" sentinel. A targeted confirmation rendered a production-generated WRC-15 catalog with named/custom preferences and the CW frequency controls in both themes at desktop/mobile sizes. Pico's shipped generated assets exercised configured and first-use station states, refusal, revision/identity changes, ambiguous writes, reconnect, journal observations, explicit DFCW orientations and raw absolute jobs. Pico remains intentionally light-only.

Impeccable visual review inspected the retained screenshots and computed behavior. The application detector ran with its declared regex fallback because optional HTML parser modules were absent; it does not provide computed contrast or a complete accessibility audit. Its 12 advisories concern unchanged runtime colors and Pico's incumbent literals compared with the Pi design palette; the pre-existing design sidecar drift was preserved; no actionable frequency-workflow defect remained. The final changed-document scan reported no findings. No tooling sidecars or runtime artifacts were added to maintained sources.

Expected device-tree absence, injected failures, incomplete legacy-fixture INI defaults and recursive-make jobserver notices remain visible. Picotool reports its intentional no-USB build notice; the final host, firmware and documentation logs contain no compiler or Sphinx warnings. Initial disposable bundle-branch, ordinary-user evidence-path, missing adapter archive, C++ standard, paged HTTP response, standalone preparation-window, JSON comparison, executable-name and mismatched BTstack input setup failures were corrected; their successful final reruns provide acceptance. No required failing or skipped case is counted as passing.

### Corrective assessment and documentation impact

The first integrated assessment identified a missing continuous scheduler/compiler/WTP/receiving-handler assertion and misleading FSKCW continuous-output prose. The regression now checks fresh accepted job identities, every event and independent grids, while an intentional extra +1500 Hz mutation fails. Review of that repair caught the host-only build dependency problem; the exporter now uses a separate explicit simulated binary, preserving the ordinary placement-test executable and the interoperability target's host-only dependency contract. Operator prose now distinguishes internal space gaps, RF-off repeat gaps and the final tail, and the Test Tone caption explains acceptance versus active runtime directly. The conditional-suite audit additionally exposed historical Pi commit/clean-checkout gates in Pico's optional actual-client fixtures. Those local admission gates were removed without changing release dependency provenance or the separate MbedTLS policy. All 163 current-source declared host tests are registered, including the edited-companion proof, TinyUSB and the correctly pinned SDK 2.3.1 BTstack input. A final documentation search corrected the remaining historical sanitizer example. The fresh complete reassessment after those repairs covers the original devel-derived feature changes, source identities, every input meaning, calibration ownership, pair lifetime, first-use readiness, persistence, schemas, generated assets, tests/skips, documentation, candidate CI and publication scope, and finds no unresolved actionable Phase 5 software issue. Final submitted-tip CI remains an explicit publication gate, not assumed passing evidence.

Documentation updated: this canonical roadmap, `docs/wtp-scheduling.md`, and Pi operator `User_Interface/Setup/Signal_Setup/cw.md` and `User_Interface/Maintenance/index.md`. Pico consumer/API material, the remaining engineering contracts and Pi WSPR, preset/profile, CLI/INI, Operations, Fleet/member and calibration guidance were reviewed and remain accurate. Pico engineering `network-control.md`, both optional companion CMake fixtures and their source verifier were updated; the new `companion_source_selection_tests.py` compiles and executes an edited Pi client without Git metadata and rejects missing-source and genuine compiler-error cases. A final semantic search caught the sanitizer example's remaining historical clean-checkout path; the corrected example selects current sources, and the documentation checker passed again. Pico consumer prose and generated UI assets require no change. Existing screenshots were considered and retained because their controls remain materially accurate. No operator documentation follow-up is required after the reviewed corrections.

The Pi operator command `source docs/.venv/bin/activate` then `python -m sphinx -b html docs docs/_build/html` passed without warnings. The affected 11 Pi HTML pages passed 1,046 local link/image/anchor checks. Pico `docs/.venv/bin/python -m sphinx -E -a -n -W --keep-going -b html docs docs/_build/html`, `docs/.venv/bin/python scripts/check_documentation.py` and `docs/.venv/bin/python tests/test_image_delivery.py` passed: 75 Markdown sources, 16 HTML pages and 1,290 local targets, zero errors. Sixteen affected Pi/Pico pages were inspected at desktop/mobile sizes with all images loading and no page overflow; the corrected CW text and Test Tone caption received focused confirmation. No documentation was deployed.

Temporary source manifests, exact command scripts, complete logs, mutation evidence and inspected screenshots are retained outside tracked source at `/private/tmp/phase5-validation`, with large traces and builds under the named Linux task directories. These establish software, integration, browser, documentation and firmware-build evidence only. No local macOS C++ build, physical device programming, RF/GPIO operation, Pi host connection, installation, target configuration, service operation, merge, PR, tag or release occurred. Physical RF accuracy, timing, installation, services and all-band qualification are separate and unclaimed.

### Work

- Run the relevant Pi and Pico host suites and current-contract interoperability tests against the actual selected sources, including uncommitted changes. Report source identities without requiring commits or pin updates.
- Verify the independent WSPR arithmetic across ordinary Pi scheduling, Pico standalone and absolute WTP events. Check backend realization separately from requested targets and measured RF.
- Run Pi UI unit and browser coverage, Pico browser and asset checks, and Impeccable review of affected desktop and mobile workflows. Preserve drafts and distinguish preview, saved, pending and committed states.
- Review Pi operator documentation and Pico consumer documentation for consistent current terminology and examples. Keep engineering contracts under the engineering layout, repair affected links and build the documentation using each repository's documented workflow.
- Run the broad applicable Linux semantics suite after focused checks pass. Record failures, skips and environment limitations without treating host or simulated results as hardware or RF qualification.

### Acceptance

- Every supported input path has one documented meaning, and tests establish the resulting request and execution-plan frequencies without omitted or repeated conversion.
- Include the Stage 2 first-use frequency-save correction: missing applied station and draft-only states cannot issue an unusable UI save; explicit station application permits an unset dial to be selected; direct invalid PUT remains guarded; successful persistence survives simulated reboot with schedules disabled. Retain draft/error recovery, identity/revision/storage protections, consumer-profile overlay coverage and the Stage 3 CW regressions through actual shipped assets and production handlers.
- No prerelease migration layer, historical Pico checkout gate, obsolete document-path fallback or CLI/UI band exception remains in the affected scope.
- Review the complete diffs, generated assets, documentation output and working-tree state. Report tests, visual evidence, documentation impact and remaining validation separately for each repository.
- Hardware, RF, installation and service qualification require an explicitly authorized finite campaign; they are not established by these software checks.

## Coordinated USB logging

USB logging implementation and acceptance are tracked by the [WsprryPico combined backlog](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/development/consolidated-backlog.md#combined-ui--backend) and its maintained USB contract. Pi deliverables include bounded dedicated Logging collection, origin-preserving journald projection, source-scoped Logs filtering and cursor state, maintenance exclusion, settings controls and operator documentation. [UDP logging](udp-logging.md) adds independently configured sending, USB forwarding and collection; its qualification and coordinated publication are tracked under the same canonical backlog.
