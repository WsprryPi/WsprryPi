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
when the selected address disappears. A route/settings change waits for current
ownership/work to end, while admission of new remote jobs is suspended.

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

## Initial endpoint capability

The first physical route is Si5351, using the existing bus/address/reference,
CLK output, drive and Si5351 PPM settings. It accepts one finite `tone` RF-on event,
14.000–14.350 MHz, with duration at most 10 seconds. It reports integer-nanohertz
planner realization and requires explicit permission for frequency adjustment.
The controller's canonical 1 ns terminal RF-off event is also accepted; CAPS
therefore permits two events and a total duration of 10 seconds plus 1 ns.
Unsupported modes/routes fail before ARM and never select another backend.

The server reads Linux kernel UTC synchronization, leap state and accumulated
maximum error through `adjtimex`, adding the UTC/monotonic sample bracket.
Unsynchronized or unusable clock observations reject ARM/start. Configured CAPS
use 2 seconds minimum ARM lead, 500 ms maximum admitted UTC uncertainty and
5 seconds output-disable timeout. These are enforced limits; SDR visibility
does not establish frequency accuracy, spectral quality or long-term timing.
The backend initializes during ARM lead, admits the final output-enable write
at the requested start, anchors tone duration to successful enable, and reads
back the Si5351 disable register during cleanup. Actual launch observations are
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
