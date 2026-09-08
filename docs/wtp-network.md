# Authenticated WTP network integration

Phase 11.1 adds the parent-owned OpenSSL `TlsStream` behind the existing portable
WTP-Client `ByteStream`. The same application, scheduler, backend, Session,
complete-job conversion, cancellation and recovery paths serve USB and network.
The portable component and its provenance/fixtures are unchanged. There is no
transport fallback or browser-owned scheduler.

## Configuration and migration

Legacy `[WTP]` sections without `Transport` mean `usb`. Their existing endpoint,
serial, VID/PID and device-ID requirements are unchanged. Network fields and USB
fields persist while inactive. The existing uncertainty and frequency-adjustment
consent apply to both transports. A complete network section looks like:

```ini
[Operation]
Transmit Backend = wtp
Transmit = false

[WTP]
Transport = network
Hostname = wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local
TCP Port = 18443
TLS Server Identity =
TLS CA File = /etc/wsprrypi/pico/ca.crt
TLS Client Certificate = /etc/wsprrypi/pico/client.crt
TLS Client Key = /etc/wsprrypi/pico/client.key
Device ID = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
Start Uncertainty ns = 1000000
Allow Frequency Adjustment = false
```

Replace the example identity, port and files with administrator-provisioned
values. There is no assumed production port. Other required application settings
and safety policy still apply; this fragment is not a complete runnable station
configuration. Keep the existing enabled-transmission/boot settings disabled
while selecting and inspecting an endpoint. GPIO peripherals and fades remain
incompatible with WTP. The CLI remains `--backend wtp --ini-file <file>`.

`Hostname` is the connection address. Blank `TLS Server Identity` verifies that
configured hostname or literal IP. For a direct-IP connection, an explicit DNS
identity can instead authenticate a certificate for that name. IP identities
require a certificate IP SAN. DNS names use ASCII labels (IDNs must be provided
as A-labels). ASCII case and one final root dot are accepted; connection, TLS
and HTTP use lowercase without that dot, while saved values retain their spelling.
URL schemes, ports in names, wildcard input, ambiguous abbreviated/octal IP
forms and IPv6 zone identifiers are rejected. Configure the TCP port separately.

DHCP plus mDNS is the intended normal deployment; address reservations are not
required. Every fresh connection resolves again through the host's system
`getaddrinfo` service. On Linux, the host administrator must provide a working
NSS resolver for `.local` multicast DNS, commonly Avahi plus `libnss-mdns` and an
appropriate `hosts` policy. WsprryPi does not install those services, alter
`/etc/hosts`, edit resolver configuration, use reverse DNS as identity, or change
system trust stores. A host lacking `.local` resolution reports connection
failure; a successful `getaddrinfo` call elsewhere does not establish this host's
mDNS readiness. Pico remains IPv4-only even though the existing host adapter
accepts IPv6 configurations for compatible peers.

The Pico deployment's public `deployment.json` and exact certificate DNS SAN
establish its hostname, normally `wsprrypico-<32-hex-device-id>.local`. Provision
using Pico's certificate helper, then build/reflash under the explicit local
deployment procedure. Its mDNS responder announces the DHCP address; it does not
supply trusted identity. A name conflict stops advertisement without renaming or
interrupting a finite job. Resolve the conflict before explicitly retrying the
same certified name through the idle-only disable/re-enable or reboot workflow.

For a known address with hostname authentication, retain the same CA/client
files and device ID and set:

```ini
Hostname = 192.0.2.27
TLS Server Identity = wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local
```

This sends the expected hostname as HTTP Host/Origin after TLS authentication.
If the IP changes, this explicit address must be updated; use the hostname in
`Hostname` for automatic re-resolution. Leaving identity blank with a literal IP
instead requires that exact IP SAN. A browser's literal-IP URL also requires its
matching IP SAN. A hostname certificate never authenticates arbitrary changing
IP URLs. Legacy IP-only bundles/configurations retain numeric authority; adding a
hostname requires a corresponding certified Pico deployment.

DHCP changes do not require certificate renewal for hostname access. Server
renewal under the same per-device CA with the unchanged hostname leaves host
settings intact. A deliberately changed hostname requires a new deployment
certificate, rebuild/reflash and matching host settings. These actions are
separate from the runtime client credential-rotation safeguards below.

### Linux resolver checks

On the intended host, read-only `getent ahostsv4 <hostname>.local` tests its NSS
address path; `resolvectl query <hostname>.local` is useful where systemd-resolved
is already configured, and `avahi-resolve-host-name -4 <hostname>.local` is useful
where Avahi tools already exist. The latter tools alone do not establish that the
application's NSS path works. Inspect `/etc/nsswitch.conf`, the existing resolver
service status and multicast restrictions when resolution fails. mDNS uses
link-local multicast UDP 5353 and usually does not cross routed/VLAN boundaries.
Do not add static hosts entries to conceal stale DHCP resolution. A conflict may
require direct-IP plus expected hostname access to inspect the intended device;
TLS and WTP device checks remain mandatory. Physical Mac/Linux `.local`, DHCP
reassignment and browser trust acceptance are Phase 11.4 gates.

## Credentials and TLS policy

Use maintained OS OpenSSL 3 development/runtime packages (`libssl-dev` and
`pkg-config` on Debian). The parent now discovers `openssl`, linking both libssl
and libcrypto; existing installer dependencies already include libssl-dev.
The build rejects older headers. TLS 1.3, server-chain and expected-name/IP
verification against exact DNS SAN or IP SAN (no CN fallback or wildcard), a matching client certificate/key and exact ALPN `wtp/1` are
mandatory. Idle management uses the same credentials with ALPN `http/1.1`.
There is no plaintext mode, trust-on-first-use, verification bypass, session
resumption cache or early-data submission.

Credential references are absolute local paths, not uploaded PEM or browser
storage. Files must be readable regular files, at most 256 KiB each, with no
final symlink or group/other write permission. Private keys must be owned by the
application user or root, with no group/other permission (typically 0600).
Administrator-managed parent directories must also remain protected. Encrypted
keys requiring an interactive passphrase are rejected without prompting.
Client validity and certificate/key consistency are checked before selection.
The configured CA bundle, not the machine-wide trust store, supplies trust.

The runtime holds an immutable validated credential snapshot and content
fingerprint. An explicit configuration apply/reload compares contents even when
paths are unchanged. Replacement is permitted only when the existing runtime
can be safely replaced. Reconnect and management detect changed/unusable files
and refuse to substitute a different authenticated principal under the old
session. To rotate, first finish/reconcile ownership with the original credential
material, then replace files and explicitly reload while unowned and inactive.
Restoring the original contents permits recovery; changing a pathname alone is
not the rotation detector. Revoking/removing the old credentials during unresolved
work can therefore prevent recovery. No private-key contents or fingerprints are
published in status, logs or configuration responses; protected configuration
responses expose only the administrator's file references.

## Bounds and output authority

System resolution runs outside the WTP worker with one process-wide outstanding
lookup. The caller stops waiting after 3 seconds or cancellation; an unresponsive
platform NSS call cannot be forcibly interrupted and may occupy that one slot
until it returns. Further opens fail closed while it remains outstanding. This
bounds worker shutdown and resource growth without claiming NSS itself is killed.
TCP connection attempts share a 3-second budget and at most eight resolved
addresses; TLS handshake has 10 seconds. The TLS stream owns at most 4096 queued
plaintext bytes with an 8-second write deadline. Session supplies bounded read/
transaction progress budgets. HTTPS has a 20-second total exchange deadline.
Idle management also performs WTP inspection before and after the exchange, so
its browser wait budget is longer. No network call belongs to per-event RF timing.

Accepted plaintext is reported as Progress when copied into the owned queue.
The exact queue buffer remains stable across OpenSSL WANT retries. Later failure
is ambiguous accepted output; it never becomes a claim that zero bytes were sent.
Transport closure, cancellation, TLS failure, reboot or lost acknowledgement
cannot establish inactive output. HELLO device/boot checks and authoritative
STATUS remain mandatory, with the same-session recovery and historical failure
rules described in [status and recovery](wtp-status-recovery.md).

`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` rejects production network access before
resolution or socket creation. The C++ `LoopbackTest` seam permits only numeric
loopback results; no CLI, INI, browser or environment option enables that seam.

## Browser and present Pico limits

The development visibility boolean still controls the existing Transmitter panel.
**Use Pico** selects the backend; **Connection** selects USB or Network (TLS).
Fields, validation failures and status polling preserve drafts. Network status
separates the configured endpoint, canonical expected identity, resolved address, last authenticated identity,
connection state/diagnostic and observation age. These are historical software
observations; failed polling explicitly presents output as unknown.

See [shared browser API](wtp-browser-api.md) for resources, revisions and delegated
job cancellation. Remote standalone settings are distinct from WsprryPi settings.
Load/save actions are explicit and available only while the selected host runtime
is ready, idle, unowned and resolved; the backend rechecks authority. Polling does
not open an HTTP connection, release ownership, recover or mutate configuration.
Schedule-only saving preserves unrelated drafts. Saved standalone configuration
requires a Pico restart; this interface does not request one. Disabling Pico
Wi-Fi can disconnect control; restoring network access then requires its supported
Console/restart workflow.

The clean Pico implementation revision is pinned in `src/tests/network/CMakeLists.txt`
and the network CI checkout. Pico defaults network control off and supports two
TLS sessions with one waiting TCP connection and one computing handshake.
WsprryPi idle management still acquires the same host operation lock, obtains
fresh unowned/inactive WTP status, disconnects the idle stream without RELEASE,
performs one HTTP exchange, then re-inspects WTP. It never releases a job merely
to obtain HTTP status.

HTTP Host and mutation Origin use the canonical configured TLS reference
identity, never the resolved TCP address or reverse lookup. Pico accepts its
certified hostname and current IPv4 separately, then requires the same canonical
Host/Origin authority. Non-443 ports are required; 443 is omitted. This preserves
numeric legacy access while preventing name/IP cross-origin combinations.

## Hardware-free validation

From `src`, with clean read-only source checkouts at the documented pins:

```sh
make wtp-api-test SUDO=
make wtp-tls-test SUDO=
make wtp-network-runtime-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
make wtp-network-interop-test SUDO= \
  PICO_SOURCE=/path/to/WsprryPico \
  MBEDTLS_SOURCE=/path/to/pico-sdk/lib/mbedtls
node ../WsprryPi-UI/tests/wtp_network_ui_integration_test.js
```

Mbed TLS pin: `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`, including its SDK
submodules. CMake verifies both clean tracked source trees and pins; no download
or sibling modification occurs. Ephemeral keys, generated credential headers,
server/client binaries and screenshots stay in ignored `src/build/wtp-network`.
The build root is owner-only because its executable embeds the test key. The
`credentials-v3` bundle is reused only after validation; existing or expired
identities are never silently overwritten. Choose a new `WTP_NETWORK_BUILD_DIR`
when fresh ephemeral credentials are needed.
The WsprryPi-owned server harness supplies a valid 32-hex device identity and
inhibited software engine, and explicitly enables the Pico standalone image's
active-job connection policy. Its POSIX adapter handles fatal socket errors with
lwIP's free-before-error-callback contract; Pico server/core source is unmodified.

Use a different `WTP_NETWORK_BUILD_DIR` for sanitizer flags; pass matching
`CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` to that CMake directory and
`WTP_NETWORK_CXXFLAGS`/`WTP_NETWORK_LDFLAGS` to Make. The CI loopback job includes
TLS, actual Pico interop and rendered browser tests. Local execution is not a CI
run. See the [Phase 11.3 acceptance record](development/phase11-3-review.md) for exact results.

Phase 11.1 and 11.2 retain their closed software scopes. Phase 11.3 software
acceptance does not close 11.4 inhibited physical acceptance, 11.5 target
resources/contention, 11.6 conducted RF or 11.7 final cross-repository closure. This is software evidence,
not physical USB, GPIO, timing, RF, installation or service qualification.
