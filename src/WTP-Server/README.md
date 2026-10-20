# WTP server core

This component is a portable snapshot of WsprryPico's WTP/1 server core at
`c13fc16819749e9b53a42609f31702c3aca9931c`. It provides bounded frame
handling, WTP session/job policy, and an inhibited engine for non-RF tests.
The source adaptations change its private include prefix from `wtp/` to
`wtp_server/`, so it can coexist with WsprryPi's WTP client headers, and add
an optional per-request admission callback after replay resolution. The latter
lets the parent atomically block a new `CLAIM`, `LOAD`, or `ARM` while preserving
WTP replay responses. A narrow `IJobService` interface lets the transport use
the parent authority facade. The original `wsprrypico::wtp` namespace remains explicit. See
`PROVENANCE.json` and `LICENSE.md`.

The endpoint also accepts a product name (default `WsprryPico`) and a shared
per-boot event counter, and exposes same-principal session replacement. The
completion acknowledgement allowance is 100 ms for Linux shutdown completion.
This is separate from start-time uncertainty and the advertised output-disable
timeout. These changes add no WTP operation or wire field.

The component does not open a socket, select a Raspberry Pi backend, control
hardware, or establish WsprryPi local-output arbitration. The parent
application must supply those adapters before this can be a Pi WTP endpoint.

Build the standalone archive with `make -C src/WTP-Server`. The archive is
separate from the WTP client library. Do not add the raw component sources to
the parent application's wildcard source list.
