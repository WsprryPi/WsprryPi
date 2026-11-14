# Fleet Operations third adversarial reassessment

Review date: 2026-11-13. This review starts from the committed second reassessment and covers controller publication freshness, browser lifecycle and queue bounds, action correlation, owner admission, retirement and recovery. Findings and final validation are recorded below. All required focused and actual carrier checks passed. The final reassessment found no further actionable findings. No implementation was committed or pushed.

## Candidate

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | 3b3ffe571efd62afe5b209f5cf6635fbbf4524bb | Base plus the tested source manifest below |
| WsprryPi-Docs | devel | 0ae72c0d0d9353b2b09dc4bb40adcdc6f4298018 | Clean, unchanged |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged intended companion |

Production changes are limited to the parent Fleet runtime and first-party WsprryPi-UI Fleet client. No named reusable library, Pico source, protocol schema, advertised capability, settings/storage format, endpoint or action-admission contract changed. No new design decision is required.

## Findings closed

| Finding | Correction and regression evidence |
| --- | --- |
| Scheduling delay during age-domain translation could rejuvenate evidence. | The controller clock is now sampled before the transport clock, exactly once each. Delay between samples biases the mapped origin toward older evidence. Checked subtraction returns unknown on transport regression or publication-domain underflow. Zero remains a valid origin. A deterministic preemption fixture failed before the correction and passes afterward; the production publisher uses the tested helper. |
| Repeated cached full replies reset the browser baseline and could make expired evidence appear fresh. | Repeated publications now preserve a conservative BigInt age envelope for unchanged observation/context bindings across device, host takeover and UTC-anchor ages. Decreasing raw ages for unchanged evidence are refused. The browser unit regression failed before the correction and passes afterward. The rendered cached-reply scene advances only the browser clock by 15 seconds: Resume remains disabled, stale device output is unknown, and scoped Pause remains available. |
| Browser delay after round-trip measurement could rejuvenate a newly received publication. | The request returns its sampled monotonic receipt timestamp with elapsed duration; snapshot installation retains that baseline instead of sampling a later time. A 20-second delay injected between timing and installation failed before the correction and passes afterward, including for a new publication. |
| Alternating controller incarnations could cause an unbounded immediate confirmation loop. | A refresh permits at most one confirming full GET. A second change invalidates the sample and returns a visible error; normal bounded background polling can try again later. Unit and rendered fixtures alternate incarnations and assert exactly two reads, invalid status and an error beside the controls. Their watchdog would terminate an unbounded implementation. No mutation is retried. |
| Returning to the established controller could strand a different pending candidate and suppress action lookup forever. | An accepted established-controller full read clears the candidate. The regression alternates controllers, restores the established incarnation and verifies that the rotating action lookup resumes with retained references. |

The final timing iteration is recorded in `/private/tmp/fleet-review-3-receipt-red.log`. Pre-fix evidence is retained in `/private/tmp/fleet-review-3-age-red.log` and `src/build/fleet-review-3-clock-red.log`. The churn and stranded-candidate regressions also fail when each prior defective behavior is restored separately in an in-memory source loader, without changing the working tree: `/private/tmp/fleet-review-3-churn-red.log` and `/private/tmp/fleet-review-3-candidate-red.log`. These expected failures are not counted as final validation success. The Setup presentation fixture now synthesizes new STATUS/publication revisions independently of assignment ETag; it no longer represents a perpetually fresh observation with an unchanged publication. Real carrier evidence remains separate from presentation fixtures.

The final reassessment traces clock subtraction and sample ordering, repeated/equal/regressing publications, context and boot changes, hidden-page/BFCache invalidation, bounded incarnation confirmation, restored action lookup, uncertain POST results, explicit reference retirement, priority Pause and manager retirement. It found no further actionable findings within the reviewed scope. This is a bounded source and software-fixture assessment, not proof of the absence of all defects.

## Validation

Linux checks use `wsprrypi-ci-phase5:latest`, `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, WsprryPi at `/work/pi`, read-only intended Pico sources at `/work/pico` and read-only Mbed TLS at `/work/mbedtls`. The selected dependency is pinned at `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Application and Fleet carrier fixtures use private loopback in `--network none` containers. No live endpoint is involved. Commands below run in `/work/pi/src` unless stated otherwise.

```sh
SIM='BACKENDS=simulated ANCILLARY_GPIO=0 SUDO='
OBJ='OBJ_DIR_DEBUG=build/fleet-operations-linux/obj BIN_DIR=build/fleet-operations-linux/bin'
NET='WTP_NETWORK_BUILD_DIR=build/fleet-operations-network'
PAIR='PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/work/mbedtls'
```

| Check | Exact command | Result/evidence |
| --- | --- | --- |
| Candidate build, Fleet owners/capacity, status/application/API | `make -j4 debug wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build wtp-status-test wtp-application-test wtp-api-cpp-test $SIM $OBJ $NET WTP_STATUS_BUILD_DIR=build/fleet-operations-linux/status WTP_APPLICATION_BUILD_DIR=build/fleet-operations-linux/application` | Passed; `src/build/fleet-review-3-production.log` |
| Actual TLS/runtime | `make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-review-3-runtime $PAIR` | Passed: 916 production checks, 34 actual TLS cases and Plain TCP isolation; production log |
| Actual network and configuration pair | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; production log |
| Actual Pico USB handlers | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; production log |
| Pi USB framing/client and actual management pair | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; production log |
| Actual application HTTP/browser/INI, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed: seven source checks, three invalid-INI refusals and both finite simulated browser-load profiles; `src/build/fleet-review-3-application.log` |
| Actual network Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; `src/build/fleet-review-3-feature-pairs.log` |
| Actual framed USB Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Shared control contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; generated sources unchanged |
| UI component, from WsprryPi-UI | `npm test`; final expanded unit regression: `node tests/fleet_client_test.js` | Passed; `/private/tmp/fleet-review-3-ui.log` and final unit output |
| Operations desktop/mobile render and workflows | `node tests/fleet_operation_ui_test.js` | Passed; 36 captures; `/private/tmp/fleet-review-3-operations.log` |
| Existing Setup workflow | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-review-3-setup.log` |
| Handbook, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed: consistent dependencies, one extension test and warning-free strict Sphinx build; `/private/tmp/fleet-review-3-docs.log` |
| Handbook rendering | `node /private/tmp/fleet-doc-review.js` | Passed: four Fleet pages at desktop/mobile sizes; `/private/tmp/fleet-review-3-doc-visual.log` |
| Source hygiene | `git diff --check`, `git diff --cached --check`; `node --check` on all four changed JavaScript files | Passed; no staged changes |

The Fleet capacity check includes 32 exact logical 60-second launches, 30 exact logical 180-second launches, slow/failed-peer isolation, priority Pause, retirement/recovery and bounded snapshots. Status/recovery contributes 3,804 checks and application contributes 1,356 checks. TLS runtime uses a native container directory to avoid the macOS bind-mount certificate-copy limitation already documented in prior evidence. Expected negative/fault-injection diagnostics do not represent final failures. The harmless host pip-cache permission warning disables only caching. The original implementation's full Linux semantics suite was not repeated for this focused review; current evidence is the focused and actual carrier checks listed here. No standalone reusable-library tests are needed because no reusable library changed.

Impeccable review preserved the existing Bootstrap layout, type and themes. Operations rendered at 1440×1050 and 390×844 in both light/dark themes. New `cached-stale` and `controller-changed` scenes supplement existing confirmations, progress, foreign/unavailable evidence, uncertain-result review, recovery and replacement checks. The fixture checks disabled-label contrast of at least 4.5:1 and page overflow. Actual new captures inspected (the final receipt-timing iteration regenerated all 36 captures and its cached-stale dark/mobile and controller-changed light/desktop captures were inspected again): `cached-stale-light-desktop.png`, `cached-stale-dark-mobile.png`, `controller-changed-dark-desktop.png` and `controller-changed-light-mobile.png`, under `src/build/fleet-operations-ui`. Error text stays beside the controls; stale data does not claim current output-off. Setup retains its existing modal/cancellation/Escape/draft/conflict checks and theme/mobile captures. The existing handbook freshness/troubleshooting paragraph was visually inspected at mobile width; its policy already matches the corrected behavior.

## Documentation Impact

- Updated `docs/wtp-fleet.md` with conservative sample order, retained browser receipt timing, repeated-publication age handling, bounded incarnation confirmation and restored action lookup.
- Considered but unchanged: WsprryPi-Docs Operations, Setup, troubleshooting and REST documentation. Existing operator guidance already states that HTTP refresh alone cannot renew stale evidence; controls and workflow are unchanged. The strict handbook build and applicable extension test were rerun, and four Fleet pages were rendered at desktop/mobile sizes. Existing screenshots remain accurate.
- Pico control/protocol documentation remains unchanged because the companion contract and implementation did not change.
- No operator documentation follow-up is required for these corrections. Physical qualification requires separately authorized evidence.

## Delivery and limitations

WsprryPi remains on devel with this uncommitted follow-up, including the first-party UI component. WsprryPi-Docs and WsprryPico remain clean on their existing devel branches. Staged diffs are empty. Nothing was committed, pushed, published, installed or operated on live hardware. No CI workflow was invoked and no new CI link exists. These checks do not qualify physical USB, deployed networking, installation, services, Pi timing, GPIO, I²C, frequency accuracy or RF output.

## Tested source manifest

Manifest SHA-256: `664f12bc204cf207e090f638ea26603a89adc7df0f1f8af30622050d1c43dcc5`. Paths are relative to `/Users/lbussy/GitHub`. This includes every edited/new source and engineering documentation file except this report, avoiding a self-referential digest. Unchanged sources come from the base revisions above. Ignored build outputs, captures and temporary logs are not source changes.

```text
a473c57173a8a9816ee14bc68cf7630c278b5ac0e9ade7e011c8a15cb46dfbdc  WsprryPi/WsprryPi-UI/data/fleet-client.js
92c5210e1050b93f61df47c7f2e5b1dab6413e8d8fac5136e7c7a3fe1c61aa81  WsprryPi/WsprryPi-UI/tests/fleet_client_test.js
a6a2465b610e119eb65739165dddccbaa9684e5de78455b9152386b964a3d2aa  WsprryPi/WsprryPi-UI/tests/fleet_operation_ui_test.js
8661d19acf54190a128a549d6bf137735763687299b0e149e8a8e6fa46549de7  WsprryPi/WsprryPi-UI/tests/wtp_pi_fleet_ui_integration_test.js
c214b95fd5e3d081a6e21d55dd32f4ae03e7a65f201c54876dffcc6eb1329f24  WsprryPi/docs/wtp-fleet.md
c4255a519eb9d3090caa2282a9c20b48e085e1b520aff1f30b1fe9509a5d3e61  WsprryPi/src/tests/wtp_fleet_test.cpp
b5e300e4370fd88424b5d93030132b7fd90f5f6bd39097c913000b27b72535b9  WsprryPi/src/wtp_integration/fleet_observation_age.hpp
a490c8e7fbbf9e37833ba58ecb5f57af9fd7d7af3de096e82357073349dad22c  WsprryPi/src/wtp_integration/fleet_runtime.cpp
```
