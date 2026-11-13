# WTP Fleet catalog and local discovery

Fleet is available as a normal tab under Setup. Opening it observes devices
and schedules without selecting a backend, changing `[WTP]`, enabling output
or saving field drafts.

The compatibility route keeps one selected endpoint in the existing `[WTP]`
INI section and runtime configuration. Independent output assignments, described
below, use separate target contexts and do not replace that selection. A separate version 1 catalog at
`<active-INI-path>.wtp-devices.json` stores up to 64 named profiles with random
local IDs, a manual or DNS-SD connection method, a complete endpoint/settings
snapshot, expected full WTP device ID, and host paths to TLS files. The private
catalog file is written atomically with owner-only permissions. It never stores
certificate or key contents, Wi-Fi passwords, claimed RF capability, or job
state. First access imports the selected legacy `[WTP]` settings into a named
profile without changing them. A legacy Plain LAN endpoint with no expected
ID is retained as `legacy_unverified` and cannot be selected again from the
catalog until its full identity is supplied. Other valid legacy INI/JSON
settings keep their current behavior.

The catalog API derives `active_id` by exact comparison with current `[WTP]`.
If an operator removes or edits the matching profile, `[WTP]` remains active
and the API reports it as unmatched. There is no second saved active ID. Catalog
edits use their own quoted SHA-256 ETag; **Use this device** additionally
requires the host configuration ETag. It is refused while transmission is
enabled or the current runtime is pending, owned, active, armed, executing,
uncertain, faulted, or otherwise not safely replaceable. Applying a profile
patches `[Operation] Transmit Backend = wtp` and `[WTP]` through the existing
configuration path. It does not set `Transmit = true`, CLAIM, LOAD, ARM, adopt
work, or recover old work. The INI write happens before the in-memory active
endpoint is published. A write failure leaves the old endpoint active; an
unexpected failure after persistence is reported as `runtime_apply_unconfirmed`
and requires a fresh config/status read before further work.

## Fleet editing workflow

Fleet identifies which actions save explicitly. Device selection and catalog,
assignment, and listener drafts do not participate in this Pi's automatic
configuration save. **Use Pico** and this Pi's connection fields retain their
automatic save behavior. A new output assignment uses the selected saved device;
without a matching saved selection, choose an output explicitly. The Pi status
HTTP port is hidden for USB Pico assignments; network profiles retain the field
for Pi takeover reconciliation. Hiding it preserves the assignment document
format and the existing port value.

Under **Pico standalone settings and Wi-Fi**, management prerequisites appear
before the editors. Read saved station progress, save valid station fields, and
apply saved progress separately. **Complete station save** is an alternative
that saves all three station fields together after reading member resources.
**RF output, GPS, and pins** groups the hardware plan and places validation and
saving after its pin, GPS, and optional Si5351 controls. Amplifier timing, filter
selectors, and GPIO drive retain their own actions. Controls depend on the
selected Pico's supported capabilities and current readback.

**Standalone schedules and combined settings** contains the combined editor.
**Save schedules only** changes recurrence; **Save combined settings** can also
change station, Wi-Fi, expiry, and standalone Enable. USB name and Wi-Fi have
dedicated controls above. These alternatives retain their existing save and
readback requirements; opening a disclosure never applies settings.

## Independent output schedules

**Output schedules** supports zero through 32 remote outputs alongside this
Pi's own schedule. Select a verified saved Pi/Pico profile, assign a finite job,
and save it explicitly. The assignment starts paused unless **Start this output's
schedule after saving** is selected. **Resume**, **Pause and stop**, **Edit schedule**,
**Reconcile**, and **Remove assignment** act on that output only. Editing requires
a paused, reconciled output. Removing a catalog profile does not silently remove
an assignment; remove the assignment explicitly.

**Pause and stop** and **Remove assignment** open an in-page confirmation naming
the selected output. Cancel, closing the dialog or pressing Escape leaves it
unchanged. A confirmed action stops any job owned by this controller; removing
also deletes the saved schedule. If another operator changes schedules while
the dialog is open, refresh and review before repeating the action.

Each assignment stores its own mode, absolute RF reference in Hz, repeat period and UTC phase. Finite tones use duration in seconds in the UI (integer milliseconds on the API). QRSS, FSKCW and DFCW use a message and dot duration. FSKCW and DFCW also specify shift. WSPR accepts a single-frame callsign/locator and dBm; its period and phase align to two minutes and transmission starts at second one. The target's current CAPS must accept the compiled job. Native GPIO, RP1 and Si5351 Pi endpoints report their WSPR, TONE, QRSS, FSKCW and DFCW capabilities. Existing processor, route, development authorization and frequency policies still apply. RP1 retains its exact-operation host confirmation; Fleet does not create that confirmation. Pi and Pico assignments are checked against current CAPS.

Assignments obey the controller's current frequency-policy settings.
WTP remains an unqualified physical route in that policy, so starting remote
work requires the explicit `--allow-unqualified-frequency` option on the
controller, or `Allow Unqualified Frequency = true` in `[Experimental]` of its
INI. Policy is checked again before CLAIM and ARM, including after an INI
reload. Non-amateur frequencies additionally require the existing separate
authorization. CAPS acceptance and a saved assignment do not silently grant
either override. A policy rejection is returned before saving the assignment.

`<active-INI-path>.wtp-assignments.json` is a private atomic file, independent of
the profile catalog and `[WTP]`. It records up to 32 assignments, the last
consumed UTC slot, in-flight state, and up to 32 local-takeover removal notices.
A slot is consumed before dispatch and is never automatically retried. Failed
or uncertain work pauses that assignment. After an interrupted process with
in-flight work, use **Reconcile**, inspect the result, then **Resume**. Other
outputs and local scheduling remain independent. Duplicate full identities and
self-targeting are rejected, including collision with the compatibility route.

If the assigned target restarts, **Reconcile** can establish a fresh session
with that same full device identity and product. It requires fresh capabilities
and status confirming the new boot is unowned and its output is inactive; a
fault or unresolved output keeps the assignment blocked. Pi local control and
takeover history are checked again. Successful reconciliation leaves the
assignment paused. **Resume** schedules future slots; it does not replay the
interrupted slot or take over another owner's job.

Pi assignments explicitly select Plain LAN and a status HTTP port (default
31415). Before CLAIM and ARM, the controller reads the target's durable takeover
generation at `/api/control/v1/host/wtp-endpoint`. A changed generation deletes that
output's saved assignment even when takeover happened while the controller was
offline. Local Enable never waits for this controller. Losing network contact
pauses progress; it does not prove output-off, select another device, or remove
an assignment as if the operator had taken over.

## Fleet Operations and host actions

Operations offers a Device selector when an assigned or unresolved retained remote context exists. This Pi always means the controller and is the initial selection after a full page load. Selecting an assignment only changes the displayed context. It does not change the compatibility WTP route, persist configuration, claim output, synchronize a clock or start a job. The remote view keeps assigned schedule state, current device/job evidence, pending occurrence, forecast and historical reports distinct. Local websocket/config updates continue updating the hidden local view; remote actions cannot invoke its handlers. Plain LAN identity is an expected-ID match, not authentication.

Each owner performs one initial read-only HELLO/STATUS/CAPS inspection for a healthy restored or new context. Crash/retirement barriers inhibit that inspection until explicit reconciliation. Foreign occupancy is healthy observation but prevents starting; it is not a reconnect failure. Healthy idle owners poll STATUS at most once per second. Actual negotiation, transaction, identity or safety failure latches recovery and inhibits dispatch; periodic ticks cannot reconnect around it. The aggregate initial inspection limit is 45 seconds. Initialization is staggered by 30 ms per context, at most 930 ms for 32 outputs.

Device evidence becomes stale at 15,000 ms. Age begins at the STATUS request or conservative negotiation start, preserves the separate receipt timestamp, includes browser round-trip time, and increases with browser monotonic elapsed time. A GET, worker heartbeat or stale historical report cannot renew it. The 1-second idle cadence, 8-second transaction timeout and 3-second browser poll leave 3 seconds of policy margin; this is a software policy, not a measured timing guarantee. Pi targets also need fresh, matching boot-bound takeover evidence. Ownership is checked again before dispatch.

| Protected host resource | Behavior |
| --- | --- |
| `GET /api/control/v1/host/fleet` | Cached committed assignments, ETag, bounded output projections and correlation metadata for Setup |
| `GET /api/control/v1/host/fleet/operations` | Cached Operations projection without endpoint/settings paths; at most 512 KiB |
| `GET /api/control/v1/host/fleet/actions?ids=...` | At most 32 distinct lowercase 128-bit hex action IDs, at most 2 KiB encoded query and 128 KiB response |
| `POST /api/control/v1/host/fleet` | Strict guarded admission of `assign`, `enable`, `pause`, `schedule`, `recover` or `remove`; acceptance is separate from completion |

All six mutations require `If-Match`, the existing request intent guard, a random 128-bit lowercase-hex `action_id`, `controller_incarnation`, `assignment_generation`, decimal-string `reviewed_control_revision`, nullable `reviewed_boot_id`, and decimal-string `accept_before_ms`, alongside the operation's fields. An uncreated assignment uses null generation/control revision. Capture the reviewed context and ETag before confirmation; obtain a fresh controller clock sample afterward, then use its `now_ms` plus 30,000 ms as the fixed admission deadline. A changed context requires review again. New admission must be before that deadline and within 45 seconds of the current controller clock. Completion can take longer. Never automatically retry mutations.

The controller incarnation and per-assignment generation are opaque 128-bit identities. A per-context control revision advances on accepted intent and dispatch revocation, including a no-op paused write. It is independent of configuration ETag, publication revision and observation revision. Every positive action has a fenced commit/dispatch permit; Pause revokes older activation permits before asynchronous storage. CLAIM, LOAD and ARM submission share the permit's short revocation lock. Already submitted operations retain their resources until quiescent. Protective owned-job stop and paused persistence can continue under the current owner after a safety fence; they cannot revive activation or establish a superseded action's success.

POST returns 202 with a correlated accepted/pending action, or 200 for retained confirmed replay. Action states are accepted, pending, confirmed, refused, superseded and outcome_unconfirmed; `work_pending` separately records whether callbacks/work remain outstanding. Exact retained replay of the original request, ETag and fixed deadline returns its record without repeating effects. A conflicting reuse is refused. Unknown expired requests cannot execute again. The volatile ledger retains up to 256 terminal records plus 64 active/window-protected slots, at most 320 records of 2 KiB each, for up to 15 minutes. Active or unquiesced work and results whose admission window remains open cannot be evicted. Each context reserves a priority Pause path. The browser retains at most 32 ordinary and 32 priority references and uses one nonoverlapping read queue; changing selection never loses pending tracking.

A confirmed Pause establishes disabled persistence, inhibited dispatch and quiescence of tracked controller work. It does not by itself establish Device stopped or measured RF-off. With no tracked job/barrier, Pause can confirm the schedule while another job is present or output is unknown. Tracked cleanup must satisfy the existing matching terminal/ownership criteria. Reconciliation requires fresh safe identity/boot evidence, clears only allowed barriers and leaves the schedule paused. Resume only schedules future eligible slots. Start-after-save assignment has the same eligibility requirements as Resume.

Recovery authority for pending removals is stored separately at `<active-INI-path>.wtp-fleet-contexts.json`: private, host-owned, atomic version 1, at most 32 contexts and 1 MiB. The closed assignment document remains version 1. Durable frozen retirement intent precedes row deletion or loss of a recoverable binding. Operator removal retains a paused row when cleanup fails; explicit reconciliation does not silently delete that retained row. Target takeover revokes the row independently, retaining the frozen context until reconciliation proves cleanup. Corrupt/unavailable recovery storage inhibits starts and edits; safe Pause of an already verified live owner remains available. Do not repair recovery authority by deleting files or guessing an endpoint. Old manager writers/sessions retire before replacement workers can dispatch or write.

Schedule-editor handoff is a one-use same-origin tab-local reference to the reviewed assignment. Unsaved entries retain their target and revision; opening another schedule requires an explicit continue/discard/cancel choice. Confirmation, progress and uncertain results remain beside the selected action. Controller power controls are hidden in remote views; displayed header clocks remain controller clocks.

See [Pi endpoint operation](wtp-pi-operation.md) for inbound listener and target-side takeover controls.

## Attached USB Picos

Setup lists attached WsprryPico USB devices automatically while the Pico pane
is visible. **Refresh list** also rescans. Enumeration reads Linux USB metadata;
it does not open serial ports, write settings, select a backend or enable output.
Only the dedicated WTP CDC function is offered. Console and management ports,
unrelated serial devices, incomplete metadata and ambiguous duplicate serials
are excluded. Persistent `/dev/serial/by-id` paths are preferred when available.

Choose an entry under **Attached USB Picos**, then **Add device**. The editor
fills the USB path, serial and vendor/product IDs and reads the Pico's full WTP
identity with HELLO, STATUS and CAPS. No clock is set and no job is claimed,
loaded or armed. Identification respects existing ownership and firmware-update
exclusion. A disconnected or changed endpoint is refused on fresh metadata
before the port is opened. A failed identity read leaves the draft available
for retry through **Read USB Pico identity**.

Name the device, choose **Save device**, then select its saved profile and
**Use this device**. USB profiles use the existing manual catalog format; no
configuration migration is required. Existing saved profiles and custom values
are retained. Discovery and draft preparation never apply the profile. Removing
a cable removes its live observation without deleting its saved profile or
discarding an open draft.

`GET /api/control/v1/host/discovery` retains its network `wtp-dns-sd/1` representation
and adds a `usb` object (`scope: wtp-usb/1`) containing availability, a reason
and metadata-only candidates. USB IDs in `POST /api/control/v1/host/discovery/identify`
are resolved again against current metadata and matched to the supplied USB
settings before identification. The UI refreshes observations with status;
unchanged observations preserve the open selector.

## DNS-SD observations

On Linux builds with `libavahi-client-dev`, a bounded native Avahi browser
observes `_wtp._tcp.local.` on local interfaces. It uses the client API in a
background thread, not shell output, PHP, browser mDNS, or RF scheduling code.
The browser tracks interface, address protocol, instance, domain, resolved SRV
target and **actual SRV port**, raw TXT, first/last observation, removal and
failure. A same-name service on another interface is a distinct candidate.
Avahi restart marks old observations stale; removal, resolver failure, malformed
records, and capacity limits are explicit. The host API returns a bounded
snapshot and never opens a WTP connection. On a host without Avahi support or
a running daemon it reports unavailable. If Avahi is running but multicast is
blocked, an empty list cannot distinguish that from no advertising devices;
the UI leaves reachability unverified. Manual profiles and configured direct
endpoints remain usable.

The accepted TXT format is version 1: `txtvers=1` must be first, and exactly one `binding=tls` or `binding=plain` must appear. Required keys are duplicate-checked without ASCII case; unknown keys are ignored. TXT does not contain a port, device ID, WTP version, credentials, or RF capability. HELLO and CAPS provide protocol/device facts. The service name and direct-connection default port 31417 are provisional pending IANA assignment. Discovery is link-local and does not imply that a Pico listener is reachable or RF ready. See the [Pico DNS-SD profile](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/protocol/WTP-DNS-SD.md) and [WTP/1 protocol](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/protocol/WTP.md).

An instance name, hostname, address, TXT record, or Plain LAN HELLO is never
used as a durable trusted identity. A discovered profile needs explicit
operator action and a bounded read-only HELLO/STATUS/CAPS check before it is
saved or used. TLS also needs device-specific CA/client references, an explicit
expected certificate identity and exact expected WTP device ID; the TLS
handshake validates its server chain, name and ALPN before association. Plain
LAN requires an explicit consent choice and operator confirmation of the
observed full device ID. That ID is an unauthenticated observation on Plain LAN.
Identification does not probe all advertisements, CLAIM, or mutate `[WTP]`.

This implementation freezes each saved DNS-SD profile's endpoint. A changed
SRV target, port, or binding blocks a new **Use this device** action until the
operator edits the profile and a fresh identity check succeeds. An already
active runtime continues to use its saved `[WTP]` endpoint and normal transport
reconnection; it does not retarget from advertisements. A TLS profile can
be edited to the new SRV endpoint only with its saved binding, expected server
identity, credentials and device ID still validating. A Plain LAN profile
requires explicit review and consent again; it never silently retargets or
downgrades from TLS. Direct/manual profiles keep their configured endpoint and
normal connection behavior. No binding fallback is attempted.

## Host API and validation boundary

Protected host resources are `GET /api/control/v1/host/discovery`,
`GET/POST /api/control/v1/host/devices`, `POST /api/control/v1/host/discovery/identify`, and
`POST /api/control/v1/host/devices/use`. The same trusted-LAN peer, proxy, Host and
Origin policy applies to each route. POST also requires same-origin JSON intent
headers and a 32 KiB body limit. Catalog POST uses `If-Match` for the catalog;
use carries `catalog_revision` in its JSON body and uses `If-Match` for the
active host config. Missing revisions return 428 and stale revisions 412.
There is no browser-local credential store or independent job authority.

`make wtp-catalog-test` exercises the injected discovery store, TXT parser, nondefault SRV port, interface collision, loss/restart/failure, legacy import, revision guard, and Plain LAN consent without an Avahi daemon. The portable Mac profile compiles the unavailable adapter. Linux CI declares Avahi development headers and compiles the native adapter. These are source/software checks. Live Avahi/multicast discovery, listener advertisement, physical TCP/TLS acceptance, GPIO, services, and RF remain separate qualification. Interoperability tests compile the selected current Pico sources, including edits; source identity is informational. Software tests do not qualify DNS-SD advertisements, physical transport or RF.

Fleet WSPR `frequency_hz` is an absolute RF center, not a USB dial or tone 0. At `14097100 Hz`, tones are 14,097,097.802734375; 14,097,099.267578125; 14,097,100.732421875; and 14,097,102.197265625 Hz. No dial or random placement is applied. Finite tone and QRSS use a carrier, FSKCW uses space, and DFCW uses dot; mark and dash add the positive shift. Mode changes update labels without changing the frequency draft or saved numeric meaning. WTP carries absolute event targets; the receiving endpoint does not add 1,500 Hz or reapply standalone settings or Pi calibration.

## Fleet capacity and rollback

The controller supports up to 32 independently scheduled remote Pi/Pico outputs,
in addition to its local schedule. The 64 saved catalog profiles are connection
records; saving one does not consume an assignment slot. Removed outputs with
unresolved cleanup retain a bounded runtime context and identity exclusion until
reconciliation succeeds; these contexts also count against Fleet capacity.

The server advertises `maximum_outputs` from `kMaximumFleetOutputs` in
`src/wtp_integration/fleet_limits.hpp`. The browser validates this advertisement
and disables new assignments when capacity is unavailable or reached. The server
independently refuses an additional assignment before target inspection or writes.
Assignment files retain their version, consumed slots, enabled states, and
in-flight barriers across restart. Uncertain work requires explicit reconciliation
before it can resume; restarting does not replay it.

The assignment read/write ceiling is derived from capacity with 16 KiB per output
plus 16 KiB for removal history and document overhead. Connection fields contain
path references, not embedded certificates. A save exceeding this ceiling is
refused before replacing the existing file. This also bounds previously accepted
extension data in settings, whose size is otherwise unconstrained.

Older software limited to eight assignments refuses files containing more than
eight and files larger than its 128 KiB read limit. Before downgrading, pause,
reconcile, and explicitly remove enough
assignments using the current software. Preserve a private backup for recovery;
never truncate the file or erase an in-flight barrier to force an older version
to load it. Software fixtures do not establish capacity on 32 physical radios.

### Capacity validation

`wtp-fleet-capacity-test` exercises the production Fleet coordinator, store,
scheduler, session, framing, server `Endpoint`, and `JobService`. Sixteen peers
advertise Pi identities and sixteen advertise Pico identities. Typed C++ fixture
providers select memory byte carriers, inhibited RF engines, and explicit logical
clocks. The production entry point retains its system clock, native transports,
and hardware guards. No CLI, INI, environment, browser, or automatic fallback
selects the fixture providers.

Logical time advances through readiness barriers. All 32 peers acknowledge ARM
before the first launch gate opens; every engine starts at logical second 60.
Completion evidence passes through the production session and cleanup paths
before the pause/barrier checks. In the second scenario, one peer's exchange is
held until the other 29 healthy peers acknowledge ARM. After release, all 30
start at logical second 180. Disconnected and changed-boot peers cannot rearm.
The fixture also verifies takeover, retained-context refusal, capacity reclamation,
explicit restart reconciliation, removal, shutdown, and no replay after recovery.
CPU scheduling and operator setup do not choose a launch time. A wall-clock
watchdog detects a hung test only; it is not an admission or timing allowance.
Stopping or invalidating waiting work wakes a blocked developer clock; system
clocks retain their ordinary bounded waits.

The store test covers every count through 32, maximum connection fields and
finite schedule parameters, full removal history, 32 crash barriers, exact file
size boundaries, duplicate/self identities, and revision preservation. Its
populated known-field state is 206,350 bytes. The shared 528 KiB save/read limit
also bounds otherwise unconstrained inactive/extension settings. Refused saves
leave the prior document and revision unchanged.

Run the focused checks without privileges or external network access:

```sh
make -C src wtp-fleet-test wtp-fleet-capacity-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
make -C src wtp-backend-test wtp-scheduler-test wtp-application-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
```

The logical outcomes were identical over three runs limited to two CPUs and a
single-CPU run on LinuxKit aarch64, Debian GCC 12.2. The final single-CPU repeats
observed 33,448–33,964 KiB peak RSS, 66 threads, four descriptors, and six user
CPU seconds. These observations include the memory-carrier endpoints and do not
measure TCP sockets, Wi-Fi contention, physical USB, or physical launch timing.
The production architecture retains one runtime and worker per output,
serialized durable store updates, and one process-wide outstanding resolver
lookup. Hardware, installation, services, and RF remain separately qualified.

The paired validation selects Pico `devel` commit
`f6c90a4925d02d60b7404487e24316f239c48e7c` from a clean isolated checkout,
with Mbed TLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` and its pinned
submodule. It excludes changes in the independently active Pico checkout.
The Pi candidate is based on `f59bba741ff367d0652f6db03b0bf67d4d10444a`,
including the Fleet changes and deterministic fixture construction. The actual
network and USB recipes remain separate from the memory-carrier capacity test:

```sh
make -C src wtp-network-runtime-test wtp-network-interop-test wtp-config-interop-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= WTP_NETWORK_BUILD_DIR=/tmp/fleet-network PICO_SOURCE=/validation/pico MBEDTLS_SOURCE=/validation/mbedtls
cmake -S /validation/pico -B /validation/pico-host -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /validation/pico-host --target usb_management_driver -j2
make -C src wtp-usb-pair-client-build wtp-usb-management-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node WsprryPi-UI/tests/wtp_usb_pair_test.js src/build/wtp-network/wtp_usb_pair_client /validation/pico-host/usb_management_driver
make -C src semantics-test-portable SUDO=
python3 -B scripts/tests/ci_execution_graph_test.py
```

The portable semantics profile excludes physical-backend semantics and is not
full Linux hardware coverage. TLS permission fixtures use container-native
storage because the macOS bind mount does not provide normal Linux overwrite
permissions. Both paired suites must pass for the intended source pair; a memory
carrier result alone does not satisfy either integration gate.

The rendered Fleet test is
`CHROME_BIN=/usr/bin/chromium node tests/wtp_pi_fleet_ui_integration_test.js`
from `WsprryPi-UI`. Chromium 156 captures at 1280×900 and 390×844 cover
32 rows in both themes, first/last rows, long names, recovery messages, final
assignment admission, capacity refusal, missing/malformed advertisements,
retained-context accounting, actions on a reassigned identity, and visibly
disabled actions while Fleet state is unreadable. Impeccable
review retains the existing visual language; its detector returned no findings.
Operator documentation passes its strict Sphinx HTML build, extension test,
and desktop/mobile rendered review. Existing Setup screenshots depict unchanged
general controls.

Adversarial review addresses save/reload size asymmetry, union accounting for
saved and retained contexts, bounded recovery-worker creation, completion/pause
races, hidden actions on reassigned identity rows, and deterministic fixture
readiness. The completion handler consumes confirmed cleanup before handling a
paused assignment; uncertain outcomes retain their barriers. Reassessment uses
fixed logical launch instants and explicit ARM/completion evidence without
relaxing protocol validation or substituting older companion sources.

### Fleet Operations paired fixture

Build `wtp-fleet-operations-driver-build` with the simulated-only profile. Run the shipped browser client through the actual selected Pico drivers after the two canonical integration builds:

```sh
make -C src wtp-fleet-operations-driver-build BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/bin/wtp_fleet_operations_driver network "$PWD/src/build/wtp-network/pico_tls_server" "$PWD/src/build/wtp-network/credentials-v3"
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 node WsprryPi-UI/tests/fleet_operations_pair_test.js src/build/bin/wtp_fleet_operations_driver usb "$PWD/src/build/usb-reference/pico-host/endpoint_driver" "$PWD/src/build/wtp-network/credentials-v3"
node WsprryPi-UI/tests/fleet_operation_ui_test.js
```

The owning coordinator and browser correlation run unchanged. The network case uses the actual Pico TLS server; the framed USB case uses the actual Pico endpoint driver over a pipe carrier. A separate production Pi authority/engine uses an explicitly constructed simulated backend. Both cases check selected-target isolation, saved scheduling intent, priority Pause and owned cleanup. The canonical USB management pair separately covers production UMP framing/ACK, serialization and journals. The rendered Operations fixture checks presentation, keyboard/modal behavior, freshness and replacement safety in both themes and sizes; it does not qualify the carrier. These are hardware-free software fixtures, not physical USB, clock, GPIO, service or RF evidence.
