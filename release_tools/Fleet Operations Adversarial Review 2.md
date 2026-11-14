# Fleet Operations second adversarial reassessment

Review date: 2026-11-13. This assessment starts from the committed fixes described in [the first reassessment](Fleet%20Operations%20Adversarial%20Review.md). It records this follow-up's uncommitted corrections and hardware-free evidence. All required follow-up checks passed. The final reassessment found no further actionable findings.

## Candidate

| Repository | Branch | Base revision | Tested changes |
| --- | --- | --- | --- |
| WsprryPi | devel | 7f4cb89fea93b849df2bc9ec4f06092eb7db7ef0 | Base plus the source/documentation manifest below |
| WsprryPi-Docs | devel | f86308da90b9642e209d88414a2e4cf6d1f81559 | Base plus the troubleshooting paragraph below |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged intended companion source |

Production changes are confined to the parent Fleet owner/runtime. The first-party UI component has test changes only; its shipped presentation and client source remain unchanged. Typed owner fixtures and the paired test driver were extended. No named reusable library, Pico firmware, WTP wire format, shared Control/1 contract or capability advertisement changed. No staging, commit, push, publication, CI invocation or live-device operation occurred.

## Findings closed

| Finding | Failure and correction | Evidence |
| --- | --- | --- |
| Admission after an owner decides to exit | A removed context could accept Reconcile after its worker had decided to exit, leaving work without an executor and potentially a permanently pending ledger record. The owner now sets its quit/admission fence under the same context mutex used to select its final exit. | A held removed-owner exit admits no late Reconcile, returns the retirement refusal, creates no action record and then releases its retained context. The pre-fix capacity fixture failed on late admission. |
| Pi takeover without a WTP boot binding | A successful Pi HTTP observation could revoke an assignment when WTP inspection had failed before obtaining any identity. Boot comparison previously ran only when an identity happened to exist. Applying takeover evidence now requires a matching WTP device ID, Pi product and boot before any retirement mutation. | A closed WTP carrier and a valid-looking HTTP takeover-generation change cannot remove the saved assignment or create journal authority; the fixture verifies that the result was consumed and explicitly rejected. This failed before the fix. |
| Unchecked initial Pi generation seed | Initial assignment seeded its generation from a partially checked HTTP observation, then performed another read. Missing boot evidence or a generation change between those samples could create retirement state for an assignment that was never saved. Initial assignment now uses one closed, boot-matched observation to seed/apply its generation. | The final regression uses correct schema/ID/generation types but omits boot, a shape that passes the HTTP helper's existing subset of checks. It must refuse the unsaved assignment without orphan journal entries or a global storage-error latch. The earlier broad malformed-schema fixture reproduced journal-health failure only through the typed provider; the real HTTP helper already rejects schema/generation-type errors, so that broader result is not claimed as production-path evidence. |
| Hidden replacement startup failure | After old owners stopped, failed new initialization left the stopped old manager published with HTTP 200 and hid the startup error. Failure state and manager removal are now published while the replacement lifecycle lock is still held. Identity resolution is inside that lifecycle, and an outer catch cannot overwrite a later startup result. | A real public start/replacement call on private corrupt state produces HTTP 503 with its bounded cause; corrected initialization restores HTTP 200 with a fresh incarnation. Both actual carrier feature pairs additionally exercise this transition with the shipped Fleet client, actual Pico handler and independent simulated Pi. |

The follow-up traced lock order, action admission versus final exit, resource and identity-lease release, pending host observations, initial assignment failure, replacement error publication, delayed old replies and restoration. No product/workflow decision was changed. The final source pass found no further actionable findings, and all required checks below passed.

Pre-fix local evidence is in `src/build/fleet-reassessment-2-retiring-red.log`, `src/build/fleet-reassessment-2-unbound-red.log` and `src/build/fleet-reassessment-2-replacement-red.log`. `src/build/fleet-reassessment-2-red.log` records the broader initial typed-provider failure described above. An initial regression build used a host clock where the target requires a schedule clock; that fixture compilation error was corrected before execution. None of these intermediate failures is treated as final validation success.

## Validation

Linux fixtures run in the existing `wsprrypi-ci-phase5:latest` Docker image with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`. WsprryPi is mounted at `/work/pi`; the intended Pico checkout is read-only at `/work/pico`; the selected Mbed TLS checkout is read-only at `/work/mbedtls` and verified at workflow-pinned commit `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Private application/feature fixtures use `--network none` and their own loopback. No production host or physical transport is involved. Commands use `/work/pi/src` unless stated otherwise:

```sh
SIM='BACKENDS=simulated ANCILLARY_GPIO=0 SUDO='
OBJ='OBJ_DIR_DEBUG=build/fleet-operations-linux/obj BIN_DIR=build/fleet-operations-linux/bin'
NET='WTP_NETWORK_BUILD_DIR=build/fleet-operations-network'
PAIR='PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/work/mbedtls'
```

| Check | Exact command | Result |
| --- | --- | --- |
| Candidate build, owner/capacity/status/application/API | `make -j4 debug wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build wtp-status-test wtp-application-test wtp-api-cpp-test $SIM $OBJ $NET WTP_STATUS_BUILD_DIR=build/fleet-operations-linux/status WTP_APPLICATION_BUILD_DIR=build/fleet-operations-linux/application` | Passed; `src/build/fleet-reassessment-2-owner-runtime-final.log` |
| Final paired fixture driver | `make -j4 wtp-fleet-operations-driver-build $SIM $OBJ` | Passed; `src/build/fleet-reassessment-2-driver.log` |
| Actual TLS/runtime | `make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-reassessment-2-runtime $PAIR` | Passed, 916 production checks, 34 TLS cases and Plain TCP isolation; owner-runtime-final log |
| Actual network suites | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; `src/build/fleet-reassessment-2-protocol-pairs.log` |
| Actual Pico USB handlers | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; protocol-pairs log |
| Pi USB client/framing and actual settings pair | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; protocol-pairs log |
| Actual simulated application HTTP, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed; `src/build/fleet-reassessment-2-feature-pairs.log` |
| Actual network Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Actual framed USB Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Shared contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; no generated changes |
| UI component, from WsprryPi-UI | `npm test` | Passed; `/private/tmp/fleet-reassessment-2-ui.log` |
| Rendered Operations | `node tests/fleet_operation_ui_test.js` | Passed; `/private/tmp/fleet-reassessment-2-operations.log` |
| Existing Fleet Setup | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-reassessment-2-setup.log` |
| Documentation, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed; consistent dependencies, one extension test and strict warning-free Sphinx build; `/private/tmp/fleet-reassessment-2-docs.log` |
| Documentation rendering | `node /private/tmp/fleet-doc-review.js` | Passed; `/private/tmp/fleet-reassessment-2-doc-visual.log` |
| Source hygiene | `git diff --check` and `git diff --cached --check` in Pi and Docs | Passed; staged diffs remain empty |

The owner/capacity fixture retains its actual four-worker HTTP contention test during blocked persistence, 32 exact logical 60-second launches, healthy-peer isolation while one peer is held, 30 exact logical 180-second launches, stale/changed-boot refusal, retirement/restart barriers, priority Pause and delayed-writer manager replacement. Status/recovery contributed 3,804 checks and application contributed 1,356 checks. These are deterministic software contracts, not wall-clock or RF measurements. The native container filesystem for TLS runtime avoids the previously documented bind-mount certificate-copy limitation. Intentional negative/fault-injection runtime diagnostics are expected; final exit codes determine the recorded result. The full Linux semantics profile passed for the original implementation and was not repeated for this focused Fleet-owner follow-up; the changed lifecycle is covered by the focused and required actual carrier paths above. No CI workflows were invoked and no new CI links exist.

Impeccable review retained the incumbent Bootstrap design. The rendered Operations test includes HTTP 503 initialization failure, the actual error text beside the selected controls, invalidation of the old snapshot, disabled Enable/Pause/Reconcile/Edit and restored reads after the failure clears. Twenty-eight captures cover desktop 1440×1050 and mobile 390×844 in both themes under `src/build/fleet-operations-ui`. The new `controller-unavailable-light-desktop.png` and `controller-unavailable-dark-mobile.png` were inspected; no overflow or misleading Running/output-off claim appeared. Existing Setup checks remain intact. The changed troubleshooting paragraph was inspected in the mobile handbook render under `src/build/fleet-operations-docs/Advanced_Operations-configuration_troubleshooting.html-mobile.png`; documentation also rendered at desktop 1440×1000. The handbook retains its existing light-only theme. No production UI source, stylesheet or screenshot asset was changed.

## Documentation Impact

- Updated Pi `docs/wtp-fleet.md`: closed initial Pi observation, binding before retirement, admission closure before owner exit, explicit failed initialization and fresh-incarnation restoration.
- Updated WsprryPi-Docs `docs/Advanced_Operations/configuration_troubleshooting.md`: initialization errors, maintenance of the reported cause, new context after corrected initialization and no inference of device output-off from controller unavailability.
- Considered but unchanged: Operations and Setup descriptions still match their controls; REST paths and current response-error contract are unchanged; Pico protocol/control documentation is unchanged because no companion behavior or schema changed; existing screenshots still depict valid workflows.
- Still required for these corrections: none after the recorded build/render checks. Physical qualification documentation depends on separately authorized physical evidence.

## Delivery and limitations

Pi and Docs remain on their existing devel branches with only these uncommitted follow-up changes. Pico remains clean on devel. Nothing is staged, committed, pushed, installed, deployed or operated on live hardware. Hardware-free software evidence does not qualify physical USB/TCP/TLS deployment, services, Pi timing, GPIO, I²C, frequency accuracy, amplifier behavior or RF output.

## Tested source manifest

Manifest SHA-256: `c2bf7691937197fc82094a422464150e0841ad9c71901f4d6dbccc611725a85b`. Paths are relative to `/Users/lbussy/GitHub`. It includes every edited/new source or documentation file except this report, avoiding a self-referential digest. Unchanged sources come from the base revisions above; ignored build outputs and transient fixture logs are not repository source changes.

```text
c4b320b4958339acf691355b400cf0989967bf99f4a4f082cf1344e538775196  WsprryPi/WsprryPi-UI/tests/fleet_operation_ui_test.js
42070d4892f7658a28231d4f0aa7b75bc3cfb7fa3fca77baf4001ea03376df18  WsprryPi/WsprryPi-UI/tests/fleet_operations_pair_test.js
273a310048fca86375a9dab93babc3955891871eaca7adda62347c98aee37ad5  WsprryPi/docs/wtp-fleet.md
60faef4b385cefb4906dae7de64d475287be510d0f1642b0ccb76b69cabc1ddb  WsprryPi/src/tests/wtp_fleet_capacity_test.cpp
d1f6459037b6dd31db0b72175bd692425a1816e0150da10e9b0d8112c05a9ec2  WsprryPi/src/tests/wtp_fleet_operations_driver.cpp
e9cf49ae28ff00d2ecb79a754579563a458d23af83f56c108f245415b202bd8c  WsprryPi/src/tests/wtp_fleet_test.cpp
da6688b8c1093a3cf02627e72ee8a4ae717a95583b7b3b5ed97142807746c3b0  WsprryPi/src/wtp_integration/fleet_runtime.cpp
758bcadc62cf73a4cef2765f8e629ddd403c595d6c439b01459c0e89fdb768aa  WsprryPi/src/wtp_integration/fleet_test.hpp
a7aa9b6782da5435ceba667c546c170e5d5d379aa4ddf30a2800b48060ab7d43  WsprryPi-Docs/docs/Advanced_Operations/configuration_troubleshooting.md
```
