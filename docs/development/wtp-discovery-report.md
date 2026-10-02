# Live DNS-SD discovery acceptance

2026-10-02, `devel`, base `960914333884158c87040c030d49ecaeb8795e65`.
Native observations run from 11:41 through 12:12 UTC, with final cleanup and
endpoint inspection afterwards.

**The four DNS-SD checks passed on the installed wspr5 application.** The
campaign also exposed a WTP reply-routing limitation and a wspr4 configuration
API stall. Their current disposition is recorded in the follow-up TODOs below; this is not complete
multihomed WTP transport acceptance or a claim of overall release readiness.

## Cases and evidence

| Case | Actual test | Result |
| --- | --- | --- |
| WiFi link loss | NetworkManager disconnected/reconnected wspr5 `wlan1`, with an independent 120-second rollback timer. | WiFi candidates became unavailable; Ethernet candidates and the selected Ethernet WTP listener survived; WiFi candidates returned. |
| Ethernet link loss | Disconnected/reconnected `eth0`, managed through WiFi, with automatic rollback armed. | Ethernet candidates and the selected listener/publication withdrew; WiFi candidates survived; Ethernet discovery and publication returned. WTP negotiation subsequently exposed the routing finding below. |
| Address change | A temporary NetworkManager profile and locally administered MAC obtained `192.168.1.62` by DHCP; the original profile/MAC/address were restored. | wspr2 received the new endpoint-specific A record and successfully identified the same full WTP device and boot IDs. Identification succeeded again after restoration to `192.168.1.54`. |
| Multihomed discovery | Observed `eth0` and `wlan1`, withdrew each separately, and restored both. | Interface-specific candidates remained distinct; loss of one observation path preserved the other; both recovered. Automatic WTP reply-path recovery remains open. |
| Natural cache expiry | Suppressed only incoming Pico A IPv4 UDP/5353 on both wspr5 interfaces using temporary traffic-control filters and a 300-second rollback timer. | Both existing candidates expired naturally, were rejected for identification, and recovered after filter removal. Seven unrelated candidates remained online; wspr2 still observed Pico A. |

The active interfaces were Ethernet `192.168.1.54/24` and station WiFi
`192.168.1.117/24`. Other WiFi interfaces were disconnected and stayed so.
The listener selected `eth0`, port 31417, with endpoint-specific SRV target
`wtp-e045017b11e54c2fb7029f0154e6deb9.local`. No SSID/password changes,
router changes, recabling, RF jobs, CLAIM, LOAD or ARM were part of the campaign.
Pico A participated in discovery/identification; Pico B was not contacted.

### Expiry was measured, not inferred from a goodbye

Pico A's ordinary PTR/SRV/TXT/A records advertised 120 seconds. Native removal
occurred **120.736 seconds on Ethernet** and **121.459 seconds on WiFi** after
the last captured pre-filter PTR response. The completed suppression interval
was 119.984 seconds; the refresh responses preceded filter installation.
UTC and monotonic durations agree. No relevant TTL-zero response was captured
during that interval. Kernel filter counters recorded eight Ethernet and four
WiFi packet drops. Packet sockets observe traffic before those ingress drops;
the counters and native API transitions establish suppression at Avahi.

Both expired candidate IDs produced HTTP 409 with
`Discovery candidate is unavailable or stale`. Their historical `removed`
records remain in the bounded API snapshot; they are unavailable for use.
Removing the filters and sending ordinary multicast queries restored both
observations. Read-only identification returned Pico A's original full ID.
No shorter TTL or synthetic advertisement was injected, and no browser,
Avahi daemon or link was restarted during expiry. The Pi PTR/TXT 4500-second
lifetime was captured but was not separately waited out; this live expiry
case exercises Avahi's shared cache using Pico A's normal 120-second lifetime.

## Follow-up TODOs

- [x] **First: isolate Si5351 probing from GPIO configuration and discovery.**
  Public configuration reads must not perform I2C transactions. Explicit inventory
  refresh must have a deadline and leave configuration reads, updates and local
  management responsive with absent or unresponsive hardware. Preserve saved
  settings, fresh selection validation and runtime readiness. Implementation and
  live regression evidence: [execution prompt](si5351-inventory-isolation-prompt.md)
  and [completed repair](si5351-inventory-isolation-report.md). **Closed.** The
  Si5351's lack of response is outside this testing scope and is not an open
  acceptance issue; responsive management with unresponsive hardware passed.
- [x] **Next: make WTP reply routing survive equal-metric route reordering.**
  Reproduce Ethernet reconnection while WiFi is preferred, make the selected WTP
  interface govern replies, and prove recovery without manual route repair.
  **Closed:** [executed prompt](wtp-reply-routing-prompt.md),
  [implementation and live acceptance](wtp-reply-routing-report.md), and
  [adversarial reassessment](wtp-reply-routing-review.md). Nine live HELLO/STATUS
  checks passed with WiFi preferred, including two Ethernet reconnect cycles;
  every captured reply used Ethernet without route-preference repair during
  acceptance. Original network state was restored afterwards.

## Operational findings from the discovery campaign

### WTP replies can follow the wrong interface after route reordering

Ethernet reconnection changed the order of two equal-metric routes. The listener
correctly rebound and republished its Ethernet address, but TCP replies from
`192.168.1.54` left through WiFi. A packet trace shows repeated, unacknowledged
HELLO replies on `wlan1` while the client requests arrived on `eth0`.
Restoring the original Ethernet route preference made independent WTP negotiation
succeed. The successful DHCP-address case temporarily preferred Ethernet and
restored the original route order afterwards.

At the end of this campaign, the operational limitation required a repair and
retest with WiFi as the preferred equal-metric route. This campaign's original
observations do not establish automatic WTP transport recovery in that topology.
The subsequent [selected-interface repair](wtp-reply-routing-report.md) reproduces
the original failure and validates usable Ethernet replies through two live
reconnect cycles with WiFi still preferred. The failure evidence above is retained.

### wspr4 configuration snapshots can block on Si5351 inventory

wspr4 initially served discovery and captured the topology changes. Its local
HTTP API subsequently stalled. The retained backtrace shows callers waiting in
`get_public_config_snapshot()` and a caller in `Si5351Device::readRegister()` /
`probe()` through `discover_si5351_addresses()` and public configuration
serialization. A kernel observation also reported `bcm2835_i2c_xfer`.

The snapshot holds the configuration mutex while collecting Si5351 inventory,
including when GPIO4 is the configured transmitter. A service restart restored
the endpoint; requesting discovery reproduced the blocked configuration read.
A second restart drained that request. Final independent WTP STATUS showed
wspr4 empty, unowned and inactive. Its binary and INI were not replaced.
At the end of this discovery campaign, the underlying probe/locking defect
remained unresolved; neither restart was a repair. The subsequent
[Si5351 isolation repair](si5351-inventory-isolation-report.md) decouples physical
inventory from public configuration serialization, bounds explicit refresh, and
verifies responsive GPIO management during an actual I2C timeout on wspr4.

wspr2 replaced wspr4 as the healthy independent observer for the accepted
address-change and cache-expiry cases. Failed wspr4-dependent attempts are
retained and excluded from those accepted results.

## Restoration and validation

wspr5's original Ethernet/WiFi addresses, Ethernet MAC, saved profiles, routes
including original Ethernet preference, and root qdiscs were restored. Temporary
DHCP profiles, multicast filters, observers and rollback units are absent.
All tracked baseline hashes match: installed binary, INI, five assignments,
catalog, Avahi configuration and the selected NetworkManager profiles. All five
assignments remain paused with identical definitions and consumed-slot watermarks.
Local Enable remains off; the controller service PID and WTP boot ID are unchanged.
Chrony remains active and `/dev/pps-gps` still resolves to `/dev/pps0`.

Final direct LAN HELLO/STATUS checks on wspr1, wspr2, wspr4, wspr5 and Pico A
reported empty, unowned and inactive output. wspr4's service was restarted for
the recorded stall; wspr5 and wspr2 services were not restarted. Pico B's
physical state was not assessed. The private network backup remains root-only
mode 0600 on wspr5 and was excluded from transferred/repository evidence.

| Validation | Result |
| --- | --- |
| Installed native Avahi/mDNS and WTP identification tests | DNS-SD cases passed; open transport/API findings above |
| `make wtp-catalog-test SUDO= BACKENDS=simulated ANCILLARY_GPIO=0` on the Mac | Failed at link: installed SDK/TAPI cannot parse `arm64e.x1-macos` |
| Same target with `CXX=/usr/bin/clang++` on the Mac | Same SDK/linker failure; no local test executable ran |
| `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-catalog-test SUDO= BACKENDS=simulated ANCILLARY_GPIO=0` on wspr5 Linux | Passed using GCC 14.2 and an archive of the exact base source outside the installed app. Archive metadata generation warned that `.git` was absent; the test built and independent execution returned zero |
| Captured-data assertions, JSON parsing, Python syntax, whitespace checks | Passed |
| RF, UI/browser rendering, full semantics suite and CI | Not run for this network-only campaign; no application or UI source changed |

See [case assertions](wtp-discovery-results/case-verification.json),
[wire/TTL evidence](wtp-discovery-results/wire-evidence.json),
[restoration](wtp-discovery-results/restoration.json),
[final direct WTP observations](wtp-discovery-results/final-wire.json),
[wspr4 service recovery](wtp-discovery-results/wspr4-final-service-health.json),
[observation counts](wtp-discovery-results/observation-summary.json),
[execution events](wtp-discovery-results/execution-events.json),
[source manifest](wtp-discovery-results/source-manifest.json), and
[adversarial review](wtp-discovery-results/adversarial-review.md).
Full observation streams remain in the private staging areas; substantial
transfers used compressed tar streams. Final harnesses and analysis accompany
the report. Intermediate harness versions were not individually hashed.

## Documentation Impact

- Updated: this development report, execution contract, review and evidence.
- Considered unchanged: `docs/wtp-fleet.md`, `docs/wtp-pi-operation.md`, and
  `Wsprry_Pi_Docs/docs/Advanced_Operations/rest_api.md` / configuration `runtime.md`.
  The accepted DNS-SD behavior matches their current contracts. Existing
  WsprryPico feature flags and UI visibility were preserved.
- Follow-up required with the operational repairs: document any persistent
  routing requirement if that becomes the selected solution; update inventory
  refresh guidance if its operator workflow changes.
- No operator-documentation repository or UI component was modified. No
  application implementation or reusable component path was modified.

The work started with a clean `devel` checkout. Only these new development
documents/evidence are pending; this execution request did not authorize a
new commit or push. These retained observations are now included with the
authorized follow-up implementation commit; current repair status is listed above.
