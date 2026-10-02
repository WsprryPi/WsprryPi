# Pi WTP endpoint and local control

Implementation reference for the development-gated Pi/Pico fleet workflow.
The [local contract](wtp-pi-endpoint-contract.md) defines the requirements and
[implementation prompt](development/wtp-pi-implementation-prompt.md) defines the
execution scope. Protocol authority is the WsprryPico
[protocol directory](https://github.com/WsprryPi/WsprryPico/tree/devel/docs/protocol),
pinned for this implementation to `c13fc16819749e9b53a42609f31702c3aca9931c`.

## Independent controls

| Control | Effect |
| --- | --- |
| Local Transmit enabled | Reserves this Pi's physical output, including idle gaps. |
| WTP Server Enabled | Opens inbound Plain LAN inspection/control connections. |
| Each output assignment Enabled | Runs that remote output's independent schedule. |

A central Pi can keep its own schedule while managing zero through eight remote
outputs. Inbound admission never makes an outbound assignment, and an outbound
assignment never enables local transmission. The existing process singleton
prevents a second normal WsprryPi process from bypassing the managed authority.

## Listener

```ini
[WTP Server]
Enabled = true
Port = 31417
Interface = auto
```

`--wtp-server-port 31418` overrides the listener port for that process without
rewriting the INI. The port accepts values 1–65535. `Interface=auto` requires
exactly one operational physical LAN IPv4 address. With multiple eligible
addresses, specify an interface such as `wlan0` or `eth0`. Only RFC1918 addresses
on an up/running physical interface are eligible; Wi-Fi must report station
mode. Bridges, tunnels, AP interfaces, loopback and public addresses are excluded.
The listener retries after interface recovery and withdraws its advertisement
when the selected address disappears. A configured listener or output settings
change waits for current ownership/work to end, while admission of new remote
jobs is suspended.

The Linux listener binds both the selected address and its interface before
accepting connections. TCP replies follow that interface even when another
interface becomes preferred by equal-metric host routes. No persistent routing
rules or route-metric changes are required. If interface binding fails, the
listener reports the error and remains unavailable without advertising or
falling back to an unrestricted socket. Interface recreation, address loss and
recovery cause withdrawal and a fresh interface-bound listener.

The managed listener is default-on after confirmed startup quiescence. Local
Enable off allows a claim when output is safe and idle. Local Enable on still
allows inspection but refuses a claim. Direct one-shot CLI invocations do not
start the managed server or fleet.

Avahi publishes `_wtp._tcp.local.` on the selected interface with the actual
listener port, first TXT `txtvers=1`, and `binding=plain`. An endpoint-specific
SRV hostname resolves to the selected listening address even on multihomed Pis.
Publication retries after an Avahi outage. Failure of Avahi does
not remove the numeric listener. Builds without Avahi expose publication as
unavailable. Plain LAN is unencrypted and all clients share WTP's `local-network`
principal; a device ID observation is not authenticated. The Pi server does not
provide TLS in this implementation. Existing outbound authenticated TCP remains
available for compatible Pico targets; there is no automatic binding fallback.

## Local takeover

On the target Pi, selecting local Enable while remote ownership exists opens
an inline confirmation. For an armed or running job, choose **End now** or
**Let it finish**. **Cancel** leaves ownership and saved Enable unchanged. Both
accepted choices immediately reject replacement remote jobs and revoke saved
future assignments. The central controller has no approval or veto.

“Let it finish” saves Enable immediately but leaves effective local output
inhibited until the current job finishes and output-off is confirmed. The file
monitor recognizes that same interactive transaction. An external enabled INI
write or direct HTTP write explicitly setting `Operation.Transmit=true` is
noninteractive and aborts remote work immediately. An unrelated HTTP settings
patch preserves the current interactive choice.

Unconfirmed output-off inhibits local transmission and new claims. **Reconcile
local output** performs a local stop/reset and creates a new WTP boot identity;
it never asks the remote controller. The takeover generation is stored in a
private `~/.wsprrypi-wtp/revocation-v1` journal belonging to the process user.
An unreadable or malformed journal inhibits admission. Preserve this file and
run managed instances under a consistent user; removing persistent state can
erase takeover history.

The output-disable timeout applies while the process is responsive. Forcibly
terminating the legacy GPIO process can leave its clock output active until
startup quiescence runs on restart. A killed process or closed connection does
not confirm output-off; inspect the restored endpoint and reconcile its output.

## Backend-derived endpoint capability

The endpoint uses the selected native GPIO, RP1 GPCLK or Si5351 backend, or the
explicit simulated backend. Native backends expose WSPR, TONE, QRSS, FSKCW and
DFCW through the same WTP ownership, scheduling and local takeover path. CAPS
modes and numerical frequency envelope come from the selected backend's
`capabilities()`. Backend selection remains explicit.

GPIO uses the selected pin, drive and existing system-clock/manual correction
settings, with the exact processor clock model and frequency policy. RP1 uses
its kernel provider, selected GPIO4/GPIO20 route and drive, retaining its existing
development authorization. An RP1 WTP job requires the existing host-side
development confirmation for that exact WTP job ID; ordinary Fleet scheduling
does not create that confirmation. Si5351 uses its existing bus/address/reference,
CLK output, drive and PPM settings. Its qualified bands from 2200m through 2m
retain their status; 8m/5m retain their separate experimental policy. A numerical
frequency envelope does not authorize every intervening frequency.

The server accepts finite contiguous RF-event plans within the protocol's 512
events and 24-hour total duration limits, including terminal RF-off. These are
protocol resource limits, not restrictions to a single band or a ten-second
tone. For an initial RF-on event, launch is observed at its output-enable transaction;
for an initial RF-off interval, launch observes admission of the silent timeline.
Unsupported modes and invalid tone sets fail before ARM.

Realized frequencies come from the backend's configured native clock/tone plan
and are reported per event in integer nanohertz. Explicit adjustment consent is
required when realization differs from the requested value. The native backend
executes the same precomputed event plan, including frequency changes and RF-off
gaps. GPIO realization includes the finite DMA dither counts; RP1 uses its
provider tone program and Si5351 uses its joint multi-tone planner. These are
planned frequencies, not measured RF accuracy.

The server reads Linux kernel UTC synchronization, leap state and accumulated
maximum error through `adjtimex`, adding the UTC/monotonic sample bracket.
Unsynchronized or unusable clock observations reject ARM/start. Configured CAPS
use 2 seconds minimum ARM lead, 500 ms maximum admitted UTC uncertainty and
5 seconds output-disable timeout. These are enforced limits; SDR visibility
does not establish frequency accuracy, spectral quality or long-term timing.
The backend initializes during ARM lead, admits the final output-enable write
at the requested start, anchors the finite event timeline to successful enable, and reads
back the selected route's output-off state during cleanup: fresh GPCLK/DMA/PWM
quiescence for GPIO, bounded provider stop/drain for RP1, and the output-disable
register for Si5351. Actual launch observations are
available on the status resource. Physical validation evidence is recorded
separately from simulated tests.

For a LAN Pi, set the controller profile's **Start Uncertainty ns** to a value
that accommodates its reported `GET_CLOCK` uncertainty, up to `500000000`.
The existing 1 ms client default remains available for tighter-clock endpoints;
it may reject an otherwise synchronized Pi using ordinary NTP. Custom profile
values are never silently increased.

Explicit `--backend simulated` runs the same managed arbitration and protocol
without hardware. It persists across managed reloads for the process lifetime
and is never written to the INI or chosen as a failure fallback.

## Local HTTP resources

All routes use the existing host peer/Host/Origin guards. The API is for the
local Pi's own web application; WTP job operations remain device-neutral.

- `GET /api/v1/host/wtp-endpoint`: listener, local requested/effective Enable,
  remote ownership/job, output uncertainty, takeover generation and launch observation.
- `POST /api/v1/host/wtp-endpoint/enable`: requires current config `If-Match` and
  JSON `choice` (`end_now` or `finish_current`), `confirmed`, `observed_owner`,
  and `observed_job`. A claim/job race returns confirmation-required with fresh
  status. An unconfirmed request changes nothing when remote ownership exists.
- `POST /api/v1/host/wtp-endpoint/recover`: JSON `{"confirmed":true}` requests
  local output recovery. Success requires confirmed inactive output.
- `GET/POST /api/v1/host/fleet`: independent assignment management, described in
  [Fleet](wtp-fleet.md).

The bounded listener accepts eight concurrent sockets with a 30-second idle
limit. The authority retains up to 512 session epochs per service boot to prevent
pre-takeover sessions reclaiming after local Disable; exhausted epoch capacity
allows inspection only until local recovery/restart. This does not alter WTP's
normal lease, response replay or retained-job bounds.
