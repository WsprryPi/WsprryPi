# Execute: WTP replies follow the selected LAN interface

## Objective and boundaries

Work on WsprryPi `devel`, starting at `d6402b1`. Close the discovery TODO for
equal-metric Ethernet/WiFi route reordering: WTP requests arriving at the selected
Ethernet endpoint must receive usable replies through Ethernet even when WiFi
is preferred by the host's ordinary routes. Preserve the existing uncommitted
Si5351 acceptance-closure edits and include them in the authorized delivery.
Si5351 hardware nonresponse is outside this task.

Inspect the parent repository, instructions, listener, runtime interface selection,
DNS-SD publication, lifecycle and tests before changes. Keep reusable components
and the UI unchanged. Preserve WTP/1, ownership, local priority, job timing,
frequency policy, Fleet assignments, endpoint identity, ports and INI semantics.
Do not add persistent host route rules, change route metrics as a product fix,
or fall back to an unrestricted socket when interface binding fails.

## Required implementation

1. Pass the selected interface and numeric address together from the managed
   runtime into the Plain LAN listener. On Linux, constrain the listening socket
   to that interface before `bind`/`listen`, so handshake packets and accepted
   connections use the same interface as DNS-SD publication.
2. Validate interface names without truncation or embedded NULs. Require a
   selected interface for non-loopback listeners. Preserve explicitly local,
   hardware-free loopback tests on Linux and macOS. Unsupported physical
   interface binding must fail closed.
3. Surface interface-binding failures through the existing listener error.
   Failure must leave no listener, bound port, socket leak or advertisement.
   Retry must still use the selected interface. Verify accepted sockets retain
   the constraint; do not assume setting it after `accept` fixes SYN replies.
4. Preserve withdrawal/rebind on selected interface/address loss. Recovery
   must not require manually restoring Ethernet route preference. Changing
   ordinary route ordering while the selected interface stays present must
   leave WTP inspection usable without resetting device or ownership state.

## Validation

- Do not compile or link WsprryPi C++ on the local Mac. Use an isolated Linux
  source directory on wspr5 and GitHub CI. Transfer substantial files through
  compressed streams; leave its normal source checkout untouched.
- Extend the existing inhibited-engine listener tests for named-interface
  binding, invalid/missing interface, missing required interface, binding
  failure without fallback, inherited accepted-socket binding and retry.
  Retain HELLO, local-ownership refusal, session replacement, client capacity
  and shutdown/restart coverage. Use typed/link-time test seams, never production
  fault-injection environment variables or configuration controls.
- Add a Linux network-namespace integration case with two equal-metric routes
  and an independent client. Show the address-only baseline misroutes, then
  require HELLO/STATUS through the selected interface under the same route order,
  including an existing connection and a new connection after reordering.
  Keep the test isolated from host networking and use an inhibited RF engine.
- Run focused listener/interface/control/authority tests, application integration
  and the full hardware-disabled Linux semantics profile. Add the namespace
  acceptance to an appropriate Linux CI job with explicit dependencies.
- Live wspr5: verify local/remote work and all five saved assignments are inactive;
  preserve binary, INI, assignments/catalog, NetworkManager profiles, addresses,
  route order, Avahi and timing services. Arm an independent rollback before any
  deployment or link operation. Use WiFi as the management path, reconnect
  Ethernet to produce WiFi-first equal-metric routing, and inspect WTP from an
  independent Pi. Capture packet-interface evidence. Repeat recovery without
  manually preferring Ethernet during each acceptance case.
- Do not transmit, claim ownership, LOAD/ARM jobs or contact either Pico for
  this network-only task. Restore the original network/configuration/Fleet state
  and leave the qualified candidate installed only after successful validation.

## Adversarial review and delivery

Review interface-binding order, SYN/accepted-socket routing, permission/unsupported
failures, name truncation, retry cleanup, missing/wrong interfaces, address changes,
route changes with connected sockets, ambiguous auto selection, DNS-SD consistency,
resource leaks and rollback. Repair every in-scope finding, rerun the affected
checks and perform another adversarial assessment.

Save the implementation report, review, finite live evidence and source hashes.
Update `docs/wtp-pi-operation.md` and the discovery TODO without erasing historical
failure evidence. Inspect sibling operator documentation read-only and report any
separate follow-up; no UI or cross-repository writing is planned. Review the final
diff and staged content, commit this task and the preserved closure edits on
`devel`, push `origin/devel`, verify remote parity and CI results, then report
behavior, exact tests, live validation, documentation impact and remaining scope.
