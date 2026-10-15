# Phase 15.3 management carriers: companion implementation and review

Status: implementation and local software acceptance passed. October 15, 2026.
Final required CI outcome, remote parity and joint P15.3 closure are retained in
the [lead acceptance record](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/phase15-3-management-carriers.md); this companion record preserves the implementation and local evidence. Pico owns the [carrier/access matrix and discovery contract](https://github.com/WsprryPi/WsprryPico/blob/d6208dbc88005383041c5ec306b738db7ec22796/docs/development/phase15-3-management-carriers.md).
P15.4 and later work has not started. No hardware, installed service, GPIO or RF
operation was performed or is qualified by this record.

## Baseline and scope

Pi devel began clean at `6a39398bb23d69ba0628e1b82171717040115ec8`. Pico devel
was `b013504aea54d2f9474da0cbae1678c69fb98def`, including the independently
committed RF-adapter fixture repair. Task implementation stayed on devel in
both repositories. Later concurrent Pi release/calibration, PPM/chrony, scheduling and UI work,
and Pico's roadmap status edit, were preserved outside the task changes.
Pi HEAD advanced to documentation-only `534328d7dd37955bc8dbc8d06e4310ac350b3d3b`
during validation; it changes no compiled source or fixture.

The reviewed Pico dependency comprises `c50382e` plus formatting-only `d6208db`,
which was cross-linked and pushed before companion publication. The strict
source guard uses clean full-SHA checkouts and clean Mbed TLS
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The unchanged WTP/1 contract remains
a separate authority.

Modified component: `WsprryPi-UI`; parent adapter/runtime under
`src/wtp_integration`, `src/wtp_runtime_bridge.cpp`, protected route policy,
Makefile, network/runtime tests and CI source-pair matrix. No reusable
WTP-Client, WTP-Server, WSPR-Transmitter or reference library source changed.

## Result

The existing engineering mTLS path is preserved. An explicit volatile consumer
station/AP selection uses local OpenSSL X25519/SHA-256/HKDF/ChaCha20-Poly1305
Consumer-Radio/1 code and the existing operation mutex, application idle gate
and configuration coordinator. It never executes device JavaScript, moves
sessions between authorities/interfaces, follows redirects, retries an uncertain
mutation or substitutes an authentication/carrier strategy.

Management-only recovery requires the configured full expected member ID and
an otherwise replaceable host authority; it authenticates device/boot/profile
generation without requiring a WTP connection. Prior owned/pending/faulted/
uncertain output still refuses. Pico status and full resulting-configuration
validation remain authoritative. Profile-owned station/Wi-Fi fields are refused;
their existing transactional setup paths remain separate. USB JSON is explicitly
unsupported, and raw Plain LAN WTP, TLS-WTP and BLE gain no JSON administration.
Engineering field HTTPS remains firmware-supported with no selected Pi client.

The host exposes only a finite protected management selection and existing
configuration operation set. Host built-in capabilities are identified as host
projections, while the independently versioned remote contract is consumed only
after authentication. Old closed `authenticated_https` metadata is preserved.
Absent discovery preserves the explicitly selected engineering compatibility
path; unsupported/malformed present discovery refuses.

Login passphrases are transient, never catalog/INI/URL/status/browser-storage
content. Their parsed string is cleansed on accepted and rejected requests;
ordinary HTTP request/parser buffers retain their request lifetime. Traffic keys
remain in RAM with idle/absolute expiry; disconnect only closes this client's
session. An occupied browser slot returns `radio_session_busy` without eviction.
Group-passphrase impersonation and active fake-endpoint offline password guessing
remain documented Consumer-Radio/1 limitations; no PAKE or unique-device trust
claim is made. Protect the browser-to-Pi trusted-LAN operator connection too.

The UI preserves settings/password drafts and saved/active/restart distinctions,
reads local connection state after reload, disables duplicate Connect and
requires authoritative settings readback before another save after uncertainty.
Carrier selection remains separate from job transport; AP use requires existing
operator-established network reachability.

## Adversarial review and repairs

The first independent read-only review found five actionable issues, plus a
lost-reply fixture defect:

1. Existing host sessions were forgotten on page reload/double Connect. Added
   protected state readback, preserved already-selected state and disabled
   duplicate Connect; independent reload and unit checks now pass.
2. The consumer executable was built but not run by CI. Current config-interop
   Make now executes it with fixed vectors; historical profiles retain their
   original API paths.
3. Rejected endpoint/already-session requests missed parsed-password cleansing.
   Installed the guard immediately after parsing a string credential.
4. Pico station mTLS availability omitted the UTC gate. Matched clock/admission/
   actual-listener predicates and preserved field preclock Configure refusal.
5. Trust prose omitted active fake-endpoint offline guessing. Corrected both
   protocol and client documentation without changing the protocol.
6. The fixture originally dropped a later read instead of the mutation reply.
   It now drops the encrypted write response after journal commit through the
   actual coordinator, then verifies no replay, expiry/login, authoritative
   readback, changed revision and stale-write refusal.

The second independent assessment found no remaining actionable code defect in
its bounded pass and approved the provisional Pico dependency. It independently
ran UI unit checks/reload restoration and the Impeccable detector (no findings).
The final bounded reassessment is recorded below.

## Validation ledger

Software environment: existing Debian ARM64 qualification image, GCC 14.2.0,
CMake 3.31.6, Node 20.19.2, OpenSSL 3.5.6. Sources are isolated copies; tests use
network-disabled containers and loopback. Required Python NumPy/jsonschema
fixtures were supplied in a separate container without repository/hardware
mounts; final execution has networking disabled. Retained pinned Noble packages,
Mbed TLS/lwIP were reused. No SDK was downloaded or tool installed on the host.
Pico Arm cross-link used retained SDK `079c6f39023649b154152db30f1d781e884879bc`
and Arm GNU 15.3.1; host build helpers were reused by that existing build.

Preliminary results: Pico 187/187 full host tests, seven focused checks, six
ASan/UBSan checks, unchanged WTP validator and RP2350 cross-link pass. Pi focused
config/API checks, UI `npm test`, rendered workflows and actual native fixed
vectors/consumer station/AP/coordinator/journal checks pass. Existing runtime
277-check run passed; expanded runtime assertions and final exact source-pair/
Pi sanitizer results are recorded below when complete.

The genuine consumer fixture drives actual native Pi packets into actual Pico
ConsumerRadioApi/Mbed TLS and BrowserApi under WtpApplication's management gate.
Its transport is in-process, not physical lwIP routing. Existing Pico adapter/
interface tests and engineering loopback tests complement it; this does not
establish installed proxy/service, Wi-Fi/AP routing, target timing or RF behavior.

## Documentation Impact

Updated: adapter/credential contract, configuration compatibility, this review,
Pico carrier matrix/versioned discovery, browser API transport drift, shared
application carrier references, USB decision and development index.

Considered unchanged: WTP/1, Config v1 storage/Console replacement policy,
profile/setup transactions, recurrence/STOP/cursors, existing field authentication
and the independent P15.1/P15.2 closure evidence.

Operator repository `WsprryPi-Docs` was inspected read-only and remains outside
write/publication scope. Exact follow-up for P15.14:
`docs/User_Interface/Setup/Transmitter/index.md#pico-development-controls` must
explain management selection independent of job transport, full expected member
ID, consumer station/AP authority and existing AP reachability, transient login,
busy/expired-session recovery, explicit settings readback, unsupported USB and
saved/active/restart distinctions. Review
`docs/Advanced_Operations/ini_configuration/transmitter_backends.md#pico-wtp`
for consistency: no durable secret or new WTP/INI credential is introduced.
Replace screenshots only where the new connection controls materially change
those instructions.

Physical acceptance remains separate: identify an exact Pico 2 W image/profile,
Pi client/service revision and station/AP NIC/authority, then verify the new
native carrier against that physical setup with finite restoration. This task
performs no such acceptance and makes no unrestricted security/availability claim.

## Final local software acceptance (2026-10-15)

The final bounded independent reassessment found no remaining actionable
implementation finding. All six first-round findings were repaired and their
affected checks rerun. It separately verified the exact-source fixture, source
guards, current CI invocation and task-only 289-check runtime result. CI is a
separate acceptance step in the lead record.

Exact clean Pico source pairs, with this companion implementation:

| Profile | Pico commit | Result |
| --- | --- | --- |
| current | `d6208dbc88005383041c5ec306b738db7ec22796` | Engineering network/config, modern and unchanged old producer, plus genuine consumer fixture passed |
| P15.2 | `87144d1364f36d04bfe0a714bd1c2f6deb974302` | Original modern/old-producer P15.2 regressions passed |
| extended | `dfffdd5220db8b7cf1acf5e74950dbd294283c55` | Original new-client/extended-server regression passed |
| legacy | `95aac22bf7cc31b67fe5773761708d743d8a6473` | Original new-client/legacy-server regression passed |

The original four P15.2 combinations remain exercised; the current profile adds
new-client/new-discovery and old-client/new-discovery coverage. Clean Pico and
Mbed TLS source guards remain mandatory. The Pi source used for final runtime
validation was baseline `6a39398` plus the explicit task file inventory and only
three task Makefile hunks; concurrent PPM/chrony/scheduling/UI/release work was
excluded from its compiled sources and will remain outside the task commit.

Reproduction commands inside the identified isolated Linux environment (Pico
from its root; Pi from `src`; UI from `WsprryPi-UI`):

```sh
cmake -S . -B build/p153 -DCMAKE_BUILD_TYPE=Debug -DWSPRRY_PICO_BUILD_TESTS=ON \
  -DWSPRRY_PICO_TEST_MBEDTLS_PATH=/deps/mbedtls -DWSPRRY_PICO_TEST_LWIP_PATH=/deps/lwip
cmake --build build/p153 --parallel 2
ctest --test-dir build/p153 --output-on-failure
python3 scripts/validate_wtp_contract.py
make wtp-config-test wtp-api-test SUDO= WTP_NETWORK_BUILD_DIR=/tmp/p153-config-final
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 wtp-network-runtime-test \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= WTP_NETWORK_BUILD_DIR=/tmp/p153-network
make wtp-network-interop-test SUDO= WTP_NETWORK_BUILD_DIR=/tmp/p153-current \
  PICO_SOURCE=/work/pico-current MBEDTLS_SOURCE=/deps/mbedtls
make wtp-config-interop-test SUDO= WTP_NETWORK_BUILD_DIR=/tmp/p153-current \
  PICO_SOURCE=/work/pico-current MBEDTLS_SOURCE=/deps/mbedtls
# For each legacy/extended/p152: configure/build the same CMake fixture with
# PICO_COMPAT_PROFILE and that exact clean source, then run the same Node driver:
node ../WsprryPi-UI/tests/wtp_configuration_interop_test.js /tmp/p153-p152 p152 /work/pico-p152
npm test
node tests/wtp_network_ui_integration_test.js
```

Results: full Pico host suite **187/187, no skips**; seven focused Pico checks;
unchanged WTP validator **23 schema, 7 raw JSON, 1 framing, 8 transition**;
Pi configuration/API checks; task-only production runtime **289 checks**; all
four exact-source profiles above; UI unit and browser integration checks passed.
Initial full-suite failures were missing Python fixtures/fixture user and were
rerun successfully after environment repair. The earlier RF-adapter stand-in
failure is fixed by the pre-existing baseline commit, not this task.

Six affected Pico tests and Pi config/API tests passed ASan/UBSan using
`-fsanitize=address,undefined -fno-omit-frame-pointer`. Retained lwIP is
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`, Mbed TLS is
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The existing RP2350 cross-link
`cmake --build build/consumer-lan-pico --parallel 2 --target WsprryPico`
passed with retained SDK `079c6f39023649b154152db30f1d781e884879bc` and Arm GNU
15.3.1, including post-link topology, pool/heap-hook and stack-guard checks. This was a cross-build; no
native macOS C++ application build or physical firmware load occurred.

Impeccable was used before and during UI changes. Two bounded visual batches
covered desktop/mobile light/dark entry, connected and busy states. The first
found dark secondary-button contrast; scoped existing-theme colors repaired it.
The second confirmed the affected layout and feedback, with no horizontal
overflow. Reload restoration, duplicate Connect, session-busy, draft preservation
and management selection independent of WTP were exercised. Screenshots are
generated test artifacts under `src/build/wtp-network/ui`; they use a mocked
backend and do not establish an installed service or physical routing.

Local evidence logs are retained outside version control in
`/private/tmp/wsprrypico-p153`: `pico-final-full.log`, `pico-sanitize.log`,
`wtp-contract.log`, `pico-arm-format.log`, `pi-final-focused.log`,
`pi-sanitize.log`, `pi-task-only-runtime.log`, `pair-current-network.log`,
`pair-current-config.log`, `pair-{legacy,extended,p152}-test.log`,
`ui-final-unit.log` and `ui-final-browser.log`. These temporary files are
supporting local evidence; the committed commands, pins and assertions are the
reproducible record.

The actual native consumer/OpenSSL-to-Pico crypto/coordinator integration also
passed ASan/UBSan without diagnostics, including fixed vectors and all fixture
negative cases (`consumer-sanitize.log`). The isolated task-only UI `npm test`
passed after reusing retained `ws` and other JavaScript dependencies; its initial
missing-module failure was environmental and was rerun.

## Publication and final CI boundary

Implementation commit: `a5605c54630d501dc84b51ea7834f59768a250ef`. Its source
was checked byte-for-byte against the isolated task-only compiled source before
commit. It depends on clean Pico `d6208dbc88005383041c5ec306b738db7ec22796`.
Independent `ls-remote` confirmed its initial origin/devel parity.

[Run 38764100628](https://github.com/WsprryPi/WsprryPi/actions/runs/38764100628)
passed all three historical profiles, strict I2C and the existing macOS CI
profile, then was cancelled by concurrent commit
`c605142e45057080ade6a0905fb6b81281e3a367`. The other three jobs were cancelled,
not accepted as passes. The successor run is superseded by this record-only
publication; no full CI success is inferred from either partial run.

The final companion revision includes that independent Chrony work unchanged.
Its resulting CI must complete before the lead acceptance record closes P15.3.
No source pin is weakened or changed by this documentation publication. Future
record-only Pico closure commits are distinct from the tested implementation pin.
