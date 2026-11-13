# Fleet Operations adversarial reassessment

Review date: 2026-11-13. This follow-up assesses the committed implementation and records its uncommitted corrections. The initial implementation evidence remains in [Fleet Operations Validation](Fleet%20Operations%20Validation.md). All required follow-up checks passed. The final reassessment found no further actionable findings.

## Reviewed candidate

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | 3a30bf892ba221f2e28bbd7571da99639fc93743 | Base plus the uncommitted files hashed below |
| WsprryPi-Docs | devel | 25c4ad9aaf25c7af4d115ca6824e0ce5f8c5a46e | Base plus its Operations guide change hashed below |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged source used by both production pairs |

The change affects the parent Fleet owner/runtime and its typed fixtures, the first-party UI component's shared Fleet client/tests, engineering guidance and the operator Operations page. No named reusable library component, Pico firmware, shared Control/1 contract or WTP wire format changed. No staging, commit, push, CI invocation, deployment or live hardware operation is authorized or performed by this reassessment.

## Findings and closure

| Finding | Failure | Correction and evidence |
| --- | --- | --- |
| Durable publication ordering | An owner could commit, be delayed before publication, then publish its older assignment snapshot after another owner's newer commit. Cached settings and ETag could regress behind disk. | A manager-local persistence mutex covers each assignment commit and cached publication, including takeover revocation. It never covers HTTP snapshot reads. A held post-commit barrier and a second owner verify ordering and final equality between cached and durable assignments/ETag. |
| Protective Pause after admission | A change to the shared ETag after Pause acceptance could throw before owned cleanup and replace the result with a recovery latch. | Admission retains its original strict ETag/control/context guard. Once accepted, Pause continues protective persistence and cleanup across unrelated later commits. The pre-fix regression failed on the missing Pause postcondition; both owners now report paused persistence without clearing their unknown crash-cleanup duty. |
| Shutdown before joins | Shutdown waited for the reaper before fencing admission and dispatch, leaving a window for new positive work during retirement. | Admission, context shutdown flags, dispatch revocation and nonblocking stop requests precede every executor join. A typed shutdown-phase check verifies the published replacement fence and disabled action authority before waiting. Existing delayed-writer replacement, identity-lease and 32-output tests remain intact. |
| Tracking exhaustion during recovery | Unknown results could fill ordinary/priority slots while Review demanded reconciliation and reconciliation itself needed a free slot. Obsolete controller/context references could remain blocked by the new owner's independent recovery state. | Explicit review after a current controller read can retire obsolete or quiescent browser references while preserving device in-flight markers and server recovery duty. Current live callbacks stay protected. Unknown/expired lookup resets a stale browser work flag to unknown. Tests cover current/old controller cases and a stale browser pending flag superseded by a fresh quiescent owner snapshot. |
| Review racing its own POST | Review could discard a reference while the browser's POST was still in flight; immediately after the response it could use the older pre-POST snapshot to retire newly accepted work. | Review refuses during a browser request. Mutation completion fences preexisting reads and requires a later full snapshot before further review. A held POST and held subsequent GET prove both boundaries. The rendered review acknowledgment remains scoped to the selected context and never clears Recovery required or enables Resume. |
| Ambiguous host failure correlation | HTTP 5xx discarded an action reference even if the controller had accepted the command. Definite refusals could remain displayed as an unknown result. | HTTP 5xx/408, unreadable replies and transport failures retain correlation and uncertainty. Recognized definite refusal statuses retire the unaccepted reference and retain a visible refusal reason. Actual network/USB feature-pair tests replace an accepted Enable reply with HTTP 500, then resolve the original action ID and assert exactly one Enable POST. A delayed POST error also preserves stronger acceptance/completion evidence already obtained by a concurrent protected GET; the held-response regression verifies that completion never regresses to uncertainty. |
| Recovery after durable deletion | A deletion followed by a publication/durability acknowledgment failure could leave the saved row absent but the live context marked retained. Reconcile could confirm yet leave that orphan context consuming capacity indefinitely. | When the saved row is absent and frozen retirement authority exists, the owner treats it as removed. Successful reconciliation retires the journal/context and reclaims capacity without recreating the assignment. A post-delete failure injection exercises the actual 32-output owner path. |
| Metadata and port coercion | Regex checks coerced numeric/array metadata into valid-looking IDs/decimal strings. Narrowing a management port before checking bounds could admit a value such as 4294967297 as port 1 before inspection. | Browser IDs require strings; decimal metadata requires canonical strings within uint64. Admission checks the original JSON integer against 1–65535 before conversion or carrier work. Negative cases cover numbers, arrays, leading zeroes, null, uint64 overflow and oversized ports. |

The source review iterated through owner/store publication, Pause/activation races, shared lock ordering, replacement/shutdown, retained recovery authority, stale browser flags, interrupted requests, error classification, identity/generation routing and strict metadata, and partial-failure recovery after durable assignment deletion. The final source pass found no further actionable findings. Test and runtime evidence remains bounded to the paths below; this is not a proof against every possible failure or hardware qualification.

Pre-fix failing reproductions are retained locally in `src/build/fleet-adversarial-publication-red.log`, `src/build/fleet-adversarial-deleted-context-red.log`, `/private/tmp/fleet-adversarial-client-red.log` and `/private/tmp/fleet-adversarial-types-red.log`. Their failures were expected evidence and the corrected final affected checks passed.

## Validation commands and results

Linux checks use the existing `wsprrypi-ci-phase5:latest` image with hardware inhibition. WsprryPi is mounted at `/work/pi`, the intended Pico checkout read-only at `/work/pico`, and the workflow-pinned Mbed TLS checkout read-only at `/work/mbedtls`. Mbed TLS was verified at `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The following arguments apply from `/work/pi/src`:

```sh
SIM='BACKENDS=simulated ANCILLARY_GPIO=0 SUDO='
OBJ='OBJ_DIR_DEBUG=build/fleet-operations-linux/obj BIN_DIR=build/fleet-operations-linux/bin'
NET='WTP_NETWORK_BUILD_DIR=build/fleet-operations-network'
PAIR='PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/work/mbedtls'
```

| Check | Command | Result |
| --- | --- | --- |
| Final parent build and focused ownership/status/API tests | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j4 debug wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build wtp-status-test wtp-application-test wtp-api-cpp-test $SIM $OBJ $NET WTP_STATUS_BUILD_DIR=build/fleet-operations-linux/status WTP_APPLICATION_BUILD_DIR=build/fleet-operations-linux/application` | Passed; 3,804 status/recovery and 1,356 application checks plus Fleet owner/capacity/API fixtures; `src/build/fleet-adversarial-owner-runtime-final.log` |
| Final TLS runtime | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-adversarial-runtime $PAIR` | Passed; 916 production runtime checks and 34 TLS cases; same log; native container filesystem avoids the known bind-mount certificate-copy limitation |
| Actual Pico network suites | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; `src/build/fleet-adversarial-protocol-pairs.log` |
| Actual Pico USB handler build | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; protocol-pairs log |
| Pi USB framing/client and actual shipped settings producer | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; protocol-pairs log |
| Real simulated application HTTP fixtures, from `/work/pi` in a network-disabled container | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed; `src/build/fleet-adversarial-feature-pairs.log` |
| Actual network Fleet feature pair, from `/work/pi` in the same container | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; `src/build/fleet-adversarial-final-client-pairs.log` for the final shared-client source |
| Actual framed USB Fleet feature pair, from `/work/pi` in the same container | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; `src/build/fleet-adversarial-final-client-pairs.log` for the final shared-client source |
| Shared source contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; no generated changes |
| UI component suite, from WsprryPi-UI | `npm test` | Passed; `/private/tmp/fleet-adversarial-ui-suite.log` |
| Targeted browser coordinator | `node WsprryPi-UI/tests/fleet_client_test.js` | Passed; typed metadata, tracking bounds, held POST/read, uncertain 5xx versus refused 409, verified-GET completion before delayed POST failure, stale pending flags and controller restart |
| Rendered Operations, from WsprryPi-UI | `node tests/fleet_operation_ui_test.js` | Passed; `/private/tmp/fleet-adversarial-operations.log`, 24 captures |
| Existing rendered Fleet Setup, from WsprryPi-UI | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-adversarial-setup.log` |
| Documentation, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed; dependencies consistent, one extension test, clean strict Sphinx build; `/private/tmp/fleet-adversarial-docs.log` |
| Rendered documentation | `node /private/tmp/fleet-doc-review.js` | Passed; four handbook pages at desktop/mobile sizes, no page overflow; `/private/tmp/fleet-adversarial-doc-visual.log` |
| Source hygiene | `git diff --check` and `git diff --cached --check` in Pi and Docs | Passed; no staged changes |

The full Linux semantics profile passed for the original implementation. It was not repeated for this focused follow-up; current evidence is the Fleet/status/application/API fixtures and both required production carrier suites, including the feature-specific paths. No reusable backend implementation changed. No GitHub workflows were invoked, so no new CI links exist. The documentation dependency check emitted only the sandbox's non-writable pip-cache warning; it found no broken requirements. Intentional negative/fault-injection diagnostics in runtime tests are expected and do not establish a failed command.

Impeccable review preserved the established Bootstrap layout, typography, themes and native controls. Operations scenes cover confirmation, scoped local/remote handlers, stale/foreign/unavailable observations, pending Enable/Pause, Running through an output-off interval, replacement, and the corrected explicit review flow. The latter confirms that references are retired without another POST while Recovery required stays visible and Resume remains disabled. Desktop 1440×1050 and mobile 390×844 captures cover both light/dark themes under `src/build/fleet-operations-ui`, including `review-recovery-light-desktop.png` and `review-recovery-dark-mobile.png`; these final recovery captures were inspected. Disabled labels retain the existing asserted 4.5:1 contrast floor. Setup preserves confirmations, Escape/cancellation, editor drafts, independent assignments and target-local takeover choices. Handbook renders under `src/build/fleet-operations-docs` use its existing light-only theme at 1440×1000 and 390×844. The edited review paragraph itself was inspected in `Operations-review-mobile.png`, beyond the initial section-heading viewport.

## Documentation Impact

- Updated `docs/wtp-fleet.md` with ordered persistence/publication, accepted protective Pause independent browser-reference retirement versus device recovery duty, and reclamation after a durably deleted assignment.
- Updated the operator Operations guide in WsprryPi-Docs with review availability, retained recovery blocks and waiting for in-flight requests.
- Considered but unchanged: Setup and troubleshooting guidance already describe explicit review and required reconciliation; Pico protocol/control documentation needs no change because its behavior and schema are unchanged; existing local-only screenshots remain accurate.
- Still required for these corrections: none after the recorded strict build and rendered review. Physical qualification documentation must follow separately authorized physical evidence.

## Delivery and qualification boundary

Pi and Docs remain on devel with only this follow-up's uncommitted changes. Pico remains clean on devel. Nothing is staged, committed or pushed. No production host was contacted, and no software was deployed or installed. Software fixtures do not qualify physical USB/TCP/TLS deployment, services, Raspberry Pi timing, GPIO, I²C, frequency accuracy, amplifier behavior or RF output. No such qualification is claimed.

## Follow-up source manifest

SHA-256 of the following manifest: `c5004e5db6ee31904a41f8f65edf0cce78c56023c414dc0714151056ac2a750e`. It includes every edited/new source and documentation file relative to `/Users/lbussy/GitHub`, excluding this report to avoid self-reference. The intended base revisions are recorded above; ignored build artifacts and transient fixture logs are not source changes.

```text
3ac38e89c2de5821ab128e8891322fc8c227d581c3ac67d16a7aa72b9e4aaa47  WsprryPi/WsprryPi-UI/data/fleet-client.js
78ea530f52a9ac63293e281aedc600a08fc00480082517564d1a1134340b67be  WsprryPi/WsprryPi-UI/tests/fleet_client_test.js
73f0e2993ccb6dd03dc5424d28e572cb3fce08614724d8eca1b5fa56870a0442  WsprryPi/WsprryPi-UI/tests/fleet_operation_ui_test.js
8b2b4e28053c19ead1f850637db6825d7345473bad1d6be1c004a287a723bad8  WsprryPi/WsprryPi-UI/tests/fleet_operations_pair_test.js
a8c26f2ac830c0383aade9f157032c2ba83592426e284fdd7b10bf11238ba13d  WsprryPi/docs/wtp-fleet.md
65958d9e5481953e13ec9f1e3fc7002d886a0cc9001e9ed3ac86236afe355ded  WsprryPi/src/tests/wtp_fleet_capacity_test.cpp
17f729ad6c8eddcc2b42119e866e0ad2cf1b26bb282976739802219ac36fe6fa  WsprryPi/src/tests/wtp_fleet_test.cpp
86a83daaa83af0cfab81203bbf66ec45954ca1192dcb6bc685076ed9ca19f5c0  WsprryPi/src/wtp_integration/fleet_runtime.cpp
343a433ceb0ebf8c98558d4a58e780d78cea42787c28d87188a7f4453dd5efe5  WsprryPi/src/wtp_integration/fleet_test.hpp
248008233eb38d5cb566f64a7d86c7cc47a997cce7e60e5e2664d5e5aa9abdf3  WsprryPi-Docs/docs/User_Interface/Operations/index.md
```
