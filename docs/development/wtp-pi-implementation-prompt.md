# WsprryPi WTP endpoint and fleet implementation prompt

Work in the existing `devel` checkout of `/Users/lbussy/GitHub/WsprryPi`.
Implement the proposed [Pi endpoint contract](../wtp-pi-endpoint-contract.md)
in bounded, reviewable slices. The WsprryPico protocol directory is the
device-neutral authority; pin the protocol artifacts to commit
`c13fc16819749e9b53a42609f31702c3aca9931c` of
`WsprryPi/WsprryPico` for this implementation and record any later protocol
change explicitly. Do not revise WTP/1 to accommodate a Pi-only behavior.

## Outcome

A WsprryPi can be discovered and controlled as a WTP/1 server for its own
transmitter, with Plain LAN available without TLS. A central WsprryPi keeps its
own schedule while explicitly assigning finite jobs to zero or more Pi/Pico
WTP outputs. Local control on each target Pi takes precedence over remote
ownership. Report separately what is implemented, simulated, installed,
observed on a Pi, and physically qualified. Never treat source-level success
as RF qualification.

## Starting state and boundaries

1. Read `AGENTS.md`, the endpoint contract, `docs/simulated-backend.md`, the
   relevant WTP integration documentation, and the pinned WsprryPico WTP/1,
   DNS-SD, JSON contract, schema, and vector artifacts. Check `git status`,
   staged and unstaged diffs, nested instructions, affected component paths,
   and the existing Makefile targets before editing or testing. Preserve all
   existing changes; `README.md` and the endpoint contract may already be
   uncommitted work from this design session.
2. Use the existing process singleton. A second normal WsprryPi runtime must
   exit before startup when the managed service owns a WTP job. Do not build a
   second transmitter authority or a separate daemon that evades the guard.
3. Keep the WTP server, current single-target WTP client, future multi-target
   client, and local scheduler under explicit independent authority. Preserve
   compatibility with existing valid configurations and existing one-target
   behavior until a reviewed migration is in place.
4. Keep Fleet hidden behind its current development gate until its complete
   workflow is accepted. For any UI edit, follow `AGENTS.md` and Impeccable,
   inspect the incumbent design, test desktop and mobile layouts and keyboard
   behavior, and keep status/confirmation near the triggering control.
5. Do not edit `../Wsprry_Pi_Docs` without its separate authorization; report
   exact operator-documentation follow-up. Avoid edits to WsprryPico and
   reusable `src/` components unless a documented defect and approved scope
   require them.

## Slice 1: server model and simulated arbitration

- Implement bounded WTP framing, request validation, HELLO-first session
  handling, principal/session ownership, replay/idempotency, leases, retained
  results, STATUS, clock, and truthful CAPS through injected clocks and the
  hardware-free simulated backend. Reuse existing codec types where suitable;
  review a component before modifying it.
- Model local enable, local scheduled work, Test Tone, WTP claim/job state,
  cleanup, and unknown output behind one atomic transmitter admission gate.
  `Operation.Transmit=true` reserves the local output, including idle periods.
  False makes it a candidate, subject to no local work and confirmed inactive
  output. Preserve the singleton as an outer process guard.
- Implement the local Pi takeover decision: an interactive local Enable
  prompts for **End remote job now** or **Let current remote job finish** when
  an armed/running remote job exists. Both choices immediately close remote
  admission. End-now invokes local abort immediately after confirmation;
  finish-current waits for only that job. A declined prompt changes nothing.
  The controller never approves, acknowledges, or vetoes local takeover.
- A validated INI load/reload or accepted direct HTTP config write setting
  `Operation.Transmit=true` is noninteractive: immediately close remote
  admission and initiate local abort, even while the remote controller is
  absent. Distinguish requested/saved enable from effective enable until
  ownership ends and output-off is confirmed. Do not defer cancellation until
  job completion. Give the browser a distinct interactive action so ordinary
  config writes cannot bypass the prompt or lose an enable/claim race.
- Define and test a durable target-side revocation record plus central
  reconciliation that removes queued and saved future assignments after an
  offline local takeover, before another dispatch. Local Enable must not wait
  for controller contact. If this cannot be completed in this slice, stop the
  slice at a truthful boundary and do not claim takeover closure.
- Test collisions, status, replay, lease expiry, startup inhibit, abort and
  output-unknown failure, INI/HTTP/browser races, declined takeover, both
  takeover choices, offline controller, singleton refusal, and restart.

## Slice 2: one physical Pi engine route

- Inspect the available Pi hardware and select one named physical backend and
  initial finite-job modes. `wspr5` has SDR/GPSDO and WsprryPi; `wspr4`,
  `wspr2`, and `wspr1` have WsprryPi, in descending capability. Choose by
  actual attached transmitter path and backend evidence, not hostname alone.
- Convert wire integer nanohertz/nanoseconds to `ExecutionPlan` with checked
  arithmetic and documented frequency realization. At LOAD, reject jobs
  outside measured backend limits; at ARM, use a measured UTC-to-monotonic
  mapping, uncertainty and lead, and schedule the immutable start. Maintain
  local timing after ARM and confirm output-off within a bounded interval on
  completion, abort, failure, and restart. Advertise only qualified CAPS.
- Use simulation and non-RF tests first. RF testing is authorized in this
  execution. Choose a short 20 m test using the existing wspr5 setup and an
  explicit stop procedure; do not ask the operator to document the hardware
  chain. The RF acceptance criterion is a corresponding signal on the SDR.
  Installation and service changes retain their separate authority under
  `AGENTS.md`. Record the host, revision, chosen test settings, SDR observation,
  and stop result needed to identify the run; do not infer clock, frequency,
  spectral, or long-duration qualification merely from visibility.

## Slice 3: Plain LAN listener and DNS-SD

- Add a default-on managed-service Plain LAN WTP listener at TCP port 31417,
  with validated saved configuration and transient CLI override. Allow
  independent listener disable. Bind only a deliberate station LAN interface;
  fail closed on ambiguous or unsafe selection. At boot, accept CLAIM only
  after startup quiescence, effective boot policy, local-disable state, and
  output safety checks. Local enable may leave inspection available but
  refuses CLAIM.
- Treat every Plain LAN client as shared `local-network` principal. Expose the
  lack of machine authentication truthfully; require explicit operator binding
  selection and full expected device ID on the central Pi. Never infer trust
  from DNS-SD or silently downgrade to Plain LAN. TLS is optional and must
  meet WTP/1 separately if offered.
- Advertise `_wtp._tcp.local.` through Avahi only while the listener is bound
  and accepting connections. Publish the actual SRV port (including CLI
  override) and valid TXT v1 binding. Withdraw on listener/interface loss;
  keep direct connection possible if Avahi is unavailable. Discovery never
  claims or arms a target.
- Verify frame limits, concurrent clients, bind failure, disabled listener,
  address/interface changes, Avahi failure, SRV/TXT parsing, and direct/DNS-SD
  client interoperability without TLS credentials. Separate source tests from
  live network observation on a selected Pi.

## Slice 4: independent central multi-target scheduling

- Implement bounded per-target runtime contexts rather than rewriting the
  single active `[WTP]` section. Preserve the existing single-target route
  during migration and prevent two runtimes from claiming the same identity.
  Reject self-targeting and duplicate full device IDs.
- Persist explicit per-output job assignments. A catalog entry does not imply
  scheduling. Support zero targets with the local schedule, one target, and
  multiple independent targets while local output is enabled or disabled.
  Changing local enable, inbound listener admission, or outbound scheduling
  must not silently toggle either other path.
- On connection/reconnect, check selected binding and expected identity,
  HELLO, boot ID, STATUS, and CAPS before mutation. Reconcile a target's local
  takeover revocation and delete queued/saved assignments before dispatch,
  even if it has become remotely eligible again. Keep outcome/status age and
  output uncertainty separate per target; report partial results rather than
  assuming atomic fleet success or moving a job automatically.
- Add operator UI for explicit assignments, per-output status, local takeover,
  and recovery while retaining the Fleet development gate. Follow Impeccable
  and the established UI design; render desktop/mobile and exercise the full
  operator workflow.

## Validation and adversarial closure

For each slice, run the smallest meaningful standalone component and parent
tests, then the appropriate `src/Makefile` semantics profile for the host.
Inspect target recipes before running them. Use explicit simulation for
application-level transmission tests. Check the final diff for whitespace and
review all changed code, tests, configuration lifecycle, UI, documentation,
failure paths, and unrelated working-tree content.

After implementation, perform an adversarial review focused on ownership
races, unconfirmed RF-off, local precedence, lost replies, stale assignments,
identity and Plain LAN trust, restart, malformed traffic, resource exhaustion,
configuration compatibility, UI prompt bypass, and overclaimed CAPS. Record
each actionable finding, repair it, rerun focused validation, and perform a
second adversarial assessment. Do not mark a slice complete with an unresolved
material finding.

Only after the authorized scope is complete and the final diff has been
reviewed, commit the intended WsprryPi changes on `devel` and push `devel` to
its configured remote. Preserve unrelated user changes and report the commit,
push result, branch parity, exact tests, skipped or blocked qualification,
component paths, UI evidence, Documentation Impact, and remaining work.
