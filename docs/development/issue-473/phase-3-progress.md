# Issue 473 phase 3: shared Setup context

Status: ✅ complete for the approved local-Pi/existing-Pico scope. The user authorized the saved phase-3 prompt after the amended phase-2 gate passed. Do not begin phase 4, stage, commit, push, operate hardware, or change deployed services. Preserve the unrelated SQLite prompt.

## Sources and boundaries

Pi starts at `b435134d152d9d0652683f4f230b2cd4f6895577` on `codex/issue-473-device-owned-scheduling`. The committed Pico companion is `ef5b80924bee9d3d483d8f2fcd0c7f52a3538ea8` in `/private/tmp/issue473-pico-candidate`, clean on its separate feature branch. Docs starts at `9d41898acf3aa62f9c79665ef6fd0dbc30184127`, clean on `codex/issue-473-device-owned-scheduling`. The ordinary Pico checkout remains untouched. Phase-2 production network/USB and source/evidence records have been inspected; any new evidence will identify the current source pair explicitly.

The approved deferred-security milestone remains mandatory at the end of phase 6, before phase 7. New controller-to-remote-Pi management remains unavailable. Existing Pico authentication, WTP, and all protocol versions are preserved. No reusable C++ component or companion change is currently required.

## Implementation direction

Use Impeccable and the existing bench-instrument design. One labeled Device selector sits beside the Setup tabs, outside the tablist and local form; it wraps to its own row on mobile. Keep Signal Setup, Transmitter, Device I/O, and Fleet. Do not add the phase-7 Scheduling tab.

Reuse phase-2 Pico editor factories, transports, scoped documents, DOM-held per-target drafts, revision rules, operation receipts, and lost-reply recovery. Move their template out of Fleet and organize its current controls into shared panes. Signal Setup owns station and existing standalone forms. Transmitter owns independent GPIO drive, amplifier timing, filters, and Si5351 controls. Device I/O owns USB/Wi-Fi, logging, connection controls, and the complete RF-engine/pin/GPS plan, whose validation/save must remain atomic. Preserve existing save/apply and restart semantics, including existing Pico scheduling controls; implement no new solar policy, recurrence, or shared scheduling master.

The local form stays separate and is hidden/inert during Pico editing. Its legacy autosave is scoped to the owning local Pi. A pending unsaved local draft is retained when switching away, and returning requires explicit Save retained local draft before autosave can resume. Already dispatched operations remain bound to their original target. Controller-only mode has no local settings. Controller time, power, output connection, firmware, and explicit backend-selection actions remain clearly controller-scoped; target selection cannot invoke them. Nearby/unverified choices lead to Fleet identification/profile actions and grant no write permission. Unsupported native remote-Pi operations remain unavailable.

## Adversarial review and corrections

The review covers the saved phase prompt, approved phase-1 decisions, source diff, production adapters, test assertions, rendered states, and operator guides. Findings are fixes, not waivers:

- Relocating the template exposed an old source assertion; it now checks the moved partial. A JavaScript guard inserted into a default parameter was repaired.
- A firmware-safety notification created observer starvation; notifications now occur only on safety-lock transitions, retaining the original enforcement.
- Mobile tabs clipped their first label; the narrow layout now shows four tabs in two rows, with Device on its own labeled row.
- Reopening a target with an unfinished request could leave the previous target's editor visible. The old editor is retired/detached before displaying the pending target. The two-service browser test asserts this explicitly.
- A delayed initial host read could make unpopulated defaults appear to be a local draft. Local editing now requires completed configuration population. The production-main browser holds that first response, selects a Pico, and checks there is no manufactured draft or write before releasing the response.
- The resumed local view retained a paused hint, and engineering authentication was described as consumer authentication. Each hint now follows the current save/connection state and has a production browser assertion.
- Retained local drafts had an automatic-save hint; that state now explicitly says autosave is paused. Host reads cannot replace the held draft/baseline.
- Repeated Pico introduction text pushed actions down on mobile. The duplicate title/intro were removed, and shared saved/active observations use a disclosure.
- The remote panes lacked Bootstrap's tab-content parent: invisible panes still occupied layout space. They now share that parent. The browser checks exactly one visible pane for each shared tab, in addition to field placement.
- The controller-output inert wrapper initially enclosed firmware recovery, which could trap Read/Recover during a maintenance hold. Firmware remains outside that wrapper, explicitly labeled as the controller output, under its existing safety guard. A browser composition assertion preserves this recovery path.
- Isolated PPS and CW presentation fixtures supply their own fields rather than loading host configuration. They now declare that supplied configuration ready; the production-main test independently verifies the real loading boundary.
- A proposed reboot assertion was stricter than approved D10: drafts belong to stable device ID/profile generation. The actual reboot test instead requires old-context refusal, unchanged saved settings, a fresh connection/readback, retained same-profile entries, and a deliberate reviewed save. It does not silently discard drafts or permit old-binding replay.

No schema, capability advertisement, WTP, TLS library, component public API, firmware feature, or production C++ behavior has changed. Parent C++ additions are isolated test fixtures/targets, including a dry-run armed-job observation. A volatile Pico software-peer restart checks identity fencing, not physical reboot persistence. GPIO/RF, installations, and deployed services are untouched.

## Acceptance evidence

All checks below passed against the intended sources. No test failure, timeout, or required integration skip is counted as a pass. Earlier fixture failures were corrected and rerun. Final UI/test/fixture hashes were recorded before acceptance and verified unchanged afterward in `/private/tmp/issue473-phase3-final-ui-source.json`. The Linux frozen source manifest is `/private/tmp/issue473-linux-pair/phase3-final-source-manifest.json`; all final C++/Makefile/build-script bytes match that tested snapshot. Final presentation and browser-fixture changes after that snapshot were tested by the final native UI unit/browser and production-pair runs; no compiled production source changed.

| Requirement | Result and evidence |
| --- | --- |
| Shared context and accessible tabs | PASS: selector outside form/tablist; four tabs only; correct tab/panel bindings; only the active pane occupies layout space; selector and keyboard focus retained. |
| Selection without output changes | PASS: actual two-Pico controller and actual native Pi; no backend selection, host save, STOP, or ownership/job mutation merely from selection/read. |
| Local/Pico isolation | PASS: held initial host response; no fabricated default draft; retained local and Pico entries; delayed host read cannot replace local draft; explicit local resume saves once to its owner; Pico save leaves host/B unchanged. |
| Two-Pico isolation and accepted writes | PASS: distinct A/B drafts and saved values; accepted A reply held while B remains editable; pending A cannot display B's editor; old-target result stays in its ledger. |
| Uncertainty and profile changes | PASS: actual storage failure, fresh-browser durable hold, authoritative readback and explicit review; profile ABA fencing; retired profiles/read-only inspection; passwords excluded. |
| Reboot and active work | PASS: actual dry-run Pico job armed with fixed owner/job ID; selection/read preserve state and engine counters; volatile peer restart changes its real boot ID; old context refuses; fresh connection/readback and deliberate review precede saving the preserved same-profile draft. |
| Existing control semantics | PASS: factories and native payloads preserved; resource-specific validation/save/apply/restart remain separate; complete RF/pin/GPS plan remains atomic; unsupported Pi CW mappings have no remote write path. |
| Controller and recovery boundaries | PASS: actual controller-only Setup/Operations; actual protected startup recovery, rejected drafts and corrupt-history read-only state; firmware Read/Recover remains outside the inert controller-connection group during a hold. |
| Visual/Impeccable review | PASS: desktop/mobile, light/dark, long names, loading/disabled/error/recovery/retained drafts, visible pane content, keyboard/focus, and tab labels. Existing UI regression suite also covers 200% text, contrast, and additional viewport widths. |
| Docs | PASS: updated guides/images/navigation; fresh Sphinx build with zero warnings; all local file/fragment links in the four Setup guides resolve; desktop/mobile renders preserve RTD navigation and fit the viewport. |

### Exact validation commands and results

From `WsprryPi-UI`:

```sh
npm run test:unit
npm run test:browser
WSPRRYPI_SHARED_SETUP_MAIN_FIXTURE=/private/tmp/issue473-controller-native/bin/shared_setup_main_fixture \
  node tests/pico_controller_http_pair_test.js /private/tmp/issue473-pair-network \
  /private/tmp/issue473-controller-native/bin/pico_controller_pair_fixture --browser --shared-browser
WSPRRYPI_CONTROLLER_FIXTURE=/private/tmp/issue473-controller-native/bin/controller_only_fixture \
  node tests/controller_mode_browser_test.js
WSPRRYPI_MANAGED_MAIN_FIXTURE=/private/tmp/issue473-controller-native/bin/pi_managed_main_fixture \
  node tests/configuration_recovery_browser_test.js
node tests/pico_target_editor_state_browser_test.js
```

All PASS. Final logs: `/private/tmp/issue473-phase3-ui-unit-final.log`, `/private/tmp/issue473-phase3-browser-regressions-final.log`, `/private/tmp/issue473-phase3-accepted-browser.log`, `/private/tmp/issue473-phase3-controller-final.log`, `/private/tmp/issue473-phase3-recovery-browser.log`, and `/private/tmp/issue473-phase3-editor-state-final.log`. The injected consumer lifecycle test is supplemental presentation evidence; the production two-Pico/native-Pi checks use actual HTTP, CSRF, OpenSSL/mTLS, Pico handlers, serialization, contexts, and journals.

The native shared fixture was built from `src` with:

```sh
make -j4 /private/tmp/issue473-controller-native/bin/shared_setup_main_fixture \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=/private/tmp/issue473-controller-native/obj \
  BIN_DIR=/private/tmp/issue473-controller-native/bin
cmake --build /private/tmp/issue473-pair-network --target pico_tls_server -j4
```

The network CMake build explicitly selects the clean Pico candidate above and Mbed TLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` from `/Users/lbussy/GitHub/WsprryPico/build/rc2-item25/mbedtls`.

The frozen Debian runner `/private/tmp/issue473-linux-pair/run_phase3_final.py` ran in `wsprrypi-rc3-validation:local`, as UID/GID 501:20, without network, capabilities, privilege escalation, or host hardware devices. Source/dependency mounts were read-only. Logs are `/private/tmp/issue473-linux-pair/phase3-final-*.log`; the overall log is `/private/tmp/issue473-phase3-linux.log`. Each command passed:

- `python3 scripts/generate_control_contract.py --source <frozen-pico> --check`.
- `npm --prefix <frozen-pi>/WsprryPi-UI run test:unit`.
- From frozen `src`, `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` with explicit `PICO_SOURCE=<frozen-pico> MBEDTLS_SOURCE=/mbedtls` and targets: `controller-only-test pi-managed-main-test pi-managed-wtp-main-test pi-managed-host-test pi-managed-host-runtime-test pi-management-service-test pico-target-controller-test pico-management-target-test pico-management-history-test pico-management-journal-test build/bin/shared_setup_main_fixture`.
- The same explicit make profile, separately: `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`.
- `cmake -S <frozen-pico> -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`, then `cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4`.
- The same explicit make profile: `wtp-usb-pair-client-build wtp-usb-management-test build/bin/pico_management_usb_pair_client`.
- `node ../WsprryPi-UI/tests/pico_management_usb_pair_test.js
  <frozen-pi>/src/build/bin/pico_management_usb_pair_client
  /tmp/pico-usb/usb_management_driver`.
- `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js
  <frozen-pi>/src/build/wtp-network/wtp_usb_pair_client
  /tmp/pico-usb/usb_management_driver`.
- The same explicit make profile: `semantics-test-portable`.

Portable semantics is the explicit simulated-only subset, not the full physical backend semantics profile. Nested make reported jobserver resets (`-j2`/`-j4`); CMake reused compiler checks. These are build diagnostics, not test skips. Expected negative-startup tests log missing/corrupt authority errors. Native Mac fixtures also report unavailable Linux device-tree metadata; their endpoints, configuration ownership, zero output writes, and required assertions passed.

In Docs:

```sh
source docs/.venv/bin/activate
python -m sphinx -E -b html docs docs/_build/html
```

PASS with zero warnings, including after final screenshot export. The local link/fragment checker passed. `/private/tmp/issue473-phase3-docs-render.js` rendered the actual built guides at 1440/390 px; log `/private/tmp/issue473-phase3-docs-render.log`. Application captures are in `/private/tmp/issue473-phase3-shared-setup-captures`, `/private/tmp/issue473-target-editor-ui` (or the configured editor capture path), and the controller/recovery capture directories named in their logs. Docs captures are in `/private/tmp/issue473-phase3-docs-captures`. These are local, volatile review artifacts, not published delivery or physical qualification.

Final PHP/JavaScript syntax checks and staged/unstaged `git diff --check` passed. No new CI run was triggered: this work remains uncommitted, and the feature branch has no push-triggered workflow. Local results are not represented as GitHub CI.

## Documentation Impact

Updated: parent Fleet/integration guidance, UI README/test entry point, phase indicator/evidence; sibling Setup overview, Signal Setup, Transmitter, Device I/O, Fleet selection/save/recovery guidance, navigation/link labels, and five affected screenshots (four replacements and the Pico transmitter view). Published Pi_IO URLs and the old station-progress bookmark remain usable.

Considered but unchanged: current shared-control/WTP/TLS contracts, INI semantics, Pico firmware/manual, and unaffected application guides. The phase relocates existing supported editors rather than changing those contracts or firmware. No further phase-3 documentation remains required. Automatic Pi pairing/TLS, remote Pi editor migration, and the future Scheduling UI remain explicitly at their approved later gates, rather than documented as present.

## Final assessments and Git state

After the fixes above, a full pass rechecked each saved-prompt requirement against implementation, negative cases, actual producer/client evidence, and rendered operator workflows. No further actionable issue remained. A second full pass rechecked authority/scope, target/draft/operation isolation, restart/maintenance recovery, strict current contracts, source pins/hashes, test outcomes, documentation links/images, and staged/unstaged state. No further actionable issue remained within phase 3. Required tests are successful; the approved security deferral is preserved, not waived or silently implemented.

At implementation acceptance, before the separately authorized commit/push, Pi and Docs were on `codex/issue-473-device-owned-scheduling` at the starting HEADs above, with the phase-3 changes unstaged. The unrelated untracked SQLite prompt remains untouched. The ordinary Pico and paired candidate remain clean at their recorded revisions. Modified component: `WsprryPi-UI` only; reusable `src/` component APIs and implementations are unchanged. Parent `src` changes are the fixture target and test harnesses described above. During implementation, no stage, commit, push, PR, merge, hardware access, installation, service operation, or physical transmission occurred. Physical USB, GPIO, timing, electrical behavior, and RF remain separate qualification. Stop here; phase 4 is not started.
