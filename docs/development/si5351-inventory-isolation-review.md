# Adversarial review: Si5351 inventory isolation

## First assessment and repairs

Review source independently of test success, using the original blocked API
backtrace and interface evidence as the failure definition.

| Finding | Repair and required reassessment |
| --- | --- |
| Gating only GPIO serialization would leave active Si5351 snapshots and explicit scans able to stall. | All serialization now uses cached metadata; physical scans run in an isolated executable with a fixed deadline and bounded output. |
| A timed-out kernel I2C operation cannot safely be cancelled by abandoning a C++ thread. | Use posix_spawn, SIGKILL and nonblocking waitpid; retain at most one pending worker and reject new scans until it exits. Ordinary cache reads reap completed workers without starting a probe. |
| A killed child could retain service listeners/client sockets. | Spawn actions close every descriptor above standard I/O after redirecting the result pipe. Descriptor-inheritance test added. |
| Spawn action initialization and closed standard descriptors could break cleanup or IPC. | Destroy only initialized actions; relocate pipe descriptors above standard I/O. Tests exercise missing executable, failed child, oversized reply, timeout, admission, reuse and closed stdin. |
| Releasing the web configuration lock could let a stale candidate overwrite a newer transaction. | Capture revision and recheck after fresh validation, preserving revision_conflict. An actual delayed fixture and competing update verify reads stay responsive and newer state survives. |
| INI readiness still ran inside the shared configuration lock. | Parse/structural validation stays locked; physical readiness runs outside it, then checks revision/file stamp. Commit rejects a prepared candidate made obsolete by a newer web transaction. |
| Single-address readiness could accidentally become a full 16-address scan or replace the full inventory cache with a partial result. | Readiness scans only the configured address, remains bounded and does not replace the full inventory cache. New selections still use fresh full inventory. |
| Setup population could scan inactive Si5351 settings; late replies could change a GPIO draft. | Only active/explicitly selected Si5351 refreshes; requests carry sequence and backend/bus guards. GPIO/WTP/RP1 requests and late success/failure cases have dedicated tests. |
| Active Si5351 population could scan twice, first using the previous bus and then receiving busy on its correct scan. | Defer the population-triggered backend scan until saved bus/address fields are ready. Execute the actual handler and population calls in a regression that requires exactly one scan, while operator selection still refreshes. |
| Timeout/unchecked inventory could be displayed as missing hardware. | Omit detected state before a check, preserve the saved address, display unconfirmed for discovery errors, and keep the specific timeout/failure message beside the field. |
| UI address replacement could retain an unavailable flag for the previous address. | Availability follows the currently selected address from checked inventory, preserving explicit replacement and fresh server-side validation. |
| The INI conflict fixture depended on the Linux-only earlier logging change; its macOS write was a no-op. | Toggle the actual prepared value and test both initial logging states on every host. |
| The strict I2C audit assumed readiness directly opened a missing bus, while metadata validation now rejects it first. | Audit zero adapter opens during early rejection, then trace the isolated worker against one verified nonexistent device path; retain all forbidden-access and ioctl checks. |
| The six-profile release job reached its 15-minute limit just after its final compile. | Increase only that job's deadline to 30 minutes; retain all six profiles, factory checks and warnings as errors. |
| A later macOS tone-engine fixture checked async completion after a fixed sleep and used an arbitrary UTC-second phase. | Wait for a terminal report with a bound, anchor the real-time fixture at a full UTC second, and use controlled time to prove a later RF-on occurs beyond the first launch boundary. Reject Failed/Missed outcomes as before; production timing is unchanged. |

## Final reassessment

Final default Linux semantics and WTP configuration transaction tests passed on
wspr5 with hardware access disabled. The process tests cover timeout, cleanup,
closed stdin, inherited descriptors, oversized output, execution failure, busy
admission and reuse. Application isolation tests verify responsive snapshots,
fresh malformed-reply rejection, bus/reference cache separation and preservation
of a newer web/INI revision.

Final UI unit and browser regressions passed after the single-load scan repair,
with the affected parent UI/source regression rerun on Linux. Desktop/mobile review confirms
saved-value preservation and truthful unconfirmed timeout state. The live wspr4
GPIO4 case exercised an actual 2.029-second inventory timeout while 40 reads
(maximum 26.7 ms) and two reversible writes (maximum 35.4 ms) succeeded. Original
INI bytes and inactive output state were preserved, and no worker remained.

No unresolved finding remains in the inventory/configuration isolation slice.
The physical I2C fault itself remains a hardware/runtime diagnostic boundary.
No WTP routing repair, RF qualification or whole-fleet release claim is in scope.

### CI repair reassessment

The corrected INI fixture passes for both initial logging states on Linux. The
strict Si5351 release and factory test pass with ancillary GPIO disabled. Inside
an ephemeral network namespace, both the non-root profile test and file-access
audit pass; the worker attempts only the verified nonexistent selected path and
performs no I2C ioctl. The audit still examines both application and worker traces
for forbidden GPIO, mailbox, MMIO and RP1 access. No installed wspr5 service or
transmitter state changed. The release-job annotation confirms the original
15-minute timeout; raising its deadline removes no compiler or test gate.
Repaired GitHub CI results remain a separate check reported at completion.

The subsequent macOS WTP-stage failure was in the existing tone-engine fixture,
after portable semantics passed. Its repair changes only the test. Later-gate
time now advances explicitly beyond the 100 ms first-launch window; no relaxed
production deadline is involved. Terminal-state waits retain assertions for
completion with output off, failure without observed enable, past-start misses,
clock loss and cancellation. Linux execution and five repeats validate the
fixture before the final CI rerun.
