# WTP session and transaction API

Phase 10 Slice 2 adds `wtp/transport.hpp` and `wtp/session.hpp` to the portable
component. The application still does not link/select WTP. There is no physical
transport adapter, discovery, Console access, scheduler, persisted recovery
journal or operator configuration in this slice.

## Ownership of the API

`ByteStream` is an injected, nonblocking interface. `read` and `write` return
`Progress` with a positive byte count, or `WouldBlock`, `Closed` or `Failed`
with zero bytes. The adapter must return promptly and must not retain spans.
The session closes the stream after invalid counts, exceptions, framing
closure, malformed messages or transaction timeouts. A write failure can have
unknown effects even when it reports no accepted bytes. `WouldBlock` explicitly
guarantees no transfer.

The application supplies a fresh logical `session_id`, its `owner_id`, and the
expected stable `device_id` through `SessionOptions`. The IDs must satisfy the
wire contract. The application must assign a fresh job ID for every new
execution; it must not recycle historical IDs after record eviction. The
session generates unique request IDs using a checked counter within that
logical session. Do not reconstruct a new Session with an old session ID: its
counter and uncertain transaction would be lost. Use the same Session object
across transport reconnects and preserve the authenticated principal.

The caller owns the ByteStream lifetime and must call `disconnect()` before
destroying an attached stream. Session is single-owner and cannot be copied or
moved. All API calls and result consumption run on that owner's event loop.
Use one monotonic millisecond clock. `poll(now_ms)` performs at most one write
and one read, each bounded to 4,096 bytes; it does not block, sleep or open an
endpoint. Poll while requests or remote jobs remain unresolved. Clock regression
closes the stream. The transport's nonblocking contract is necessary for these
latency bounds; the component cannot preempt a blocking adapter.
The complete HELLO/STATUS/CAPS negotiation, including any required STATUS
refresh after events, shares one absolute `transaction_timeout_ms` budget.
An event stream cannot keep negotiation alive indefinitely.

## Negotiation and observations

`connect(stream, now_ms)` starts HELLO. Every stream then receives fresh STATUS
before CAPS and before any mutation. The expected device ID must match. A
different boot from the previously negotiated identity closes the stream and
reports `IdentityChanged`; no old mutation is replayed. No claim, renewal,
abort, release, reload or rearm occurs automatically during connection.

`Ready` means negotiation completed. It does not mean the device is owned,
idle, RF-enabled or qualified. Inspect `safety_fault()`, `uncertain()`,
`needs_status()`, the last STATUS and any job evidence. `status()` is a retained
snapshot: when `needs_status()` is true or the connection is not Ready, it is
not current admission evidence. `owns()` and lease timing are separate from
new-request admission; only `request()` performs the full admission check.

`request(operation, body, now_ms)` accepts a typed body, returning false if
another public result is unconsumed, a request is pending, its wire encoding
is invalid, or session policy denies it. False means nothing was submitted.
The caller consumes results with `take_result()`. There is one request in
flight, one public result and at most one separate recovery result; callers
must drain both results before another public request. Internal STATUS refresh
can continue while a public result awaits consumption.

Responses must match the logical session, request ID and operation. Stale
responses do not satisfy the pending request. A matching ID with a different
operation, unexpected body, wrong owner/job, invalid LOAD adjustment or
inconsistent ARM mapping closes the stream. Duplicate request IDs are never
generated for a new transaction, including HELLO and STATUS on reconnect.

Events are advisory and only the latest event is retained. Session/boot
identities and increasing event IDs are checked; gaps cause authoritative
STATUS refresh and duplicates are counted without replaying their effects.
State events never establish successful completion. New events invalidate
current admission evidence and trigger a bounded STATUS transaction.
If an event arrives after STATUS was submitted, that reply cannot clear the
reconciliation requirement: the session issues another STATUS after the event.
Session replacement closes the stream. Explicit fault evidence latches a
safety fault even if subsequent STATUS shows locally recovered idle.

## Mutations and leases

| Operation | Additional client admission |
| --- | --- |
| CLAIM | Reconciled, inactive, unowned transmitter; requested owner must match this session's owner ID. |
| RENEW | This session's established ownership and a conservatively valid known lease, or ownership resolved from a lost CLAIM/RENEW whose grant is unknown. |
| LOAD | Established ownership, valid lease, inactive empty/terminal state, fresh job ID, supported advertised mode/ranges/event and duration limits. |
| ARM | Established ownership, valid lease, matching current loaded job and validated LOAD acknowledgment. |
| ABORT | This session's matching current loaded/armed/running/aborted job; may be used for explicit cleanup while an earlier mutation is unknown. |
| RELEASE | Established ownership, inactive output and empty/loaded/complete/aborted/missed state. |

The client deliberately does not replace an existing loaded job with a different
job. Abort/release it first. Startup never adopts a foreign or standalone job
based only on a matching owner label. When STATUS establishes ownership lost,
or the server returns NOT_OWNER, prior ownership proof is discarded.

Lease grants use the original local request submission time as a conservative
lower bound, not response arrival time. Exact expiry (`age >= granted duration`)
is not valid. `renewal_due(now_ms)` becomes true halfway through a valid grant;
renewal is explicitly driven by the caller. Delay or replay cannot extend a
grant locally. After a lost CLAIM/RENEW, STATUS may prove current ownership but
does not supply the historical lease grant: a fresh explicit RENEW is needed
before LOAD/ARM. The server remains authoritative for exact device expiry.
Armed/running work can outlive the lease; expiry or disconnect never means it
stopped. ABORT still requires this session's matching current ownership/job.

An ARM acknowledgment is checked against the submitted job/start/uncertainty,
the advertised lead/horizon/holdover/uncertainty limits, the returned UTC-to-
monotonic mapping, both job-end arithmetic bounds and pending-leap exclusion.
This is validation of an acknowledgment, not a new host clock source or the
parent scheduler's full preparation/admission policy. Device UTC must already
be independently provisioned. GET_CLOCK observes it; this component cannot
set it or compensate by changing the requested start.

## Results and uncertain mutations

| Result | Meaning |
| --- | --- |
| NotSent | No request bytes were accepted or potentially transferred. |
| Acknowledged | A matching, validated success response was received. ARM acceptance does not mean execution started. |
| Rejected | A matching error response was received for this attempt. It does not disprove an earlier unknown execution. |
| Unknown | Some request bytes may have been transferred, but no trustworthy acknowledgment was obtained. |
| Reconciled | Fresh STATUS establishes the stated current ownership/job effect; it does not reconstruct a historical acknowledgment or ARM mapping. |

The immutable packet, typed request, original deadline and tracked job remain
in memory across stream loss. A timeout/cancelled write closes the stream.
On reconnect, matching HELLO identity and fresh STATUS precede recovery:

- Lost CLAIM/RENEW/RELEASE replies reconcile current ownership only. Those
  operations are never replayed blindly, regardless of cache TTL or `retryable`.
- Lost ARM resolves when STATUS establishes the same job armed/running or a
  retained terminal result. If still loaded, an explicit safe retry may resolve
  it. Neither a start mapping nor execution onset is invented from STATUS.
- Lost LOAD keeps its missing acknowledgment unresolved while the job is loaded;
  its adjustments cannot be reconstructed from STATUS. A matching terminal
  result can establish the final effect without making it executable again.
- Lost ABORT resolves against the matching terminal result. A completion that
  won the race remains completion, not cancellation.
- Missing current/retained job evidence, an expired/evicted record, a boot change
  or a retry returning JOB_NOT_FOUND does not authorize another execution.

`retry_uncertain(now_ms)` is explicit and preserves the original packet bytes
and absolute deadline. LOAD retry requires the same current loaded job,
ownership, inactive output and a valid lease. ARM retry requires that same
loaded-state admission. ABORT retry requires the same current cancellable job.
There is no reliance on the replay cache staying resident: capacity eviction
can occur before its TTL. No mutation retry starts once its original transaction
deadline has elapsed. Fresh explicit ABORT is a separate bounded cleanup
transaction; it can resolve still-cancellable work after the old deadline.
Some unknown outcomes remain blocked and require higher-level investigation.

`JobEvidence` binds device, boot and job identity. `completed()`/`cancelled()`
require authoritative matching terminal state and explicit inactive output.
An active global output also prevents a retained record from being treated as
safe completion. JobEvidence and raw STATUS retain the distinction between
current device output and historical job output; legitimate foreign activity
does not by itself latch a safety fault. Failed/unsafe output remains a latched fault;
automatic STATUS/reconnect cannot clear it. Safe matching ABORT remains available
where the server lifecycle permits it. Other new mutations remain blocked.

`disconnect()` is local cancellation/shutdown, not remote ABORT. It preserves
uncertain work and invalidates output evidence. The parent backend consumes
and reports unresolved results during shutdown. This slice provides no durable
process-restart recovery or physical emergency-stop mechanism.

## Tests and reference endpoint

`make test` and the parent `make wtp-protocol-test SUDO=` include the deterministic
session tests and existing wire tests. Tests use scripted byte streams, software
state and virtual clocks; they do not instantiate a physical adapter.

For the optional pinned WsprryPico endpoint/job-service interoperability test:

```sh
make interop-test PICO_SOURCE=/absolute/path/to/WsprryPico
```

The target verifies HEAD and all `src/wtp` file contents against the revision
in `PROVENANCE.json` before compiling. It reads the sibling checkout and writes
only to this component's BUILD_DIR. Separate translation units prevent the
client/server header names from masking each other. A software engine tests
local execution without transport input; it is not GPIO, timing or RF evidence.
Ordinary component tests and the library require no sibling checkout.

The maintained transition vectors describe the server's lifecycle graph. Client
STATUS observations may skip intermediate states after disconnection, so that
graph is not applied as an adjacency rule to client observations. Session tests
exercise admission/reconciliation, and the optional test exercises actual server
transitions. No full protocol/firmware conformance claim is made.

## Parent network integration

Phase 11.1 supplies authenticated TLS in the parent integration layer, alongside
USB, through this same byte-stream contract. TLS, resolution, credentials, worker
ownership and shared browser resources remain outside this portable library.
An explicitly selected parent Plain LAN binding may create a session with an
empty expected device ID and `learn_device_identity=true`. The first valid
`HELLO` binds that ID for the lifetime of the session; a reconnect with another
ID enters `IdentityChanged`. USB and TLS still require an expected ID at
construction. This changes no WTP/1 frame or server operation.
See [parent network contract](../../docs/wtp-network.md). The existing provenance
pin and normative protocol ownership are unchanged.
