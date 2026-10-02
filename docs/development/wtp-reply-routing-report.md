# Selected-interface WTP reply routing: execution report

2026-10-02, WsprryPi `devel`, base `d6402b1002a2d22fa7c65a5e8c8c6b1b286e91c6`.

**The equal-metric reply-routing failure is repaired and live acceptance passed.**
The [comprehensive prompt](wtp-reply-routing-prompt.md) was executed, findings
were repaired, and the [final adversarial reassessment](wtp-reply-routing-review.md)
found no unresolved issue in this slice. Si5351 hardware nonresponse remains
outside this testing scope; its management-stall acceptance is closed.

## Implemented behavior

- Managed runtime supplies the selected interface and numeric address together.
  Linux sets `SO_BINDTODEVICE` before `bind`/`listen`, constraining handshake and
  accepted-socket replies to the same interface used for DNS-SD publication.
- Non-loopback startup requires an interface. Missing, oversized or NUL-containing
  names fail; permission/unsupported binding failure leaves no unrestricted
  listener, socket or bound port. The existing error/retry path remains.
- Interface kernel index is part of station identity, so recreation with the
  same name/address withdraws and replaces the old socket. Address/link loss
  retains the existing withdrawal/recovery behavior.
- Product behavior installs no routing rules and changes no route metrics.
  WTP/1, local priority, ownership, job execution, configuration and Fleet
  assignment semantics are unchanged. No reusable component or UI source changed.

Parent implementation paths: `src/wtp_endpoint/plain_listener.*`,
`interface_selection.*`, and `runtime.cpp`. Tests, Make targets and Linux CI
cover the repair. Existing Si5351 closure-document edits were preserved for
this authorized commit.

## Automated validation

All C++ work used `/home/pi/wtp-reply-routing-20261002/source` on wspr5;
the normal Pi checkout was untouched. Substantial transfers used compressed
tar streams. Local Mac C++ compilation/linking was not attempted.

| Command from isolated `src` | Result |
| --- | --- |
| `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 wtp-pi-listener-test wtp-pi-interface-selection-test wtp-pi-routing-probe SUDO=` | Passed. Named binding, accepted-socket inheritance, invalid/missing names, injected failures, closed descriptor, retry, local claim refusal, session replacement and bounded clients. |
| `sudo -n env WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 tests/wtp_pi_reply_routing_test.py --probe build/wtp_pi_routing_probe` | Passed before and after final repair. Three private namespaces reproduce WiFi misrouting, then require Ethernet replies with both existing and new connections after equal-metric reordering. |
| `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 semantics-test release wtp-pi-authority-test wtp-pi-control-test wtp-pi-config-transaction-test SUDO=` | Passed. Default Linux profile with libgpiod headers; the authority target also runs the existing WTP-Server standalone suite. |
| Final source: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 JOBS=2 wtp-pi-interface-selection-test wtp-pi-listener-test semantics-test wtp-production-test release SUDO=` | Passed after kernel-index tracking. Full semantics and parent WTP integration; expected injected-failure diagnostics remain in logs. |
| Python harness syntax and `git diff --check` | Passed. |
| Local `python3 docs/development/wtp-reply-routing-results/rollback_test.py` | Passed. Injected unavailable listener cannot prevent atomic original-binary restoration; former inode and network-only retry verified without services or hardware. |

The first namespace attempt used `route add` for duplicate equal-metric routes;
Linux rejected the second route. The harness now uses `append` and retains
diagnostics. Its failure cleanup removed every namespace and fixture process.
The baseline binding bypass exists only in the link-time test fixture.
Final review also repaired rollback ordering and atomic replacement in the live
harness; its failure-injected regression runs entirely in a temporary directory.
The root-owned Pi harness was updated without changing the qualified application.

Linux CI now runs the namespace acceptance on the host network-validation job,
with explicit `iproute2`/`tcpdump` dependencies. macOS retains portable loopback
coverage. Final pushed CI status and commit identity are reported at completion.

## Live wspr5 acceptance

Independent client: wspr4 `192.168.1.120`. Target: wspr5 Ethernet
`192.168.1.54`, WiFi management `192.168.1.117`, WTP port `31417`.
Only HELLO/STATUS inspection was used; neither Pico was contacted. Local Enable
and all five saved assignments were off throughout. No CLAIM/LOAD/ARM or RF.

1. Root-only original binary/configuration backup and an independent rollback
   timer protected the operation. The SDR API service was not operated on.
2. Original listener: reconnecting Ethernet produced WiFi-first routes at metric
   100. HELLO timed out after **3.35 seconds**; capture shows Ethernet requests
   with WiFi SYN-ACK/payload replies. This reproduced the recorded failure.
3. Qualified candidate: `ss` reports `192.168.1.54%eth0:31417`. Three independent
   HELLO/STATUS connections passed immediately with WiFi still preferred.
4. Two further Ethernet reconnect cycles withdrew and recovered listener/DNS-SD.
   WiFi remained preferred at equal metric; three inspections passed after each
   recovery. **9/9 total**, maximum inspection time **0.368 seconds**. Every
   captured reply used Ethernet. No manual route-preference repair occurred
   during these acceptance cases.
5. Device ID remained `e045017b11e54c2fb7029f0154e6deb9`. Expected deployment
   restart changed boot ID once; both subsequent reconnects retained the new
   boot ID `f92ead4639f0e969f37ad4e66addd6f5` and service PID.

Restoration verified exact original route order, address/prefix identity, INI,
Fleet assignment/catalog files, NetworkManager profiles and Avahi configuration
hashes. Assignment definitions/watermarks/state remain identical and paused.
Chrony stays active; `/dev/pps-gps` remains `/dev/pps0`. Endpoint is empty,
unowned and known off. Rollback and capture units are inactive; no test
namespace or fixture remains. The qualified candidate remains installed.
Private backups stay root-only on wspr5 and were not transferred or committed.

Candidate binary SHA-256:
`f2156011ecaaadf0708470a51f5dbb760c93a6cdb28f0468bbcaf552e78ffe38`.
Build metadata identifies the base plus dirty candidate; exact source hashes
accompany the evidence. It is not represented as an already committed binary.

See [public evidence](wtp-reply-routing-results/), especially
`final-namespace-validation.json`, live packet/peer receipts, `restoration.json`,
`source-manifest.json`, and final Linux validation log.

## Documentation Impact

- Updated: `docs/wtp-pi-operation.md`, discovery TODO/disposition, this executed
  prompt/report/review, and the preserved Si5351 closure records.
- Considered unchanged: sibling operator `docs/Advanced_Operations/ini_configuration/runtime.md`
  and `rest_api.md`, plus Fleet/backend documentation. Existing explicit interface
  selection, listener-error reporting and recovery contracts remain applicable;
  there is no new operator setting or persistent routing requirement.
- No separately required operator-documentation change for this implementation.
  No cross-repository or UI changes were made. Impeccable rendering was not
  applicable to this parent networking change.
