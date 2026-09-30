# WsprryPi as a WTP/1 endpoint

**Status:** Implementation contract with a bounded first implementation on
`devel`. [Pi endpoint operation](wtp-pi-operation.md) describes the implemented
settings and limits. The requirements below remain the acceptance authority;
source tests, live network tests and RF observations are reported separately.

## Protocol authority and goal

The maintained, device-neutral protocol lives in the WsprryPico
[protocol directory](https://github.com/WsprryPi/WsprryPico/tree/devel/docs/protocol).
In particular, [WTP/1](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/WTP.md),
the [DNS-SD profile](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/WTP-DNS-SD.md),
the [machine-readable contract](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/wtp-1-contract.json),
the [schema](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/wtp-1.schema.json),
and the [test vectors](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/protocol/test-vectors/wtp-1.json)
are the protocol references. Pin their exact revision for an implementation or
acceptance run. Resolve any disagreement among those artifacts at the protocol
source before claiming conformance; this local document does not revise WTP/1.

A WsprryPi can act as a WTP/1 **server** for its selected transmitter output.
A WTP client must be able to discover, identify, inspect, claim, load, arm,
observe, abort, and release that Pi through the same WTP operations it uses for
a WsprryPico. Product identity and truthful `CAPS` may differ; client control
semantics must not depend on whether the endpoint is a Pi or a Pico. A central
WsprryPi can manage its own schedule and independently control zero or more
WTP servers, each through its own client session. DNS-SD locates an endpoint;
WTP/1 controls its finite transmitter jobs.

This contract adds no Pi-specific WTP operation, frame field, service type, or
station-configuration command. WTP/1 does not manage the remote Pi's INI,
stored schedules, installation, services, or arbitrary GPIO. Those remain
outside the WTP endpoint contract.

## Managed Pi: one transmitter authority

The managed Pi SHALL have one authority over the selected transmitter chain.
Local scheduled work, local operator stop, and remote WTP work SHALL arbitrate
before access to that chain and use the same backend safety and cleanup path.
A network listener, WTP session, browser route, or scheduler must not create a
second path that can independently key output. A remote `CLAIM` SHALL be refused
when local work or unresolved output prevents exclusive ownership. Local work
SHALL not start while a remote job owns, is armed/running, or has unresolved
output. Admission must be atomic with respect to these competing paths.

For the normal managed service, the effective `Operation.Transmit` setting is
the operator's **local-control enable**. While it is true, local control keeps
the selected transmitter authority, including idle time between scheduled
jobs; the server SHALL refuse remote `CLAIM`. While it is false, the Pi is a
**candidate** for remote control. False does not itself grant a WTP claim or
prove output inactive: the server may accept `CLAIM` only after it confirms no
local job, Test Tone, startup request, cleanup, or unknown output holds the
chain. A successful remote `CLAIM` reserves it for the protocol-defined
ownership lifetime; local reuse also requires confirmed safe output. Remote
WTP work SHALL NOT set `Operation.Transmit=true` or silently create a local
schedule. This inbound ownership rule concerns the Pi's own transmitter
chain. It does not disable the Pi's outbound WTP client sessions or require
its locally managed schedule to stop controlling other devices.

Every local transmit entry point in the running service SHALL participate in
this authority gate. Normal WsprryPi runtime invocations acquire the existing
process singleton before full CLI parsing and runtime startup. If the managed
service holds it, a second WsprryPi process exits; a direct CLI startup request
is not a way to take over its remote job. When it is the singleton owner, a
direct CLI startup request can run with `Operation.Transmit=false`. Test Tone
can be requested when scheduled transmission is disabled, but must be refused
while WTP owns the chain. A local safety stop remains available regardless of
ownership. Disabling local control prevents new scheduled work on the Pi's own
transmitter but does not by itself make an active job safe; remote `CLAIM`
remains refused until that job finishes or is explicitly stopped and output
is confirmed inactive.

If the operator selects local Enable on the Pi whose transmitter is remotely
owned, that **local Pi** SHALL show a takeover confirmation identifying the
remote ownership and any known job. The prompt belongs to the target Pi's
local interface, never the central Pi's interface. For an armed or running WTP
job, the local operator chooses either **End remote job now** or
**Let current remote job finish**. The first choice invokes the Pi's trusted
local abort immediately after local confirmation, regardless of whether the
remote controller is connected. The second choice lets only that job reach its
terminal result.
Both choices close admission to replacement remote jobs immediately and end
remote ownership after the chosen job outcome and confirmed inactive output.
An idle claim or loaded but unarmed job is released or canceled after local
confirmation. Declining leaves ownership and the saved local-enable value
unchanged. The remote controller has no approval, acknowledgment, veto, or
cancellation choice in this local workflow; it observes the resulting job and
ownership state through WTP.

The browser's local Enable control SHALL use an explicit local takeover action,
not an ordinary configuration write, so its choice is enforced even if a
remote `CLAIM` races with the displayed status. The action atomically checks
ownership: without a remote owner it can enable local control; with a remote
owner it reports that confirmation is required without changing the saved or
effective enable state. After the local prompt, the browser submits the chosen
end-now or finish-current action. This is an application control path, not a
Pi-specific WTP operation.

For the managed service, a valid INI load or reload that leaves
`Operation.Transmit=true` after any applicable boot policy is a local takeover
request even when no operator is present to answer a prompt. If a remote WTP
claim or job exists, the Pi SHALL close admission to new remote work and
initiate its trusted local abort as soon as the enabled INI change is validated.
It SHALL cancel an armed or running remote job rather than defer the enable
until that job finishes. It SHALL not wait for the controller to respond or
approve cancellation. The saved INI value is the requested state; effective
local enable and new local RF work remain
blocked until remote ownership ends and output is confirmed inactive. If abort
or output confirmation fails, the Pi SHALL report the requested-versus-effective
state and keep RF work inhibited without silently rewriting the INI. A newer
INI change must be reconciled before committing the transition.

An accepted direct HTTP configuration write that sets `Operation.Transmit=true`
is likewise a noninteractive local takeover request. This includes `PUT` or
`PATCH /config` and `PUT /api/v1/host/config`; the server SHALL not infer an
interactive prompt from the request's browser headers, origin, or route. With
remote ownership present, it SHALL close remote admission and initiate the
trusted local abort immediately after validating the write, with no controller
approval or wait for the remote job to finish. Its response and subsequent
status must distinguish an accepted request from confirmed effective local
enable. Abort or output-confirmation failure leaves RF work inhibited and
reports the unresolved state.

Local Enable SHALL complete once remote ownership ends and output is confirmed
inactive, without waiting for the remote controller or central schedule
cleanup. Unknown output or failed cleanup leaves local enable off and reports
the blocking state beside the control. The transition, WTP `CLAIM`, and all
local start paths must share one atomic decision, including concurrent web,
scheduler, Test Tone, and network requests. Local safety stop remains available
even if the takeover confirmation is declined or pending.

The local Pi SHALL durably record a takeover that revokes its future remote
assignments, including when the central Pi is offline. The central Pi SHALL
cancel queued jobs and delete saved future assignments for that output when it
reconciles the revocation, before any further dispatch to the target. Local
Enable is never contingent on that deletion. The revocation must survive
restart and remain discoverable if local control is later disabled again.
WTP/1 cannot delete assignments stored on the central Pi; the reconciliation
mechanism is an application concern outside the WTP job protocol and remains
open below.

At startup, the effective `Operation.Enable on Boot` policy is applied before
remote eligibility is determined. `Always` makes the managed Pi locally
enabled after restart, whereas `Never` makes it a remote candidate only after
startup quiescence and the other admission checks. Restart invalidates old WTP
ownership; it never resumes or rearms remote work. An operational DNS-SD
advertisement identifies a reachable listener, not remote eligibility: a
locally enabled Pi may answer inspection requests but must refuse `CLAIM`.

The selected local backend determines the server's advertised engine, modes,
frequency ranges, event and duration limits, start uncertainty, arming lead,
and output-disable timeout. `CAPS` SHALL be derived from implemented and
validated behavior, not copied from Pico limits or inferred from a backend
name. A backend or mode without the required finite-event timing and confirmed
output-disable behavior SHALL not be advertised. The server must reject an
unsupported job before `ARM`; it must not silently choose another backend.

At `LOAD`, the Pi SHALL validate the complete immutable `rf-events/1` job and
prepare a locally owned execution plan. The conversion from wire integer
nanohertz and nanoseconds to the existing `ExecutionPlan` and backend types
requires checked arithmetic and explicit frequency-realization policy. No
event may be dropped, stretched, reordered, or timed by network messages.
Unrealizable frequency is rejected unless the job permits adjustment; an
accepted adjustment is reported by `LOAD` and remains fixed after `ARM`.
Backend-specific GPIO, power, calibration, and frequency-safety policies remain
local admission requirements, not WTP fields.

At `ARM`, the Pi SHALL use its own validated UTC-to-monotonic clock mapping,
enforce the negotiated lead and uncertainty limits, and arrange local execution
at the immutable requested start. It must recheck clock admission before output
is enabled and report a missed start rather than shift or automatically repeat
the job. Once running, all event boundaries are owned by the Pi's local engine.
Merely wrapping the current synchronous `execute()` call does not establish
these scheduling guarantees.

At startup/reset, the Pi SHALL command output off and verify it before
accepting WTP connections. It has a stable, full 32-lowercase-hex `device_id`
and a fresh `boot_id` after reset, following WTP/1. `STATUS` is authoritative
for job, owner, and output state. `ABORT` and natural completion require bounded
confirmation of inactive output. Failure to confirm it enters the protocol's
failed/unknown-output condition and blocks new RF work. Disconnect, lease loss,
an `ARM` reply, or a terminal label alone never proves output inactive. Local
safety stop may use the protocol's local-abort provision, with the same terminal
and output checks; remote ownership cannot prevent a local safety stop.

The server SHALL preserve WTP/1 session/principal binding, request replay,
resource idempotency, lease behavior, terminal retention, reconnect, and boot
change semantics. A restart must not adopt or rearm an old job. Reuse of Pico's
portable server concepts is preferable where practical, but any code-sharing
decision must preserve both repositories' component boundaries and the upstream
protocol authority.

## TCP binding and DNS-SD advertisement

The Pi WTP server SHALL support the same **Plain LAN** binding as the
WsprryPico consumer listener: raw WTP/1 frames over TCP, without TLS,
certificates, or ALPN. This is a required capability, not a fallback or a
temporary substitute for TLS. The existing WsprryPi `network_plain` client
adapter can use it after the central Pi gains independent per-target runtimes.
The Plain LAN listener's default TCP port SHALL be 31417, with a validated
override in saved configuration or on the command line. A command-line
override applies to that process without silently rewriting the saved port.
The Plain LAN listener is enabled by default for the managed service and can
be explicitly disabled independently of local transmit enable and outbound
fleet scheduling. It is restricted to an operational station LAN interface.
After startup quiescence and the effective boot policy, it SHALL accept remote
`CLAIM` when local `Operation.Transmit=false` and no local work, owner, cleanup,
or unknown output blocks the chain. If local transmission is enabled, the
listener may serve read-only WTP inspection but SHALL refuse `CLAIM`. It must
not open on an unsafe or ambiguous interface, because a TLS certificate is
present, or because Avahi found another device.

Every admitted Plain LAN client has the protocol's shared `local-network`
principal. WTP session ownership and `CLAIM` serialize jobs, but do not
authenticate a person or a particular client machine. Any host able to reach
that listener can attempt WTP control. The operator therefore chooses the LAN
and explicitly selects Plain LAN on the central Pi. A matching `HELLO`
`device_id` detects some wrong-endpoint mistakes but is not cryptographic
identity proof. DNS-SD data cannot authorize a connection or turn on the
Plain LAN binding. There is no automatic Plain LAN/TLS fallback in either
direction.

TLS is an optional additional binding, not a prerequisite for a usable Pi WTP
endpoint. If implemented, it SHALL satisfy WTP/1's TLS 1.3, server identity,
ALPN `wtp/1`, and principal rules; engineering use requires mutual
authentication and device-specific credentials. Credential storage and rotation
must not strand unresolved ownership. Each enabled binding has its own listener
and DNS-SD instance with its own SRV port. Listener enablement, interface,
port, binding, and any credential references must traverse the normal
configuration defaults, parsing, validation, persistence, reload, and status
lifecycle. Invalid or incomplete settings must fail before opening a listener.

On an operational interface where the WTP/TCP listener is bound and admitting
connections, the Pi SHALL advertise the existing WTP service type
`_wtp._tcp.local.` using Avahi. The TXT profile is the maintained version 1
format: first `txtvers=1`, and exactly one `binding=tls` or `binding=plain`.
The SRV record supplies the actual listener target and port. The instance name
is display text, not durable identity. TXT must not contain credentials, device
ID, job state, capabilities, or a duplicate port. Advertisements are withdrawn
when listener admission or the interface is lost; an Avahi failure makes
discovery unavailable without changing transmitter state. The referenced
DNS-SD profile treats the service name and direct-connect port 31417 as
provisional pending IANA assignment; a client must use the discovered SRV port.
Manual direct endpoints remain possible when
mDNS is unavailable. Discovery does not trigger `CLAIM`, `LOAD`, or `ARM`.

## Central Pi: local schedule and independent target sessions

A central Pi SHALL retain its local scheduler with **zero** managed remote WTP
targets; a configured local transmitter continues to work. It SHALL support
independent control of zero to a bounded number of remote Pi or Pico WTP
servers while managing its local schedule, including when its own transmitter
is enabled and active. The local transmitter is one output authority; each
outbound WTP target is another. The central scheduler may continue dispatching
remote jobs when its own transmitter is disabled.

Enabling or disabling the central Pi's local output SHALL NOT implicitly start,
stop, claim, release, or retarget a remote server. Likewise, a remote target's
failure or recovery SHALL NOT change the central Pi's local enable setting or
silently cancel its local schedule. If one scheduling decision produces local
and remote jobs, admission and outcomes remain explicit for every output.
The central Pi SHALL assign jobs explicitly to each local or remote output;
adding a target to the catalog does not copy the local schedule or automatically
schedule that target. Each remote Pi receives finite WTP jobs from the central
Pi, not a remotely installed schedule or station configuration.

A locally confirmed takeover on a target Pi overrides the central Pi's
assignment to that output. The central Pi may observe an abort, completion, or
ownership loss through WTP, but it cannot approve or delay the target's local
action. Its ordinary WTP `ABORT` operation for jobs it owns does not authorize
or gate a target Pi's local takeover. Before dispatching another job to a
reconnected target, the central Pi SHALL reconcile that target's durable
takeover revocation, cancel queued work, and delete its saved future
assignments for that output. This also applies if the target's local output
has since been disabled and it could accept a new WTP claim. Reconciliation
must not silently recreate those assignments.

The current [Fleet catalog](wtp-fleet.md) stores multiple profiles but selects
only one active `[WTP]` backend endpoint; the current parent runtime in
`src/wtp_runtime_bridge.cpp` holds one selected client. These are existing
implementation limits, not limits on the central Pi contract. Multi-target
control SHALL introduce a bounded, independent runtime context for each active
remote target: immutable endpoint/binding selection, optional TLS credential
references, full expected device identity, session and owner IDs, worker,
pending and active job, clock and CAPS observations, reconciliation latch, and
last result. The target set may be empty. It must not implement concurrent
control by repeatedly rewriting the single active `[WTP]` section or switching
its runtime while another target has work. The local schedule and transmitter
SHALL NOT consume a remote target slot. Outbound target activation SHALL be
independent of the central Pi's local backend choice and local transmit-enable
state. During coexistence with the current single-target backend, two runtime
paths must not claim the same device independently. The target set SHALL reject
the central Pi's own `device_id` and duplicate target identities; discovering
its own listener must not create an outbound self-session.

The current selected-backend path couples `Operation.Transmit` to one WTP
target when `Transmit Backend=wtp`. That behavior remains a compatibility
concern, not the basis of multi-target scheduling. Implementing this contract
requires remote job admission and scheduling that do not depend on selecting
WTP as the central Pi's sole transmit backend or enabling its own output.
Local output enablement, inbound WTP listener admission, and outbound fleet
scheduling SHALL be independent control paths. Their configuration, persisted
state, and status must remain distinguishable; changing one shall not silently
toggle either of the others.

For every newly selected or reconnected target, the central Pi SHALL verify
the selected binding and, for TLS only, its expected certificate identity.
It then performs `HELLO`, checks the exact expected device ID and any boot
change, and obtains `STATUS` and `CAPS` before mutation. A changed boot ID
invalidates prior ownership and job assumptions; it does not authorize
automatic reload or rearm. A Plain LAN connection needs an
operator-confirmed expected full device ID, while acknowledging that its
`HELLO` is unauthenticated. A discovered instance, address, hostname, TXT
record, or Plain LAN `HELLO` is not a trusted replacement identity. A changed
binding or endpoint needs the existing explicit catalog review; no silent
retargeting, downgrade, job adoption, or
replay follows discovery or reconnect. Each target's `STATUS` age and unknown
output condition remain distinct. A failure on one target must not clear a
different target's state or block bounded cleanup of that target.

The central Pi may schedule its own output and finite jobs for several remote
targets at the same UTC start, but this is a set of independently admitted
outputs, not an atomic fleet operation. It SHALL prepare and arm each remote
target within that target's own CAPS and clock limits, report local and
per-target admission and outcome, and expose partial success or uncertainty.
It must never infer that a failed or disconnected target is RF-off or
automatically move its job to another Pi or Pico. Each managed Pi retains
local event timing after its own `ARM`.

Existing Fleet visibility remains development gated as described in
[wtp-fleet.md](wtp-fleet.md). This contract does not authorize revealing the
UI, changing current profile-selection semantics, or implementing a batch
transmission workflow. Any future UI work requires the repository's Impeccable
workflow and separate operator documentation review.

## Implementation boundaries and acceptance

Implement in bounded slices, with an explicit review of the affected
`src/` component before modifying it:

1. **Server core and local arbitration:** WTP framing, schema, session/replay,
   job service, selected-backend admission, startup inhibit, and status through
   injected clocks and the hardware-free simulated backend. Prove local work
   and WTP claims cannot overlap. Exercise enabled-but-idle refusal, disabled
   idle admission, disabled-with-local-work refusal, declined or failed local
   takeover, local end-now and finish-current-job choices, blocked replacement
   jobs during takeover, offline-controller takeover without a remote response,
   valid enabled-INI reload during an armed or running WTP job with immediate
   local abort and no wait for job completion, invalid or superseded INI changes,
   direct HTTP enable through each supported config route with the same abort
   policy, browser enable/claim races that require a local prompt, truthful
   requested-versus-effective HTTP responses, durable revocation and eventual
   removal of saved future assignments, and concurrent enable/claim and Test
   Tone/claim races. Preserve the process singleton so a second runtime is
   refused while the managed service owns a remote job.
2. **Pi engine adapter:** exact event/frequency conversion, local ARM timer,
   backend execution, abort, and output confirmation for one named backend.
   Expand `CAPS` only as additional modes/backends pass their own acceptance.
   WTP-to-WTP forwarding through an attached Pico requires a separate ownership,
   clock, and recovery assessment before it is advertised.
3. **Network and discovery:** bounded Plain LAN listener, station-interface
   restriction, shared-principal behavior, Avahi lifecycle, SRV/TXT behavior,
   default-on listener with claim admission gated by local enable and confirmed
   output safety, default port 31417 and validated saved/CLI override, direct
   configuration, and no binding fallback. Check that DNS-SD publishes the
   effective SRV port, including a command-line override. TLS is a separate
   optional extension with its own credential and authentication acceptance.
4. **Central multi-target client:** zero-to-many per-target runtimes, resource
   bounds, identity/reconnect/recovery behavior, simultaneous local and remote
   finite-job scheduling, and per-output partial-result reporting. Preserve
   existing single active `[WTP]` behavior until a reviewed migration or
   coexistence design is complete; it is not the multi-target architecture.
   Prove zero-, one-, and multiple-target operation while the local schedule
   remains enabled; catalog enrollment without implicit scheduling; independent
   control-path toggles; a refused inbound claim that leaves outbound work
   alone; and reconnect after local takeover with queued and saved assignments
   removed before another dispatch, even if the target is remotely eligible
   again.

The software gate includes the upstream WTP schema, contract and vectors;
fragmented/combined frames; parser bounds; duplicate and replay behavior;
ownership collisions with local scheduling and Test Tone; singleton refusal
of a second runtime while the service owns a remote job; enable/claim races;
local disable while a job remains active; declined and
failed interactive takeover with no persisted enable; `Enable on Boot` policy;
lost replies; enabled-INI reload during remote ownership;
noninteractive HTTP enable during remote ownership; browser takeover prompt
versus generic config-write behavior;
requested-versus-effective status after an abort or output-confirmation failure;
reconnect and boot changes; clock loss and leap exclusions; missed starts;
zero-target local scheduling; concurrent local and multi-target work; local
enable changes without implicit remote action; explicit per-output assignment;
per-target failure isolation;
output-unknown latching; and direct and DNS-SD Plain LAN connection without TLS
credentials. Run relevant standalone component and parent semantics tests from
`src` with hardware access disabled. The simulated backend qualifies software
paths only. Separately authorize and record Linux Avahi/Plain LAN
interoperability, service lifecycle, physical backend timing and output-disable
behavior, and conducted RF for each hardware route. No source test or
advertisement establishes installation, GPIO, electrical, frequency, spectral,
or RF qualification.

Before operator release, review the separate `Wsprry_Pi_Docs` repository for
the local-enable/remote-candidate boundary, independent local and remote
scheduling, endpoint enablement, LAN trust and identity, selection, status age,
stop/recovery, and multi-output partial outcomes. Do not edit that repository
without its separate authorization.

## Selected implementation decisions

- Native GPIO, RP1 GPCLK and Si5351 routes: derive CAPS from the selected backend
  capability metadata and clock/planner envelope. Execute WSPR, TONE, QRSS,
  FSKCW and DFCW through native finite event plans; preserve existing processor,
  route, development authorization, band qualification and frequency policy.
  The protocol bounds remain 512 events and 24 hours. Per-event realization must
  come from the same configured clock/tone program used for execution. RP1's
  existing exact-operation host confirmation remains required; the adapter
  does not promote its development route into ordinary Fleet availability.
- Plain LAN only for the Pi listener. Default 31417 with INI/CLI overrides;
  automatic selection requires one eligible physical RFC1918 IPv4 station-LAN
  address. An explicit interface resolves multiple-interface ambiguity.
- Durable private per-device generation journal on the target. Central Pi
  status reconciliation compares generations before CLAIM/ARM and removes
  changed assignments without local takeover waiting for controller contact.
- Up to eight independent target contexts and one saved finite periodic
  schedule per output, alongside the existing local scheduler. The legacy
  `[WTP]` route remains available; full-identity exclusion prevents duplicate
  authority. Assignments have a separate atomic private file and revision.
- Failed jobs pause their assignment; consumed slots are not retried. Lost
  contact and an in-flight restart require reconciliation, never assumed RF-off.
- Fleet retains its existing development gate. Promotion and additional
  route/mode qualification remain separate work.

The clock/ARM/cleanup limits in the implementation are enforced bounds. Only
recorded live evidence qualifies their tested conditions; SDR visibility is
RF acceptance for this execution and does not establish unrelated accuracy,
spectral, long-duration, or release claims.
