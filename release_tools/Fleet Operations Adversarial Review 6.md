# Fleet Operations sixth adversarial reassessment

Review date: 2026-11-13. Actionable findings were reproduced, corrected and reassessed. The final bounded assessment found no further actionable findings or open implementation decisions. Nothing was staged, committed or pushed.

## Candidate

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | 542e18a7e76f30c92dbd150224098a78ba78ee6a | Base plus source manifest below |
| WsprryPi-Docs | devel | ad69a5353e9cbb4c52c55d7991aba0cf2f956cf5 | Base plus Setup guidance |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged intended companion |

Production changes affect only WsprryPi-UI/data/fleet-schedules.js. The rendered Setup fixture and engineering/operator guidance also changed. No parent C++ runtime, reusable src component, Pico source, advertised capability, shared control schema, wire format or server admission contract changed. Submission fields and immutable action bindings are browser draft metadata, not a new action-history ledger or protocol version.

## Findings closed

| Finding | Correction and evidence |
| --- | --- |
| Late confirmation erased entries edited after Save returned pending. | Save captures its submitted fields. Immediate or delayed confirmation clears only entries still identical to that submission, with the complete immutable action binding verified. Newer entries retain their original assignment, reviewed revision and field values. The handled reference is detached so later confirmations cannot erase them. Missing submission proof also preserves entries, including with a canceled editor, without reopening it. The regression verifies server duration remains 6 seconds while newer draft duration remains 9 seconds, the original reviewed revision stays unchanged, legacy entries survive and unchanged submissions still clear normally. No automatic rebase or mutation retry is added. |
| Another action's result could appear as this draft save's failure; the latest device summary could also hide this save's own confirmation. | Feedback now requires the exact action and all retained immutable bindings. Selection checks the device summary, active references and current validated output for that action rather than borrowing another action's state. The regression overlays an unrelated refusal, verifies it is never reported as this Save's result, and then verifies the original confirmation is handled from current validated output despite that unrelated summary. Legacy metadata lacking the full binding cannot clear entries or claim verified persistence. |

Pre-fix browser failures: `/private/tmp/fleet-review-6-newer-draft-red.log` and `/private/tmp/fleet-review-6-other-result-red.log`. These expected failures establish reproduction; they are not final validation successes. The browser fixture returns structured-cloned HTTP snapshots and can delay schedule/assignment completion while retaining the production action shape.

The final source reassessment traces immediate/pending/refused/unconfirmed results, current versus restored draft metadata, exact immutable bindings, canceled editors, changed versus unchanged fields, later unrelated results, preserved review authority, ownership admission, priority Pause and bounded recovery. It found no further actionable findings in the reviewed scope. This is a bounded source and hardware-free fixture assessment, not proof that all defects are absent.

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
| Candidate debug, feature driver and parent API | `make -j4 debug wtp-fleet-operations-driver-build wtp-api-cpp-test $SIM $OBJ` | Passed; `src/build/fleet-review-6-production.log` |
| Actual runtime/TLS | `make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-review-6-runtime $PAIR` | Passed: 916 production runtime/configuration checks, 34 TLS cases and Plain TCP isolation; production log |
| Actual network and configuration suites | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; production log |
| Actual Pico USB handlers | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; production log |
| Actual USB management/framing and settings pair | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; production log |
| Actual application browser/INI, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed: seven source checks, invalid-INI refusal and both finite simulated browser-load profiles; `src/build/fleet-review-6-feature-pairs.log` |
| Actual network Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Actual framed USB Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Shared contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; no generated changes |
| UI component, from WsprryPi-UI | `npm test` | Passed on final sources; `/private/tmp/fleet-review-6-ui.log` |
| Rendered Setup | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-review-6-setup.log` |
| Rendered Operations | `node tests/fleet_operation_ui_test.js` | Passed, 36 captures; `/private/tmp/fleet-review-6-operations.log` |
| Documentation, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed: consistent dependencies, one extension test, strict warning-free Sphinx build; `/private/tmp/fleet-review-6-docs.log` |
| Documentation rendering | `node /private/tmp/fleet-doc-review.js` | Passed: four Fleet pages at desktop/mobile sizes; `/private/tmp/fleet-review-6-doc-visual.log` |
| Hygiene | `node --check` on both changed JavaScript files; `git diff --check` and `git diff --cached --check` in Pi and Docs | Passed; staged diffs empty |

The carrier feature pairs retain the shipped browser client, actual coordinator, actual Pico TLS/framed handlers and independent simulated Pi target, including selected-output isolation, committed Enable, priority Pause, owned cleanup and failed-manager replacement/restoration. Canonical USB separately covers UMP framing/ACK, protected requests and real management services. Expected negative/fault-injection runtime diagnostics are not final failures; all listed commands exited successfully. The benign host pip-cache permission warning disabled caching only. Full Linux semantics and standalone owner/capacity fixtures were not repeated for this UI-only correction; no parent/companion runtime or reusable library changed, and current actual application/runtime/carrier checks are listed above.

Impeccable review preserved the existing Bootstrap design and themes. Final Setup captures use 1280×900 desktop and 390×844 mobile in both themes; Operations uses 1440×1050 and 390×844. Newer-draft detail captures in all four size/theme combinations were inspected under src/build/wtp-pi/ui. They show duration 9 and adjacent confirmation/preservation guidance without closing the editor. Existing overflow, disabled-label contrast (6.22:1 light, 9.86:1 dark), keyboard/Escape, capacity, stale-revision, profile-binding and persisted-page lifecycle checks pass. Operations retains 36 captures. Four handbook pages were rendered at desktop/mobile sizes, including a dedicated capture of the changed Setup paragraph; its mobile text was inspected. No shipped screenshot replacement is needed.

## Documentation Impact

- Updated WsprryPi docs/wtp-fleet.md with submission proof, retained binding, exact-result selection and preservation behavior.
- Updated WsprryPi-Docs docs/User_Interface/Setup/index.md with unchanged-submission confirmation and preservation/review guidance for newer or unproven entries.
- Considered but unchanged: Operations, REST, troubleshooting and Pico protocol/control guides; their routes, contracts and ordinary workflows are unchanged. Existing screenshots remain materially accurate.
- No further documentation update is required for these fixes after the recorded strict build and rendering checks.

## Delivery and limitations

Pi and Docs remain on devel with these unstaged changes; Pico remains clean on devel. WsprryPi-UI is the only modified application component. No reusable src component changed. No commit, push, CI, publication, service operation, installation or live-device operation occurred. Hardware-free fixtures do not qualify physical USB, deployed networking, installation, services, GPIO/I²C timing, frequency accuracy or RF output. Exact validation commands and limits appear above.

## Tested source manifest

Manifest SHA-256: `86ed79bf68ba5ec55638407fe32cba182a062902fbc3fa50b178cde3a3fbba66`. Paths are relative to `/Users/lbussy/GitHub`. Every modified source/documentation file is included except this report, avoiding a self-referential digest. Unchanged sources are defined by the base revisions above. Ignored builds, captures and temporary logs are not repository source changes.

```text
2a68d71031d3b9ae6f78f34e022eb85f85ba48cead6832c6195b11770d3ec4a3  WsprryPi/WsprryPi-UI/data/fleet-schedules.js
98c4529ae99a6c277f119883de0bf1405683315988c3718814562eb7e5522c35  WsprryPi/WsprryPi-UI/tests/wtp_pi_fleet_ui_integration_test.js
3399f73681b6753d3026125c5db6fad031a8c84e4a5ba0138a63a5bc840849b9  WsprryPi/docs/wtp-fleet.md
6c7739f93e6d5a5658084c0e6ee71c6777a257333886ea7e1be11b7c1bd67f9e  WsprryPi-Docs/docs/User_Interface/Setup/index.md
```
