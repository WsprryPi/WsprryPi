# Pico control and configuration over USB

The normal WsprryPi application supports USB WTP/1 jobs plus the separately
versioned USB-Management/1 channel on matching normal WsprryPico firmware.
Pico Wi-Fi credentials, an AP, DNS, SNTP and engineering certificates are not
prerequisites. Firmware without the management role retains its supported WTP
behavior; USB management reports unavailable without probing Console or falling
back to a network connection.

## Workflow and identity

In Signal Setup, select **Show Pico connections and settings**, then configure
and select the explicit USB WTP endpoint, serial and VID/PID. An expected full
device ID can be supplied; otherwise the runtime learns it on first negotiation
and pins subsequent observations for that selected runtime. Use **Reconcile Pico**
to establish ownership/output observations. Selecting USB does not enable
transmission or take over another job.

The management adapter pairs interface 4 with selected WTP interface 2 under the
same canonical Linux sysfs USB parent, exact serial and VID/PID. Console interface
0 is never probed. Tty numbering and mutable names are not identity. Every
management envelope binds the full device ID, boot ID and connection session.
Device metadata and physical access are local authority, not cryptographic proof.
Host tty permissions remain the access boundary. Network authentication is
unchanged.

The Pico's verified configured hostname is its primary common name in connected
status and active device selection. The USB connection method and full identity
remain separate. Names pending restart are distinguished from the active name;
cached unconfirmed names are marked last confirmed. A mutable common name never
owns a job or substitutes for an identity. Duplicate fleet names retain their
secondary identity information.

In **Pico standalone settings and Wi-Fi**, select **USB cable** if another
management carrier was selected. Shared station, hardware, schedules and
configuration editors retain validation, revision, saved-versus-active and
ownership rules. The **USB name and Wi-Fi** section provides an explicit read,
name save, Wi-Fi save, removal, restart and bounded AP request. Management edits
require idle/unowned output and a fresh read. UTC refresh uses its separate
channel while finite jobs execute locally.

## Management coverage

| Operation | USB behavior |
| --- | --- |
| Identity/capabilities/status | Negotiated without Wi-Fi or valid UTC |
| Station, hardware and pin preview | Existing shared application/resource handlers |
| Complete configuration and schedules | Existing preserving configuration coordinator; no duplicate schema |
| Saved progress | Read partial-field outcomes/readiness, save station units independently, explicitly apply saved station; API also retains supported domain update/apply semantics |
| Common name | Existing hostname resource; consumer/default names are writable, certificate-bound engineering names remain protected |
| Saved Wi-Fi | Create, replace/preserve password, remove last and recreate |
| Network enable/disable | Existing network control resource; USB remains connected |
| Restart | Existing revision-bound deferred restart, applied only after response acknowledgement |
| Setup AP | Explicit new bounded request; cannot renew an already active deadline |
| Jobs/abort/completion | Unchanged complete-job WTP/1 and local RP2350 timing |
| UTC | Qualified host sample/submit; existing Pico clock arbiter and admission |
| Factory reset | Not added to ordinary USB settings; existing recovery restrictions remain |

Saved station progress is separate from applied configuration and transmit readiness.
Read it, edit the existing station fields, save valid units, then explicitly apply
the saved station. Partial valid fields may persist while the station remains
incomplete; none feeds the scheduler before explicit application.

The current saved-network representation supports one connection. A Wi-Fi save
is durable configuration success, not proof of association or DHCP. Passwords
are redacted as null. Empty password on the UI preserves an existing password;
a new connection requires credentials under the existing supported security
rules. Explicit DELETE carries wifi:null and is distinct from disabling Wi-Fi
or preserving a redacted password. Removing Wi-Fi preserves hostname, station,
pins, schedules, STOP and legitimate watermarks. Pending restart is shown
separately from the active station connection.

USB changes consume the shared revision. Every mutation invalidates editors;
read authoritative state before another change. A lost reply may follow a
committed save. The UI preserves the draft, never retries a mutation, and asks
for review before a new explicit change after uncertain delivery.

## Wire and timing

The third CDC control interface is 4 (CDC instance 2). WTP/1 on interface 2 and
Console on interface 0 retain their wire definitions. Management uses ASCII
UMP1, a four-byte unsigned big-endian payload length, then strict JSON. Request
limit is 32768 bytes and response limit 65536 bytes. One request, response and
acknowledgement transaction is live; identities, strictly increasing IDs,
duplicate keys, nesting, lengths and absolute deadlines are checked. The
resource response carries original status, JSON body string and ETag. A matching
ACK completes response delivery; only its transaction's deferred effects can
then apply. No blind replay or transport fallback occurs.

The production host clock probe is read-only Linux adjtimex. It requires
TIME_OK, no unsynchronized/clock-error/pending-leap flags, nonnegative maxerror
and esterror, and maxerror at most 500 ms. USB exchange charges the larger
clock error, the rounded-up half-width of the sample request/response interval,
measured wall/monotonic skew and the unchanged 100 microsecond processing/drift
reserve. The sample is bracketed before its request and after its response,
before the mandatory delivery ACK. A midpoint and this half-window bound cover
fully asymmetric delivery; no symmetric path is assumed. ACK waiting does not
move the sampled instant; Pico clock aging still charges subsequent elapsed time. A clock step exceeding
1 ms, regressing clock or exchange over one second is rejected. Bounds are
reported without clamping to a transmitter admission limit.

A one-use boot/session-bound Pico sample is submitted within its finite expiry.
Background refresh runs at most every five seconds, with an additional refresh
before ordinary inspect/prepare and each CLAIM/ARM admission. Pico can reclaim
its optional management session to retain full WTP LOAD capacity. The Pi then
makes at most one reopen attempt, verifies the same device and boot, reads the
name and takes a fresh time sample. Configuration writes are never replayed.
The existing ARM notice still rejects preparation that consumes its allowance.
A verified mismatch between USB device/startup identities blocks new jobs and
requires explicit reconciliation; ordinary management unavailability does not
create that fault. A later background read cannot clear the mismatch.
Exchange failure does not strengthen or extend the Pico clock. Pico source
arbitration, disagreement latch, uncertainty, launch and holdover limits remain
authoritative. A healthy SNTP source can remain selected; inner accepted and
selected fields are reported separately from a transport status of 200.

The unchanged default Pi job maximum is 1000000 ns (1 ms). An accepted USB
UTC observation can exceed that per-job limit and therefore still refuse
CLAIM/ARM admission. The bounded target check observed 56.7–71 ms using the
production client and qualified host; it did not arm a job or qualify default
job readiness. Do not widen or clamp timing limits to hide this distinction.
The subsequent [clock investigation](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/usb-time-uncertainty.md)
corrects the sample bracket and records its remaining measured uncertainty.
Job timing policy is separate from transfer accounting; the 1 ms default
remains unchanged. Runtime observations now report host error, sample round
trip and midpoint radius, clock discrepancy, processing margin and ACK duration
separately. A 100 ms ordinary-operation UTC policy is a documented proposal,
not an applied setting.

STOP and owned-job cancellation signal the USB transaction before waiting for
runtime authority, so a silent management read is interrupted cooperatively.
The native descriptor uses bounded nonblocking transfers. Physical kernel/USB
scheduling and hard RF timing remain separate target qualifications.

USB UTC synchronization does not calibrate RF frequency or an SDR sample clock.
Pico Wi-Fi disabled does not imply disabling the Pi's network or time service.

## Preservation and validation

This slice changes no job/event/frame/duration limits, supported waveform modes,
scheduler recurrence authority, uncertainty/holdover/launch limits or RF
servicing reserves. Existing standalone, consumer Wi-Fi, Plain LAN, engineering
TLS/HTTPS and engineering BLE paths remain, with the explicitly selected finite
AP lifetime policy on the Pico. Final firmware resource and physical evidence
is recorded with the coordinated Pico implementation, not inferred from host
checks.

Hardware-free checks from src:

```sh
make wtp-usb-test wtp-usb-management-test wtp-api-test wtp-config-test SUDO=
make wtp-production-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
node ../WsprryPi-UI/tests/wtp_usb_settings_test.js
```

Use an identified Linux environment for C++ builds. The actual-pair runner
builds wtp-usb-pair-client-build and Pico's usb_management_driver. It executes
production Pi framing/browser/configuration adapters against production Pico
USB/application/time handlers over fragmented anonymous pipes with erased
volatile flash and no network. Run the UI producer with the executable pair:

```sh
node WsprryPi-UI/tests/wtp_usb_pair_test.js   src/build/wtp-network/wtp_usb_pair_client /absolute/path/to/usb_management_driver
```

This qualifies software interoperability, not Linux USB topology, electrical
behavior, physical unplug/re-enumeration, AP advertising, RF or target timing.
The dedicated USB CI pin selects Pico product
`100e749c815f7485d5097c9086852d3b97adca9f`. Exact source manifests, independent
review, command results and bounded physical USB/AP evidence are recorded in
the coordinated Pico execution record. Unpublished source does not claim a remote CI pass.

## Documentation impact

Updated this guide, production/USB/browser/configuration references and README.
Historical Phase 10/11/15 records retain their original tested applicability.
The separate Wsprry_Pi_Docs repository is outside this authorization; follow-up
must document the visible Pico control toggle, cable selection/permissions,
common name, USB Wi-Fi create/remove, UTC readiness, uncertain-save readback
and bounded AP request with screenshots of the final verified UI.
