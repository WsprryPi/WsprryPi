# P15.4 shared member resources

Status: implementation and local software acceptance passed; publication and
pushed-head CI acceptance pending.

## Baseline and source-backed gap assessment

Pi devel starts at `fbb80fa2d1f654c10b8fcf695390144e945e0dd1`;
Pico devel starts at `ddb00dd7960373cec271f7816c0e1b10117416e9`.
Both matched origin/devel at inspection. Pi has an unrelated, preserved edit
to `release_tools/Release Punchlist.md`; Pico is clean. The accepted P15.3 pair
remains Pi `61542aff2e6965b8773053a2429f9b42b863bf98` and Pico
`d6208dbc88005383041c5ec306b738db7ec22796`.

Pico already implements the closed application/station/hardware schemas,
pin choices and nonpersistent validation through BrowserApi, its scheduler and
verified journal. Consumer-Radio and field HTTPS already reach those handlers.
Pi currently reads application only as legacy settings context; it lacks shared
resource routes, typed complete validation, native client allowlists and controls.
No new Pico wire schema or operation-discovery version is needed.

## Selected mapping and negotiation

| Protected Pi route | Authenticated Pico route | Behavior |
| --- | --- | --- |
| GET `/api/v1/member/application` | GET application, station, hardware, pins | Reconciled resource observations |
| GET `/api/v1/member/station` | Same resource read set | Same explicit member context |
| PUT `/api/v1/member/station` | PUT `/api/v1/station` | Complete station, exact target/If-Match |
| GET `/api/v1/member/hardware` | Same resource read set | Saved and active separately |
| PUT `/api/v1/member/hardware` | PUT `/api/v1/hardware` | Complete supported plan, exact target/If-Match |
| GET `/api/v1/member/pins` | Same resource read set | Advertised GP choices; hardware owns active state |
| POST `/api/v1/member/pins/validate` | POST `/api/v1/pins/validate` | Bare complete plan, no remote If-Match, no persistence |

Use the explicitly selected P15.3 carrier, runtime operation mutex and
application idle-management gate. Engineering mTLS authenticates the WTP-bound
member; Consumer-Radio station/AP authenticates device/boot/profile generation
without requiring WTP. No endpoint/authentication fallback is allowed.

Authenticate and validate existing `application_resources` discovery and the
closed application schema. Keep `transmitter-management/1` and its exact
operations unchanged. Keep legacy `authenticated_https` metadata unchanged.
Absent resource discovery makes these controls unavailable while preserving
the legacy editor; malformed/present unsupported discovery refuses safely.

Read station/hardware under the same common ETag and compare saved values and
generations with application readbacks bracketing the operation. Live recurrence
and active observations remain timestamped request observations, not a frozen
status snapshot. uint64 values stay decimal strings. `active_revision` is never
an If-Match token. A host editor context additionally invalidates mutation authorization after
carrier selection or any attempted write; a fresh read is required to save.

## Controls, compatibility and budgets

Station replaces callsign, locator and reported WSPR dBm together. It never
changes Pi station/job settings. Consumer setup owns effective station values;
the resource editor is read-only for that profile. Type 1 encoding restrictions
remain authoritative; extended stored callsigns are displayed without truncation.

Hardware offers direct PIO RF GP, optional active-low button GP and the
onboard/external/disabled indicator with polarity. Choices use Pico GP numbering,
exclusive roles and advertised adapter support. Fixed I2C pairs can be inspected;
I2C RF, amplifier, LPF, drive, calibration and waveform controls are unavailable.
Preview reports validity and operational support separately and never saves.
Saved, active, unknown active, pending restart and application failure are distinct.
No save restarts, reconnects, enables recurrence, claims or transmits.

Scoped write bodies: 1,024 UTF-8 bytes. Pin preview: 2,048 bytes. Resource
responses: 4,096 bytes each; discovery/status retain the existing bounded
16,384-byte reader. Complete Config remains Pico's 1,800-byte authority.
Duplicate keys, unknown fields, numeric identities, impossible shapes and
unsupported versions refuse. There is no fallback to Config/update or full PUT
for an unsupported scoped operation. P15.2 atomic settings, schedule-only saves,
credential drafts and uncertain-write reconciliation remain supported.

## Acceptance gates

Required: UI-produced requests through protected host classification/browser
routing, coordinator and actual Pico handlers over engineering TLS and genuine
consumer station/AP crypto; journal reload and zero-mutation refusals; strict
shapes/targets/revisions; cross-editor/carrier invalidation; preview without
persistence/activation; profile/encoder/pin/ownership/storage/output faults;
dropped-reply reconciliation; legacy/extended/p152/p153 compatibility; active
Make/CI tests; sanitizers; desktop/mobile light/dark rendering; independent
adversarial review, repairs and reassessment; exact committed-source checks and
pushed-head CI/remote parity. Results below will distinguish attempts and passes.

## Documentation Impact

Updated: Pi browser API and compatibility guide, this acceptance record, and
Pico's adoption reference, development index and contract/API pointers. Pico's
roadmap stays ⏳ until acceptance and publication finish, then closes ✅.
Considered unchanged: historical P15.1–P15.3 records, normative schemas, storage,
WTP/1 and field/setup authority.

Operator repository `/Users/lbussy/GitHub/WsprryPi-Docs` was inspected read-only,
including its AGENTS.md. Exact P15.14 follow-up:

- `docs/Advanced_Operations/rest_api.md`: add member resource routes, normalized
  read envelope, complete scoped payloads, common ETag/context headers, preview
  semantics and uncertainty/readback requirements.
- `docs/User_Interface/Setup/index.md` and
  `docs/User_Interface/Setup/Transmitter/index.md`: explain member scope versus
  host/job settings, explicit Load/Validate/separate Save, profile-owned station,
  Type 1 restriction, reported-power metadata, Pico GP choices, unavailable
  adapters, saved/active/null, restart/inhibit, faults, login/expiry and recovery.
- `docs/Advanced_Operations/ini_configuration/transmitter_backends.md`: confirm
  no new persistent management secret, hardware application authority or WTP
  configuration semantics. Replace materially inaccurate screenshots with the
  settled desktop/mobile light/dark workflow; build/render per that repository.

No operator documentation was modified or published. No hardware,
installed-service, RF, unrestricted security or full platform parity claim follows
from this software task. P15.5 concurrent web servicing and later roadmap work
remain outside scope.

## Implementation and review repairs

Modified component: `WsprryPi-UI` (presentation, explicit resource producer and
its tests). Parent changes are limited to the browser/configuration adapter,
finite transport allowlists, runtime admission/selection, protected HTTP parsing,
network/runtime fixtures, Make targets, pins and CI matrix. No reusable
WTP-Client, WSPR-Transmitter, WSPR-Reference or RF-backend implementation changed.
Pico companion changes are documentation only.

Independent resource/runtime and UI adversarial reviews challenged the closed
P15.3 contract, target and carrier substitution, schema/numeric validation,
revision reuse, saved/active claims, profile authority, fallback and actual
fixture routing. Actionable findings and repairs:

1. Legacy/raw alias editors could remain blocked after a scoped save. Fresh
   alias readbacks now reauthorize only the intended editor; all uncertain
   attempts keep other contexts invalidated.
2. The engineering idle coordinator automatically reconnected WTP after a scoped
   save. Scoped station/hardware saves now end detached, including exceptions;
   a deliberate later Load can reacquire idle read authority.
3. Optional indicator polarity could cause a false uncertain result. Readback
   comparison preserves explicit polarity and understands omitted active-high.
4. Nested resource invariants and numeric equality were incomplete. Complete
   schemas, integer types, decimal uint64 bounds, schedules, plan ownership,
   active/saved/restart/storage invariants and exact serialized equality now
   reject malformed/impossible responses, including float substitutions.
5. Capability/profile contradictions and unconfirmed successful replies needed
   stronger refusal. Authenticated feature/profile consistency is checked, and
   post-dispatch malformed results remain explicitly uncertain.
6. A browser selection check could race runtime replacement. All editor paths
   now pass an expected selection to the runtime, checked under the operation
   mutex before dispatch; process-unique opaque editor contexts prevent restart
   reuse. Selection changes cannot silently redirect a draft.
7. Preview was coupled to writable storage, expiry/output-disable cases were
   missing, and an active-plan assertion was tautological. Read-only/fault/null
   observations can now validate an explicit candidate without saving; actual
   expiry, post-commit disable failure and saved GP3 versus active GP28/null are
   asserted on all three carriers.
8. The resource fixture initially manufactured mutation intent. It now forwards
   actual UI-emitted headers into the installed-route request adapter, real
   BackendHttpGuard, WtpBrowserApi and production runtime/coordinator. Negative
   peer, duplicate-header and missing-intent cases prove rejection.
9. Generic host form listeners could autosave Pico drafts. Both delegated
   listeners exclude the management section; rendered DOM input/change events
   prove member/credential/carrier edits do not schedule host autosave while
   ordinary host edits still do.
10. Preview responses lacked the documented 4,096-byte check. The coordinator
    now enforces it before parsing; an actual authenticated oversized preview
    result is refused with no persistence or activation.
11. Historical rows lacked actual resource Load evidence. Every active profile
    now loads through the new UI producer and protected runtime/carrier path,
    verifies unchanged member state and either complete resource observations
    or the precise unavailable refusal, before preserving all legacy save tests.

The repaired UI/source reassessment found no actionable finding. A final
source-pair, sanitizer, documentation and publication assessment follows below.

## Validation environment and commands

Linux validation uses the retained Debian Trixie aarch64 image
`wsprrypico-p153-env:local` (`c7f2a891a6ff`), GCC 14.2.0-19, CMake 3.31.6,
OpenSSL 3.5.6 and Node 20.19.2. The disposable container has networking disabled
and no hardware mounts. No native macOS C++ build, host tool installation or SDK
download was performed. Retained clean Mbed TLS
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` and lwIP
`77dcd25a72509eb83f72b033d219b1d40cd8eb95` were reused. Python NumPy/jsonschema,
PHP, Chromium, retained `ws`/Noble dependencies and a synthetic `pi` account live
only in the disposable validation environment.

The isolated Pi source is baseline `fbb80fa` plus task-owned files, excluding
`release_tools/Release Punchlist.md`. Each source file is compared against the
published task diff before commit; final pushed-head CI is an additional exact
committed-source boundary. Pico's isolated current checkout is clean `ddb00dd`.
CMake's exact clean Pico/Mbed TLS guards are unchanged.

From isolated Pi `src` (`PICO_SOURCE=/work/pico`,
`MBEDTLS_SOURCE=/deps/mbedtls`):

```sh
make wtp-config-test wtp-api-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-network-runtime-test \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= JOBS=2
make wtp-network-interop-test SUDO= \
  PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/deps/mbedtls
make wtp-config-interop-test SUDO= \
  PICO_SOURCE=/work/pico MBEDTLS_SOURCE=/deps/mbedtls
```

Historical rows use separate build directories, exact `/refs/<profile>` sources
and `-DPICO_COMPAT_PROFILE=<profile>` with the same CMake source guard. They reuse
the identical built Pi client, then execute:

```sh
node ../WsprryPi-UI/tests/wtp_configuration_interop_test.js \
  /work/pi/src/build/wtp-network-<profile> <profile> /refs/<profile>
# Additionally for p153, preserving actual consumer execution:
build/wtp-network-p153/consumer_interop \
  /refs/p153/docs/protocol/Consumer-Radio-v1-vectors.json
```

The checked-in `make wtp-config-interop-test` executes those same tests in CI;
its historical matrix includes all four profiles. Current additionally executes
`wtp_resources_interop_test.js`, covering engineering mTLS, consumer station and
consumer AP with storage-failure, actual output-disable-failure and blank/null
resource cases. The normal consumer executable also runs fixed crypto vectors,
actual ownership LOAD/ARM/running/failed-cleanup refusals and no-WTP
management-only admission.

Pico host commands from its clean isolated root:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug \
  -DWSPRRY_PICO_BUILD_TESTS=ON -DWSPRRY_PICO_BUILD_FIRMWARE=OFF \
  -DWSPRRY_PICO_TEST_MBEDTLS_PATH=/deps/mbedtls \
  -DWSPRRY_PICO_TEST_LWIP_PATH=/deps/lwip
cmake --build build-host --parallel 3
ctest --test-dir build-host --output-on-failure
python3 scripts/validate_wtp_contract.py
```

ASan/UBSan builds are separate, with C/C++ flags
`-fsanitize=address,undefined -fno-omit-frame-pointer` and matching executable
linker flags, `ASAN_OPTIONS=detect_leaks=1`, `UBSAN_OPTIONS=halt_on_error=1`.
Pico targets: application_resource_tests, config_update_tests,
pin_allocation_tests, management_carrier_tests, consumer_radio_tests,
consumer_radio_crypto_tests, standalone_tests and consumer_storage_tests;
CTest additionally executes the application/config-update schema checks.
Pi uses `WTP_NETWORK_BUILD_DIR=build/wtp-network-sanitize`,
`WTP_NETWORK_CXXFLAGS='-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer'`
and `WTP_NETWORK_LDFLAGS=-fsanitize=address,undefined` for config/client/API
Make targets, plus instrumented CMake fixtures and the actual resource and
consumer executables. No affected Pico implementation changed, so a new firmware
cross-link is not applicable; host results do not substitute for target linkage.

UI commands from the isolated component:

```sh
npm test
node tests/wtp_network_ui_integration_test.js
```

Impeccable was used before/during implementation with PRODUCT.md/DESIGN.md.
Two bounded render batches covered 1280×900 desktop and 390×844 mobile in light
and dark themes. Final resource images cover loaded, consumer setup-owned/null
active, storage-fault and unsupported states. Root and independent reviewers
inspected wrapping, labels, disabled states, explicit actions and nearby feedback.
The detector returned no findings. Render tests use mocked browser transport
with actual emitted carrier resource fixtures; they are distinct from the actual
protected-route/TLS/crypto interoperability evidence.

### Attempts and limitations

Early test-harness compile attempts exposed missing fixture include/link inputs,
a wrong test state enum and a dangling parsed view in the new dump helper; these
were repaired before successful runs. An initial consumer re-login attempt hit
the actual login throttle because synthetic fixture time had not advanced; the
fixture now explicitly advances time before a new intentional login.

The first Pico host run passed 180/187: seven tests lacked the synthetic `pi`
account or retained Noble dependencies in the isolated environment. After those
environment fixtures were supplied, seven reruns and the complete 187/187 suite
passed. No baseline failure count was copied forward. The first Pi runtime
attempt failed when `std::filesystem::copy_file` met bind-mount permissions on a
generated rotation certificate; generated TLS fixtures were moved to the
container-native filesystem and the full runtime target passed 358 checks.
The network Make build emits its existing nested `-j4` jobserver warning.

No devices, firmware, physical flash, installed proxy/services, NIC routing,
GPIO timing, RF or unrestricted security behavior were exercised. Engineering
Pico HTTP uses real loopback TLS. Consumer OpenSSL/Mbed TLS packets use in-process
transport and actual authenticated handlers; this does not qualify physical
station/AP networking. Protected host HTTP admission/parser/runtime is exercised
in-process, rather than claiming installed socket/service acceptance.

Required baseline CI was refreshed rather than assumed green.
[Run 38776813533](https://github.com/WsprryPi/WsprryPi/actions/runs/38776813533)
on `fbb80fa` had two unrelated harness failures: macOS could not find GNU
`timeout`, and the Test Tone browser's 16 workflows passed before Chromium
profile removal failed with `ENOTEMPTY`. The minimal validation repairs retain
the 30-second lifecycle deadline using Python's portable subprocess timeout,
and request bounded graceful browser shutdown before retrying transient profile
cleanup. No Test Tone product behavior changes. The hardware-free lifecycle
target, complete `npm run test:browser` and a repeated Test Tone browser run
passed locally; hosted macOS acceptance is reserved for final pushed-head CI.

## Final local result ledger

| Check | Result |
| --- | --- |
| Pi config/API Make targets and their UI producer tests | Passed |
| Full Pi network runtime target | Passed; 358 production checks, plus TLS/Plain TCP checks |
| Final production binary after preview-bound repair | Passed; 358 checks |
| Current network interoperability Make target | Passed; real loopback TLS, recovery, replay and scheduled wait |
| Current config interoperability Make target | Passed; legacy producer, native consumer and nine shared-resource carrier/state scenarios |
| Pi component `npm test` | Passed |
| Pi `npm run test:browser` and repeated cleanup case | Passed |
| Final rendered resource workflows | Passed; desktop/mobile light/dark, labels, no overflow, no host autosave and no preview PUT |
| Pico full host CTest | Passed; 187/187 |
| Pico WTP contract validator | Passed; 23 schema, 7 raw, 1 framing and 8 transition cases |
| Pico affected ASan/UBSan | Passed; 10/10, including schema/resource/carrier/storage paths |
| Pi ASan/UBSan configuration/API and native consumer | Passed; no sanitizer diagnostics |
| Pi ASan/UBSan nine resource scenarios | Passed; all three carriers, storage/disable-failure/blank cases, no diagnostics |
| JavaScript syntax, PHP template lint, Impeccable detector, relative documentation links and whitespace | Passed |
| Firmware cross-link | Not applicable: no Pico implementation changed |

Exact clean Pico rows against the final Pi task source (baseline `fbb80fa` plus
explicit task inventory; later publication recorded below):

| Profile | Pico commit | Result |
| --- | --- | --- |
| current | `ddb00dd7960373cec271f7816c0e1b10117416e9` | Passed; full scoped workflow on engineering mTLS and consumer station/AP |
| p153 | `d6208dbc88005383041c5ec306b738db7ec22796` | Passed; actual resource Load, unchanged old producer, atomic settings and native consumer |
| p152 | `87144d1364f36d04bfe0a714bd1c2f6deb974302` | Passed; actual resource Load, unchanged old producer and atomic settings |
| extended | `dfffdd5220db8b7cf1acf5e74950dbd294283c55` | Passed; actual resource Load and preserved extended legacy Config |
| legacy | `95aac22bf7cc31b67fe5773761708d743d8a6473` | Passed; actual absent-resource refusal, disabled controls and preserved strict legacy Config |

Every historical resource read asserts GET-only exchanges and unchanged full
member dump before existing save tests. The unchanged producer's source digest
remains asserted. Only current receives the complete new scoped-write suite;
older rows retain their specified read/legacy-write compatibility scope.

Local supporting logs and generated screenshots are retained outside version
control in `/private/tmp/wsprrypi-p154`. Key logs: `pi-focused-final.log`,
`pi-runtime-final2.log`, `production-reviewed-test.log`, `pi-network-final.log`,
`pi-config-reviewed.log`, `compat-{legacy,extended,p152,p153}-test.log`,
`compat-p153-consumer.log`, `pico-final-full.log`, `pico-contract-final.log`,
`pico-sanitize-tests.log`, `pi-config-unit-sanitize.log`, `pi-api-unit-sanitize.log`,
`pi-consumer-sanitize-reviewed.log`, `pi-resources-sanitize.log`, `ui-unit.log`,
`ui-browser-final.log` and `ui-render-final.log`. Temporary
artifacts are supporting evidence; committed pins, commands and assertions are
the durable reproducible record. Resource screenshots are under isolated
`pi/src/build/wtp-network/ui/*-resources-*.png`.

## Publication gate

Local acceptance, final independent evidence assessment, commits, origin/devel
pushes, actual pushed-head CI and independent `ls-remote` parity are distinct
steps. P15.4 remains in progress until all applicable gates pass. No PR or release
is requested. The separate release-punchlist edit stays outside task commits.

Initial publication: Pi implementation `085ef702b43be57b13baf3637651cb36084d985d`
and Pico documentation `0048ad82a0f1db3b499ebe7a58ee8f6031801878` pushed to
origin/devel and independently matched `ls-remote`. All 37 original staged Pi
files were compared byte-for-byte with the tested snapshot after commit. Pico's
five-file companion diff is documentation only; the clean implementation pin
remains `ddb00dd7960373cec271f7816c0e1b10117416e9`.

[Implementation CI run 38786878022](https://github.com/WsprryPi/WsprryPi/actions/runs/38786878022)
passed strict I2C and all four historical compatibility jobs. Hosted macOS got
past the repaired timeout command and exposed the next baseline fixture issue:
the slow-observer test's blocking socket pair did not complete its saturated
send under `MSG_DONTWAIT`. A bounded native Python check reproduced the wait on
both UNIX and loopback TCP sockets; explicit nonblocking mode returned a partial
send immediately. No native Mac C++ build ran. Apple's
[generic socket send path](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/uipc_socket.c#L1828-L1857)
explains why per-call flags alone are insufficient for this Darwin fixture.

The Apple-only test fixture now asserts successful F_GETFL/F_SETFL and adds
O_NONBLOCK to that sender. Linux keeps its blocking descriptor and per-call
MSG_DONTWAIT regression. Both retain 1 MiB pressure, three-second completion and
clients-mutex cleanup assertions. This is a portability repair to the test;
Darwin production WebSocket behavior remains unqualified. Independent review
accepted this boundary rather than claiming a Mac server fix. The Linux target
and a repeat pass; hosted acceptance requires a new pushed-head CI run. No
product WebSocket implementation changed.
