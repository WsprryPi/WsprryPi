# Privileged Network Safety Contract

## Purpose

Protect privileged HTTP operations and the browser-facing WebSocket endpoint from clients outside the Raspberry Pi's directly connected LAN while preserving the existing port-80 Apache proxy workflow.

This is network-location access control, not user authentication. It reduces exposure of privileged controls but does not identify users, distinguish trusted users on the same LAN, encrypt traffic, or replace appropriate network and host security.

## Governing Architecture

Apache is the browser-facing proxy boundary. It sees the browser's actual TCP
peer and SHALL overwrite the dedicated `X-WsprryPi-Client-Address` request
header with that connection address before forwarding HTTP requests or a
WebSocket upgrade. WsprryPi is the authorization boundary: it accepts this
dedicated identity only from an actual loopback backend peer and evaluates the
effective client against current eligible interfaces for every request or
upgrade.

The backend remains a defense-in-depth boundary. It SHALL:

- validate the actual socket peer for direct backend-port access;
- reject an invalid or duplicate dedicated proxy identity received from loopback;
- ignore a dedicated proxy identity received from a non-loopback direct client;
- validate a local `Host` identity;
- validate an `Origin` when one is supplied;
- ignore `Forwarded`, `X-Forwarded-For`, `X-Real-IP`, and every equivalent forwarded-client header; and
- fail closed when required network information cannot be established.

The port-80 Apache proxy and same-origin browser workflow SHALL remain the normal browser path. Browser code SHALL NOT fall back to backend port `31416` when the Apache WebSocket path is unavailable.

Direct backend ports remain available only for compatible same-LAN non-browser clients. They are not the supported browser path.

## Protected Operations

### HTTP

The following operations are privileged:

| Method | Route | Operation |
|---|---|---|
| `PUT` | `/config` | Replace configuration |
| `PATCH` | `/config` | Modify configuration |
| `POST` | `/config/repair` | Repair or restore configuration |
| `POST` | `/control/stop` | Stop transmission and disable configured transmission |
| `POST` | `/api/support-bundles` | Create a support bundle |
| `GET` | `/api/support-bundles/{id}` | Read support-bundle job status |
| `GET` | `/api/support-bundles/{id}/download` | Download a support bundle |
| `DELETE` | `/api/support-bundles/{id}` | Delete a support bundle |
| `POST` | `/api/network-safety` | Change privileged network safety |
| `POST` | `/api/rp1-gpclk-route` | Apply or recover an RP1 GPCLK route |

Preflight and other method handling for a protected route SHALL NOT provide a bypass. A request that would enable or target a protected operation SHALL pass the same Apache boundary and retain the protected route's Host, Origin, CORS, malformed-request, and method-validation policies.

`GET /config`, version information, ordinary status, and telemetry are not privileged under this contract and SHOULD remain available where practical. No read-only route may mutate configuration, runtime state, support-bundle state, or transmission state as a side effect.

`GET /api/support-intake` reports read-only support-intake availability and SHOULD remain available where practical.

### WebSocket

The following commands are privileged:

- `shutdown`
- `reboot`
- `stop`
- `tone_start`
- `tone_end`
- `bounded_tone`

The following commands or protocol operations are read-only:

- `get_tx_state`
- `wspr_band_catalog`
- `echo`
- ping and pong
- server broadcasts

Apache cannot authorize individual commands after a WebSocket upgrade. In enforced mode, Apache SHALL therefore restrict the entire browser-facing `/wsprrypi/socket` endpoint to the directly connected LAN. This intentionally makes read-only WebSocket commands, ping/pong, and server broadcasts unavailable through that endpoint to off-LAN clients.

There is no separate off-LAN read-only WebSocket path.

## Directly Connected LAN

An allowed browser or direct-backend peer is an address in a subnet assigned directly to an eligible active interface on the Pi.

Eligible interfaces are active non-loopback Ethernet or Wi-Fi interfaces with a usable IPv4 or IPv6 address and netmask/prefix. Discovery excludes:

- loopback interfaces from subnet discovery, while still allowing an actual loopback backend peer;
- point-to-point interfaces;
- tunnel and VPN interfaces;
- container interfaces;
- software bridge interfaces;
- multicast and unspecified addresses; and
- interfaces without a usable address and netmask/prefix.

On Linux, physical Ethernet and Wi-Fi eligibility requires kernel sysfs device
evidence in addition to the link type. Interface names are only a defensive
exclusion signal; renaming a bridge, veth pair, tunnel, or other software link
to resemble Ethernet does not make it eligible.

IPv4-mapped IPv6 addresses SHALL be normalized before comparison. IPv4 and IPv6 subnet matching SHALL use the interface's actual netmask or prefix. A host is not considered local merely because its address is private, unique-local, link-local, or resolves to a local-sounding name.

Multiple eligible physical interfaces and their directly assigned subnets MAY be allowed simultaneously. Extending eligibility to a VPN, tunnel, bridge, container network, or manually declared subnet requires a separate reviewed feature; it SHALL NOT happen implicitly.

If no eligible subnet can be discovered, enforced mode SHALL fail closed for non-loopback protected access and report the discovery failure without silently disabling protection.

Discovery does not require an Internet route, upstream gateway, DNS, or a
network manager. An isolated Ethernet or Wi-Fi LAN, direct link, or Pi-hosted
Wi-Fi network is eligible when it supplies an active usable address and prefix
on an otherwise eligible interface.

Configured HTTP and WebSocket listeners are independent of discovery. They
start once during normal application startup, remain listening through network
loss and reconnection, and are not restarted for interface or subnet changes.
An actual bind or listen error is a listener failure; absence of an eligible LAN
is an authorization state and leaves legitimate loopback access available.

## Apache Enforcement

Apache SHALL use the browser's actual connection address. It SHALL NOT derive
identity from an inbound client header.

The managed Apache configuration SHALL:

- preserve the incoming browser-visible `Host`;
- retain explicit proxy mappings for the HTTP and WebSocket routes exposed
  through Apache;
- scope identity replacement to the WsprryPi configuration, control, API,
  version, and WebSocket namespaces;
- remove any inbound copy of the dedicated identity header;
- overwrite that header with Apache's connection peer address for both HTTP and
  WebSocket proxy requests;
- leave per-client authorization to WsprryPi;
- keep unrelated status and telemetry mappings readable where practical; and
- avoid broad proxy grants that accidentally expose future API routes.

The managed Apache include is static with respect to network addresses. It uses
`mod_headers` and `CONN_REMOTE_ADDR`, is validated before publication, and does
not contain generated subnet allowlists. Network changes therefore require no
Apache reload and cannot leave stale CIDR authorization behind. Generic
`Forwarded`, `X-Forwarded-For`, `X-Real-IP`, and similar headers remain ignored.

## Backend Enforcement

For protected HTTP operations on backend port `31415`, the backend SHALL require:

- an actual loopback or same-eligible-subnet peer when enforcement is active;
- a syntactically valid local `Host`; and
- a matching local `Origin` when `Origin` is present.

For direct WebSocket access on backend port `31416`, the backend SHALL apply the same peer, Host, and optional Origin rules before completing the upgrade. Because direct WebSocket access is for non-browser clients, no port-80-to-31416 browser Origin exception is provided.

An established WebSocket SHALL be reauthorized against current eligible
networks before processing inbound frames and before sending broadcasts or
keepalives. If its client is no longer eligible, WsprryPi SHALL close the
connection rather than retaining startup-time authorization.

When `Origin` is supplied, its host identity and explicit/effective port SHALL match `Host`. A non-browser client MAY omit `Origin`. Missing `Host`, malformed Host or Origin syntax, `Origin: null`, a foreign identity, or a port mismatch SHALL fail closed.

Proxied backend traffic arrives from Apache loopback. Without the dedicated
identity header, it remains within the existing local-process trust boundary.
With the header, WsprryPi validates the represented browser peer against the
current eligible networks. A non-loopback direct client always uses its actual
socket peer and cannot promote an attacker-supplied identity header.

## Administrator Override

The authoritative setting is:

```ini
[Security]
Privileged Network Safety = enforced
```

Accepted values are:

- `enforced`
- `insecure-disabled`

Missing, empty, malformed, boolean-like, numeric, or otherwise unknown values SHALL mean `enforced` and SHALL produce a warning suitable for operator diagnosis. Only the exact explicit value `insecure-disabled` disables the peer/subnet restriction.

The override affects only the protected operations and WebSocket endpoint described by this contract. It SHALL NOT disable or weaken:

- Host validation;
- Origin validation;
- CORS policy;
- HTTP method validation;
- malformed-request rejection;
- command validation;
- forwarded-header distrust; or
- ordinary input and configuration validation.

Normal configuration autosave SHALL NOT read, write, normalize, delete, or otherwise alter this setting.

## Apply and Reload Transaction

Changing the administrator override SHALL use one validated transaction:

1. Parse and validate the requested setting.
2. Generate candidate backend and static Apache identity policy artifacts.
3. Validate the candidate WsprryPi configuration.
4. Validate the complete candidate Apache configuration with the platform's Apache configuration test.
5. Publish the candidate configuration atomically.
6. Reload Apache without rebooting the Pi or restarting WsprryPi solely for this setting.
7. Confirm the published static Apache identity policy and active backend mode.

If any step fails, the operation SHALL report failure, preserve or restore the previously active configuration and policy, and leave protection enforced unless the previously active state was already the explicit insecure override. A failed apply must never convert an enforced system into an insecure-disabled system.

The design SHALL distinguish the requested, persisted, and active values so a partial or failed transaction cannot be reported as successful.

## User Interface Contract

UI implementation requires the repository's Impeccable workflow.

Disabling network safety SHALL require:

- a conspicuous explanation that this exposes privileged controls beyond the directly connected LAN;
- deliberate confirmation separate from normal configuration autosave; and
- the exact typed phrase `DISABLE LOCAL-LAN SAFETY`.

Enabling protection SHOULD require an explicit apply action but does not require the insecure confirmation phrase.

The UI SHALL preserve the operator's draft when validation or reload fails and SHALL show the failure beside the control that initiated the action. It SHALL distinguish requested, persisted, and active state.

When the active value is `insecure-disabled`, the UI SHALL conspicuously display:

```text
NETWORK SAFETY OFF
```

The browser uses `GET /api/network-safety` for configured and active status and
`POST /api/network-safety` for the separately confirmed apply operation. The
mutation route is itself a protected operation. Transaction results use stable
named states rather than numeric implementation values.

## Status, Configuration, and Logging

When network safety is disabled, logs, status output, configuration reporting, and the UI SHALL conspicuously report the exact text:

```text
NETWORK SAFETY OFF
```

The backend begins enforcing the parsed configuration as soon as its runtime is
initialized, so the initial active state is known without an Apache reload or
network-discovery pass. Status and configuration reporting SHALL expose both
configured and active state without exposing sensitive network details
unnecessarily. An active state becomes unknown only if an administrative
transaction cannot restore a known-good configuration after failure.

Security logs SHALL distinguish at least:

- off-LAN peer rejection;
- enforced operation with no eligible LAN;
- eligible-interface discovery failure;
- invalid trusted-proxy identity;
- invalid or foreign Host;
- invalid, foreign, or mismatched Origin;
- malformed request or WebSocket upgrade;
- invalid administrator setting defaulted to enforced;
- Apache validation failure;
- Apache reload failure; and
- successful enablement or explicit insecure disablement.

Logs SHOULD contain enough peer and policy context for diagnosis while avoiding trust in or promotion of forwarded-client values.

## CORS and Failure Behavior

Protected HTTP responses SHALL NOT use a permissive wildcard CORS policy. Allowed browser requests use the normal same-origin Apache path. The insecure override SHALL NOT change CORS behavior.

An off-LAN protected HTTP request SHALL receive a generic `403 Forbidden`
response from WsprryPi before executing the operation. A rejected WebSocket
upgrade SHALL receive a generic HTTP `403` and SHALL NOT establish a WebSocket
connection.

A direct backend HTTP or WebSocket rejection SHALL likewise fail before executing an operation. Error responses and close behavior SHALL avoid disclosing whether a particular privileged resource, job identifier, or command would otherwise be valid.

Malformed requests remain malformed-request errors when they reach the applicable parser after passing network policy. Network rejection takes precedence at the browser-facing Apache boundary.

## Compatibility and Non-Goals

The implementation SHALL preserve:

- the simple port-80 Apache browser workflow;
- same-origin browser operation on an allowed LAN;
- existing valid ordinary configuration values;
- read-only HTTP status and telemetry where practical; and
- same-LAN direct backend access for compatible non-browser clients.

The implementation does not provide:

- authentication or per-user authorization;
- TLS or traffic confidentiality;
- protection from another client already on an allowed LAN;
- remotely available read-only commands through the protected WebSocket endpoint;
- automatic VPN, tunnel, bridge, container, or manually declared subnet trust;
- network monitoring or Apache regeneration on interface or DHCP changes; or
- a replacement for firewall, router, VPN, or operating-system security.
