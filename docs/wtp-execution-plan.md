# WTP execution-plan conversion

Phase 10 Slice 3 implements the pure parent application adapter in
[`src/wtp_integration`](../src/wtp_integration/execution_plan.hpp). It consumes
the reusable WSPR-Transmitter execution-plan type and produces WTP-Client
types. Neither component depends on the other. The adapter is compiled by its
focused test target; it is not linked into the production application yet.

## API and authority

`prepare_wtp_plan(plan, caps, options)` returns an owned `WtpPreparedPlan` or a
`WtpPlanResult` containing a specific error, explanation and event index when
applicable. Failure returns no partial job or ARM request. The source plan and
previously prepared results are not modified. The result retains source plan
and request IDs for later integration; those process-local numbers are not WTP
job identities.

The caller supplies a fresh 32-character lowercase hexadecimal job ID, an
absolute UTC start in unsigned integer nanoseconds, and a maximum permitted
start uncertainty. The result contains a complete finite `wtp::Job` and a
matching `wtp::ArmRequest`. The UTC start is copied exactly; UTC end overflow
and an uncertainty request above the advertised maximum are rejected.

`ExecutionPlan` contains no absolute schedule. The caller must derive the
explicit UTC value from the committed scheduled slot, including its offset,
using checked arithmetic. This slice does not yet connect that scheduling path.
The converter cannot assess clock age, holdover, leap exclusions, minimum lead
or horizon without a fresh device clock and the preparation timeline. Those
checks belong to later scheduler/backend admission; the session already
validates correlated ARM acknowledgments. Never treat conversion as ARM success
or compensate for an unsuccessful ARM by starting late.

CAPS must come from the selected device's validated session. The converter
checks the conversion-relevant profile, mode, frequency ranges, event count,
duration and encoded payload bounds. It does not establish device identity,
ownership, clock validity, RF permission, output state or physical qualification.
Session admission must run again before submission. A previously compiled
plan's backend label is retained only in the source plan; calling this pure
function does not select or fall back to any physical backend.

Device UTC must be independently provisioned. GET_CLOCK is observation only;
USB Console clock provisioning remains a separate proposed scope item.

## Representation rules

| Input | WTP representation |
| --- | --- |
| WSPR, QRSS, FSKCW, DFCW | Same mode, when advertised by CAPS. |
| Explicit finite TONE | Same complete timeline, including any final RF-off event. |
| Event offset and duration | Exact supplied integer nanoseconds. |
| RF-on event | Gate on with its event frequency, converted to integer nanohertz. |
| RF-off event | Gate off, frequency omitted; inactive source frequency is not an output request. |
| `policy.allow_quantization` | Explicit `allow_frequency_adjustment` Boolean. |
| `policy.allow_backend_approximation` | Does not authorize dropping unsupported features. |

Events remain uncoalesced and in order, preserving source indexes for later
adjustment/progress correlation. The event count must match the summary,
offsets must start at zero and remain contiguous, durations must be positive,
and the events must fill the declared total. RF_ON and SET_FREQUENCY events
must have RF enabled, RF_OFF must have RF disabled, and HOLD can represent
either state. Unknown event types and contradictory gates are rejected.
No truncation, splitting, stretching or inserted timing is permitted.

The converter uses event frequencies, not `reference_frequency_hz` or the
summary frequency extrema. Those values can contain legacy backend offsets or
declared extrema not present in a particular message. Each requested RF-on
frequency must fit an advertised range after representation conversion. No
hard-coded experimental Pico band or 162-event limit replaces CAPS.

Binary64 hertz values are converted by exact multiplication by 1,000,000,000
and rounding to the nearest integer, with exact halfway cases rounded upward.
Nonfinite, nonpositive, zero-after-rounding and uint64-overflowing values are
rejected. The calculation uses two integer words, avoiding dependence on
extended `long double` precision or compiler-specific integer types. This
representation step changes the supplied binary64 value by at most half a
nanohertz. It cannot recover decimal precision already lost upstream.
Device realization/quantization is separate and requires the explicit policy
flag and a validated LOAD acknowledgment.

Timing is already quantized to integer nanoseconds in `ExecutionPlan`, so the
converter performs no per-event time rounding. The existing shared WSPR
compiler supplies 162 periods of 682,666,666 ns, totaling 110,591,999,892 ns,
as documented in the [simulated backend contract](simulated-backend.md).
That timeline is preserved. A producer using cumulative nearest rounding of
the exact 2,048,000,000/3 ns period instead supplies varying integer
durations totaling 110,592,000,000 ns; that timeline is also preserved.
Changing the shared compiler's existing timing is outside this slice.

## Unsupported features and policy boundaries

- CW is rejected even if advertised: the current application execution-plan
  compiler does not implement CW. A wire mode name is insufficient integration.
- Implicit continuous tone is rejected, including the compiler's default
  finite surrogate for an unspecified duration. No automatic repeated jobs
  emulate an indefinite transmission.
- Nonzero/nonfinite host PPM and any calibration reference snapshot are
  rejected. WTP/1 has no equivalent calibration field. The converter neither
  applies host correction to requested RF nor reuses a Pi chipset correction.
  The selected device's frequency calibration is independently provisioned.
- Any envelope shape other than NONE, or a nonzero fade-in/out duration, is
  rejected. With shaping disabled, `fade_slice` is inactive metadata.
- No electrical power control or phase-continuity guarantee is represented.
  WSPR power metadata is already encoded in the supplied RF events; it is not
  a hardware drive-strength request. The future backend must reject unsupported
  output requirements not carried by `ExecutionPlan` before calling this API.

Stop/truncation policy, experimental frequency permission and hardware-profile
authorization are retained in the source plan and remain application/backend
responsibilities. A successful conversion does not enforce those policies or
grant permission to execute.

## Hardware-free validation

Run from `src`:

```sh
make wtp-plan-test wtp-protocol-test SUDO=
make wtp-plan-test SUDO= WTP_PLAN_BUILD_DIR=build/wtp-plan-sanitized \
  WTP_PLAN_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_PLAN_LDFLAGS='-fsanitize=address,undefined'
make semantics-make-regression-test semantics-test-portable SUDO=
```

The focused test compiles the actual execution-plan compiler, pure adapter
and WTP client without a physical backend. It tests all five supported modes,
canonical and cumulative-rational WSPR boundaries, exact frequencies, gates,
limits, invalid inputs, unsupported features, wire round trips and failure
atomicity. A separate Python `Fraction` oracle checks binary64 conversions
across exponents, halfway cases and uint64 boundaries. Tests use no device,
transport, installation or service operations. CI runs the focused target on
macOS and Linux; a local macOS pass is not evidence that Linux CI ran.

## Documentation impact and remaining work

This document, the root README and the WTP component README describe the
developer API. No operator behavior or configuration changes in Slice 3.
The separate Wsprry_Pi_Docs backend, INI and Transmitter documentation was
considered and remains unchanged. Later operator integration needs separately
authorized documentation for endpoint identity, finite jobs, unsupported
features, clock prerequisites and truthful cancellation/output-unknown status.

The [USB CDC transport adapter](wtp-usb-cdc.md) is implemented in Slice 4.
The [backend](wtp-backend.md) and [early scheduler](wtp-scheduling.md) are
implemented in Slices 5 and 6. Slice 7 supplies status/recovery, and the parent
[production integration](wtp-production-integration.md) now connects selection,
an owned worker and UI controls with a temporary default-off development toggle.
Physical USB behavior,
device timing, RF output and release readiness remain unqualified by these tests.

## Selected Pico message limits (Phase 11.5 R3 v2)

On the WTP backend, QRSS, FSKCW and DFCW accept at most 32 characters,
including spaces. The shared compiler rejects 33 without truncating or splitting;
other hardware backends retain their existing message behavior. Existing Morse
alphabet, case/spacing rules and WSPR framing remain unchanged. A worst-case
32-character native plan has 383 events (192 marks and 191 gaps), below the
unchanged 512-event rf-events/1 ceiling. The Pico's separate compact browser
message compiler adds a 1,000 ns off tail and therefore uses 384 events.

The selected Pico firmware permits 3,600 seconds total per finite job. Actual
negotiated CAPS remain authoritative: the converter still rejects a job above
an older device's smaller event or duration limits and checks encoded payload
size independently. Errors report the actual count/duration and advertised
limit. Message length alone cannot guarantee that slow timing fits one hour.
Raw event plans carry their explicit constraints, with no inferred text length.
The native Tone stop marker is included in its total duration; requesting an
RF-on interval of exactly 3,600 seconds plus that marker exceeds a one-hour cap.

The request builder checks finite positive timing and nanosecond conversion
bounds before casting or multiplying durations. The WTP compiler checks event
sum overflow. Host regressions include 31/32/33 characters and counted spaces
in every mode, smaller CAPS, exact duration boundaries and integer overflow.
The parent WTP production tests exercise the request builder. These checks
qualify software behavior; final physical long-job and R3 acceptance is tracked
in WsprryPico's Phase 11.5 ledger and remains open during implementation.
