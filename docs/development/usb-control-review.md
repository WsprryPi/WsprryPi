# USB control implementation and software acceptance

Date: 2026-10-17. This records the WsprryPi portion of the coordinated normal
Pico USB control slice. Hardware observations/restoration and the final Pico
product commit are recorded by the coordinating execution in WsprryPico.

## Scope and behavior

The production runtime uses the exact selected Linux WTP endpoint, serial and
VID/PID to find its unique management sibling under the same physical USB
parent. A separately framed management session supplies configuration, hostname,
Wi-Fi lifecycle and qualified UTC without Pico networking. Existing Console
and WTP interfaces and WTP/1 semantics remain unchanged.

The ordinary Transmitter pane exposes a page-local **Show Pico connections and
settings** checkbox. It reveals the Pico tab without console commands or a
configuration save. Advanced fleet scheduling keeps its existing explicit
visibility path. The configured Pico Wi-Fi hostname is the primary connected
common name; immutable device/boot identities remain separate. Cached names are
marked last confirmed after loss, failed reopen or missing fresh name readback.

The USB UI supports read/name changes, Wi-Fi create/replace/remove-last/recreate,
network enable/disable through existing controls, deferred restart and a bounded
AP request. Saved station progress has read-only completeness/applied/STOP
observations plus separate valid-field save and explicit station application,
using the existing station editor. Its save action stays disabled until that
editor is loaded. Complete config/schedules, station/hardware and pin preview
retain their existing coordinated validation and persistence paths.

Fresh reads, shared revisions, exact selection/device/boot bindings and explicit
mutation contexts are required. All editors become stale before a mutation;
a lost result forces readback/review. No mutation is automatically replayed.
USB-selected capability projections identify usb_physical and remove USB JSON
management from unavailable capabilities. Historical network schemas and
carriers keep their original strict meaning. USB discovery additionally validates
the exact transmitter-management/3 selected-carrier, target, row order, frozen
operations and limits; no unconditional discovery bypass remains.

UTC observations use read-only adjtimex qualification, measured wall/monotonic
exchange bounds and one-use Pico samples, refreshed at a bounded five-second
cadence and before inspection/preparation plus CLAIM/ARM admission. Inner accepted/selected/source
results remain distinct from successful envelope delivery. Existing Pico clock,
uncertainty, holdover, launch and WTP job limits are not widened.

## Adversarial repairs

Independent coordinated review identified and drove repairs for:

- Cancellation waiting behind silent management I/O: STOP/owned-job cancel and
  fleet halt/recovery notify the USB transaction before acquiring authority.
  Periodic USB time is serviced before the outer transmitter lifecycle lock.
- Independently learned WTP/management identities: the real scheduler checks
  device and boot agreement before CLAIM/LOAD and again before ARM.
- Optional management session reclaimed during maximum WTP LOAD: one bounded
  reopen rebinds the same device/boot, reads its name and takes a new one-use
  time sample before ARM. The existing launch allowance remains authoritative,
  configuration writes are not retried, and a dedicated lifecycle mutex
  serializes worker and periodic/UI access. A production-runtime regression
  models logical reclamation at LOAD while CDC remains connected.
- Known USB binding contradictions: a fresh HELLO with a different boot cannot
  reuse retained old WTP/management agreement to authorize LOAD/ARM. A latch
  blocks admission and published readiness until explicit fresh reconciliation
  or reselection, without treating absent legacy management as a contradiction.
  Recovery reports failure while that fault remains; UI identifies the USB
  mismatch separately from the independently observed RF output state.
- First identity lease races: a dedicated mutex serializes acquisition and
  rejects a different device; blank expected IDs do not transfer a previous
  runtime's lease merely because both saved IDs are empty.
- Worker callback lifetime and fleet pointer replacement: the application joins
  while binding/lease state is alive, and cross-thread cancellation protects
  the replaceable target pointer.
- Stale connected/common-name observations after failed reopen: disconnected
  status invalidates name confirmation without erasing historical diagnostics.
- Successful time envelope with an unselected/rejected source: actual inner
  acceptance/selection and active source are reported.
- Ambiguous no-op writes: readback/review and an explicit new change are required.
- Initial USB profile projection: the actual-pair test exposed an empty station
  tuple before applied Config existed. Pico was repaired; Pi validation was not
  weakened.
- Wi-Fi recreation: empty-configuration UI selects the existing pool.ntp.org
  default; null-password preservation applies only to the same network.

## Deterministic validation

All C++ compilation below used identified Linux Docker images, never the known
broken local macOS C++ build path. The retained wsprrypi-item16-gcc13:local image
is Linux ARM64 with GCC 13.3; the Pico portable driver used the retained
wsprrypico-p153-env:local image with GCC 14.2. No SDK/tools were downloaded.

Passing checks during implementation:

| Check | Result |
| --- | --- |
| WTP USB CDC unit/sysfs/PTY suite | 2757 checks; role 4, physical-parent pairing, duplicate serial isolation, ambiguous-role rejection included |
| WTP application suite | 1337 checks; optional USB identity learning and existing lifecycle |
| Actual production runtime executable | 447 checks after binding/lease race, failed reopen, cancellation, LOAD reclamation recovery and known-identity contradiction repairs |
| USB management negative suite | Fragments, duplicate keys, ACK binding, wrong boot, no replay, finite deadline, cancellation, qualified UTC and browser selection/revision guards |
| USB management ASan/UBSan | Passed using -fsanitize=address,undefined |
| Full production runtime ASan/UBSan | 447 checks passed with all objects rebuilt in an isolated -O0 sanitizer directory |
| Existing configuration C++ suite | Negotiation, closed shapes, revision, identity and uncertain-write checks passed |
| Actual pinned current-network consumer pair | Passed configure/compile/link/run against Pico 804e8a06585ec5a0e650e69fcd71ada398b6cc9f and MbedTLS 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20; station/AP crypto, recovery, strict resources, journal and management-only admission |
| Existing browser API C++ suite | Precision, bounded parsing, revisions, cancellation replay and protected routes passed |
| Existing WTP UI and management Node tests | Passed |
| New USB settings Node producer | Name/Wi-Fi lifecycle, redacted/password preservation, partial progress, uncertainty and explicit review passed |
| Actual production Pi/Pico pair | Passed over fragmented anonymous pipes with erased volatile flash and no network |
| Whitespace | git diff --check passed |

The complete Linux `wtp-config-interop-test` subsequently passed on the frozen
product with the unchanged current-network and MbedTLS pins above. This covers
engineering TLS, consumer station/AP, protected UI/runtime/resources and journal
failure/recovery, plus both shipped-browser crypto contexts with full-capacity
lost-read recovery. The extra local network-runtime attempt passed Plain TCP
but did not reach its TLS runtime cases: its first image lacked the OpenSSL CLI
and the complete image encountered a bind-mount fixture-copy permission error.
No local full-target pass is claimed. The separate GitHub network-loopback job
passed in its complete Linux environment.

The actual pair executes production UsbManagementClient, protected browser
request parsing, BrowserApi and configuration/resource adapters against Pico's
production USB endpoint, API, persistence and time handlers. It covers blank
name access, rename, Wi-Fi create/replace/delete-last/recreate, partial station
progress remaining unapplied, explicit application into the first no-network
Config, a later shared station edit and qualified UTC exchange. Software time
quality is explicitly injected only in pipe mode; physical --usb mode uses
actual host qualification.

Current-network compatibility was rerun in the retained GCC 14.2 image with
isolated clean exact-pin source checkouts and unchanged PICO_COMPAT_PROFILE=current.
The build directory is src/build/wtp-network-usb-current; retained configure,
compile/link and execution logs are src/build/wtp-network/usb-current-network-
{configure,build,test}.log. The direct consumer_interop executable passed; this
is distinct from claiming the combined Make/Node network targets passed.

The retained GCC image lacks Node. Initial combined wtp-api-test and
wtp-config-test targets therefore reached passing C++ executables and failed
their Node substeps with command-not-found. Those compiled executables were
then rerun successfully in Linux; their corresponding JavaScript tests passed
using the existing host Node runtime. No failed target is reported as a full
combined-target pass. Production runtime logs also include expected missing-Pi
model metadata and deliberate negative cleanup diagnostics in the inhibited
simulated profile; assertions pass.

The full runtime sanitizer build initially used -O1 and stopped at a GCC 13
libstdc++ std::regex maybe-uninitialized warning treated as an error. The clean
-O0 build preserved strict warnings and rebuilt all objects. Its first run
reached the new fixture's three-second wall-clock watchdog under instrumentation;
the watchdog was increased to fifteen seconds without changing any production
protocol/scheduler deadline. The final full run passed 447 checks with no
ASan/UBSan report. Retained final logs are src/build/wtp-network/
usb-production-sanitized-o0-{build,test}.log.

## UI verification

Impeccable was applied before UI editing; existing font, spacing, forms and
theme tokens were retained. The isolated PHP/Chrome fixture rendered desktop
1440x1100 and mobile 390x844, both light/dark. Screenshots were inspected in two
bounded rounds. No horizontal overflow occurred. Native input measurements
were 14px text, 39px height, 591px desktop/316px mobile width, with keyboard focus.
Normal Pico visibility leaves advanced scheduling hidden. The fixture uses
synthetic status and does not contact a transmitter.

Ignored evidence paths are src/build/wtp-network/ui-usb/*.png and
src/build/wtp-network/ui-usb-progress/*.png, with checks.json. The second set
shows partial-progress readback and the unchanged station editor layout. The
later final station-editor gate is verified by the Node DOM producer test;
those screenshots show its prior enabled-save state before the final gate.
No third unchanged layout/polishing round was performed.

## Reproduction

From the repository root, on a suitable Linux development host:

```sh
make -C src wtp-usb-test wtp-usb-management-test wtp-api-test wtp-config-test SUDO=
make -C src wtp-application-test SUDO=
make -C src wtp-production-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
node WsprryPi-UI/tests/wtp_usb_settings_test.js
make -C src wtp-usb-pair-client-build SUDO=
node WsprryPi-UI/tests/wtp_usb_pair_test.js   src/build/wtp-network/wtp_usb_pair_client /absolute/path/to/usb_management_driver
```

For the full production sanitizer run, from src/:

```sh
make build/bin/usb-reclaim-sanitized-o0/wtp_production_test CXX=g++ \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  OBJ_DIR_DEBUG=build/obj/usb-reclaim-sanitized-o0 \
  BIN_DIR=build/bin/usb-reclaim-sanitized-o0 \
  CXX_DEBUG_FLAGS='$(CXXFLAGS) -O0 -g $(DEBUG_DEFS) -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined -lpthread -latomic $(PKG_LIBS) $(OPENSSL_LIBS) $(AVAHI_LIBS) $(SYSTEMD_LIBS)' -j4
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./build/bin/usb-reclaim-sanitized-o0/wtp_production_test
```

Pico's usb_management_driver is built using its normal portable CMake project
with WSPRRY_PICO_BUILD_FIRMWARE=OFF, WSPRRY_PICO_BUILD_TESTS=ON and target
usb_management_driver. The new CI job wtp-usb-management-pair uses the dedicated
exact product pin src/tests/network/pico-usb-revision.txt. The existing current
network pin and all historical pins retain their own evidence. Source-pair
publication and pins require the actual Pico product commit; a base checkout
or old network pin does not qualify this new USB implementation.

For an explicitly authorized physical check, the developer client also accepts:

```sh
src/build/wtp-network/wtp_usb_pair_client --usb   /dev/serial/by-id/SELECTED-WTP-INTERFACE SERIAL FULL_DEVICE_ID
```

This mode requires exact cafe:4012 identity and pairs interface 4 through the
same production resolver. Each stdin line is a protected fixture request such
as {"method":"GET","path":"/api/v1/member/hostname","body":""}, or
{"synchronize":true}. Read hostname immediately before AP/restart and reuse its
ETag. The status response includes actual Pico status at body.host.pico. This
bounded developer harness does not itself qualify an installed Pi service,
physical RF timing, target output or electrical behavior.

## Documentation impact and repository state

Updated README, production/USB/browser/configuration developer references and
the new USB management guide. Existing historical execution records, network
pins and operator documentation remain distinct. Wsprry_Pi_Docs was considered
but is outside authorized write scope; its required operator updates are listed
in docs/wtp-usb-management.md. No other repository was modified by this Pi scope.

The dedicated USB product pin is
`100e749c815f7485d5097c9086852d3b97adca9f` in WsprryPico. Existing network and
historical pins remain unchanged. Final source manifests, bounded physical
USB/AP outcomes, exact restoration and publication are recorded in the
[coordinated Pico execution record](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/usb-control-results.md).
That evidence distinguishes the tested pre-commit image label from the
subsequent clean product build and does not claim transmitter RF qualification.

The complete [non-hardware CI workflow](https://github.com/WsprryPi/WsprryPi/actions/runs/39039613596)
passed on product `b2160ca39bed539dfd5d39a0c449dd64d37c53a2`: the new exact USB
pair, all seven historical compatibility jobs, current network loopback,
non-hardware validation, macOS simulated, strict I2C and all six GCC13 release
profiles. The final follow-up changes only this evidence and the operator
clock-readiness explanation; production source and all compatibility pins
remain exactly those tested.

## Subsequent USB UTC investigation

The [USB uncertainty investigation](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/usb-time-uncertainty.md)
records the ACK-inclusive sample window, corrected midpoint interval, component
measurements and a proposed ordinary-operation UTC requirement. The client now
captures the sample response before ACK and retains a separate post-ACK clock
qualification/step check. Delayed or lost ACKs cannot bias the sample midpoint
or permit an unconfirmed submit. The 1000000 ns Pi default and all Pico timing,
service, capacity and uncertainty bounds remain unchanged.

Final focused tests cover fully asymmetric interval endpoints, odd rounding,
200 ms ACK delay, missing ACK, clock steps both inside the sample window and
during ACK, regression, overflow and loss of qualification. Normal and Linux
ASan/UBSan builds pass. The final native production runtime passes 447 checks;
the exact production-target Node UI regression and production USB pair pass.
No UI presentation was changed. No macOS Pi C++ build was attempted.

The compiler-only GCC13 image lacks Node, and a relink in the Node-capable
image lacks the Avahi development linker metadata required by the cached
production objects. Final native source is rebuilt/run in the complete GCC13
compiler environment; the existing Node test runs separately in the retained
Node-capable environment. No product source or dependency policy was changed
to accommodate these environment limitations. The initial mistaken Node
filename was corrected to the exact command in the Makefile.

The physical clock-only results use the production native client and inhibited
Pico firmware with no saved station network. They do not qualify 1 ms ARM, an
installed service, RF output or absolute UTC accuracy. The canonical result
records the exact client/source/image hashes and original full-flash restore.
A 100 ms ordinary-operation UTC requirement is recommended, not applied.
