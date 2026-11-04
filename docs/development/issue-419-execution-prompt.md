# Issue 419 execution contract

Implement the native macOS MCP2221A transport under the existing Si5351 backend
from origin/devel de11d3e9. Work only in the isolated
codex/issue-419-native-macos worktree. Preserve the original checkout and all
existing work. This prompt authorizes its software implementation, review,
commit, and push; it does not authorize hardware operation, merging, issue
closure, service changes, or operator-documentation repository writes.

## Required work

1. Record the macOS API decision, provenance, support matrix, licensing,
   packaging, identity, transaction, deadline, and cleanup contracts before
   production implementation. Use primary Apple and Microchip references.
2. Introduce a typed transaction boundary beneath portable Si5351 register
   behavior. Keep Linux descriptors, headers, ioctls, and device paths inside
   the Linux implementation. Preserve the existing injected qualification seam
   and behavioral fixtures through an explicit compatibility adapter, rather
   than silently weakening their assertions.
3. Select linux-i2c or mcp2221a-hid explicitly at build time. Preserve the Linux
   default profile, reject incompatible/unknown/multiple selections, and isolate
   objects by transport. macOS requires explicit mcp2221a-hid selection.
4. Implement native IOKit metadata discovery and exact stable identity matching:
   VID/PID plus serial where exposed, otherwise physical location identity.
   Refuse missing, duplicate, changed, or detached bindings. Never pick the first
   adapter or invent a serial. Bind a live session to its registry generation.
5. Implement serialized 64-byte command/response exchanges, exact length and
   echo validation, bounded asynchronous report submission and response waits,
   100 kHz setup, checked write completion, pointer-write/STOP/read, and parameter
   bursts used by Si5351. Reject NACK, timeout, short reports/transfers, unknown
   states, disconnects, and failed cleanup without replaying a write.
6. Do not cancel an idle engine. Verify state after any active transfer cancel;
   distinguish adapter cancellation from output inhibition. Retain primary
   failure plus uncertain-output cleanup diagnostics. Never resume a faulted
   session or switch transport, adapter, backend, or bus automatically.
7. Carry the existing explicit Adapter Identity configuration through native
   CLI/backend use. Report selected transport and metadata truthfully. Preserve
   Linux configuration semantics. Do not change Pi/Pico control schemas or WTP.
8. Add deterministic protocol/identity/failure tests, Make selection regressions,
   retained planner/transition/startup tests, Apple Clang physical-profile build,
   strict Linux and default Linux regression checks, and source/symbol audits.
   Use no USB passthrough, live probes, clocks, GPIO, services, or RF.
9. Review documentation impact. Update repository engineering/component guides;
   inspect the separate operator guide read-only and report exact follow-up.
   Add reproducible CI coverage for the new macOS profile.
10. Perform adversarial review of identity races, response confusion, timeout and
    callback lifetimes, cancellation, partial transfers, output uncertainty,
    build isolation, source selection, and Linux regressions. Fix findings,
    repeat affected validation, and assess again until no actionable software
    finding remains. Record commands, evidence, and outstanding acceptance gates.
11. Review the complete diff, commit only this task's changes, and push only the
    feature branch. Do not merge, close #419, or claim physical/release readiness.

## Acceptance boundaries

#411 is merged but its final CI/closure remains an external prerequisite.
Physical macOS MCP2221A/Adafruit product 4471 + Si5351A product 5640 qualification
requires separately approved exact hardware, frequency, mode, duration, load,
and stopping procedure. Operator publication and final merge remain outstanding.
The deferred machine reboot/power-off capability/UI slice follows the transport
foundation and is not part of this transport execution; it needs Impeccable,
command admission tests, visual review, and its own documentation review.
