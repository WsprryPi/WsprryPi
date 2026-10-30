<!-- omit in toc -->
# Journald SSE Streaming API

- [Endpoint](#endpoint)
- [Unified Event Schema](#unified-event-schema)
  - [Field meanings](#field-meanings)
- [Time \& Timestamp Handling](#time--timestamp-handling)
- [Severity, Labels, and Coloring](#severity-labels-and-coloring)
- [Cursor \& Resume Behavior](#cursor--resume-behavior)
  - [Consumer rule (important)](#consumer-rule-important)
- [Reconnect Semantics](#reconnect-semantics)
- [Status Badge (Consumer UI)](#status-badge-consumer-ui)
- [Query Parameters](#query-parameters)
  - [Playback \& Replay](#playback--replay)
  - [Filtering](#filtering)
- [Heartbeats](#heartbeats)
- [Internal Events](#internal-events)
- [Typical Consumer Flow](#typical-consumer-flow)
- [Debugging](#debugging)
- [Notes \& Limitations](#notes--limitations)
- [Usage](#usage)

This endpoint exposes **systemd-journald** logs over **Server‑Sent Events (SSE)**.
It is designed as a **data‑only transport layer**: the PHP service emits normalized
JSON events, and the consumer application is responsible for rendering,
persistence, and UX decisions.

---

## Endpoint

```http
GET /log_stream.php
```

Response is an **SSE stream** (`text/event-stream`).

---

## Unified Event Schema

Every SSE `data:` message contains exactly one JSON object with the following
fields. **All events — journal entries, internal messages, and heartbeats —
use the same schema.**

```json
{
  "type": "journal" | "internal",
  "playback": true | false,
  "__CURSOR": "s=...;i=...;b=...;m=...;t=...;x=..." | null,
  "__REALTIME_TIMESTAMP": "1738198123456789",
  "PRIORITY": "0",
  "SYSLOG_IDENTIFIER": "wsprrypi",
  "MESSAGE": "Started WsprryPi.",
  "_SYSTEMD_UNIT": "wsprrypi.service"
}
```

### Field meanings

| Field | Description |
| ------ | ------------- |
| `type` | `journal` for real journald entries, `internal` for adapter-generated events |
| `playback` | `true` when emitted during replay/backlog |
| `__CURSOR` | Journald cursor; null for internal events |
| `__REALTIME_TIMESTAMP` | Microseconds since Unix epoch, emitted as a decimal string so 32-bit PHP cannot overflow current timestamps |
| `PRIORITY` | Syslog priority (`0` = emerg … `7` = debug) |
| `MESSAGE` | Human-readable log message |

---

## Time & Timestamp Handling

- Transport uses `__REALTIME_TIMESTAMP` (µs since Unix epoch) as a decimal string.
- Consumers render timestamps as **ISO‑8601** strings.
- Microsecond precision is preserved internally.
- Formatting and timezone presentation are consumer responsibilities.

Example rendered timestamp:

```text
2026-01-31T00:09:25.035Z
```

---

## Severity, Labels, and Coloring

- `PRIORITY` follows syslog semantics (`0..7`).
- Consumers map priority to labels:

| Priority | Label |
| -------- | ------- |
| 0 | EMERG |
| 1 | ALERT |
| 2 | CRIT |
| 3 | ERROR |
| 4 | WARN |
| 5 | NOTICE |
| 6 | INFO |
| 7 | DEBUG |

- **Coloring is consumer-side only**.
- Coloring must be **idempotent** and replay-safe.
- Internal events may be styled separately or hidden.

---

## Cursor & Resume Behavior

- Journal events include an SSE `id:` equal to the URL‑encoded `__CURSOR`.
- Browsers resend this value as `Last-Event-ID` on reconnect.
- Server resumes using `journalctl --after-cursor`.

### Consumer rule (important)

> Persist **only** cursors from events where  
> `type === "journal"` and `__CURSOR !== null`.

---

The Logs viewer loads up to 200 recent matching records when the page opens or its source or severity changes, including when returning to a previously viewed source. A reconnect or Resume view continues the records retained in that browser view from its last verified journal cursor. Clear removes the displayed records and retains that continuation point; reconnecting does not restore the cleared rows. Persisted cursors from a previous page are not used to populate an empty new view. Collector and peer binding changes invalidate the affected current view; unverified collector identities cannot store or resume cursors.

## Reconnect Semantics

- Browsers auto-reconnect on transient network errors.
- If `EventSource.readyState === CLOSED`, auto-reconnect has stopped.
- Consumers must initiate a **manual reconnect with backoff**.
- Jittered exponential backoff is recommended to avoid reconnect storms.

---

## Status Badge (Consumer UI)

Consumers may present a **connection-status badge** for visibility:

| State | Meaning |
| ------ | --------- |
| Connected | SSE stream active |
| Reconnecting | Manual reconnect scheduled |
| Disconnected | Stream closed and retry pending |

The badge is **informational only** and does not affect protocol behavior.

---

## Query Parameters

### Playback & Replay

| Parameter | Default | Description |
| ---------- | --------- | ------------- |
| `playback` | `1` | Enable replay/backlog |
| `backlog` | `200` | Number of entries to replay |

Replay occurs even on resume when enabled, scoped by cursor if present.

---

### Filtering

- `priority_min`, `priority_max` map to `journalctl -p`
- `source=all|local|pico:<32-lowercase-hex-device-id>` selects origins; default `all`. Invalid source values fall back to local records.
- Access remains scoped to `wsprrypi.service`; the legacy `unit` query is ignored, including `unit=*`.
- `priority_min` and `priority_max` filter severity inside that scope.

---

## Heartbeats

- Internal heartbeat events are emitted during idle periods.
- `MESSAGE` is `[HEARTBEAT]`
- Priority is `7` (DEBUG).

---

## Internal Events

Internal events report adapter state such as:

- Connection start
- Replay start/end
- Follow restarts
- Heartbeats

They share the same schema and never advance cursors.

---

## Typical Consumer Flow

1. Open `EventSource`
2. Render events
3. Persist last journal cursor
4. Handle reconnect and resume
5. Style or suppress internal events

---

## Debugging

- Open your browser DevTools Console on the logs page and run any of these:
  - `viewLogs.toggle()` → switches Journal ⇄ Internal
  - `viewLogs.showJournal()` → show journald view
  - `viewLogs.showInternal()` → show internal view
  - `viewLogs.showBoth()` → show both panes
  - `viewLogs.get()` → returns current view
  - `viewLogs.help()` → prints the available commands
- Also supported:
  - URL override: `?view=journal`, `?view=internal`, or `?view=both`
  - The selection is persisted in localStorage (so reloads keep your last view).

---

## Notes & Limitations

- Each client spawns a `journalctl` process.
- Intended for admin dashboards, not public fan-out.
- Requires read access to systemd journal.

---

## Usage

Transport-only API.  
Rendering, persistence, and UI conventions are consumer-defined.

## USB origin metadata

Journal entries additionally expose `log_schema`, `source`, `origin_kind`, `origin_id`, `origin_name`, `origin_boot`, `origin_sequence`, `origin_utc_ns`, `origin_monotonic_ns`, `event_code`, `facility`, `dropped`, `collector_id`, `receipt_us`, `ingress` and `forwarders`. Counters and times remain decimal strings; unavailable origin values remain null. Historical native records may lack origin session/sequence metadata. `__CURSOR` always remains the actual journal cursor.

Imported USB records are sent by the application process to its journal unit with custom `WSPRRY_*` origin fields. The adapter preserves journald's trusted fields and receipt timestamp. The browser uses origin UTC when available and explicitly labels receipt-only timestamps `received`. Source and severity context include the verified collector identity in the cursor storage key. No cursor is resumed or stored before collector identity is verified, and internal SSE messages cannot advance it.

`GET`/`PUT /api/control/v1/host/logging` manage the Pi-owned `host-usb-logging/1` collection policy. PUT requires the exact `collector_id`, `If-Match` and a bounded list of saved Fleet USB source identities. Firmware export is independent: `GET`/`PUT /api/control/v1/member/logging`, through selected physical USB management, proxy the current `logging` resource with target/revision guards. Closing or pausing the browser view does not change either policy.

The collector resolves only dedicated Logging role 6 under the selected physical USB parent. Console role 0 remains available during collection. USB-Logging/1 STATUS and START replies are closed, versioned and device/boot-bound; selected-Pico settings use the shared Control/1 contract. Native Pi collector policy and USB logging wire framing retain their own formats. Earlier firmware lacking role 6 reports `dedicated_logging_unsupported` without opening Console.

## Pico clock-state events

`CLOCK_STATE` identifies `UTC synchronized`, `UTC holdover`, and `UTC unavailable`. First usable UTC and recovery from unavailable use Info (6); routine synchronized/holdover transitions use Debug (7); loss of previously usable UTC uses Warning (4). Startup without UTC and unchanged states are quiet. Holdover retains UTC within the existing clock validity and uncertainty policy. Historical `UTC available` records remain readable and do not identify which usable state applied. Event schema, numeric severity semantics, and saved filters are unchanged.
