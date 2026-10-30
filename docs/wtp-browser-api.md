# WsprryPi shared control API

The shared logical API uses `/api/control/v1` and `Wsprry-Control/1`.
Selected-Pico logical control responses identify that contract; their mutations
require it. Native host configuration and maintenance operations retain their
explicit host-owned envelopes and validation.
Pico resource types are stable names governed by this one current control major.
Historical resource generations and save-strategy fallback are unsupported.
Update the Pi application, UI and Pico firmware together.

Browser requests use the `/wsprrypi` application proxy before the backend paths
below. Host projections retain explicit `wsprrypi-host/1` scope. That scope does
not identify a Pico resource version or grant remote authority. Native Pi
configuration and job-production workflows remain owned by the host application.

## Resources and authority

| Backend resource | Authority |
| --- | --- |
| GET `capabilities`, `status`, `jobs` | Host-scoped capabilities and copied current observations; no new ownership or automatic recovery. |
| POST `jobs`, `jobs/{id}/abort` | Cancel the exact tracked host-owned/waiting job through the existing runtime. |
| GET `config`, GET/PUT `network` | Selected Pico observations and the retained network alias; schedule edits use `member/configuration` and do not configure the Pi's own scheduler. |
| GET/POST `member/management` | Explicit volatile selected-carrier context and authentication. |
| GET/POST `member/configuration` | Serialized selected-member context and one intended-fields settings update. |
| GET `member/application`, `member/station`, `member/hardware`, `member/pins` | Reconciled canonical current member observations and choices. |
| PUT `member/station`, `member/hardware` | Scoped complete station or pin-plan replacement through Pico authority, with exact target and revision. |
| POST `member/pins/validate` | Protected nonpersistent candidate pin review. |
| GET/POST `member/progress`, `member/progress/update`, `member/progress/apply` | Read independently saved drafts, save fields or explicitly apply a saved revision/domain. |
| `member/gpio-drive`, `member/gpio-drive-progress` | Saved/boot/pad drive observations and independent drive draft/application. |
| `member/amplifier-timing`, `member/filter-selector` | Authoritative selected-member domain settings and readback. |
| `host/config`, `host/discovery`, `host/devices`, `host/fleet` | Existing explicitly host-owned configuration, discovery, catalog and assignment operations. |

All names in this table are below `/api/control/v1/`; supported methods and
finite budgets remain enforced by the production route registry. A listed
resource does not imply every carrier admits every operation. The host exposes
ABORT rather than browser raw CLAIM/LOAD/ARM; ordinary job submission uses the
existing host application. Plain LAN WTP and TLS-WTP carry complete WTP/1 jobs,
not a generic settings channel.

## Current hardware and settings

The general member coordinator consumes the complete current application and
hardware model, including amplifier-enable GPIO and polarity, GPIO drive and
saved versus boot-active state. Pin preview validates all supported roles,
conflicts and current capabilities independently before a save. Amplifier
timing and filter selection retain their authoritative domain resources.
Native Pi BCM/RP1/Si5351 and amplifier controls remain separate.

Drive, pin, amplifier and selector changes retain explicit restart and latched
output inhibition, including A/B/A saves. A saved draft is not applied hardware.
Reads can report unavailable or nullable observations; saving never measures
RF power, filter response or amplifier readiness.

Ordinary configuration updates contain intended changes only. Omitted settings
remain saved; explicit false and zero remain values. Pi never reconstructs a
complete configuration from a redacted read, and unsupported context never
selects an older save method. Profile-owned station/network credentials retain
Pico transactional setup. Aggregate observations do not imply one atomic write
across independent journals.

Saved progress accepts bounded known field units, per-field outcomes and exact
request identities. Application uses a saved draft revision and explicit domain,
not unsaved browser edits. A complete station is not required merely to save an
independent draft; complete admission remains necessary before application or
schedule enablement. Receipts bind the exact submitted logical bytes, including
the control contract, before encryption/framing.

## Validation and concurrency

Known messages are closed, bounded and typed. Missing/extra fields, duplicate
keys, wrong types/units, unknown contracts, inconsistent advertisements and
wrong device/boot/profile bindings refuse before mutation. Unknown optional
features can remain unused only at the contract's explicit extension points.
Capability discovery does not grant write permission.

Session durations and resource budgets are authenticated typed policy data
within reviewed client ceilings. They are not matched to historical version
number tables. Clients use admitted policy or their stricter supported bounds;
unsupported context refuses without substitution. Public reads do not renew
login idle time or absolute lifetime.

Member writes use Pico's exact If-Match/ETag. Host config/catalog revisions stay
host-owned and cannot replace a remote member revision. Missing revisions
refuse; stale ones preserve the browser draft for review. Attempted mutations
invalidate cached editor write contexts. An unconfirmed or malformed response
requires authoritative readback and explicit review before another deliberate
save. No automatic retry, carrier change or alternate peer is selected.

WTP identifiers, times and frequencies retain exact integer-string precision.
Standalone recurrence reports the saved whole-Hz USB dial as exact nHz text;
an unset dial is not a selected transmit frequency. Host job RF frequencies,
reported WSPR dBm and physical drive strength have separate meanings.

## Authentication and delivery

The host trusted-network, Host/Origin and proxy-identity guards apply. Mutations
also require JSON intent and the existing fetch-metadata policy. There is no
new CORS grant. Consumer station/AP requests retain encrypted authenticated
Consumer-Radio/1; engineering TLS retains certificate identity. USB has local
physical authority and cannot be requested by a network caller. Management
never grants transmitter ownership or bypasses scheduler/output admission.

USB retains exact boot/session/target framing and matching response ACK before
deferred effects. Network retains complete-response delivery confirmation.
Disconnect can cancel undelivered restart/network effects while durable writes
and accepted jobs remain effective. Readback is required after uncertainty.
Credentials remain redacted, outside browser persistence and excluded from logs.

Authenticated login retirement remains an explicit selected-session recovery
operation; it is not automatic eviction, replacement login or mutation replay.
Password/profile/boot changes retain their defined session invalidation rules.

## Validation evidence

The [configuration contract](wtp-configuration-compatibility.md) and
[USB adapter](wtp-usb-management.md) describe selected-member boundaries.
Pico owns the canonical contract and generated definitions. Both actual network
and USB production pair suites must pass against the recorded candidate source
pair, including amplifier-enable operations and superseded-contract refusal.
Source provenance, generated artifacts and tested full-source identities remain
separate records. Software checks do not establish installed proxy/service,
physical USB, GPIO, timing or RF qualification.
