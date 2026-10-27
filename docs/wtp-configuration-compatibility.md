# Pico configuration compatibility

Current normal-firmware USB management, hostname/Wi-Fi lifecycle and UTC
support are documented in [USB management](wtp-usb-management.md).

The Pi settings form uses one host-coordinated, serialized settings transaction.
The existing WTP application remains the only scheduling/ownership authority;
Pico remains the only remote configuration validator and journal authority.
Explicit consumer management carriers preserve WTP/1, RF engines and recurrence
policy. [Scoped member resources](wtp-browser-api.md#shared-pico-member-resources)
coexist with the atomic settings transaction. It does not split a legacy
settings save into station and hardware writes.

Beta 1 adds a separate [saved station progress](wtp-browser-api.md#independent-saved-station-progress)
workflow on supporting firmware, using exact network reference
`165303458e49feaac1de54c8344058f56578d533`. The prior network reference
`804e8a06585ec5a0e650e69fcd71ada398b6cc9f` is retained as `p158`; current,
`legacy`, `extended`, `p152`, `p153`, `p154`, `p156`, `p157` and `p158` are nine
separate network rows. The independent USB reference remains
`100e749c815f7485d5097c9086852d3b97adca9f`. These are software test pairs,
not firmware image selections or general physical qualification. The complete
configuration and station APIs below retain their atomic semantics.

## Context and edit API

GET `/api/v1/member/configuration` is a protected management read, not the host's
public capabilities projection. It temporarily uses the existing idle HTTPS
slot, after fresh WTP negotiation. It reads actual Pico capabilities, redacted
Config and status, plus the advertised application projection where available,
and verifies a consistent configuration readback. The advertised `api_version`
must be the integer 1; numeric coercion from fractional values is refused.
Advertised update modes must be strings; unknown string modes can coexist with
the supported `merge` mode. It
returns:

```json
{
  "schema":"wsprrypi-pico-settings/1",
  "target":{"scope":"member","device_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","boot_id":"00000000000000000000000000000001"},
  "config":{},
  "update_strategy":"merge",
  "configuration_updates":{},
  "application":null,
  "reboot_required":false
}
```

The identities and empty objects above illustrate shape, not a valid complete
configuration. `config` is the authoritative redacted Config v1 or null;
`configuration_updates` contains actual remote metadata or null. The application
projection supplies saved/active hardware and pending restart state when
advertised; null means unavailable, not inactive. The response ETag is Pico's
exact common ETag. No host revision substitutes for it.

POST requires exactly `schema`, `target`, `operation`, `config`, with that
If-Match. `operation` is `save` or `schedules`; target must exactly match the
freshly negotiated member device/boot. For example:

```json
{"schema":"wsprrypi-pico-settings/1","target":{"scope":"member","device_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","boot_id":"00000000000000000000000000000001"},"operation":"save","config":{"station":{"callsign":"K1ABC"}}}
```

Config is a nonempty closed subset of known Config v1 fields. Station/Wi-Fi
objects are nonempty known-leaf subsets; arrays replace atomically and pins
remain complete plans. Null is only the existing password-preserve sentinel or
unallocated pin slots. False and zero are explicit values. Schedule-only edits
contain exactly `schedules`. The form exposes its existing fields only, without
adding advanced pin/frequency controls or deliberate full replacement.

The host request is limited to 2,048 UTF-8 bytes. The coordinator re-reads saved
configuration/capabilities and verifies identity/revision before any mutation.
Every remote JSON read is bounded to 16,384 bytes, unique object keys and depth
32; complete redacted Config is bounded to 1,800 bytes. A legacy outgoing full
candidate is at most 1,800 bytes. A new merge envelope is at most 2,048 bytes
and the advertised request bound. Unsupported/malformed context refuses safely.
Advertised application state must include complete known station, hardware,
recurrence and capability projections, matching nested targets and typed
saved/active/restart fields. Unhealthy storage or application faults block
mutation. Pico still validates the complete resulting Config and its 1,800-byte
limit.

The independently saved Pico `wspr_dial_frequency_hz` stays outside generic configuration resources. Complete PUT, merge, replace, partial saves and Console omission preserve the dial while retaining each operation’s existing ownership of other domains, including explicit replacement resets. A first selection leaves schedules disabled; later dial changes activate at the next job and preserve STOP and accepted jobs. The dedicated resource owns whole-Hz dial values; the strict recurrence projection and display use exact nHz strings. No base-to-dial conversion or old-member reader is applied.

## Negotiation and fallback

| Observed context | One selected mutation |
| --- | --- |
| Configured Pico advertises supported `transmitter-config-update/1`, POST `/api/v1/config/update`, merge and usable budgets | Targeted merge envelope containing intended fields |
| No update capability and known complete Config v1 | Full PUT built from authoritative saved fields plus intended edits |
| No saved configuration | Initial complete PUT with explicit credentials; no update-only operation |
| Schedule-only edit | Existing scoped schedules PUT |
| Present unsupported/malformed update capability, unknown field or unsafe legacy extension introduction | Refuse before mutation |

Legacy fallback retains supported fields actually observed in the authoritative
Config, including nondefault pins and true flags. It never forwards arbitrary
GET metadata or adds absent advanced extensions to an old strict parser. Known
older optional expiry and time-server edits retain their Config v1 meaning.
An unrecognized future field causes safe refusal rather than loss. Present but
unsupported update support never silently downgrades. Host feature flags, WTP
CAPS, firmware dates and transport reachability do not establish update support.

Engineering Network (TLS) remains the default certificate management carrier.
Explicit authenticated consumer station/AP management is described in
[the adapter contract](wtp-browser-api.md#explicit-consumer-management). Outbound
allowlists retain the existing finite pairs and add GET station/hardware/pins,
PUT station/hardware and POST pins/validate. Authenticated canonical
Host/Origin and JSON intent apply; scoped writes retain the common If-Match,
while the bare nonpersistent remote preview does not require it. These controls
use existing authenticated application-resource discovery, independently of the
closed management-operation list. USB uses its own management contract;
Plain LAN WTP is not a management carrier. No carrier is substituted automatically.
Existing direct `/config`, `/schedules` and `/network` resource compatibility
remains available; the settings form uses the coordinated resource above.

## Drafts, credentials and uncertain saves

Normal Load replaces the form with saved values. Save sends changed fields
only; no-change Save does not mutate. Schedule-only Save preserves unrelated
station/Wi-Fi/password drafts. Passwords remain null on readback, stay outside
browser persistence/logs and clear only after confirmed settings-save success.

A stale, rejected or unconfirmed write invalidates the browser's write context.
It requires explicit saved-state readback before another Save. That readback
keeps drafts and password entry, displays saved values beside fields and asks
the operator to review before another deliberate action. It cannot compare a
redacted saved password to the retained entry. No automatic mutation retry,
fallback replay or endpoint substitution occurs, including after a commit whose
reply was lost. Malformed successful replies remain unconfirmed.

Successful feedback follows actual restart reporting. Pending network/pin
activation is distinct from station/schedule settings used by later admissions.
Unavailable restart data is stated as unconfirmed. Profile-owned station/Wi-Fi
changes must use Pico setup when Pico rejects them; Pi never claims they saved.

## Compatibility fixture boundaries

The nine network rows listed above retain their separately pinned sources and expectations. Tests use actual Pico handlers, native TLS and consumer station/AP fixtures. Older peers retain their supported legacy editor; absent shared resources or authenticated retirement remain unavailable.

Network management/1, /2 and /4 and USB management/3 and /5 discovery are validated exactly. Historical /1–/3 retain five-minute idle and fifteen-minute absolute consumer limits. Current /4 and /5 retain their respective carrier shapes with thirty-minute idle and eight-hour absolute limits. The capability descriptor and returned document must agree on the supported version and selected carrier. Unsupported versions, malformed limits and inconsistent targets refuse before mutation or fallback. See the [management policy table](wtp-browser-api.md) for the exact per-version limits and session capacities.

The strict legacy server's ETag hashes boot/config/network values without modern
device/generation binding. It cannot detect an external A-to-B-to-A sequence.
Its fallback row covers preservation and ordinary stale rejection, not modern
ABA guarantees. Fresh Pi WTP/TLS identity and the explicit host target still
bind the selected member; modern Pico also checks the remote target.

Software loopback/RAM-journal checks do not qualify firmware, physical flash,
installed proxy/services, transport timing or RF.

## Versioned WSPR formats

The separately advertised `application_resources_v2` descriptor negotiates
`transmitter-application/2`, `transmitter-station/2` and
`wspr_type123_auto`. Its exact paths are GET `application/2` and `station/2`,
PUT `station/2` and `config/2`, and POST `config/update/2` below `/api/v1/`.
The complete seven-field descriptor is closed; unknown schema, literal, path,
plan type or shape refuses before a mutation. Absence selects the historical
Type 1 contract. Present unsupported support never falls back.

The host member-facing routes remain unchanged. A station save uses the schema
actually read from the negotiated resource. Atomic settings retain Config v1
and config-update/1 envelope/omission/password-preserve semantics on the new
paths. Consumer profile station identity still belongs to encrypted Pico setup;
member station writes remain unavailable for those profiles. Both native TLS
and encrypted station/AP outbound allowlists retain explicit methods, bounds,
revision requirements and their original authentication.

The versioned application adds bounded `wspr-plan/1` metadata: supported state,
one/two known frame types, 120-second slot separation, rationale and explicit
Type 2 ambiguity/alternate call. A 12-character stored token includes brackets.
Ordinary six-character locators retain Type 1/four on air. Compound plus four
selects Type 2 without a locator; compound plus six selects Type 2 then Type 3;
explicit brackets plus six select a Type 3 single. No preference selector is
introduced. Reported dBm is message content, separate from RF output.

`/00`–`/09` remain supported, sharing Type 2 bits with `/Q`–`/Z`; readback and
paired Type 3 hashing retain the requested identity. Receiver hash context and
ambiguity resolution remain receiver limitations. Frozen legacy application/1
and station/1 projections retain their shape/literal and explicitly refuse a
selected new-format station. Unchanged release Pi can continue existing Type 1
workflows; a selected Type 2/3 station requires this companion or a later
integration. WTP/1, management/1 and /2, Consumer-Radio/1 and Consumer-Recovery/1
remain unchanged.
