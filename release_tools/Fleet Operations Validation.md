# Fleet Operations implementation validation

Validation date: 2026-11-13. This report records local hardware-free candidate evidence. No staging, commit, push, CI invocation, publication, deployment, service operation or live hardware operation occurred.

## Candidate and scope

| Repository | Branch | Base revision | Candidate |
| --- | --- | --- | --- |
| WsprryPi | devel | 99c5af0fb717d572f1f3cb7493a5dd9ab1238f58 | Base plus the uncommitted files hashed below |
| WsprryPi-Docs | devel | 22225464299363fb7e4386bd81faa0faabfa5f2f | Base plus the four uncommitted handbook files hashed below |
| WsprryPico | devel | b4e966797cf016f6b8ed873dbe638522fc4d81c7 | Clean, unchanged source selected explicitly for both production pairs |

The first-party UI component changed. Parent application, API, Fleet store/runtime, build recipes, behavioral fixtures and engineering documentation changed. No reusable component under a named library root changed. Pico WTP, shared Control/1, capabilities and firmware behavior remain unchanged; controller-only observation, action and recovery metadata required no companion schema change.

The Operations selector preserves This Pi as the initial view and isolates remote views/actions from local handlers. Fleet owners publish bounded cached observations, eligibility and job-bound timing/frequency evidence. Mutations use asynchronous per-context execution, short revocable dispatch permits, strict review/correlation guards and a bounded replay ledger. Frozen retirement authority is persisted independently before loss of an assignment binding. Setup shares the browser read/correlation coordinator and preserves editor drafts and their original authority.

## Environment and reproducible commands

Linux execution used the existing `wsprrypi-ci-phase5:latest` Docker image, with `/Users/lbussy/GitHub/WsprryPi` mounted at `/work/pi`, `/Users/lbussy/GitHub/WsprryPico` read-only at `/work/pico`, and `/private/tmp/gray-line-mbedtls` read-only at `/work/mbedtls`. Mbed TLS was verified at the workflow-pinned commit `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Every Linux application fixture used `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`. HTTP and feature-pair fixtures used `--network none` with container-private loopback; TLS interoperability used isolated local fixture services, never production hosts. Browser fixtures used owned temporary Chrome profiles and loopback PHP servers with bounded cleanup.

Commands below use these shell arguments from `/work/pi/src` unless a different directory is shown:

```sh
SIM='BACKENDS=simulated ANCILLARY_GPIO=0 SUDO='
OBJ='OBJ_DIR_DEBUG=build/fleet-operations-linux/obj BIN_DIR=build/fleet-operations-linux/bin'
NET='WTP_NETWORK_BUILD_DIR=build/fleet-operations-network'
PAIR='PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/work/mbedtls'
```

| Check | Exact command | Result and retained log |
| --- | --- | --- |
| Candidate application and focused suites | `make debug wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build wtp-status-test wtp-application-test privileged-network-policy-test $SIM $OBJ $NET WTP_STATUS_BUILD_DIR=build/fleet-operations-linux/status WTP_APPLICATION_BUILD_DIR=build/fleet-operations-linux/application` | Passed; 3,804 status and 1,356 application checks; `src/build/fleet-operations-candidate-final-build.log` |
| Final expanded retirement and real HTTP contention fixtures | `make wtp-fleet-test wtp-fleet-capacity-test $SIM $OBJ` | Passed; `src/build/fleet-operations-retirement-and-http-final.log` |
| Shared production HTTP adapter | `make wtp-api-cpp-test SUDO= $NET` | Passed; `src/build/fleet-operations-query-fixed.log` |
| Full Linux semantics profile | `make -j4 semantics-test SUDO= OBJ_DIR_DEBUG=build/fleet-semantics-linux/obj BIN_DIR=build/fleet-semantics-linux/bin` | Passed; full profile with installed libgpiod development headers and hardware inhibition, including backend-specific runtime and cleanup executables; `src/build/fleet-operations-semantics-runtime-final.log` |
| Production TLS runtime | `make wtp-network-runtime-test $SIM $OBJ WTP_NETWORK_BUILD_DIR=/tmp/fleet-runtime-network $PAIR` | Passed; 916 production configuration/runtime checks, 34 TLS cases and Plain TCP isolation; same semantics/runtime log |
| Actual Pico TLS job and settings pairs | `make wtp-network-interop-test wtp-config-interop-test $SIM $OBJ $NET $PAIR` | Passed; `src/build/fleet-operations-protocol-pairs-final.log` |
| Actual Pico USB management build | `cmake -S /work/pico -B build/fleet-operations-pico -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build/fleet-operations-pico --target usb_management_driver endpoint_driver -j2` | Passed; actual selected Pico handlers without SDK or physical USB |
| Pi USB client/framing tests | `make wtp-usb-pair-client-build wtp-usb-management-test SUDO= $NET` | Passed; protocol-pairs log |
| Shipped settings producer through production USB pair | `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js build/fleet-operations-network/wtp_usb_pair_client build/fleet-operations-pico/usb_management_driver` | Passed; protocol-pairs log |
| Real simulated application HTTP/configuration fixtures, from `/work/pi` | `python3 -B src/tests/production_browser_load_test.py` then `python3 -B src/tests/production_ini_roundtrip_test.py src/build/fleet-operations-linux/bin/wsprrypi_debug` | Passed; `src/build/fleet-operations-http-and-pairs-final.log` |
| Fleet network feature pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver network /work/pi/src/build/fleet-operations-network/pico_tls_server /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; `src/build/fleet-operations-feature-pairs-final.log` (final shipped client) |
| Fleet framed USB feature pair, from `/work/pi` | `node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/fleet-operations-linux/bin/wtp_fleet_operations_driver usb /work/pi/src/build/fleet-operations-pico/endpoint_driver /work/pi/src/build/fleet-operations-network/credentials-v3` | Passed; `src/build/fleet-operations-feature-pairs-final.log` (final shipped client) |
| Shared source contract, from WsprryPi | `python3 scripts/generate_control_contract.py --source ../WsprryPico --check` | Passed, no generated changes |
| Network build/dependency/cache recipes, from WsprryPi | `python3 scripts/tests/network_build_test.py` | Passed, 25 tests; `/private/tmp/fleet-network-build-final.log` |
| UI component suite, from WsprryPi-UI | `npm test` | Passed, including new deterministic Fleet browser coordinator tests; `/private/tmp/fleet-ui-unit-final.log` |
| Rendered Operations, from WsprryPi-UI | `node tests/fleet_operation_ui_test.js` | Passed; `/private/tmp/fleet-ui-final.log` |
| Existing rendered Setup/Fleet, from WsprryPi-UI | `node tests/wtp_pi_fleet_ui_integration_test.js` | Passed; `/private/tmp/fleet-existing-ui-final.log` |
| Documentation dependencies, from WsprryPi-Docs | `source docs/.venv/bin/activate` then `python -m pip check` | Passed; benign sandbox pip-cache warning only |
| Documentation extension tests, in the same venv | `python -m unittest discover -s tests -p 'test_*.py' -v` | Passed, one test; `/private/tmp/fleet-doc-tests.log` |
| Clean strict handbook build, in the same venv | `python -m sphinx -E -a -b html -n -W --keep-going docs docs/_build/html` | Passed with no Sphinx warnings; `/private/tmp/fleet-docs.log` |
| Rendered handbook | `node /private/tmp/fleet-doc-review.js` | Passed, four affected pages at desktop/mobile sizes without page overflow; `/private/tmp/fleet-doc-visual.log` |
| Source hygiene | `git diff --check` and `git diff --cached --check` in Pi, Docs and Pico | Passed; staged diffs empty |

There are no CI links because these were local executions of the checked-in recipes. Build outputs/logs/screenshots remain ignored local artifacts, not source content. The manifest below durably identifies the tested edited source independently of those transient artifacts.

## Behavioral and adversarial evidence

The deterministic 32-output fixture uses production Fleet owners, scheduler/client, framing, endpoints and job services with typed clocks and inhibited engines. All 32 ARM acknowledgments and exact logical 60-second launches passed; 29 healthy peers continued while one was held, followed by 30 exact logical 180-second launches. Disconnected and changed-boot peers were excluded. This is deterministic scheduling-contract evidence, not measured real-time or physical timing performance.

The same fixture checks an accepted Enable blocked in persistence, a priority no-op Pause with unchanged ETag, superseded old completion/replay, direct stale control-review refusal, and manager replacement waiting for old writers/identity leases. Its real HTTP server has four workers and handles 24 concurrent full/projection reads plus Pause admission while persistence remains blocked. Cached reads and priority admission finish without waiting for that owner. An independently blocked Pi takeover HTTP observation does not stop the same context's idle WTP STATUS revision from advancing. Healthy foreign occupancy remains observed and does not create a reconnect/recovery loop.

Ledger tests retain protected results, saturate admission, preserve the priority Pause reserve, expire and evict old records, refuse identical expired POSTs even with unchanged ETag, and inhibit new admission after monotonic-clock discontinuity until a new incarnation. Restart fixtures seed retirement before deletion, after deletion, with conflicting frozen endpoints, and with a corrupt journal. They verify preserved assignment boundaries, retained capacity/recovery duty, no guessed target or initial carrier contact, and refusal of unsafe recovery. Source review traced permit locking, frozen bindings, late storage/transport callbacks, manager shutdown, unknown actions and post-Pause positive authority.

Both feature pairs execute the shipped Fleet browser client against the actual C++ coordinator. A selected Pico uses the actual Pico TLS or framed endpoint handler; the independent Pi uses production Pi authority/engine and an explicitly constructed simulated transmission backend. They verify correlated committed scheduling intent, selected-device isolation, Pause/cleanup and controller replacement with rejection of the old binding. The canonical USB management pair separately covers production UMP framing/ACK, crypto/serialization and persistence. The USB Fleet carrier is a pipe fixture, not a physical USB connection.

The browser coordinator test verifies a single blocked read despite forty resume notifications, coalesced polling, rotation through all 64 retained references, ordinary saturation and reserved-stop refusal, explicit retirement of only the reviewed output's unknown references, no automatic mutating retries, exact local 14,999/15,000 ms ages, hidden-page read suspension and BFCache rendering/poll resumption.

Rendered UI review used Chrome 157.0.8103.51 and the incumbent Bootstrap/theme design through Impeccable. Operations evidence covers long duplicate names, scoped selection, local-handler isolation, wrong-context completion refusal, cancellation/focus, pending Enable with a usable Pause path, stale/foreign/unavailable states, authoritative Running during an output-off interval, current-job requested-target acceptance and same-ID assignment replacement. Twenty captures cover Operations, confirmation, stale, foreign and unavailable scenes at 1440×1050 desktop and 390×844 mobile in light/dark themes. Files are under `src/build/fleet-operations-ui`. Disabled-button label contrast is asserted at least 4.5:1 in both themes. Setup/Fleet review additionally exercised confirmation cancellation/Escape, feature visibility, stale revisions, duplicate prevention, takeover choices, draft preservation/editor handoff and independent assignments; evidence is under `src/build/wtp-pi/ui`.

The visual review found and corrected nearly invisible disabled secondary labels in the light Operations view. The evidence review also made current output-active reports explicit, labeled historical frequency realizations separately and named pending Enabling/Pausing/Reconciling progress consistently in Operations and Setup. Representative final desktop/light Operations, mobile/dark confirmation, handbook mobile Operations and desktop REST API renders were inspected. The four handbook pages were rendered at 1440×1000 and 390×844; their existing theme has no dark variant. Evidence is under `src/build/fleet-operations-docs`. Existing local-only Operations screenshots remain accurate and were retained.

Earlier validation failures were resolved and affected checks rerun: a mixed native/Linux ignored object directory was replaced with isolated build directories; a Docker bind-mount permission failure in the TLS certificate-copy fixture was avoided by using native container `/tmp`; build-cache count expectations were updated for the actual added HTTP source dependency; and the slow-peer fixture gained explicit fresh review only after definite pre-admission ETag conflicts; the new retirement assertion was corrected to require the plan's higher-priority Removed headline after deletion. Intentional negative/fault-injection diagnostics in the full semantics/TLS suites are expected and did not fail the final commands. No required check remains failed, cancelled, timed out or skipped.

## Documentation Impact

- Updated WsprryPi engineering guides: `docs/wtp-fleet.md` and `docs/wtp-status-recovery.md` describe freshness, host resources, asynchronous correlation, dispatch ownership, bounded tracking, retirement recovery duty and fixture boundaries.
- Updated WsprryPi-Docs: Operations, Setup, REST API and configuration troubleshooting describe current selector/control semantics, separate schedule/job/output evidence, uncertain results, explicit review/reconciliation and draft/removal behavior.
- Considered but unchanged: Pico WTP/shared-control documentation, because no Pico protocol/schema/capability behavior changed; existing local-only screenshots, because their depicted view remains accurate.
- Still required for this source change: none. Documentation for future physical qualification must be based on its own evidence.

## Limits and delivery state

No physical USB/TCP/TLS deployment, installation, service lifecycle, Raspberry Pi timing, GPIO, I²C, frequency accuracy, amplifier, transmitter-chain or RF qualification was performed. Simulated and inhibited backends qualify software contracts only. No production host was contacted or operated. This report does not claim release or hardware readiness.

Pi and Docs remain on their original devel branches with the requested uncommitted changes. Pico remains clean on its original devel branch. Nothing is staged, committed or pushed. The plan itself remains the approved design authority; its historical planning-status text was not rewritten as implementation evidence.

## Tested source manifest

Manifest SHA-256: `3fc4d8bde505976a75ccc22c058ff443073acae7771693fced733f9666ccd49d`. The manifest includes all changed/new Pi source and documentation plus changed Docs files, excluding this report to avoid a self-referential digest. Paths are relative to `/Users/lbussy/GitHub`. Unchanged files come from the exact base revisions above.

```text
3c886b75d2e44ba459100b8c58aa82dcd145c9990410474e49694bd48ea5ffa3  WsprryPi/.github/workflows/debian-non-hardware.yml
7728d16dd1a879b8bf6c7cab7597f191a07d247fcd3b08210a3556eb8ba3248e  WsprryPi/WsprryPi-UI/DESIGN.md
6909919e8ea1ba46a4040342cd1c822da9e7ac5c034a31b320fce255c374c8b9  WsprryPi/WsprryPi-UI/data/app_state.php
e2e1408eb6052251fec184b67352b22c16a285312237ca53d8f554bc800c7d41  WsprryPi/WsprryPi-UI/data/card_header.php
9eb8faad09e2aec736d8ac042741e08a2534a91ed03982cdd66c12f3efe535f8  WsprryPi/WsprryPi-UI/data/fleet-client.js
2f48500dca4580a7e349e5013307da4e6de9d1cece81b8dc7e4b8f272fb99c69  WsprryPi/WsprryPi-UI/data/fleet-operation.js
1fc770267d64755a63ba9094a06634da04936c77caff3c9e99c2cf86ba40a2aa  WsprryPi/WsprryPi-UI/data/fleet-schedules.js
60a62abe4ead3928a9162e00239d4dba58e64539474c7526be73e2d14e2c20e5  WsprryPi/WsprryPi-UI/data/operation.css
e039f79b071dd1458d89d7327c498c897dcd8825ef14c129fd784c2e2c3ccade  WsprryPi/WsprryPi-UI/data/operation.js
7be6dbcdff0e3e2dd2ffd959ba0199d011e19dd45dd2dda9fb1c242ec5f9433c  WsprryPi/WsprryPi-UI/data/site.js
429ad46e0f4af10051425a8786a71dc79949b15f63593b3c25c19a2d8e81faf6  WsprryPi/WsprryPi-UI/data/views/fleet-operation.php
075d94cc44d3787336d57d2df97303e38cf9ba9cec635d5ca56b0336092bbed4  WsprryPi/WsprryPi-UI/data/views/fleet-schedules.php
6127e8ff2ddff2b471799f82c86faa42a72d4a45f7252a29c175f6e1451a3a2a  WsprryPi/WsprryPi-UI/data/views/operation.php
4334a40292c82d1011d75fc86519dbfc39f5c769c4f772bc52c185eeda839ea5  WsprryPi/WsprryPi-UI/package.json
8cc0f6a659a7089c1470c1055149a6ab89bf0119e6b0d20cad30d55253669d6f  WsprryPi/WsprryPi-UI/tests/fleet_client_test.js
7ff3566cbf9157916da0080aae602dab3bab66a210226488c14d7d66f7eb9547  WsprryPi/WsprryPi-UI/tests/fleet_operation_ui_test.js
899a8ede8bfa56f2be9412da45c07bf8bd3ef75f9fd485474b3deb79aba4a6ce  WsprryPi/WsprryPi-UI/tests/fleet_operations_pair_test.js
779a8f718d516a879d8668ae166adfe6bded64857e8ad36ced99816e5cd2950c  WsprryPi/WsprryPi-UI/tests/wtp_pi_fleet_ui_integration_test.js
1f992dbf0665223ed39a60c0541d419525a804b0f546f2e462591e4308ed1d44  WsprryPi/docs/wtp-fleet.md
6f1a3ff26c9b135cfb24e5148e163b5c1b240bbaf56e94753ed794cb09891349  WsprryPi/docs/wtp-status-recovery.md
04989d170ced14a5906e8f4d70fb818e7e0fdd8f56085dd6f7700ad5b0ef3aa5  WsprryPi/scripts/network_build_rules.mk
e58a8136173d475fe840a4810506fdf5ab1cb40a276db6283ff72282ceb14acc  WsprryPi/scripts/tests/network_build_test.py
ae13ed7992ecee6790700b1d14d7d9d06bcd69b6efb71a193e70169e770b51db  WsprryPi/src/Makefile
810ab9a0badb60aa9bda087c2c096d49eb0f2a545455fb28717215bf8b6ae08e  WsprryPi/src/privileged_network_policy.cpp
71361fbd60f260427d289b3241f9afd153cbb21bed51c3511ba327dc803af327  WsprryPi/src/tests/privileged_network_policy_test.cpp
4bf9da169ea32fc56b8fdcb477eee7202884c5b5ef4131793d6544a667fa2a6e  WsprryPi/src/tests/production_ini_roundtrip_test.py
f1b2eba2d241c8b37df38af655c51cbc3dafe861aa8baf4e0cacf48a560eb4ea  WsprryPi/src/tests/wtp_application_test.cpp
da76317f64444fd45a0719e49201b37e73e4df8f400d3ec0584987d6192b787b  WsprryPi/src/tests/wtp_browser_api_test.cpp
2cdf952662ff2e8bd74d77123938a3a52570ad538cf2e4884b4aa3ada44a6f8b  WsprryPi/src/tests/wtp_fleet_capacity_test.cpp
3302a1fc1eac3f9df13c845eff174e20faf57e8657d236e43c488ef5b2690b38  WsprryPi/src/tests/wtp_fleet_operations_driver.cpp
43485d33ca935af9f8dcd4f7bb1f676e04e60491365ff7cbb07979647e97dd7d  WsprryPi/src/tests/wtp_fleet_test.cpp
119e4fd46a582aeb2e06e2be4a32dc29e35eadb50e2ac44b7a823e072e6f1862  WsprryPi/src/web_server_control_routes.cpp
ecdd717c7d4cd9bea5cd3115c8905ac0a843dc45b7fad7569eefb0323a280e06  WsprryPi/src/wtp_integration/application.hpp
1ab727e0a1e3e4208a7f5ae9b85b75de5a3e66987697415c7a871e2e101498f1  WsprryPi/src/wtp_integration/backend.cpp
6a62effe699b857f7746ca149d0d1f227e491769ac8046a34d3bc72b5cede636  WsprryPi/src/wtp_integration/backend.hpp
d25d42965db2267a080ba6979648db4e5b9ccb471baecf98c22b532b6e365fac  WsprryPi/src/wtp_integration/browser_http.hpp
817db6ef4af42f0a5c88ef562f1753edc373479e7c80b444c3c65cdfc26c37b7  WsprryPi/src/wtp_integration/fleet_runtime.cpp
cd1ca0a5cbead98ed027b34fb892f3436a99f4fe5453b09b0cdee140d5da8a2c  WsprryPi/src/wtp_integration/fleet_runtime.hpp
0201274b9972c74832506e346b3b03bfaa8a8791232f0f822597b919cec507a4  WsprryPi/src/wtp_integration/fleet_store.cpp
64bec0bb247406a65c02bc45f4b7ca1d0d80d41a2908c167d3453ea12d938ca3  WsprryPi/src/wtp_integration/fleet_store.hpp
c083329ce27cbdcbc48e5fc737e886457260950a373257c1ac1c6aabb32565b4  WsprryPi/src/wtp_integration/fleet_test.hpp
899f36ed8cb3c3d432a8a69e1e0419626e6c9060c98d287242cdd6dee495c0a3  WsprryPi/src/wtp_integration/scheduler.cpp
d909f6500ea448cc1a0e90985263466ce2c4b6fa148c2ef010a3f43d65c8302c  WsprryPi/src/wtp_integration/scheduler.hpp
9be15a136831591f6cdc06ac202334d0839c9b4dc1810578e13ffc05c29cecf9  WsprryPi-Docs/docs/Advanced_Operations/configuration_troubleshooting.md
a5427bdfda5f9168eb3748265b83c9fa6e7d14287a584a406f7296e1d5470f17  WsprryPi-Docs/docs/Advanced_Operations/rest_api.md
5dfe6d9180d4cb6ef7717f38ae2538be33bc71bc0b932a5a49745604f5ccb29c  WsprryPi-Docs/docs/User_Interface/Operations/index.md
bde9c19c6382c8fc091a3405e5ea0dd4fcdb98af750f869a367309f899c7a81f  WsprryPi-Docs/docs/User_Interface/Setup/index.md
```
