# SQLite feasibility prototype implementation prompt

Implement a standalone feasibility prototype and repeatable Linux test harness
for a Raspberry Pi 1 and Raspberry Pi 5 running from SD cards. Determine whether
the mechanisms in the architecture below have acceptable memory, latency, and
host storage-write costs before undertaking production integration.

This prompt authorizes building the prototype and its safe local checks. It does
not authorize production changes or contacting, installing on, or running tests
on a remote Pi. The operator will run the delivered harness on each Pi. Do not
stage, commit, push, create a remote, or open a pull request.

## Scope and architecture

Implement exactly this experimental path:

```text
Local Python HTTP workload generator
                  |
Standalone PHP loopback test service
                  | Unix-domain socket
Standalone C++ engine
                  |
Engine-private persistence adapter
```

Provide three persistence modes behind the same engine interface: memory,
atomic file, and SQLite WAL. SQLite is private to the engine. PHP must not open
the database. There is no shared database command queue.

The experiment answers whether these mechanisms warrant production integration
work. It does not establish production superiority, exact SD-card lifetime,
complete Apache/PHP resource use, or transmitter timing correctness.

Keep the implementation outside every production checkout. Use an explicitly
selected, new directory, with `~/wsprrypi-feasibility` as the documented example.
Before writing, resolve its physical path, inspect applicable instructions, and
verify that it is neither a production checkout nor inside one. Preserve existing
content. If the destination already contains unrelated content, request a
different destination rather than overwriting it. Respect filesystem permissions;
do not work around a denied destination by putting implementation into WsprryPi.

Read production sources only when useful to understand the context. Do not
import, link against, copy, edit, or depend on WsprryPi or sibling components.

Exclude schedulers, radio semantics, Pi/Pico protocols, credentials, GPIO, I2C,
MMIO, transmitter devices, actual transmissions, authentication systems, browser
interfaces, remote orchestration, containers, service installation,
and database migration of real settings. Do not add a new project framework,
release pipeline, GitHub automation, or generic benchmark platform.
Do not use `sudo`, change system configuration, resize filesystems, disable swap,
change CPU governors, or alter host logging as part of the experiment.

## Deliverables and dependencies

Deliver only the files needed for this experiment:

```text
README.md
Makefile
src/                 C++ engine, adapters, and socket framing
web/                 headless PHP endpoint and router
harness/             Python runner, collectors, workloads, and reporter
fixtures/            fixed synthetic input definitions
profiles/            frozen workloads, limits, and decision budgets
tests/               focused correctness and harness checks
.gitignore           generated builds, local runs, and machine overrides
```

Small protocol and measurement specifications may live in README or separate
documents if that makes the instructions easier to use. Avoid duplicate sources
of truth. Do not create empty or speculative files.

Use C++17, PHP, Python 3.11 or later, system SQLite development libraries, and
only the small JSON/digest dependencies actually needed. Prefer a distribution
JSON library and a maintained SHA-256 implementation over custom parsers or
cryptography. Record dependencies and their licenses; preserve attribution.
Do not download dependencies at build or benchmark time. Missing prerequisites
must produce actionable diagnostics, not automatic package installation.

Build natively on each Pi using the same source bundle and fixture definitions.
The Makefile must provide documented build, safe check, smoke, and report entry
points. Keep generated build output separate from source and measured data.
This is a Linux/Raspberry Pi experiment; cross-platform collectors are outside
scope. A smoke run on another platform cannot count as Pi evidence.

Use PHP's built-in development server bound to `127.0.0.1`, with one worker and
no external HTTP dependencies. It is a headless API fixture, not a browser UI or
an Apache replacement. Its memory and latency are not Apache qualification.

## Minimal request and state contract

Implement only reading settings, saving replacement settings, and retrieving a
retained operation receipt. Define one closed request/response schema, explicit
size limits, a bounded framing protocol, and stable error codes. Reject malformed
documents, unknown operations, wrong types, extra fields, oversized messages,
partial frames at deadline, and invalid revision values without mutation.

Use a length-prefixed UTF-8 JSON message over the socket; bound both announced
length and actual input. Define a maximum message large enough for the 64 KiB
settings fixture plus framing and response metadata. Bound HTTP bodies, socket
buffers, socket deadlines, and outstanding requests as well. No unbounded queues.

Settings are synthetic opaque test content with a revision. Do not reproduce
production settings validation. Generate valid payload documents of exactly
8 KiB and 64 KiB in canonical UTF-8 encoding; define precisely which serialized
bytes the sizes and SHA-256 digest cover. Use deterministic non-secret content.

Each save includes an operation ID, expected revision, and payload digest. The
engine verifies the digest. Use nonnegative revisions within SQLite's signed
64-bit integer range and reject exhaustion instead of wrapping.

Apply checks in this order:

1. Validate the complete document and digest.
2. Look up a retained operation ID. An identical request returns its original
   receipt, even if its expected revision is now old. Different content under
   the same ID is a conflict.
3. For a new operation, check the expected revision.
4. Return a no-change result without persistent mutation if content is unchanged.
5. Otherwise atomically update settings, revision, and a successful receipt.

Use the engine-recomputed SHA-256 digest as the content fingerprint, together
with operation type and expected revision, when comparing request identity.
Receipts retain these identity fields, not a complete historical settings payload.
A no-change result does not retain a receipt. Retention is exactly the newest 256
successful changed operations by monotonically increasing revision, with
deterministic eviction in the same transaction as the save.

Receipt lookup after eviction returns `not_retained`, not success or proof that
the operation never happened. Duplicate correctness cases use still-retained
IDs. Do not claim perpetual deduplication from bounded history.

The reply to a changed save is sent only after its durable boundary completes.
The memory mode uses the same logical behavior but is explicitly nondurable.

## Persistence modes

All modes receive identical logical requests and return equivalent results.
Keep transaction construction, validation, payload serialization, and receipt
contents comparable. Reject storage errors; do not acknowledge a failed commit.

**Memory:** retain state only in RAM. This estimates the frontend, socket, and
validation cost without durable storage. It is not a durability baseline.

**Atomic file:** hold the complete settings/revision/receipt snapshot in one
bounded JSON file. For a changed save, serialize once, write a uniquely created
temporary sibling, handle short writes and errors, `fsync` its descriptor, close,
rename over the owned destination, and `fsync` the parent directory before
acknowledgment. Fail closed after an uncertain storage error. On restart, validate
the final snapshot before use and ignore incomplete temporary files without
inventing success. Remove only positively identified owned temporary files after
measurement, never during a timed cleanup hidden from accounting.

This is a simple durable reference, not a clone of WsprryPi's multi-file journal.
Do not label its results as production WsprryPi measurements.

**SQLite:** use separate logical settings and receipt tables with the necessary
uniqueness constraints and minimal indexes. Commit each changed save and any
receipt eviction in one transaction. Use one writing connection, WAL, and
`synchronous=FULL`. Freeze page size, a bounded page cache, a 1,000-page automatic
checkpoint threshold, a 500 ms busy timeout, and schema version in the profile.
Verify and report effective values. Do not relax durability or switch storage
modes after a failure. Declare that filesystem/card sync behavior remains outside
process-restart proof.

Do not run `VACUUM`, shrink or truncate storage after every save, maintain durable
heartbeats, persist ordinary status reads, or create periodic event writes.
Use an explicit final FULL checkpoint and verify all available WAL frames were
checkpointed. Include connection close and any final checkpoint writes in costs.
Do not repeatedly optimize the schema or settings after observing results.

## Fixtures and matrix

Use these two fixed synthetic datasets:

| Dataset | Settings document | Initial retained receipts |
| --- | --- | --- |
| Small | 8 KiB | 0 |
| Populated | 64 KiB | 256 |

Each receipt has a deterministic 512-byte synthetic content field, with its
metadata overhead documented. Create valid initial receipts with consistent
revisions and IDs. The full serialized reference snapshot must remain bounded
at 256 KiB; verify the fixture fits before benchmark execution.

Each profile runs one dataset and one repetition across all three modes. A full
campaign consists of two datasets times three repetitions: six profiles. Rotate
mode order deterministically across repetitions. Use fresh run data per mode
and record the fixed seed, exact request sequence, and fixture hashes.
Represent the replay sequence as IDs, revisions, digests, and deterministic
generation parameters. Generate payloads lazily; do not retain thousands of
64 KiB request bodies in RAM or logs.

Default per-profile limits: four hours, 1 GiB of observed writes to the measured
whole device, at least 256 MiB of free filesystem space, and a bounded 8 MiB
measurement buffer. A complete campaign therefore has upper limits of 24 hours
and 6 GiB, not four hours and 1 GiB. Report this before execution. These are abort
ceilings, not desired write volumes or mandatory endurance targets.

Observe write/free-space limits once per second; explain that in-flight I/O and
the sampling interval can overshoot a software limit. Do not promise a hard
physical write cap. Abort safely, preserve evidence, and mark the profile
incomplete when a limit is reached.

## Fixed workloads

For every mode and dataset run:

| Scenario | Workload |
| --- | --- |
| Idle | 10 minutes with service and engine running, no HTTP requests |
| Reads | Two settings reads per second for 10 minutes |
| Saves | 1,000 genuinely changing saves offered at two per second |
| Mixed | Two reads per second plus 1,000 changing saves at two per second |
| Negative | 100 each of retained duplicate, conflicting ID, stale revision, and unchanged saves |
| Retention | Covered by saves and mixed runs after the 256-receipt cap is reached; report before/after-cap costs |

Also run equal-duration 10-minute background controls immediately before and
after each mode block, with the collectors running but prototype services absent.
The source fixture is immutable; initialize fresh mode data before its first
scenario and carry state forward through that mode's scenarios. Negative requests
are generated from the current known state so the intended condition is exercised.
Do not reuse evicted IDs for the retained-duplicate case.

Use one ordered mutation stream to keep expected revisions intentional, plus one
read stream. Cap HTTP requests in flight at four and use a two-second end-to-end
deadline. Record scheduled arrival, actual send, completion, and engine transaction
duration using monotonic clocks. If a request cannot be admitted on schedule,
record an overload miss; never silently slow the offered schedule or hide missed
arrivals. A timeout or ambiguous mutation acknowledgment ends the dependent
mutation stream and makes that scenario incomplete; reconcile only for cleanup
and correctness, not to manufacture a successful latency sample.

Warm up identically, then reset to the pristine dataset and finish all setup
I/O before establishing the measurement interval. Use bounded, recorded readiness
and counter-settling checks; inability to settle is an explicit limitation.
Record setup, warm-up, timed workload, finalization, and export separately.
For SQLite, checkpoint pristine fixture creation before the timed block so
setup WAL debt cannot be charged to the idle scenario. Preserve identical
logical starting state across modes after warm-up and reset.
Do not drop OS caches or claim cold-cache results. Include all timed retention
and automatic checkpoint work. Do not add unbounded throughput searches, browser
campaigns, or hour-scale soaks beyond this fixed matrix.

## Measurement implementation

Collect once per second with bounded overhead. Keep raw readings and request
timings in a preallocated or explicitly bounded RAM buffer; account for sampler,
driver, and buffer memory separately. No timed logs, progress files, server access
logs, or reports may spill to the measured SD filesystem. Capture child stdout
and stderr into bounded buffers. Diagnostic overflow fails the run rather than
silently dropping evidence or enabling disk logging. Inherited OS service logging
is background activity, not a reason to change host configuration.

Prefer an in-memory collector buffer to a growing tmpfs file. Account for kernel
socket/page-cache effects through system memory measurements; process PSS alone
is not the complete memory cost. Export after counters for the measurement window
have been finalized. Export writes remain visible in whole-profile write budgets
and must settle before the next block begins.

### Memory and CPU

Record per-process PSS from `/proc/PID/smaps_rollup`, falling back to summed
`smaps` with the fallback named. Record RSS, high-water RSS, CPU time, system
`MemAvailable`, and swap-in/out counters. Record the exact owned PID set and
start-time identities. Report sampled simultaneous engine-plus-PHP PSS peaks;
do not add unrelated historical peaks. Report harness/collector memory separately.
If PSS is unavailable, mark that metric inconclusive, not RSS-equivalent.
State the one-second sampling limitation for short allocation peaks.

### Latency

Report count, median, p95, p99, maximum, offered and achieved rates, schedule lag,
timeouts, and overload misses. Define percentiles using the nearest-rank method
and retain sample counts; small negative-case groups do not support strong tail
claims. Latency ends when the caller has received the complete response. Separate
transaction duration from end-to-end latency. Memory-mode responses must not be
described as durable acknowledgments.

### SD writes

Resolve the data path to its filesystem, partition, and whole backing device.
Support directly mounted SD partitions only for SD evidence. Reject tmpfs,
network, overlay, loop, USB-backed, or unresolved device-mapper storage for that
label. Read `/sys/class/block/<device>/stat` and convert sectors written using
512-byte units. Read process I/O counters as supporting diagnostics, never add
them to device totals. Counter reset, device identity change, or unreadable
required counters invalidates the storage interval.

Report raw whole-device bytes, bytes per successful changed save, idle/read write
rates, finalization writes, and SQLite database/WAL sizes. The card also contains
OS writes; no per-application or physical NAND attribution is implied.
The principal durable-write comparison uses complete mode-block bytes, including
finalization, divided by successful changed saves. Scenario bytes are diagnostic:
do not assign a later checkpoint to a single earlier scenario without evidence.

Keep background controls and raw mode totals. Show background-adjusted estimates
separately; do not clamp negative estimates to zero or use subtraction as proof
of application causality. Controls bracket each mode block but do not exactly
match every scenario. If their variation can explain a measured difference,
report the write comparison inconclusive. Use the before/after control rates as
a conservative range, not a statistical confidence interval. Define a material
regression before the run: more than 10 percent above the file reference and
larger than the bracketing-control uncertainty, consistently across all three
matched repetitions. For each mode define the adjusted-byte interval as
`[raw_bytes - high_control_rate * duration, raw_bytes - low_control_rate * duration]`,
using its own bracketing controls and complete block duration. Normalize by
completed changed saves. These are sensitivity bounds under the observed control
rates, not guaranteed bounds on unseen background activity. A storage pass requires
the SQLite upper bound to be at most 1.10 times the file lower bound in all three
matched repetitions. A storage fail requires the SQLite lower bound to exceed
1.10 times the file upper bound in all three. Overlap, inconsistent repetitions,
zero completed saves, a nonpositive reference lower bound, or another identified
confounder yields inconclusive. Report raw results alongside this classification.

After the workload, checkpoint SQLite, close engine and PHP, and observe counters
until unchanged for five consecutive one-second samples, with a maximum 60-second
settling interval. Retain its entire cost. A failure to settle makes write
attribution inconclusive. Observed quiescence is not proof of physical flash
completion or absence of later OS writes. Do not call global `sync`, drop caches,
change mount settings, or use privileged tracing. Record leftover files and WAL
state instead of concealing incomplete finalization.

## Host preflight and process safety

Record model, RAM, OS, kernel, architecture, compiler and library versions,
SQLite compile options, source/fixture/configuration hashes, filesystem, card
identity where readable, free space, swap settings, and available temperature,
frequency, and throttling indicators. Optional unreadable indicators are explicit
limitations, not silently assumed healthy. Do not require or install monitoring
packages. Do not read private station data.

Require enough memory for the bounded collector without swapping before startup.
Start only owned unprivileged processes, use private directories and a short
socket path, verify listener readiness through an actual response, and fail on
occupied ports without killing the occupant. Bind loopback only; do not expose
the development server to the LAN. Use restrictive socket and data permissions.

Permit one active profile per prototype root using a process lock. Refuse
unrelated files, symlink escapes, run-ID collisions, and attempts to reuse a
partially failed run. Use structured subprocess arguments without a shell.
Track process start identities, reap children, and preserve primary and cleanup
errors. An interrupt must stop new workload, finalize where possible, terminate
only owned processes, and retain an interrupted result. No broad cleanup commands.

Do not stop running WsprryPi or other host services. Flag background load,
throttling, power warnings, or concurrent storage activity as confounders. Refuse
SD measurement on a host where required counters or the data path cannot be
verified. Never turn an incomplete or contaminated run into a pass.

## Decision budgets

Freeze the profile before collecting results. Ship these proposed screening
budgets, clearly identified as experimental rather than production requirements:

| Metric | Proposed criterion |
| --- | --- |
| Correctness | Every required check passes |
| Engine plus PHP sampled PSS | At most 32 MiB |
| Available system memory | At least 64 MiB throughout |
| Swap-in/out | No increase during measured workloads |
| Read latency | p95 at most 250 ms |
| Changed-save latency | p95 at most 500 ms and p99 at most 1 second |
| Load handling | No request deadline or offered-load admission misses |
| Idle/read persistence | No intentional application mutation |
| SD writes | No demonstrated material regression under the defined matched comparison |

Report per-metric `pass`, `fail`, or `inconclusive` separately for each Pi. Missing
required measurements, contamination, early aborts, or insufficient completed
repetitions cannot yield an overall pass. Distinguish observed swap increments
from attribution: they make the clean memory budget fail without proving that
the prototype caused all of them. Compare each Pi's modes locally; differences
between the Pis include their cards and OS environments, not just their CPUs.
Apply candidate feasibility budgets to SQLite mode; memory and file mode results
are reference measurements with their own descriptive budget comparisons. Require
correctness in every mode. Evaluate engine/PHP memory only while those processes
exist, not during service-absent controls. The final per-Pi candidate verdict covers
both datasets and all three repetitions: any candidate budget failure fails it;
otherwise any inconclusive required metric makes it inconclusive.

Changing budgets requires a new configuration identity and complete affected
reruns. Preserve earlier results. A passing campaign supports considering an
integration experiment; it does not approve production migration, final web
architecture, RF operation, or exact card-life claims.

## Correctness and measurement checks

Before performance execution, implement and run focused checks for:

- Equivalent accepted state and receipts across all modes, plus restart persistence
  for durable modes and explicit loss for memory mode.
- Valid saves, no-change behavior, stale revisions, duplicate replay before stale
  checking, conflicting IDs, malformed input, revision bounds, and deterministic
  receipt eviction. Rejected/no-change requests leave persistent logical state
  unchanged; do not equate that assertion to zero OS writes.
- Commit failures without success acknowledgment. Inject bounded adapter failures
  for write/commit errors; do not fill a real card or change its permissions globally.
- Child termination at explicit before-commit and after-commit/before-reply barriers.
  An after-commit restart retains the operation even when its response was lost.
  For the file mode, after-commit means after parent-directory synchronization.
  Place the before-commit barrier before rename for the file adapter and before
  COMMIT for SQLite; verify old state after termination at that barrier. The
  after-commit barrier must precede response emission for both adapters.
  Use barrier handshakes, not sleeps, to establish injection points.
- Interrupted framing, unavailable engine, port conflict, deadline enforcement,
  process ownership, cancellation, and incomplete finalization.
- Sector conversion, counter discontinuity, percentile calculations, timeout and
  missed-arrival accounting, fixture reset, buffer overflow, budget aborts, and
  report classification using known synthetic collector inputs.
- Aggregate memory attribution, separation of export writes, and inclusion of
  finalization. Critical collection failures must be represented in reports.

Do not claim process-kill tests simulate electrical power loss. Do not add
power-cut hardware, privileged faults, production integration suites, or RF tests.

## Commands and evidence

Deliver documented commands for dependency checking, building, safe checks,
short smoke execution, one measured profile, resuming a campaign with new run
directories, and generating a combined report from existing completed results.
Make smoke output impossible to mistake for a measured campaign. Resume means
running missing profiles; it never appends to or overwrites an interrupted run.

The CLI must require an explicit data root and offer no implicit access to live
settings. Show the selected device, expected matrix, duration/write ceilings,
configuration identity, and result destination before a measured invocation.
The operator's explicit measured-run command authorizes that bounded local run;
do not add interactive prompts that prevent repeatable execution.

Use versioned machine-readable JSON for manifests and results, with strict shape
validation when importing reports. Preserve per-request timings and counter
samples in a compact machine-readable form plus a concise Markdown report.
Reject malformed/unsupported report formats and missing evidence rather than
silently skipping them. Report generation must not execute commands from input
files, fetch URLs, or read paths outside explicitly supplied result roots.

Reports include all attempts, errors, primary and cleanup failures, mode ordering,
sample counts, effective settings, initial/final counters, setup/export costs,
repetition ranges, and applicable limitations. A report generated from failures
is a successful reporting operation, not a passing experiment. Do not hide an
aborted run when later repetitions succeed. Record exact commands and exit codes.

## Implementation order and completion

1. Inspect the authorized workspace and available tools. Freeze the contract,
   fixtures, workloads, report schema, and budgets before implementation.
2. Implement the bounded socket engine, PHP fixture, and memory mode.
3. Add the atomic-file and SQLite adapters with equivalent semantics.
4. Pass focused correctness checks before collecting benchmark evidence.
5. Implement collectors, accounting checks, workload scheduling, and reports.
6. Run safe local checks and a short smoke test in a disposable owned directory.
   If the current host is not an authorized Linux test environment, run what is
   supported and report the precise remaining validation; do not provision a VM
   or access a Pi automatically.
7. Deliver the runnable bundle and operator commands. Native Pi 1 and Pi 5
   campaigns are operator-run follow-up, not fabricated completion evidence.

Before completion, adversarially assess scope, equivalent semantics, durability,
measurement bias, Pi 1 footprint, bounded writes, lifecycle safety, failure
classification, reproducibility, and whether the conclusions overreach the data.
Fix findings and repeat a full assessment until no further actionable findings
are identified. Report residual empirical unknowns separately; reviews cannot
prove future performance. Do not add features in response to speculative concerns.

The final implementation report must identify files created outside production,
commands/checks actually run, exact results, unsupported environments, outstanding
Pi runs, documentation impact, and any deviations. State explicitly that no
production code, service, hardware, or live configuration was changed and that
nothing was staged, committed, or pushed. If implementation is incomplete, name
the next unfinished step instead of claiming completion.

## Documentation impact and references

Write standalone build/run/recovery instructions, metric definitions, interpretation
limits, and bounded cleanup instructions. Do not modify production operator
documentation or claim this prototype changes WsprryPi behavior. Cleanup may
remove only an explicitly selected, verified owned run directory after evidence
export, outside timed windows; the normal runner must retain failed runs.

Use the owning technical documentation to verify implementation details:

- [SQLite WAL](https://www.sqlite.org/wal.html)
- [SQLite synchronization settings](https://sqlite.org/pragma.html#pragma_synchronous)
- [Linux block statistics](https://docs.kernel.org/block/stat.html)
- [Linux process memory accounting](https://www.kernel.org/doc/html/latest/filesystems/proc.html)

Dependency versions and effective platform behavior must be recorded at execution
time. Do not substitute these references or a prototype pass for production,
installation, physical timing, or RF evidence.
