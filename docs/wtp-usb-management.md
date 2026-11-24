# Pico control and configuration over USB

The Pi discovers USB-Management/1 directly on matching Pico firmware. Pico Wi-Fi, DNS, SNTP, and engineering certificates are not prerequisites. Missing management support refuses without probing Console or falling back to another carrier.

## Workflow and identity

In Fleet, save the explicit USB connection and manage that target. Saving does not select an output or enable a schedule. Discover interface 4 directly. Existing interface-2 locators may resolve interface 4 only under the same canonical physical USB parent, exact serial and VID/PID. Validate full device, boot, role, and session identity. Ambiguous or missing roles refuse. Console interface 0 is never probed; tty numbering and mutable names are not identity. Physical USB access and host permissions remain the authority boundary.

The Pico's verified configured hostname is its primary common name in connected status and active device selection. The USB connection method and full identity remain separate. Names pending restart are distinguished from the active name; cached unconfirmed names are marked last confirmed. A mutable common name never owns a job or substitutes for an identity. Duplicate fleet names retain their secondary identity information.

In **Pico standalone settings and Wi-Fi**, select **USB cable** if another management carrier was selected. Shared station, hardware, schedules, and configuration editors retain validation, revision, saved-versus-active, and ownership rules. The **USB name and Wi-Fi** section provides an explicit read, name save, Wi-Fi save, removal, restart, and bounded AP request. Management edits require idle/unowned output and a fresh read. UTC refresh uses its separate channel while finite jobs execute locally.

## Management coverage

| Operation | USB behavior |
| --- | --- |
| Identity/capabilities/status | Negotiated without Wi-Fi or valid UTC |
| Station, hardware, and pin preview | Existing shared application/resource handlers |
| Complete configuration and schedules | Existing preserving configuration coordinator; no duplicate schema |
| Saved progress | Read partial-field outcomes/readiness, save station units independently, explicitly apply saved station; API also retains supported domain update/apply semantics |
| Common name | Existing hostname resource; consumer/default names are writable, certificate-bound engineering names remain protected |
| Saved Wi-Fi | Create, replace/preserve password, remove last and recreate |
| Network enable/disable | Existing network control resource; USB remains connected |
| Restart | Existing revision-bound deferred restart, applied only after response acknowledgement |
| Setup AP | Explicit new bounded request; cannot renew an already active deadline |
| Native status and schedule stop | Authenticated shared status and revision-bound stop; no generic jobs API |
| UTC | Qualified host sample/submit; existing Pico clock arbiter and admission |
| Factory reset | Not added to ordinary USB settings; existing recovery restrictions remain |

Saved station progress is separate from applied configuration and transmit readiness. Read it, edit the existing station fields, save valid units, then explicitly apply the saved station. Partial valid fields may persist while the station remains incomplete; none feeds the scheduler before explicit application.

The current saved-network representation supports one connection. A Wi-Fi save is durable configuration success, not proof of association or DHCP. Passwords are redacted as null. Empty password on the UI preserves an existing password; a new connection requires credentials under the existing supported security rules. Explicit DELETE carries wifi:null and is distinct from disabling Wi-Fi or preserving a redacted password. Removing Wi-Fi preserves hostname, station, pins, schedules, STOP, and legitimate watermarks. Pending restart is shown separately from the active station connection.

USB changes consume the shared revision. Every mutation invalidates editors; read authoritative state before another change. A lost reply may follow a committed save. The UI preserves the draft, never retries a mutation, and asks for review before a new explicit change after uncertain delivery.

The current Pico capability document can include a Test Tone descriptor. The Pi validates its closed engine map, limits, and availability fields during management negotiation. Availability describes the Pico implementation. Native admission still depends on the Pico's current engine, configuration, clock, and output ownership. Direct GPIO uses SDK pad observations, while Si5351 uses current execution-epoch register evidence. The Pi gateway and Consumer-Radio client deny Test Tone Review, Start, Status (including query forwarding), and Stop.

## Wire and timing

The third CDC control interface is 4 (CDC instance 2). Console remains interface 0, Logging interface 6, and interfaces 2/3 are Reserved and inert in application images. RFBench alone retains its non-WTP Commands exception. Management uses ASCII UMP1, a four-byte unsigned big-endian payload length, then strict JSON. Request limit is 32768 bytes and response limit 65536 bytes. One request, response, and acknowledgement transaction is live; identities, strictly increasing IDs, duplicate keys, nesting, lengths, and absolute deadlines are checked. The resource response carries original status, JSON body string and ETag. A matching ACK completes response delivery; only its transaction's deferred effects can then apply. No blind replay or transport fallback occurs.

The production host clock probe is read-only Linux adjtimex. It requires TIME_OK, no unsynchronized/clock-error/pending-leap flags, nonnegative maxerror and esterror, and maxerror at most 500 ms. USB exchange charges the larger clock error, the rounded-up half-width of the sample request/response interval, measured wall/monotonic skew and the unchanged 100 microsecond processing/drift reserve. The sample is bracketed before its request and after its response, before the mandatory delivery ACK. A midpoint and this half-window bound cover fully asymmetric delivery; no symmetric path is assumed. ACK waiting does not move the sampled instant; Pico clock aging still charges subsequent elapsed time. A clock step exceeding 1 ms, regressing clock or exchange over one second is rejected. Bounds are reported without clamping to a transmitter admission limit.

A one-use boot/session-bound Pico sample is submitted within its finite expiry. Background time refresh is independent of a selected transmitter backend. Exchange failure does not strengthen or extend the Pico clock. Pico source arbitration, disagreement latches, uncertainty, launch, and holdover limits remain authoritative. A healthy SNTP source can remain selected; accepted and selected observations are distinct from transport success.

The shared physical management owner also refreshes UTC on its own bounded worker, independently of a selected transmitter backend or an open browser. It services only an already negotiated management connection, yields to a busy settings transaction, and does not reopen a disconnected tty. Firmware maintenance and cancellation suppress refresh. The final channel owner stops and joins the worker before closing the stream. An unqualified host observation supplies no time sample and cannot extend the Pico's clock authority.

Native execution independently rechecks age-grown uncertainty before launch. USB time support changes no measurement accounting, symbol timing, missed-start deadline, holdover, clock-step/leap guard, capacity, or RF servicing budget. See [native execution safety](native-execution-safety.md).

Runtime observations report host error, sample round trip and midpoint radius, clock discrepancy, processing margin and ACK duration separately. A transport observation is not an absolute RF start-error measurement. Deterministic acceptance remains separate from physical/RF qualification.

STOP and owned-job cancellation signal the USB transaction before waiting for runtime authority, so a silent management read is interrupted cooperatively. The native descriptor uses bounded nonblocking transfers. Physical kernel/USB scheduling and hard RF timing remain separate target qualifications.

USB UTC synchronization does not calibrate RF frequency or an SDR sample clock. Pico Wi-Fi disabled does not imply disabling the Pi's network or time service.

## Preservation and validation

USB management preserves native execution limits, waveform modes, recurrence authority, holdover/launch policy, and RF servicing reserves. Current Consumer-Radio and certificate HTTPS management remain independently authenticated.

Hardware-free checks from src:

```sh
make wtp-usb-test wtp-usb-management-test wtp-api-test wtp-config-test SUDO=
make wtp-production-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
node ../WsprryPi-UI/tests/wtp_usb_settings_test.js
```

Use an identified Linux environment for C++ builds. The actual-pair runner builds wtp-usb-pair-client-build and Pico's usb_management_driver. It executes production Pi framing/browser/configuration adapters against production Pico USB/application/time handlers over fragmented anonymous pipes with erased volatile flash and no network. The pair verifies that PUT, merge, and replace preserve the Pico-owned hostname and an incomplete saved draft independently of the applied station. Run the UI producer with the executable pair:

```sh
node WsprryPi-UI/tests/wtp_usb_pair_test.js   src/build/wtp-network/wtp_usb_pair_client /absolute/path/to/usb_management_driver
```

This qualifies software interoperability, not Linux USB topology, electrical behavior, physical unplug/re-enumeration, AP advertising, RF or target timing. USB CI selects the committed Pico revision recorded in `src/wtp_integration/contract/PROVENANCE.json` by default and accepts the explicit `pico_ref` dispatch override. The host-only endpoint build consumes that selected checkout; source revision and dirty state are diagnostic.

## Related configuration

Physical claims remain bounded by their exact tested source and setup identities. The negotiated [Pico GPIO drive extension](wtp-gpio-drive.md) provides four nominal RF-pad choices with explicit restart activation.
