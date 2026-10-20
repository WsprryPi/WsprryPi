# WTP-Client

Portable C++20 WTP/1 wire and session support, implemented as Phase 10 Slices
1–2. This is an ordinary component of the WsprryPi repository. Its public headers, sources,
tests, MIT license and fixture provenance stay together if it is extracted.
It has no Pico SDK, application, OS, hardware or third-party runtime dependency.

## Implemented boundary

- `wtp/frame_parser.hpp`: WTPF framing, big-endian fields, CRC-32C, fragmented
  and coalesced input, resynchronization, invalid-frame limits and partial-frame
  progress timeout. Frame payloads still require codec validation.
- `wtp/codec.hpp`: owning typed requests, success/error responses and advisory
  events for all WTP/1 operations. Strict UTF-8/JSON, decoded duplicate keys,
  closed objects, scalar bounds, clock shapes and universal job arithmetic are
  validated before a message is returned. Only requests are serialized.
- `wtp/wire.hpp`: bounded partial-write state, injectable monotonic timeout
  budgets and an owning `RequestPacket` that retains exact serialized bytes.
  Positive write progress never extends the absolute deadline.
- `wtp/transport.hpp` and `wtp/session.hpp`: injected nonblocking byte streams,
  HELLO-first negotiation, response correlation, ownership/lease accounting,
  exact-byte recovery and authoritative STATUS reconciliation. See the
  [session API and recovery contract](SESSION.md).

The production application does not select this component yet. The portable
component itself contains no backend, endpoint discovery, USB I/O, Console time
provisioning, scheduler integration, configuration or UI in these slices. The Session layer
validates an `ARM` acknowledgment against its request and advertised clock bounds;
the wire codec alone does not. Writing a frame does not establish acknowledgment.
The codec preserves explicit active-output evidence in status, terminal
records and events; a decoded terminal state is not a success verdict.

Phase 10 Slice 3 adds a separate parent application
[execution-plan converter](../../docs/wtp-execution-plan.md). It consumes this
component's types without introducing a WSPR-Transmitter dependency here.
Slice 4 adds the parent's [USB CDC transport](../../docs/wtp-usb-cdc.md), with
Linux identity resolution and hardware-free POSIX tests. Neither adapter adds
OS dependencies to this portable component. Slice 5 adds the parent's
[complete-plan backend](../../docs/wtp-backend.md), tested through the shared
transmitter controller. This component retains its standalone boundary.

## Building and testing

From this directory, with a C++20 compiler, `make`, `ar` and Python 3:

```sh
make all
make test
make test BUILD_DIR=build/sanitized \
  CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined'
```

`all` builds `build/libwtp_client.a`. Consumers include `include/`, link the
archive and use namespace `wsprrypi::wtp`. The tests run without privileges,
network, device access or another checkout. Use a separate `BUILD_DIR` when
changing compiler flags; existing objects do not track command-line changes.
The parent entry point is `make wtp-protocol-test SUDO=` from `WsprryPi/src`.
The existing non-hardware CI workflow runs this target on macOS and Debian.

Tests cover maintained schema/raw-JSON/framing vectors, all operation and
event bodies, independent malformed-message and boundary expectations,
CRC golden values, every split of a known frame, large coalesced streams,
resynchronization limits, timeouts with virtual clocks, partial writes,
immutable payload ownership and deterministic malformed-input stress.
Session tests add scripted transport failures, leases, replay and reconciliation.
The optional [pinned endpoint test](SESSION.md#tests-and-reference-endpoint)
exercises the Pico endpoint/job service using a software engine. Ordinary tests
do not establish endpoint interoperability; neither suite establishes target,
USB or RF qualification. Parent portable semantics tests do not exercise WTP.

## Using the wire primitives

1. Construct a `Request` with an explicit operation and matching body. Create
   a `RequestPacket`, checking failure. Retain that packet for the complete
   transaction: a same-ID retry must reuse its bytes, including after reconnect.
2. Start a `FrameWriter` with the packet payload and explicit total/idle budgets
   in milliseconds. Send at most the span returned by `remaining(now_ms)` and
   report only bytes actually accepted to `consume(count, now_ms)`. Zero bytes
   do not advance progress. Inspect `state()` for completion/failure.
3. Feed received bytes to a `FrameParser`. Each call consumes at most 4,096
   bytes; retain the unconsumed suffix and drain returned events before feeding
   more. Check `closed()` even if the same result also contains payload events.
   Call `check_timeout` while awaiting input, and `end_of_stream` on disconnect.
4. Decode each `Payload` event and check `DecodeResult` before reading its owned
   variant. Use Session for identity/operation correlation, HELLO-first and
   authoritative STATUS reconciliation before mutation.

All `now_ms` arguments come from the same injected monotonic clock for each
object. Clock regression fails closed. Exact deadline boundaries expire.
These classes are single-owner primitives without internal synchronization.
Spans from `remaining` become invalid when the writer is mutated; a transport
must complete/cancel any I/O using a span before modifying its writer.
`cancel()` cancels only local writing; it sends no ABORT and proves nothing
about the remote job. After a failed or cancelled partial write, discard the
transport stream before sending another frame: appending a new frame to a
truncated frame could corrupt the peer's framing. Reconciliation belongs to
the Session layer. A new writer budget is not a new retry deadline;
the transaction must retain its original absolute deadline across attempts.

Per-call input and output are bounded: the parser retains at most one incomplete
65,536-byte payload plus its header and one 4,096-byte input chunk. Valid payloads
returned in one call total at most that bound. Drain results rather than keeping
an unbounded history. JSON input is limited to 65,536 bytes and 16 container
levels, with integers limited to signed 32 bits and decimal-string quantities
to unsigned 64 bits. Schema-defined array/string limits are enforced. STATUS
record count has no independent protocol maximum and is bounded by the payload.
Container capacity and typed allocations add bounded overhead; this is not a
hard real-time or allocation-free component.

## Protocol ownership and provenance

WsprryPico owns the authoritative [WTP document][wtp], schema, contract data and
vectors. `PROVENANCE.json` pins the reviewed revision and SHA-256 of each
upstream file reused here. The JSON parser and framing implementation were
adapted under the [MIT license](LICENSE.md). Host framing adds bounded per-call
consumption and timeout checks before accepting newly arrived bytes. The typed
codec and write/packet APIs are WsprryPi code.

The three files under `tests/fixtures` are unmodified test snapshots, checked
against provenance hashes by the test runner. They are not a second protocol
specification. Refresh them only after reviewing the complete authoritative
contract and recording the new revision/hashes. A schema-valid capability is
not proof of usable device policy or RF support: for example, the current
schema permits `minimum_lease_ms <= 5000` without a lower bound, while actual
lease requests and grants remain bounded to 5000–60000 ms. The wire layer does
not reinterpret that value as permission for a shorter lease.

[wtp]: https://github.com/WsprryPi/WsprryPico/blob/40812e7438f180c5e8d8ad75d4eb227271152b10/docs/protocol/WTP.md

## Remaining Phase 10 prerequisites

Slice 6 implements the parent [early scheduler and UTC handoff](../../docs/wtp-scheduling.md).
Slice 7 adds parent [runtime status and recovery reporting](../../docs/wtp-status-recovery.md).
The parent [production integration](../../docs/wtp-production-integration.md)
now supplies configuration, selection, an owned worker and operator/UI wiring.
The USB CDC adapter is implemented but still needs physical target validation.
No new job
may be inferred safe from a disconnect, lost acknowledgment, boot change,
expired result or `JOB_NOT_FOUND`. The complete approved plan still governs
those later slices; wire tests do not complete Phase 10.

For later scheduling, the device must already have independently provisioned
valid UTC with acceptable uncertainty, holdover and leap state. `GET_CLOCK`
only observes that state. Host UTC validity and RF calibration do not establish
device UTC validity. USB Console time provisioning remains a separate proposed
adapter requiring its own scope; this component never opens the Console.

The parent UI includes a default-off, browser-local development visibility
toggle. It preserves saved backend selection and never enables transmission or
clears a fault. It is temporary development control, separate from backend
admission checks; this portable component owns no UI or persistent settings.

## Documentation impact

This README and the parent development link document the new developer API.
No operator behavior changes in Slices 1–7. The separate `Wsprry_Pi_Docs` repository
was reviewed and remains unchanged. Later status/configuration/UI slices need
separately authorized updates to `docs/Command_Line_Operations/transmitter_backends.md`,
`docs/Advanced_Operations/ini_configuration/transmitter_backends.md`,
`docs/User_Interface/Setup/Transmitter/index.md`, and the applicable Operations
guidance for endpoint identity, finite jobs, clock prerequisites, cancellation
and output-unknown status. The development toggle needs developer guidance,
not permanent operator documentation.

The portable client API and implementation are unchanged in Slice 7.
