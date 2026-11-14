# Fleet Operations seventh adversarial reassessment

Review date: 2026-11-14. One actionable finding was reproduced, corrected and reassessed. The final bounded source assessment found no further actionable findings or open design decisions. No staging, commit or push is authorized for this review.

## Candidate

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | 2c120a129b84ca9f96cf51bd250ab50885805d9d | Base plus tested source manifest below |
| WsprryPi-Docs | devel | 681da70407b98f38e68f8d6a3ef4a71f2d0c15ec | Base plus Operations guidance |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged intended companion |

Production changes are confined to the parent application integration at src/wtp_integration/fleet_runtime.cpp. The existing parent capacity regression, WsprryPi-UI rendered test and engineering/operator documentation also changed. No reusable src library, production UI source, Pico source, shared control schema, advertised capability, wire format or storage format changed. The existing foreign state is now selected correctly; no new protocol version or companion compatibility path is introduced.

## Finding closed

Authoritative terminal evidence for an earlier owned job could mask current foreign occupancy. WTP deliberately retains authoritative evidence from terminal history even when STATUS names another owner or job. Fleet checked only whether job evidence was authoritative before using its state as current. Following Pause of an armed job, a foreign claim could therefore leave Fleet reporting the old aborted state; Operations fell through to Idle despite the foreign owner. Dispatch remained blocked by the independent idle/ownership eligibility check, but the operator-facing classification was false.

The publication predicate now requires this session's current ownership and a matching remote job ID before treating non-idle occupancy as the assignment's current job. Historical terminal evidence remains available, but current foreign ownership or another job wins the classification. Existing recovery, unavailable and stale precedence remains intact. No additional abort, release, claim, reconnect or automatic takeover is added.

The corrected production-path regression fails against the original runtime with `historical owned completion cannot mask current foreign ownership` in `/private/tmp/fleet-review-7-foreign-red.log`. It uses the actual Fleet coordinator, WTP session/framing, authority, Endpoint and JobService with typed logical clocks and an inhibited engine. It arms then pauses an owned finite job, proves its terminal evidence remains authoritative, observes a foreign claim, and requires foreign classification. The final expanded regression first requires all 32 owned projections to report Armed and then Running at their controlled logical boundaries, preserving the positive classification. It also loads a different job under that foreign owner, checks the classification again, proves scoped Pause preserves both owner and job, lets that principal abort/release its own work, then requires fresh unowned eligibility. Initial fixture attempts either lost the relevant job evidence, held the wrong recurrence boundary or held the clock before a post-start STATUS could report Running; they were corrected before reproducing the production defect and are not presented as qualifying failures.

A further owned-empty/LOAD-pending hypothesis was tested using a held real LOAD reply. It did not reproduce a false foreign classification: the runtime withdrew remote/job/ownership evidence during the unsettled request and published unavailable with Resume disabled. The probe log is `/private/tmp/fleet-review-7-own-load-red.log`; its expected owner/empty precondition timed out, so it is not a successful regression or a confirmed product finding. The experimental hold was removed, restoring the capacity test byte-for-byte to its final successful run. Existing unsettled-request behavior remains unchanged.

The rendered Operations regression separately requires Other job on device, disabled Resume, no current job-bound realization claim and a distinct Historical cancelled report. It retains the paused assignment's independent schedule and verifies the established controls/layout at both sizes and themes.

The final reassessment traces terminal history versus live owner/job IDs, current owned Armed/Running evidence, intentionally inactive intervals, lease expiry, foreign claim/LOAD, Pause scope, independent eligibility, stale/unavailable/recovery precedence, action correlation, retained cleanup and manager replacement. No further actionable findings remain in the reviewed scope. This is a bounded source and hardware-free fixture assessment, not proof that all defects are absent.

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
| Candidate debug, feature driver and parent API | `make -j4 debug wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build wtp-api-cpp-test $SIM $OBJ` | Passed; `src/build/fleet-review-7-production.log` |
| Expanded deterministic owner/capacity regression | `make -j4 wtp-fleet-capacity-test $SIM $OBJ` in a separate `--network none` container | Passed on final test source, including foreign LOAD and Pause isolation; `src/build/fleet-review-7-capacity.log` |
| Actual runtime/TLS | `make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-review-7-runtime $PAIR` | Passed: 916 production runtime/configuration checks, 34 TLS cases and Plain TCP isolation; production log |
| Actual network and configuration suites | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; production log |
| Actual Pico USB handlers | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; production log |
| Actual USB management/framing and settings pair | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; production log |
| Actual application browser/INI, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed: seven source checks, invalid-INI refusal and both finite simulated browser-load profiles; `src/build/fleet-review-7-feature-pairs.log` |
| Actual network Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Actual framed USB Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Shared contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; no generated changes |
| UI component, from WsprryPi-UI | `npm test` | Passed on final sources; `/private/tmp/fleet-review-7-ui.log` |
| Rendered Setup | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-review-7-setup.log` |
| Rendered Operations, from WsprryPi | `node WsprryPi-UI/tests/fleet_operation_ui_test.js` | Passed, 40 captures; `/private/tmp/fleet-review-7-operations.log` |
| Documentation, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed: consistent dependencies, one extension test, strict warning-free Sphinx build; `/private/tmp/fleet-review-7-docs.log` |
| Documentation rendering | `node /private/tmp/fleet-doc-review.js` | Passed: four Fleet pages at desktop/mobile sizes; `/private/tmp/fleet-review-7-doc-visual.log` |
| Hygiene | `node --check WsprryPi-UI/tests/fleet_operation_ui_test.js`; `git diff --check` and `git diff --cached --check` in Pi and Docs | Passed; staged diffs empty |

The carrier feature pairs retain the shipped browser client, actual coordinator, actual Pico TLS/framed handlers and independent simulated Pi target, including selected-output isolation, committed Enable, priority Pause, owned cleanup and failed-manager replacement/restoration. Canonical USB separately covers UMP framing/ACK, protected requests and real management services. Expected negative/fault-injection runtime diagnostics are not final failures; all listed commands exited successfully. The benign host pip-cache permission warning disabled caching only. The full Linux semantics profile was not run. The debug application, API, owner/persistence, expanded 32-output capacity, runtime, actual application and carrier fixtures above use the explicit simulated-only profile or hardware-inhibited adapters. No reusable library or companion source changed.

Impeccable review preserved the existing Bootstrap layout, typography, colors and themes. Operations captures use 1440×1050 desktop and 390×844 mobile in both themes. Final foreign-history captures in all four size/theme combinations were inspected under src/build/fleet-operations-ui. They show Other job on device, historical canceled work, a paused schedule, disabled Resume and separately labeled assigned frequency. Existing no-overflow and disabled-label contrast checks pass alongside keyboard/modal, local-handler isolation, stale data, pending actions and replacement workflows. Setup retains its desktop/mobile light/dark confirmation, draft and independent-assignment checks. Four handbook pages were rendered at desktop/mobile sizes; the changed Operations paragraph was inspected in the dedicated mobile capture. The handbook retains its light-only design. Fixture screenshots are not replacement operator assets or hardware evidence.

## Documentation Impact

- Updated WsprryPi docs/wtp-fleet.md: authoritative terminal history cannot mask current foreign ownership/job evidence.
- Updated WsprryPi-Docs docs/User_Interface/Operations/index.md: earlier completed/canceled work cannot establish Idle while another owner is present.
- Considered but unchanged: Setup, REST, troubleshooting and Pico protocol/control guides. Existing routes, controls, formats, action behavior and normal screenshots remain materially accurate.
- No further documentation update is required for this correction after the recorded strict build and rendering checks.

## Delivery and limits

Pi and Docs remain on devel with only this review's unstaged changes. WsprryPi-UI has a test-only change; the parent Fleet integration and parent capacity test changed. Pico remains clean on devel. No reusable component was modified. No commit, push, CI, publication, installation, service change, GPIO/I²C operation or live-device operation occurred. Hardware-free fixtures do not qualify physical USB, deployed networking, installation, service behavior, timing, frequency accuracy or RF output.

## Tested source manifest

Manifest SHA-256: `1dd40e4880cff7c638887686e3f9df0944971d8515bef2036f9fa49dac93dac5`. Paths are relative to `/Users/lbussy/GitHub`. Every modified source/documentation file is included except this report, avoiding a self-referential digest. Unchanged source is defined by the base revisions above; ignored builds, captures and temporary logs are not repository source changes.

```text
cb4a9cf50ae6929e75be8d4b60940d08ff8ef2a1a4ea078add3f23b6ef983e32  WsprryPi/src/wtp_integration/fleet_runtime.cpp
22845358c84f01b88ebe45a42c8248ffa0be6c27d35be320033a9b376aa4a53f  WsprryPi/src/tests/wtp_fleet_capacity_test.cpp
09c4e0823a5f55f0d692d67fb61b5ba1f28871312dc441393a2fef8045961729  WsprryPi/WsprryPi-UI/tests/fleet_operation_ui_test.js
b35ddcefa5c6ca67b7e45f291b73e329a777f9fdd66505720cb724a70364f679  WsprryPi/docs/wtp-fleet.md
0376341702d66c338bc467d72c3c333005bf6755fabd17dc1b094a6c4e7c3e63  WsprryPi-Docs/docs/User_Interface/Operations/index.md
```
