# WsprryPi shared browser API adapter

The host adopts Pico API v1 resource names for their compatible meanings and
labels host projections `scope: wsprrypi-host/1`. It retains `/config`, `/api/wtp`,
`/api/wtp/recover` and existing controls. Browser URLs use the application proxy
prefix `/wsprrypi`; the table below shows backend paths.

| Resource | Implemented authority |
| --- | --- |
| GET `/api/v1/capabilities` | Host capabilities, observed WTP CAPS, supported ABORT, remote management availability and explicit limitations. |
| GET `/api/v1/status`, `/api/v1/jobs` | Copied observed remote `job`, `standalone: null`, transport `network`, and explicitly scoped `host` status. No new connection or recovery. |
| POST `/api/v1/jobs` or `/api/v1/jobs/<job-id>/abort` | Cancellation of that exact currently tracked host-owned/waiting job through the existing runtime. |
| GET/PUT `/api/v1/config` | Pico standalone station/Wi-Fi/schedule configuration, through serialized idle HTTPS. |
| GET/PUT `/api/v1/schedules` | Pico standalone schedule subresource, sharing Pico config revisions. |
| GET/PUT `/api/v1/network` | Pico network status/control, with its own remote revision. |
| GET/POST `/api/v1/member/configuration` | Host-coordinated selected-Pico settings context and one compatible atomic save; schema `wsprrypi-pico-settings/1`. |
| GET/PUT `/api/v1/host/config` | Explicit `wsprrypi-host-config/1` application and transport settings. PUT applies a merge patch. |
| GET `/api/v1/host/discovery` | Bounded local Avahi snapshot; no WTP connection. |
| GET/POST `/api/v1/host/devices` | Host-owned known-device catalog, revision-guarded add/edit/rename/remove. |
| POST `/api/v1/host/discovery/identify` | Explicit bounded read-only HELLO/STATUS/CAPS for one selected candidate. |
| POST `/api/v1/host/devices/use` | Applies a saved profile through the active host config/runtime path, with both catalog and config revisions. |

The host does not redefine Pico `/config` as WsprryPi configuration. WsprryPi's
schedule remains owned by its existing application; remote standalone schedules
do not start a competing host worker. Browser raw CLAIM/LOAD/ARM, independent
ownership, SoftAP and BLE are unavailable. Capabilities report ABORT only and
`job_submission: host-application`; ordinary transmission uses the existing host
workflow. Management is temporarily unavailable during work or unresolved output,
not an independent connection around the scheduler.

## Cancellation, replay and precision

```json
{"session_id":"11111111111111111111111111111111","request_id":"22222222222222222222222222222222","operation":"ABORT","body":{"job_id":"33333333333333333333333333333333"}}
```

All three identities require 32 lowercase hexadecimal characters. Browser session
IDs scope request replay, not WTP ownership or authentication. The trusted host
operator delegates cancellation to the host runtime's real owner. The runtime
atomically checks the exact tracked job and ownership/waiting state before stop;
foreign jobs cannot be adopted or aborted. Browser cancellation never enables
transmission. A racing natural completion can win; successful `cleanup_ok` means
cleanup was confirmed, not that the terminal outcome was necessarily cancelled.

Identical canonical payloads with the same browser session/request return the
cached result. Changed payloads under that key return 409 `request_id_reuse`.
The process retains up to 1024 entries, reserves an unknown outcome before
invoking cancellation and fails closed at capacity instead of forgetting requests.
Replay history is process-local. After a restart, fresh WTP identities and
read-only inspection still cannot adopt old work. No automatic browser retry is
performed after an ambiguous result. Responses preserve the shared `ok`,
`request_id`, `result`/`error` envelope for cancellation; other failures use
`error.code`. WTP 64-bit counts, IDs/times/frequencies remain decimal strings.

## Revision and security policy

Host config GET provides a quoted SHA-256 ETag of its internal saved snapshot.
Host PUT requires exact If-Match: absent/empty is 428, stale is 412, and a
successful patch returns the resulting ETag. Legacy `/config` GET also supplies
ETag; its existing PATCH/PUT accept conditional writes, while legacy clients
without If-Match retain compatibility. The current UI autosave sends its loaded
revision. Conflict responses preserve the browser draft; load the saved settings
before retrying. Remote PUT passes through the Pico resource's exact If-Match and
ETag, without replacing its revision with a host-generated value. A lost remote
write result is unconfirmed; explicitly read saved state before retrying.
The [Fleet catalog](wtp-fleet.md) has a separate ETag for saved profiles; its
selection action still uses the host config ETag and never stores a second
active-endpoint authority.

The existing trusted-LAN peer, Host, Origin and proxy-identity guard applies both
to direct backend requests and deployed proxy traffic. Shared mutations also
require Origin, `Content-Type: application/json`,
`X-WsprryPico-Request: 1`, and absent/same-origin/none Sec-Fetch-Site. No CORS grant
is added. Unknown shared routes, encoded/query aliases and duplicate authority/
intent headers are rejected. JSON is bounded to 32768 bytes, nesting to 32 and
object keys must be unique. The host route and API body guards are additional to
the existing server parser bounds. This remains the application's trusted-LAN
operator policy, not per-person login authentication; outbound mTLS authenticates
the configured host client credential.

Responses use no-store and nosniff. Copied status has no certificate/private-key
material. Host config exposes local credential references only. Remote config
retains Pico's password redaction; new passwords are submitted only by an explicit
save, kept out of browser storage and cleared after confirmed settings-save success. A
schedule-only save keeps password and other unrelated drafts. Management never
runs periodically and never releases host ownership to poll Pico TLS sessions.

## Compatible Pico settings

The [configuration compatibility contract](wtp-configuration-compatibility.md)
defines the settings form's host-scoped context/edit resource. Its GET reads
actual selected-member capabilities and saved state under serialized idle
authority, binding device, boot and exact common ETag. Its POST re-reads that
context, then sends at most one mutation: advertised targeted merge, a complete
understood legacy Config v1, initial PUT, or schedule-only PUT. Present but
unsupported update capability and unknown configuration fields refuse safely.
Host `/capabilities` and WTP CAPS remain separate from those remote capabilities.

The form sends only changed fields. Omitted form fields never reset pins or
advanced frequency flags. Ordinary edits use no replacement/reset mode. An
unchanged Save performs no mutation. Explicit edits preserve password null;
new passwords clear only after confirmed settings-save success.

Conflict or unconfirmed results disable further saves until an explicit saved
state readback. That readback preserves the draft/password and shows saved
values for review; a subsequent deliberate Save uses the new context. No
mutation retries, strategy switches or endpoint substitution occur automatically.
Normal Load replaces the draft, and polling never does. Feedback uses actual
`reboot_required`/application state rather than claiming every save needs reboot.


Outbound management authenticates the canonical configured expected TLS identity
(or configured target when blank) and uses it for both Host and Origin. An
explicit IP destination with expected hostname therefore retains hostname HTTP
authority. Numeric identity remains compatible with a corresponding certificate
IP SAN. Resolver results never grant HTTP authority. Pico independently validates
its hostname/current-IP allowlist and requires Host/Origin to match each other;
this does not relax the WsprryPi host proxy's own security policy.

Copied host network status distinguishes saved `hostname`, canonical
`expected_identity`, numeric `resolved_address` and `authenticated_identity`
from the last successful handshake. A new failed connection clears authenticated
identity; transport closure cannot establish inactive output. The remote network
resource additionally exposes Pico discovery state, separate from this host's
resolver/TLS observations. None of these fields installs or changes Linux NSS.
