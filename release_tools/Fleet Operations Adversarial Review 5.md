# Fleet Operations fifth adversarial reassessment

Review date: 2026-11-13. This assessment starts from the committed fourth reassessment. It covers action/result correlation, lookup and journal bindings, owner recovery/admission boundaries, Setup control availability and rendered feedback. Actionable findings were closed. The final bounded reassessment found no further actionable findings. Nothing was staged, committed or pushed.

## Candidate

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | 287278cced5a374d035b5be032cad06ba091f8d4 | Base plus the tested source manifest below |
| WsprryPi-Docs | devel | 826972ba4af617966568c6a512bad1d8268f34ae | Base plus action-result guidance |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged intended companion |

Production changes are confined to the first-party WsprryPi-UI Fleet client, Setup controller and one existing template marker. No parent/companion runtime, shared settings/control advertisement or schema, WTP/carrier format, server admission contract or named reusable library changed. The client validates the existing producer contract; it does not introduce a new API version or narrow supported correlated action states.

## Findings closed

| Finding | Correction and evidence |
| --- | --- |
| A successful POST reply could report another action as this request's success. | The client now validates the reply envelope, action ID, device, controller, generation, operation, reviewed boot and control revision before merging or returning evidence. A new assignment generation is bound only afterward. Accepted/pending records require work_pending=true and confirmed/refused records require false; superseded/unconfirmed records retain either legitimate callback state. Known action revisions cannot drift and are retained in journal serialization. Eleven malformed/mismatched POST cases, changed full-read control revision and serialized revision tests pass. Valid correlated refused, superseded-with-pending-work and unconfirmed replies remain supported; Setup labels these with their actual state instead of calling them accepted. A real rendered Setup save receives a foreign confirmed reply without admission: its draft stays intact, its original unbound reference remains tracked, rows are unchanged and no POST retries occur. Independently correlated GET proof can still resolve a delayed/lost POST. |
| Invalid full-read action evidence could poison trusted metadata. | Full reads and lookup batches now preflight referenced evidence before installing clocks/publications, learning generations or merging records. The regression supplies an invalid action with a larger clock/publication, verifies the trusted values stay unchanged, then successfully reads the original valid sample. |
| A malformed unknown-result lookup stub could erase retained bindings. | The old generic merge accepted extra null identity fields and replaced the device/controller/generation on retained references, stranding later lookup and review. Unknown-result stubs now require exactly action_id and state. The regression injects null bindings into a batch, verifies every retained reference remains intact, then verifies valid lookup continues. |
| Setup exposed Reconcile for inhibited or busy owners. | Saved rows now inhibit Reconcile while action callbacks remain pending. Retained rows also honor can_reconcile=false and pending callbacks, matching Operations. The browser regression exercises ordinary pending work, retained inhibition, retained pending work and restored quiescent admission. Retained descriptions now state removal without inventing a local-takeover cause; durable operator removal can also leave a retained context. |
| Loaded Fleet rows kept their initial loading instruction. | The template's initial paragraph lacked the data-empty marker consumed by the existing renderer. It now uses that marker, so an accepted nonempty projection removes the obsolete instruction. The rendered regression asserts that fully populated output rows do not retain the loading hint. |

Pre-fix failures are recorded in `/private/tmp/fleet-review-5-correlation-red.log`, `/private/tmp/fleet-review-5-journal-red.log`, `/private/tmp/fleet-review-5-unknown-red.log`, `/private/tmp/fleet-review-5-reconcile-red.log` and `/private/tmp/fleet-review-5-placeholder-red.log` and `/private/tmp/fleet-review-5-atomic-red.log`. These expected regression failures are not final validation successes. The unit POST adapter now returns the actual producer envelope rather than an action-only approximation. The browser corruption fixture returns a foreign success without applying the submitted mutation, distinguishing uncertain admission from independently proven persistence.

The final source pass traces fresh, delayed and mismatched replies; legitimate non-success results; active versus terminal work flags; original and restored reference bindings; malformed and valid unknown-result lookup; changing control revisions; assignment generation binding; pending ordinary/retained controls; manager retirement and recovery; and preservation of current drafts. It found no further actionable findings in the reviewed scope. This is a bounded source and hardware-free fixture assessment, not proof that all defects are absent. No new operator or implementation decision is required.

## Validation

Linux checks run in `wsprrypi-ci-phase5:latest` with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`. The parent checkout is `/work/pi`, the intended Pico checkout is read-only at `/work/pico`, and read-only Mbed TLS at `/work/mbedtls` is clean at pinned commit `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Application/feature fixtures run in `--network none` containers with private loopback. No live target is involved. Commands use `/work/pi/src` unless otherwise stated:

```sh
SIM='BACKENDS=simulated ANCILLARY_GPIO=0 SUDO='
OBJ='OBJ_DIR_DEBUG=build/fleet-operations-linux/obj BIN_DIR=build/fleet-operations-linux/bin'
NET='WTP_NETWORK_BUILD_DIR=build/fleet-operations-network'
PAIR='PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/work/mbedtls'
```

| Check | Exact command | Result/evidence |
| --- | --- | --- |
| Candidate debug, feature driver and parent API | `make -j4 debug wtp-fleet-operations-driver-build wtp-api-cpp-test $SIM $OBJ` | Passed; `src/build/fleet-review-5-production.log` |
| Actual runtime/TLS | `make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-review-5-runtime $PAIR` | Passed: 916 production runtime/configuration checks, 34 TLS cases and Plain TCP isolation; production log |
| Actual network and configuration suites | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; production log |
| Actual Pico USB handlers | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; production log |
| Actual USB management/framing and settings pair | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; production log |
| Actual application browser/INI, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed: seven source checks, invalid-INI refusal and both finite simulated browser-load profiles; `src/build/fleet-review-5-feature-pairs.log` |
| Actual network Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Actual framed USB Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Shared contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; no generated changes |
| UI component, from WsprryPi-UI | `npm test` | Passed on final sources; `/private/tmp/fleet-review-5-ui.log` |
| Rendered Setup | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-review-5-setup.log` |
| Rendered Operations | `node tests/fleet_operation_ui_test.js` | Passed, 36 captures; `/private/tmp/fleet-review-5-operations.log` |
| Documentation, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed: consistent dependencies, one extension test, strict warning-free Sphinx build; `/private/tmp/fleet-review-5-docs.log` |
| Documentation rendering | `node /private/tmp/fleet-doc-review.js` | Passed: four Fleet pages at desktop/mobile sizes; `/private/tmp/fleet-review-5-doc-visual.log` |
| Hygiene | `node --check` on all four changed JavaScript files; `php -l WsprryPi-UI/data/views/fleet-schedules.php`; `git diff --check` and `git diff --cached --check` in Pi and Docs | Passed; staged diffs empty |

The carrier feature pairs retain the shipped browser client, actual coordinator, actual Pico TLS/framed handlers and independent simulated Pi target, including selected-output isolation, committed Enable, priority Pause, owned cleanup and failed-manager replacement/restoration. Canonical USB separately covers UMP framing/ACK, protected requests and real management services. Expected negative/fault-injection runtime diagnostics are not final failures; all listed commands exited successfully. The benign host pip-cache permission warning disabled caching only. Full Linux semantics and standalone owner/capacity fixtures were not repeated for this UI-only correction; no parent/companion runtime or reusable library changed, and current actual application/runtime/carrier checks are listed above.

Impeccable review preserved the incumbent Bootstrap design. Final Setup captures use 1280×900 desktop and 390×844 mobile in both themes; Operations uses 1440×1050 and 390×844. New retained-inhibited and uncorrelated-reply variants have full-page and detail captures under src/build/wtp-pi/ui. The final dark/mobile and light/desktop uncorrelated-save details and light/mobile and dark/desktop retained-owner details were inspected. Failed correlation keeps the draft and its feedback together; an inhibited owner has a disabled Reconcile control, neutral removal text and no stale loading hint. Existing page-overflow, disabled-label contrast, modal/keyboard/Escape, capacity, draft/profile binding and persisted-page lifecycle checks still pass. Operations retains its 36 captures. The changed Setup handbook paragraph was rendered and inspected at mobile width; the handbook keeps its existing light-only design. Screenshots are fixture evidence, not shipped replacement assets.

## Documentation Impact

- Updated WsprryPi docs/wtp-fleet.md: per-request POST correlation, work-state consistency, immutable retained action revisions, closed unknown-result stubs and Setup reconciliation guards.
- Updated WsprryPi-Docs docs/User_Interface/Setup/index.md: unmatched replies remain unconfirmed with entries retained, and earlier processing or controller inhibition disables Reconcile.
- Considered but unchanged: Operations, troubleshooting, REST and Pico protocol/control guides. Routes, producer schemas, physical behavior and ordinary workflows are unchanged. Existing screenshot assets remain materially accurate.
- No further documentation update is required for these fixes after the recorded build/render checks. Physical qualification remains a separately authorized activity.

## Delivery and limitations

Pi and Docs remain on their existing devel branches with only these uncommitted changes. WsprryPi-UI is the modified application component; no src library changed. Pico remains clean on devel. Staged diffs are empty. No commit, push, publication, installation, service operation or live-device operation occurred. No CI workflow was invoked and no new CI link exists. Software tests do not qualify physical USB, deployed networking, services, installation, Pi timing, GPIO, I²C, frequency accuracy or RF output.

## Tested source manifest

Manifest SHA-256: `626f15ed747906a98ea2dbe01469646b1e21349a25dd7d26cd5df9012c49e30f`. Paths are relative to `/Users/lbussy/GitHub`. It includes every edited/new source and documentation file except this report, avoiding a self-referential digest. Unchanged sources come from the base revisions above; ignored build output, captures and temporary logs are not repository source changes.

```text
31ab5bc5f59ed3f4c45f6d6faec9f9ea960c319d5a51bc0262baa590ba9f3de2  WsprryPi/WsprryPi-UI/data/fleet-client.js
e5b5a7dce7f8ef3cdd5d773c954d256d85640b84506e6af2dea28add975041d0  WsprryPi/WsprryPi-UI/data/fleet-schedules.js
f7f33f120a55b76819ffeeb451af23049f0ba448b6a6cb0a85d707def01b4616  WsprryPi/WsprryPi-UI/data/views/fleet-schedules.php
fb1ef194e41d40571e9c43dafa616a0cdd7b0bc318a61c9922d4b0b6fc422cb4  WsprryPi/WsprryPi-UI/tests/fleet_client_test.js
40f6d604f67b80daf70c076788740a130cd1fc59a25563389978b346d5008f53  WsprryPi/WsprryPi-UI/tests/wtp_pi_fleet_ui_integration_test.js
6f353c766f1763676a4b79635ee89a47b18201e2e10d5e217c8f999654568954  WsprryPi/docs/wtp-fleet.md
ed453fd27c6be9f555db5590a6b40de069f6c9d6031513a0b526b22520a1330e  WsprryPi-Docs/docs/User_Interface/Setup/index.md
```
