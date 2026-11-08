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

`GET/POST /api/control/v1/host/fleet` returns the assignment document, per-output runtime
observations and a quoted ETag. POST requires `If-Match` and the existing host
request guard. Operations are `assign`, `enable`, `pause`, `schedule`, `recover`,
and `remove`. See [Pi endpoint operation](wtp-pi-operation.md) for inbound
listener and target-side takeover controls.

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
eight. Before downgrading, pause, reconcile, and explicitly remove enough
assignments using the current software. Preserve a private backup for recovery;
never truncate the file or erase an in-flight barrier to force an older version
to load it. Software fixtures do not establish capacity on 32 physical radios.

### Capacity validation

The hardware-free capacity target exercises the production Fleet coordinator,
store, scheduler, session, framing, server `Endpoint`, and `JobService`. Sixteen
endpoints advertise Pi identities through `WtpPlainListener`; sixteen advertise
Pico identities through a loopback byte-carrier fixture. All use inhibited RF
engines. The test qualifies host software, not firmware execution on 32 boards.
A coherent projected fixture clock and a test-only Linux `adjtimex` linker
wrapper provide clock qualification without changing the host clock. The
executable queries interfaces in its current network namespace and refuses to
run while any non-loopback interface is up.

The workload admits 32 assignments, rejects another before connecting, reloads
all 32, completes a shared UTC slot, and checks a second slot with a 150 ms Pi
management delay, a disconnected target, and a restarted target. It checks pause
completion, explicit restart reconciliation, takeover with an unreachable
controller transport, retained-context refusal, successful capacity reclamation,
removal, and shutdown. The separate store test covers every count through 32,
maximum connection fields and finite schedule parameters, full removal history,
32 crash barriers, exact file-size boundaries, duplicate/self identities, and
revision preservation. Its populated known-field state is 206,350 bytes; the
528 KiB save/read limit also bounds otherwise unconstrained inactive/extension
settings. Refused saves leave the prior document and revision unchanged.

Run the coordinator fixture inside a private network namespace or a dedicated
container with no external network. The CI recipe builds the executable before
entering the namespace:

```sh
make -C src wtp-fleet-test build/bin/wtp_fleet_capacity_test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
sudo unshare --net sh -c 'ip link set lo up; exec ./src/build/bin/wtp_fleet_capacity_test'
```

The November 2026 software run used Debian GCC 12.2 on LinuxKit aarch64 with
18 reported CPUs. Three passing mixed workloads observed 36,988–37,224 KiB peak RSS, 162 threads,
132 open descriptors, and 19–147 ms shutdown. These process measurements include
all endpoint fixture threads, sockets, and HTTP services as well as the
controller. They are observations, not resource guarantees for a deployed Pi.
The architecture retains one runtime and worker per output, serialized durable
store updates, and one process-wide outstanding resolver lookup. The test uses
loopback addresses and local hostname resolution; arbitrary external DNS stalls,
physical USB capacity, Wi-Fi congestion, services, and RF remain unqualified.

The paired validation selected Pico `devel` commit
`f6c90a4925d02d60b7404487e24316f239c48e7c` from a clean isolated checkout,
with Mbed TLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` and its pinned
submodule. It excluded changes in the independently active Pico checkout.
The Pi source was the candidate based on
`f59bba741ff367d0652f6db03b0bf67d4d10444a`, including the Fleet diff.
The required recipes passed:

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
full Linux hardware coverage. TLS permission fixtures used container-native
storage because the macOS bind mount did not provide normal Linux overwrite
permissions. Initial fixture failures were corrected and affected checks rerun;
the final production network-runtime rerun reported 916 passing checks.

The rendered Fleet test passed via
`CHROME_BIN=/usr/bin/chromium node tests/wtp_pi_fleet_ui_integration_test.js`
from `WsprryPi-UI`. Chromium 156 captures at 1280×900 and 390×844 covered
32 rows in both themes, first/last rows, long names, recovery messages, final
assignment admission, capacity refusal, missing/malformed advertisements,
retained-context accounting, and actions on a reassigned identity. Impeccable
review retained the existing visual language; its detector returned no findings.
Operator documentation passed its strict Sphinx HTML build, extension test,
and desktop/mobile rendered review. Existing Setup screenshots depict unchanged general controls.

Adversarial review resolved save/reload size asymmetry, union accounting for
saved and retained contexts, bounded recovery-worker creation, a completion/pause
race that could retain a confirmed-complete in-flight barrier, and hidden row
actions surviving a skipped intermediate reconciliation poll. The completion
handler now consumes confirmed cleanup before handling a paused assignment;
uncertain outcomes retain their barriers. Final reassessment passed the repeated mixed
workload and complete-diff review without relaxing protocol validation or
substituting older companion sources.
