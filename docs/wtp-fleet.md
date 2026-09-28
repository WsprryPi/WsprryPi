# WTP Fleet catalog and local discovery

Fleet is a session-visible development pane under Setup. It remains hidden and
disabled on every page load until the existing `WtpUi.developmentControlsVisible`
boolean is explicitly set for that browser session. Hiding Fleet does not
change the selected backend, `[WTP]`, a pending job, or saved drafts.

The host keeps one active endpoint authority: the existing `[WTP]` INI section
and runtime configuration. A separate version 1 catalog at
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

The accepted TXT format is version 1: `txtvers=1` must be first, and exactly
one `binding=tls` or `binding=plain` must appear. Required keys are duplicate-checked
without ASCII case; unknown keys are ignored. TXT does not contain a port,
device ID, WTP version, credentials, or RF capability. HELLO and CAPS provide
protocol/device facts. The service name and direct-connection default port
31417 are provisional pending IANA assignment. Discovery is link-local and
does not imply that a Pico listener is reachable or RF ready. See the
[Pico DNS-SD profile](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/WTP-DNS-SD.md)
and [WTP/1 protocol](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/WTP.md).

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

Protected host resources are `GET /api/v1/host/discovery`,
`GET/POST /api/v1/host/devices`, `POST /api/v1/host/discovery/identify`, and
`POST /api/v1/host/devices/use`. The same trusted-LAN peer, proxy, Host and
Origin policy applies to each route. POST also requires same-origin JSON intent
headers and a 32 KiB body limit. Catalog POST uses `If-Match` for the catalog;
use carries `catalog_revision` in its JSON body and uses `If-Match` for the
active host config. Missing revisions return 428 and stale revisions 412.
There is no browser-local credential store or independent job authority.

`make wtp-catalog-test` exercises the injected discovery store, TXT parser,
nondefault SRV port, interface collision, loss/restart/failure, legacy import,
revision guard, and Plain LAN consent without an Avahi daemon. The portable
Mac profile compiles the unavailable adapter. Linux CI declares Avahi
development headers and compiles the native adapter. These are source/software
checks. Live Avahi/multicast discovery, listener advertisement, physical
TCP/TLS acceptance, GPIO, services, and RF remain separate qualification.
The network interop reference at `src/tests/network/pico-reference-revision.txt`
still pins `95aac22bf7cc31b67fe5773761708d743d8a6473`; its WTP wire
coverage does not exercise the later published DNS-SD advertisement commit
`c13fc16819749e9b53a42609f31702c3aca9931c`. Use that published Pico
revision for a separately authorized live/adapter discovery campaign before
claiming advertisement interoperability.
