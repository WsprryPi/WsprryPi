# Fleet Operations fourth adversarial reassessment

Review date: 2026-11-13. This assessment starts from the committed third reassessment. It covers owner/recovery and admission boundaries, publication and action correlation, Setup lifecycle, editor restoration and target binding. Five actionable UI findings were reproduced and corrected. The final bounded reassessment found no further actionable findings. Validation is recorded below; no staging, commit or push occurred.

## Candidate

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | cb8924eb5377f1101344b89b5ca4a826087cd7d4 | Base plus the tested source manifest below |
| WsprryPi-Docs | devel | 0ae72c0d0d9353b2b09dc4bb40adcdc6f4298018 | Base plus Setup guidance |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged intended companion source |

Production changes are limited to the first-party WsprryPi-UI Setup controller and a scoped disabled-button selector in the existing stylesheet. Parent and companion runtime sources, shared Control/1 advertisements/schemas, WTP, carrier and storage protocols, server mutation contracts, and named reusable libraries are unchanged. No new design decision is needed: a replaced draft remains inspectable and cannot inherit replacement authority, as the approved plan already requires.

## Findings closed

| Finding | Correction and evidence |
| --- | --- |
| Setup stayed closed after a persisted page return. | The shared Fleet client resumed, but the Setup subscription ignored subsequent snapshots and its handlers remained closed. Setup now reopens, invalidates its cached projection and resumes full reads on persisted pageshow. The regression changes an assignment while away, then asserts the accepted new name is rendered, the draft/ETag are retained, manual refresh works and no POST is retried. |
| Draft restoration allowed competing editor actions while awaiting the catalog. | Restoration now owns the existing busy guard, prevents New/Edit/Save races, checks lifecycle/visibility before installing data, and releases the guard on success or failure. A held catalog read reproduces the earlier enabled-control gap; the regression verifies blocked competing actions and the original target, values and revision afterward. Restored headings and Save labels follow the retained operation. |
| A restore error could be rendered inside a still-hidden editor. | Restoration now reports pending work visibly and puts a failure beside visible controls, retaining the draft. The isolated regression starts with empty feedback and a closed editor, fails the catalog read, and verifies a visible error, retained storage and no unpopulated editor. |
| A new-assignment draft could silently follow a changed catalog profile. | Drafts now retain a frozen profile binding: profile ID/name and the existing settings values, with settings-key order normalized. No credential file contents are read or stored; these are the same configuration references already present in assigned-row drafts. Changed or missing bindings show inspectable entries with the original output identity (or explicit unavailable identity), disable target changes and Save, and require explicit discard/reopening for renewed review. The regression changes the device ID behind the same profile ID/name, verifies no POST and retained storage, checks a missing binding also cannot infer authority, and verifies the unchanged original profile restores its original entries. A placeholder prevents an absent selection from silently choosing the first profile. |
| Disabled draft Save text faded into its background. | Visual inspection found the primary button still used Bootstrap's 0.65 disabled opacity. The editor now shares the existing Fleet row disabled-control palette. Rendered checks require opacity 1 and label contrast at least 4.5:1 in both themes and sizes. |

Expected pre-fix failures are retained in `/private/tmp/fleet-review-4-return-red.log`, `/private/tmp/fleet-review-4-draft-red.log`, `/private/tmp/fleet-review-4-draft-error-red.log`, `/private/tmp/fleet-review-4-profile-red.log` and `/private/tmp/fleet-review-4-contrast-red.log`. They are not final validation successes. The original Setup fixture returned its mutable backend objects directly; that aliasing initially masked the closed subscription. Replies now return independent structured clones, matching real JSON parsing, and the stale-view regression fails without the lifecycle correction. An earlier error assertion also saw feedback from a prior scenario; resetting that feedback isolated and reproduced the hidden-error defect.

The final source pass follows successful, failed and held restoration; persisted page departure/return; immutable HTTP samples; changed, missing and unchanged profile bindings; explicit discard; preserved fields; current action guards; and server retirement/recovery boundaries. It found no further actionable findings within the reviewed scope. This is a bounded source and software-fixture assessment, not proof that all defects are absent.

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
| Candidate debug, feature driver and parent API | `make -j4 debug wtp-fleet-operations-driver-build wtp-api-cpp-test $SIM $OBJ` | Passed; `src/build/fleet-review-4-production.log` |
| Actual runtime/TLS | `make -j4 wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-review-4-runtime $PAIR` | Passed: 916 production runtime/configuration checks, 34 TLS cases and Plain TCP isolation; production log |
| Actual network and configuration suites | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; production log |
| Actual Pico USB handlers | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; production log |
| Actual USB management/framing and settings pair | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` then `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; production log |
| Actual application browser/INI, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed: seven source checks, invalid-INI refusal and both finite simulated browser-load profiles; `src/build/fleet-review-4-feature-pairs.log` |
| Actual network Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Actual framed USB Fleet pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; feature-pairs log |
| Shared contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed; no generated changes |
| UI component, from WsprryPi-UI | `npm test` | Passed on final sources; `/private/tmp/fleet-review-4-ui.log` |
| Rendered Setup | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-review-4-setup.log` |
| Rendered Operations | `node tests/fleet_operation_ui_test.js` | Passed, 36 captures; `/private/tmp/fleet-review-4-operations.log` |
| Documentation, from WsprryPi-Docs | `source docs/.venv/bin/activate`, `python -m pip check`, `python -m unittest discover -s tests -p 'test_*.py' -v`, `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed: consistent dependencies, one extension test, strict warning-free Sphinx build; `/private/tmp/fleet-review-4-docs.log` |
| Documentation rendering | `node /private/tmp/fleet-doc-review.js` | Passed: four Fleet pages at desktop/mobile sizes; `/private/tmp/fleet-review-4-doc-visual.log` |
| Hygiene | `node --check` on both changed JavaScript files; `git diff --check` and `git diff --cached --check` in Pi and Docs | Passed; staged diffs empty |

The carrier feature pairs retain the shipped browser client, actual coordinator, actual Pico TLS/framed handlers and independent simulated Pi target, including selected-output isolation, committed Enable, priority Pause, owned cleanup and failed-manager replacement/restoration. Canonical USB separately covers UMP framing/ACK, protected requests and real management services. Expected negative/fault-injection runtime diagnostics are not final failures; all listed commands exited successfully. The benign host pip-cache permission warning disabled caching only. Full Linux semantics and standalone owner/capacity fixtures were not repeated for this UI-only correction; no parent/companion runtime or reusable library changed, and current actual application/runtime/carrier checks are listed above.

Impeccable review preserves the existing Bootstrap design and palette. Setup renders at 1280×900 and 390×844 in both themes; Operations renders at 1440×1050 and 390×844. New `changed-draft-light/dark` and `persisted-return-light/dark` variants have full-page and editor-detail captures under `src/build/wtp-pi/ui`. The final dark/mobile and light/desktop changed-draft details were inspected after the CSS correction; the persisted-return light/mobile and dark/desktop details were also inspected. The test checks page overflow and disabled editor labels at opacity 1 and contrast at least 4.5:1. Existing keyboard confirmation, Escape, cancellation, draft/conflict, capacity and independent-target workflows remain covered. Persisted-page coverage dispatches the production pagehide/pageshow lifecycle events in real Chrome; it does not claim proof that a particular browser navigation was admitted to its native back/forward cache. The updated Setup handbook paragraph was inspected in the current mobile render, with no overflow; the incumbent handbook is light-only. Screenshots are fixture evidence, not shipped replacement assets.

## Documentation Impact

- Updated WsprryPi `docs/wtp-fleet.md`: exclusive draft restoration, visible errors, immutable new-profile bindings, inspectable refusal and persisted-page read resumption.
- Updated WsprryPi-Docs `docs/User_Interface/Setup/index.md`: changed/unavailable saved-output review, retained inspectable entries, disabled Save and explicit reopening before renewed review.
- Considered but unchanged: Operations, troubleshooting, REST and Pico control/protocol documentation. Controls outside the editor, wire contracts and recovery behavior are unchanged; existing operator screenshots remain materially accurate.
- No further operator documentation change is required for these fixes after the recorded build/render checks. Physical qualification requires separately authorized evidence.

## Delivery and limits

WsprryPi and WsprryPi-Docs remain on their existing devel branches with only these uncommitted changes. The modified application component is WsprryPi-UI; no reusable src component changed. Pico remains clean on devel. Staged diffs are empty. Nothing was committed, pushed, published, installed or operated on a live device. No CI workflow was invoked and no new CI link exists. Software fixtures do not qualify physical USB, deployed networking, services, installation, Pi timing, GPIO, I²C, frequency accuracy or RF output.

## Tested source manifest

Manifest SHA-256: `fef656398e9d4c7f37730d3a747f8b58e35497608778782105abf35205d15fa6`. Paths are relative to `/Users/lbussy/GitHub`. This includes every edited/new source and documentation file except this report, avoiding a self-referential digest. Unchanged sources come from the base revisions above. Ignored captures, build outputs and transient logs are not repository source changes.

```text
83d430d3d12e5f1627194906fe04898049c04457597ce914fdc77766fb2143de  WsprryPi/WsprryPi-UI/data/fleet-schedules.js
0900bd3662d6d5308a14dc128890f790f699a355aaf94d38e08a0f83bc4b0746  WsprryPi/WsprryPi-UI/data/site.css
3335b65eac69d0193fd4d32f72229f1f44ff6ab7b0ffa277c27de90493402a59  WsprryPi/WsprryPi-UI/tests/wtp_pi_fleet_ui_integration_test.js
43e0f5b3de82ef3bd8489e252f5b731bb838058beda65eb36dae71a10445e91e  WsprryPi/docs/wtp-fleet.md
02d54fd11ef2d61863b15d0d99d495ce719f01b60aadadad73490ea653f858fb  WsprryPi-Docs/docs/User_Interface/Setup/index.md
```
