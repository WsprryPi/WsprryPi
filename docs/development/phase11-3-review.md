# Phase 11.3 WsprryPi implementation and review

Status: reviewed local software/integration scope passed, 2026-09-08; remote CI
acceptance is tracked separately. Coordinating
contract and single acceptance checklist are in WsprryPico
`docs/development/phase11-3-identity.md` and `phase11-3-plan.md`. Initial clean
WsprryPi devel input was `07a7a8b`; origin was refreshed before edits. Changes are
limited to this parent repository's settings/TLS/HTTP adapter, UI, tests and docs.
No reusable WTP protocol implementation or other component library changed.

## Reviewed architecture and behavior

The full settings lifecycle was reviewed: defaults, typed settings, INI/JSON
parsing, strict validation, inactive settings, web population/drafts, serialization,
merge patch, credential-rotation admission, reload invalidation and runtime use.
Stored spelling is preserved. Canonicalization applies at connection/TLS/HTTP
use only. DNS names accept ASCII case and a single root dot; malformed authority,
wildcard, ambiguous numeric and DNS-label input is rejected before resolution.

The existing resolver still admits one outstanding system lookup, at most eight
addresses and original cancellation/deadline behavior. Every new connection
resolves afresh. Exact configured DNS SAN/IP SAN verifies TLS; neither CN nor
wildcards authenticate a deployment. HTTP authority follows the successful
configured identity, independently of the numeric connection address.

Application/scheduler ownership, complete jobs, WTP device/boot/session checks,
unknown outcomes, credential snapshots, idle management and backend/proxy
security guards remain authoritative. No discovery-driven trust, automatic
fallback, resolver/service/trust changes or hardware access was added.

## Validation and adversarial record

- Baseline WTP protocol/plan/USB/backend/scheduler/status/application and API
  commands passed. Core counts are retained in `/tmp/phase11-3-pi-core.log`.
- Extended UI unit validation passes; Impeccable mechanical detector reports no
  findings. Rendered desktop 1280x900 and mobile 390x844 workflow passed; inspected
  hostname/expected-identity forms, ready/unknown status, active/conflict/failed
  discovery, revision conflicts, schedule success and preserved masked password
  draft under `src/build/wtp-network/ui`. No overflow or additional visual defect
  was found. Dark-theme/focus capture was not repeated; incumbent rules remain.
- TLS 34-case suite, simulated production 1995/runtime-rotation 2012 checks and
  explicit portable semantics profile passed. Separate ASan+UBSan TLS build also
  passed all 34 cases after final identity repairs. Actual client/Pico interop
  passed normally and under ASan+UBSan against clean Pico
  `d8cde03f8127b3c2aaf727f2c21c20960f658e84`; both harness and CI pin that revision.
  Mbed TLS remains `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.
- Actual second IPv4 loopback bind fails on this Mac with EADDRNOTAVAIL. The
  harness explicitly reports that case skipped without changing interface or
  resolver configuration; supported Linux execution remains required for that
  real socket-address replacement case. Injected resolver changes are distinct.

Review repairs and final dispositions: configuration-only validation previously left direct TLS
callers open to malformed names; validation now precedes resolver admission.
OpenSSL's prior flags permitted CN fallback/full-label wildcards; exact SAN-only
verification now rejects both. The first expanded TLS contract run failed because
Darwin `inet_pton` accepted leading-zero IPv4 text that Linux rejects; comparison
to canonical dotted-decimal and embedded-IPv4 validation repaired this cross-host
identity inconsistency. The original failure is retained in
`/tmp/phase11-3-pi-tls-first-failure.log`; affected 34-case TLS rerun passed.
HTTP previously derived numeric authority from resolution; it now uses the
verified configured reference identity. Independent review confirmed embedded
NUL in IPv6 could be
truncated by C string address APIs, and root-dotted hexadecimal names could
become numeric only after normalization. Early ASCII/control checks and
post-normalization numeric-form rejection close both. UI also explicitly rejects
non-ASCII/control bytes before regex matching, closing JavaScript final-newline
anchor acceptance. C++ and Node repros were rerun independently. The host harness now protects the build root (0700), since
its executable embeds the ephemeral server key, and checks raw-string delimiter
encoding plus credential configure dependencies.

UI used a loose character whitelist and different file-reference limits; validation now
covers the same DNS/IP grammar and preserves all drafts.

### Commands and evidence

All commands run from `src` unless specified; host macOS, no real transmitter.

| Command | Result |
| --- | --- |
| `make wtp-protocol-test wtp-plan-test wtp-usb-test wtp-backend-test wtp-scheduler-test wtp-status-test wtp-application-test SUDO=` | PASS; protocol 10373, vectors 946, Session 2924, plan 1033 + frequency 1937, USB 2644, backend 5472, scheduler 50700, status 20959, application 38891. Synthetic sysfs/PTYs only. |
| `make wtp-production-test wtp-network-runtime-test wtp-api-test privileged-network-policy-test backend-http-guard-test websocket-upgrade-guard-test apache-privileged-network-policy-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` | PASS after repairs; production 1995/runtime-rotation 2012; shared API and all four HTTP/proxy guard tests. Expected Mac device-tree absence and deliberately unresolved-output diagnostics remain visible in log. |
| `make semantics-test-portable SUDO=` | PASS explicit simulated-only subset, including 23 UI publication checks; not the full Linux physical-capability semantics profile. |
| `make wtp-tls-test SUDO=` | PASS 34 actual TLS cases plus canonicalization/config/resolver/credential contracts; first Darwin grammar failure retained then repaired. |
| `make wtp-tls-test SUDO= WTP_NETWORK_BUILD_DIR=build/wtp-network-asan WTP_NETWORK_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' WTP_NETWORK_LDFLAGS='-fsanitize=address,undefined'` | PASS all 34 actual TLS cases and final identity contracts, no sanitizer report. |
| `make wtp-network-interop-test SUDO= PICO_SOURCE=/Users/lbussy/GitHub/WsprryPico MBEDTLS_SOURCE=/Users/lbussy/GitHub/pico-sdk/lib/mbedtls` | PASS real TLS and Pico service with named/injected resolution, explicit IP plus DNS identity, literal IP SAN pass/mismatch, management/revisions, finite job, failed DNS/bad-cert reconnect without additional WTP bytes, same-request lost LOAD/ARM/ABORT recovery, foreign ownership and HELLO boot/device mismatch. Second IPv4 socket case explicitly skipped on Mac. |
| Same interop Make target with the sanitizer directory/flags above, after configuring the CMake server with matching C/C++/link sanitizer flags | PASS same workflow under ASan+UBSan, no sanitizer report. Same explicit second-IPv4 skip. |
| `node tests/wtp_ui_test.js` from UI | PASS final DNS/IP/control/newline validation and inactive/draft behavior. |
| `node tests/wtp_network_ui_integration_test.js` from UI | PASS rendered mocked-response desktop/mobile workflow and discovery states. |
| Impeccable detector, changed presentation files; `git diff --check` | PASS; zero detector findings, no whitespace errors. |

Logs are retained at `/tmp/phase11-3-pi-core.log`,
`/tmp/phase11-3-pi-final-contracts.log`, `/tmp/phase11-3-pi-semantics.log`,
`/tmp/phase11-3-pi-tls.log`, `/tmp/phase11-3-pi-tls-asan.log` and
`/tmp/phase11-3-pi-browser.log`, `/tmp/phase11-3-pi-interop.log` and
`/tmp/phase11-3-pi-interop-asan.log`. These are local evidence; remote CI is tracked
separately. The sanitizer run includes the final code that closed the independently
confirmed NUL and numeric-normalization findings. Both protected build roots are
0700 and generated private headers 0600, verified after CMake generation.

A final independent identity/authority reassessment confirmed the repairs and
found no additional actionable defect. The Pico source was separately reviewed
from the host-client viewpoint for manifest, authority and board-binding
agreement; no contract disagreement remained. Live NSS/mDNS, physical operation
and the Mac-unavailable second IPv4 case are not claimed by these passes.

## Linux CI orchestration repair

Initial client implementation `efcc792cb45780c8b87ebfa83838ecb9eb9cdf47`
was committed and pushed to `devel` with exact remote parity. GitHub Actions
[run 34284241406](https://github.com/WsprryPi/WsprryPi/actions/runs/34284241406),
network job `102255976179`, failed at the unchanged 180-second interop deadline.
The Linux log explicitly records address2/address1 server restarts and
“Actual changed IPv4 loopback address with unchanged DNS TLS identity passed”
before the stall. That socket/address-change acceptance passed; the full network
job failed and its later browser step did not run. The failed log is retained at
`/tmp/phase11-3-ci-network.log` (independent copy
`/tmp/phase11-3-ci-network-agent.log`).

The driver mixed OS selector readiness with Python buffered text `readline()`.
An informational line and the next `RESTART boot` flushed together could be
prefetched into that text buffer: the first line was consumed, the second became
invisible to OS readiness, and the client waited for `READY` until the deadline.
The repair uses unbuffered binary pipes, one bounded OS read and explicit line
framing that consumes every complete line while retaining at most a 4096-byte
tail. It drains output through EOF, rejects incomplete/oversized control lines,
and bounds process-exit waiting by the original deadline. Server startup uses the
same framing with its existing 15-second deadline; a partial readiness line
cannot block `readline()`, and startup exceptions terminate the owned child.
No production C++, TLS/identity behavior, source pin or Linux second-address
acceptance is changed.

`make wtp-network-process-test SUDO=` adds five deterministic tests, including an
actual child that atomically writes a notice and `RESTART`, then waits for the
parent's `READY` before continuing. Fragmented/coalesced lines, partial EOF,
oversized lines, successful readiness, stalled partial readiness and closed
stdout while the child stays alive are covered. This target is a prerequisite
of the existing CI interop target. Full normal and ASan+UBSan driver reruns use
the already built, clean-input `d8cde03` Pico and `efcc792` client binaries because
the coordinator's Pico checkout now contains only later test/metadata work.
New CI still rebuilds against the unchanged exact clean Pico source gate.
All five Make-wired subprocess tests and both final driver reruns passed;
ASan+UBSan reported no finding. The local second-IPv4 case remains explicitly
skipped on macOS. Independent review closed the buffering, startup and EOF
findings, reran all five tests and found no remaining actionable issue.
Regression evidence is `/tmp/phase11-3-pi-process-regression.log`.
Repair logs are `/tmp/phase11-3-pi-interop-pipe-repair.log` and
`/tmp/phase11-3-pi-interop-pipe-repair-asan.log`; fresh full CI remains a required
gate after this repair.

## Documentation Impact

Updated: `docs/wtp-network.md`, `docs/wtp-browser-api.md` and this review record.
Reviewed and retained: production integration, status/recovery, privileged-network
safety and simulator guides; their ownership, output and host-security contracts
remain unchanged. PRODUCT.md/DESIGN.md preserve the existing bench-instrument
interface; no design tokens or local skill artifacts changed.

Inspected read-only, not edited: separate `/Users/lbussy/GitHub/Wsprry_Pi_Docs`.
Required follow-up under separate authorization:

- `docs/Advanced_Operations/ini_configuration/transmitter_backends.md`: add all
  network keys, default USB/inactive retention, canonical spelling, DNS/IP SAN,
  per-device CA/client credentials and DHCP/NSS prerequisites.
- `docs/Command_Line_Operations/transmitter_backends.md`: replace USB-only WTP
  description with explicit transport selection and hostname/IP behavior.
- `docs/User_Interface/Setup/Transmitter/index.md`: replace obsolete visibility
  and Use Pico over USB copy; document current console visibility, Connection,
  expected/resolved/authenticated observations and preserved management drafts.
- `docs/User_Interface/Operations/index.md`: DHCP/conflict/reconnect diagnostics,
  explicit recovery and unchanged authoritative output-unknown handling.
- `docs/Advanced_Operations/rest_api.md`: scoped shared resources and outbound
  hostname authority, revisions/intent and distinct host versus Pico security.

## Remaining gates

No live Pico, USB control, flashing, services, router/NSS/trust changes, target
contention/timing or RF was exercised. Phase 11.4–11.7 remain open; loopback TLS,
injected resolution, actual responder packets and physical NSS/mDNS acceptance
must not be conflated. Initial commit/push/parity are verified above; fresh
remote CI after the orchestration repair remains pending.


Primary source review: OpenSSL's documented
[hostname checking flags](https://docs.openssl.org/3.0/man3/X509_check_host/)
confirm no-subject/no-wildcard policy. The systemd
[resolver client guide](https://wiki.freedesktop.org/www/Software/systemd/writing-resolver-clients/)
explains the NSS integration distinction; operational `.local` resolution is
still an explicit host acceptance prerequisite.
