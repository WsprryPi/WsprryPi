# Browser and production-client acceptance follow-up

The 2026-09-09 RF-inhibited Pico coverage exposed a production scheduling defect:
a job 60 seconds ahead waited silently past the Pico's 30-second connection idle
budget, then failed before ARM. The isolated fix was rebased onto the user's
latest `origin/devel` at `89f23e5`. Source commit
`923ab570fe53ef2ccca7d12e519c9dc36adf7e93` adds bounded read-only STATUS observations
on the scheduler's sole owner thread during Waiting. A failed observation blocks
new work. No early CLAIM, automatic mutation retry, shifted slot, new setting,
protocol change or reusable-component edit was introduced.

The [scheduling contract](wtp-scheduling.md) documents transaction and stop/reload
behavior. The complete cross-repository attempt history, browser findings and
joint matrix are maintained in WsprryPico's
[coverage record](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/browser-production-coverage.md).

## Validation and adversarial assessment

The new 60-second scheduler regression failed on the original code. After the
repair, `make wtp-scheduler-test SUDO=` passed 54,641 checks, also under
ASan/UBSan. Fault cases include lost STATUS, changed boot, cancellation and reload
during STATUS, cancellation followed by a new request, and I/O crossing the
start cutoff. No failed pending request reached CLAIM/LOAD/ARM.

`make wtp-backend-test wtp-status-test wtp-application-test SUDO=` passed
5,472 / 20,959 / 38,957 checks. `make wtp-network-interop-test SUDO=` with the
existing pinned Pico `d8cde03` and MbedTLS `0bebf8b` passed the real TLS 60-second
wait, shared management, fragmented I/O, lost mutations and recovery cases.
The requested UTC start was preserved. The actual second-loopback-address case
was explicitly skipped because this macOS host could not bind `127.0.0.2`;
injected address coverage still ran. Interop pins were unchanged.

Review round 1 reproduced and fixed the wait defect. Round 2 challenged timeout,
identity, cancellation/reload and clock boundaries. Round 3 checked source/binary
identity, complete diffs and the physical results below. No further actionable
finding remained in this scoped companion change. Existing broader physical
acceptance gates are not closed by these tests.

## Authorized physical acceptance

Built the real application in isolated
`/home/pi/wsprrypi-browser-coverage-20260909` on wspr5 using
`make release BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= -j2`, then explicitly
selected `--backend wtp` with the existing controller credentials. Binary
`src/build/bin/wsprrypi-browser-coverage` SHA-256:
`993ad62def487e5f03d82a5a7a1f5f82b972b16bd36eaa5b0d9a8c13a7166323`.
No replacement of the installed binary occurred.

Pico 2 W / RP2350, USB serial `0BF4B4AEC9FFB344`, device
`fd6127d11d6aca42a9905fa3fb1bf1d5`, installed revision `f88fa71eef5e`, boot
`b3cf6adbca443d954750abaa161cde18`, engine `inhibited-standalone-simulator`.
The destination was fresh USB-reported `192.168.1.47:18443`, with independently
verified TLS identity `wsprrypico-0a60df.local`. These are explicit-IP transport
cases, not DNS/NSS qualification.

Two finite QRSS `E` jobs at nominal 3,570,100 Hz, 20 seconds each, were scheduled
60 seconds ahead:

- `edd768b3a53fb8760000000000000001`: start 12:46:34Z, complete authoritative
  job and successful cleanup. Actual Chrome observed Armed, Running, Complete
  and released ownership, with foreign controls disabled.
- `7e879120cd36366d0000000000000001`: start 12:48:14Z, cancelled through the
  production `/api/v1/jobs` ABORT path after Running. Typed report `cancelled`,
  matching authoritative aborted job, `stopped:true`, cleanup OK. A foreign
  browser-certificate HTTPS session's ABORT/RELEASE received `NOT_OWNER` while
  preserving this job and production ownership.

The separately approved runner paused/restored the installed service for each
bounded test, checking its binary/INI against each run's baseline and provider
output inactive. Final independent USB evidence showed empty, inactive, unowned
Pico state with retained disabled standalone settings; the service was active.
The earlier failed 60-second run on source `2e47641` remains recorded separately.
These results establish inhibited application/device behavior, not RF timing,
spectral performance, general availability or installation qualification.

## Documentation Impact

Updated the scheduling contract and this acceptance record. Existing operator
backend guidance was considered; no new operator control/configuration requires
an edit in the separate operator documentation repository. The coordinator
retains unexecuted Chrome Loaded observation and other physical acceptance gates.
