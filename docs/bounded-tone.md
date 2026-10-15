# Bounded Qualification Tone Control

## Purpose

The `bounded_tone` WebSocket command provides a product-owned, self-terminating
Test Tone transaction for an authenticated qualification helper running on the
same host. It prevents loss or delay of the helper connection from extending RF
past the requested interval.

This software contract does not qualify GPIO timing, RF output, frequency,
power, spectral purity, cabling, attenuation, or receiver behavior.

## Containment

Start WsprryPi with `--no-http --socket-loopback-only` for a qualification
session. The HTTP listener stays disabled while the WebSocket listener binds to
a literal loopback address rather than the wildcard address. The
command-line-only `--socket-loopback-family auto|ipv6|ipv4` policy defaults to
`auto`: it prefers
IPv6 `::1` and falls back to IPv4 `127.0.0.1` only when the operating system
specifically reports that IPv6 or its loopback address is unavailable. Port
conflicts, permission failures, socket-option failures, listen failures, and
unexpected errors fail closed without changing families. Callers that require
a deterministic endpoint select `ipv6` or `ipv4` explicitly. Because
this combination does not expose a network listener, it does not require
privileged external-network policy reconciliation.
The `bounded_tone` command fails closed unless that mode was selected. Existing
`tone_start`, `tone_end`, and default browser-facing binding behavior are
unchanged.

The flag is command-line-only. It is not persisted by configuration reads or
writes and intentionally makes the WebSocket unavailable to remote browser
clients for that process lifetime.

## Request and lifecycle

Example request:

```json
{
  "command": "bounded_tone",
  "request_id": "qualification-001",
  "duration_ms": 2000,
  "frequency_source": "custom_rf",
  "frequency_hz": 14097100
}
```

`request_id` must contain 1 through 128 ASCII letters, digits, dots, hyphens,
or underscores. `duration_ms` must be an integer from 1 through 60000. Frequency
selection uses the existing Test Tone validation contract.

WsprryPi rejects overlapping bounded transactions. After the normal Test Tone
start succeeds, it arms a monotonic process-owned watchdog before acknowledging
success. At the deadline it runs the normal Test Tone stop and scheduler restore
path, then broadcasts a terminal `bounded_tone` event carrying the request ID,
stop result, scheduler-restore result, and status.

An explicit `tone_end` cancels the bounded transaction and uses the same cleanup
path. WebSocket-server shutdown requests watchdog stop, joins it, and quiesces
an active bounded tone before completing shutdown. Failure to arm the watchdog
immediately invokes Test Tone cleanup and reports an error.

## Integration boundary

An automation client must authenticate its host access, launch or select an
explicitly loopback-contained WsprryPi process, correlate both response events,
and enforce an outer deadline. Those client controls are defense in depth; the
WsprryPi watchdog remains the RF-duration authority.

## Transaction ordering and status

The scheduler serializes complete Start, End, operator Stop, and restoration
transactions. Each accepted Test Tone has a process-local generation. The
watchdog can end only that generation; operator Stop, an ended transaction,
or a successor prevents stale cleanup or restoration. An ordinary End can
restore a previous CLI direct tone, so `stopped=true` describes the Test Tone
transaction, not a blanket assertion that all output is off.

The server reuses one watchdog thread. End cancels its pending deadline without
joining it. Server shutdown cancels the deadline and quiesces only its still-owned
bounded generation without restoring earlier work, then joins without holding
command, lifecycle, or watchdog-state locks. Process exit also prohibits restoration.
Failure to create the watchdog quiesces the newly accepted generation. A finite
execution that reconciles before the deadline retains its terminal outcome for
the matching watchdog without repeating cleanup or restoration.

The existing request forms and response booleans remain available. Optional
`client_request_id` strings (up to 128 characters) are echoed for browser
correlation. Additive `test_tone` metadata carries `generation`, monotonic
`revision`, `owned`, `state`, `message`, and `restoration`. These identifiers are
local to the process; clients reacquire evidence after reconnect. States include
`starting`, `active`, `completed`, `failed`, `cancelled`, `ended`, and `unknown`.
The main loop publishes changed observations as `test_tone_status` messages;
`get_tx_state` also includes the metadata. Receivers should ignore older
revisions and tolerate additional fields.

`started=true` means the request was accepted and asynchronous startup was
requested. `active` is an observed controller runtime event, never an RF
measurement. Terminal callbacks record their own generation and let the main
loop perform cleanup/restoration outside the worker. A terminal event may
precede the Start acknowledgment. End responses additionally expose
`restoration_failed` and `output_inactive`; restoration warnings must not be
hidden by `stopped=true`. Unknown cleanup keeps output arbitration inhibited.
An inactive legacy End remains inert with `tone_was_active=false` and
`stopped=false`; the metadata distinguishes that case from uncertain cleanup.
An End recovering uncertain cleanup reports the cleanup result explicitly and
does not resume prior work; successful recovery can have `stopped=true` with
`tone_was_active=false`.

For RP1 execution, provider rejection becomes an operation failure with its
underlying diagnostic. The finite plan retains its complete duration through
the provider's one-second execution chunks. STOP requests bounded drain; a
deadline-expired operation remains failed even if that drain succeeds. Cleanup
failure stays latched and prevents renewed output without recovery. These
software observations do not attest RF inactivity or physical qualification.
RP1-enabled status includes `rp1_last_operation` with timing, terminal reason,
provider detail, local endpoint state and cleanup evidence. It identifies the
last operation, and exposes lease presence without its token.

Configuration reloads and saved web edits during Test Tone defer runtime
application until cleanup, retaining the committed backend, mode, frequency,
and selector snapshot. Saved repair/reset operations follow the same rule.
Operator Stop retains its existing persistence option and clears restoration
ownership. Browser replies are versioned so an older result cannot overwrite a
newer observation even when HTTP Stop and WebSocket delivery overlap.

Broadcasts, including keep-alive traffic, do not wait for a slow peer's send
buffer. A peer that cannot accept a complete frame is disconnected and must
reconnect for current evidence. Tone worker callbacks never broadcast or take
the scheduler lifecycle/configuration locks. These are software ordering and
reporting contracts, not hardware or RF qualification.

An old controller response without correlation or revision evidence cannot
prove which tab's pending request it answers. The browser retains a conservative
recovery state for an uncorrelated rejection rather than enabling another
Start. Matching acknowledgments that arrive after newer runtime observations
settle their request timers without rolling the observed state backward.

### Status polling and command delivery

The server retains incomplete WebSocket frames across TCP reads. Individual
frames are limited to 64 KiB and queued input to 128 KiB or 1,024 frames; an
exceeded limit closes that connection. Consecutive pending `get_tx_state`
queries may be coalesced into one current observation. Other commands retain
their arrival order, so End/STOP does not wait for a backlog of redundant
status queries. A status query already in progress retains its provider-control
deadline. Qualification clients should keep at most one status query outstanding
and record both STOP send completion and the observed provider terminal state.
