# Phase 11.1 review and software acceptance

Date: 2026-09-08. Scope: WsprryPi host network integration only. Baseline clean
`devel` at `dff597467f8ec8aebaf30e4361c985c073953eab`, equal to refreshed origin.
Execution plan: [phase11-1-plan.md](phase11-1-plan.md), written before implementation.

## Phase 11.7 current-source addendum

Updated 2026-09-20. The original 11.1 implementation and validation narrative
below remains historical. The final joint review tested Pico
`95aac22bf7cc31b67fe5773761708d743d8a6473` with Pi
`2298a268d8139409ed650d2311d794b3813d99ab`. Pi's latter commit changes only
the actual-Pico interoperability harness and its immutable Pico pin relative to
the Phase 11.6 companion source `c39fae35...`; production WTP client,
backend/scheduler/status/application and transmitter-plan sources are unchanged.

Fresh hardware-free validation exercised the actual current Pico TLS server and
Pi client/application paths, including DNS and explicit-IP identities, mTLS,
credential and connection failure recovery, shared management, finite jobs,
partial I/O, same-request replay after lost LOAD/ARM/ABORT acknowledgements,
foreign ownership, boot/device changes and same-session recovery. The host could
not bind a second loopback IPv4 address; the injected-address contract passed.
This does not qualify a physical network or RF path.

The authoritative closure is WsprryPico
[`docs/development/phase11-7-review.md`](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/phase11-7-review.md).
It closes Phase 11 within its documented software, bounded physical and scoped
conducted-RF acceptance. Exactly 13 Phase 11.6 rows are retained as accepted;
no Phase 11.6 WSPR row is accepted. Phase 12 provisioning and Phase 13 broad
hardware/release qualification remain open. The original "Remaining roadmap and
qualification" section below is a contemporaneous 11.1 checkpoint and is
superseded for current Phase 11 status by this addendum.

Pico reference remained read-only at
`0fd8191c5218d3b5f2da9122a2ae55bf728ae3f2`; Mbed TLS at
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The portable WTP-Client provenance
remains `40812e7438f180c5e8d8ad75d4eb227271152b10`. Its only component change is
SESSION.md progress/boundary documentation. WSPR-Transmitter and other reusable
component sources were not changed. Parent integration, configuration, HTTP
policy/routes, first-party UI, test harness and developer docs changed.

## Implemented boundary

- Explicit default-USB/network selection through JSON, INI, runtime and gated UI;
  inactive values persist and credentials are validated before active selection.
- OpenSSL TLS 1.3/mTLS, configured DNS/IP verification, mandatory ALPN, immutable
  credentials, fresh bounded resolution and nonblocking connection/I/O.
- Existing complete-job application/scheduler/backend/ownership/reconciliation
  across TLS. No automatic backend fallback, raw browser WTP submission, second
  scheduler, adopted foreign work or relaxed output-unknown handling.
- Shared API status/capabilities/job cancellation, serialized idle remote
  config/schedules/network management, separate host-config scope, revision CAS,
  direct/proxy protection, and compatible legacy routes.
- Actual unmodified Pico TLS server/core under WsprryPi-owned loopback adapters.
  The fixture supplies valid identity and inhibited software execution; it does
  not change Pico's protocol or server constraints.

## Adversarial review and closure

| Finding or threat examined | Repair or disposition and evidence |
| --- | --- |
| OpenSSL WANT could otherwise be mislabeled as definitely-unsent input | Own bounded plaintext queue; Progress on copy, stable SSL retry bytes. Actual fragmented application I/O and sustained socket backpressure pass, retaining ambiguity on write deadline. |
| DNS blocking could stall the owner/shutdown or create unbounded abandoned workers | Outside-worker system resolver, one outstanding lookup, caller deadlines/cancellation, fresh result per open. Deterministic failure/address-change/cancel tests pass. NSS thread itself cannot be killed; documented bounded-slot limitation. |
| Address/name confusion or downgrade | Separate configured connection and authenticated identity; exact TLS1.3 and ALPN, CA verification, hostname/IP SAN fixtures. Wrong names, invalid chains/client material, TLS1.2 and wrong ALPN fail closed. |
| Same-path credential replacement could silently change principals | Snapshot content fingerprint checked on apply/reconnect/management. Filesystem replacement tests pass; injected production runtime admits identical credentials, rejects rotation during prepared work and admits replacement after cleanup. No material serialized. |
| Lost LOAD/ARM/ABORT might duplicate jobs or clear safety | Actual Pico TLS loss tests retain blocked history and require same-session cleanup. Same-request LOAD replay, boot/device change rejection and foreign ownership pass against actual server/core. |
| Pico host TCP fixture left ECONNRESET connections allocated | Initial actual LOAD-recovery runs failed. WsprryPi's adapted POSIX/lwIP host glue now frees fatal sockets before its error callback. Repaired actual-server loss/reconnect suite passes; no Pico source or threshold changed. |
| UI absent snapshot enabled remote management | Require selected, ready, idle, unowned snapshot; runtime independently rechecks fresh authoritative idle state. Failed polling disables actions and shows Unknown. Browser checks pass. |
| TLS cleanup erased useful failure diagnostic | Close preserves failed state/diagnostic. Contract test verifies it; UI labels identity as last authenticated and gives observation age. |
| Schedule-only save discarded unrelated password draft | Clear password only after full-config success; schedule-specific feedback explicitly preserves other drafts. Browser assertion and masked desktop/mobile captures pass. |
| Backend hint still used USB-only switch label | Hint now matches Use Pico. Desktop/mobile review passes. |
| Host config snapshot/revision could race updates | Shared recursive update lock covers revision snapshot, web patch and global JSON/INI translation/commit writers. Concurrent same-revision patches admit exactly one and reject the other; actual HTTP stale write returns 412. |
| Other UI config saves did not refresh returned revision | Transmit, disabled-mode and autosave success paths all update ETag. Portable UI/source regression suite passes. |
| Empty If-Match could evade intended conditional branch | Shared host PUT and explicitly conditional legacy writes reject empty revision with 428. Actual host route missing/stale/context tests pass. Legacy clients omitting the header retain compatibility. |
| Browser owner labels/replay could bypass real runtime authority | Browser IDs only scope a bounded process-local replay ledger. Runtime checks exact tracked host job under operation lock. Reserve unknown before callback; never evict history to allow duplicate execution. API replay/mismatched-ID tests and actual foreign-owner tests pass. |
| Competing HTTP connection or implicit RELEASE could disrupt a job | One runtime operation lock plus fresh unowned/inactive WTP inspection, deliberate idle close without RELEASE, one HTTP exchange and read-only reconnect. Management during work rejected in actual interop; periodic browser reads copy observations only. |
| New paths might bypass backend or Apache trust | Explicit route classification, existing guard and proxy identity policy, mandatory mutation intent, bounded unique-key JSON, no CORS grant. Direct/proxy/off-LAN/forged-origin tests pass; installed proxy syntax inspected only. |

A second assessment checked repaired paths and reran their applicable tests.
No actionable finding remains open within this host software scope. Resource
bounds, process-local history, NSS behavior and present Pico limitations remain
explicit constraints, not physical qualification claims.

## Validation results

All commands below ran from `src` unless stated. Local host: macOS; no real
transmitter connection, service operation, installation or system trust mutation.
Some socket/PTY/browser commands ran outside the filesystem sandbox solely to
permit loopback listeners and local test subprocesses.

| Command | Result and evidence scope |
| --- | --- |
| `make wtp-protocol-test wtp-plan-test wtp-usb-test wtp-backend-test wtp-scheduler-test wtp-status-test wtp-application-test SUDO=` | PASS: protocol 10373; vectors 946 plus framing/pinned snapshots; Session 2924; plan 1033 plus 1937 frequency vectors; USB 2644; backend 5472; scheduler 50700; status 20959; application 38891 checks. Scripted peers, synthetic sysfs/PTYS, real portable software. |
| `make wtp-production-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` | PASS, final core run 1995 checks plus WTP UI suite. Real parent config/INI/runtime and localhost registered HTTP handlers; runtime application suppressed/injected. |
| `make wtp-network-runtime-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` | PASS, 2012 checks including same-path credential rotation during prepared work and after cleanup; requires fixtures generated by TLS suite. |
| `make semantics-test-portable SUDO=` | PASS, explicit simulated-only subset; excludes physical-capability runtime-semantics and cleanup-lifecycle executables. Includes existing configuration, UI/source and 23 publication checks. |
| `make wtp-api-test SUDO=` | PASS: resource shapes, decimal-string precision, revisions, duplicate/oversized JSON, cancellation replay and route classification; legacy UI tests. |
| `make wtp-tls-test SUDO=` | PASS: 29 actual TLS certificate/identity/version/ALPN/fragmentation/stall/EOF/backpressure cases plus injected resolver, cancellation, guard, credential and settings contract checks. |
| `make wtp-network-interop-test SUDO= PICO_SOURCE=/Users/lbussy/GitHub/WsprryPico MBEDTLS_SOURCE=/Users/lbussy/GitHub/pico-sdk/lib/mbedtls` | PASS: actual Pico server shared management/revisions/redaction, finite application job, lost LOAD/ARM/ABORT, exact-request replay, reconnect, foreign ownership, boot/device changes and unresolved cleanup. |
| `make backend-http-guard-test privileged-network-policy-test apache-privileged-network-policy-test SUDO=` | PASS: direct/backend and proxy policy, including off-LAN, forged peer and invalid Origin cases. Source/policy evidence only. |
| `node ../WsprryPi-UI/tests/wtp_network_ui_integration_test.js` | PASS: rendered desktop 1280×900/mobile 390×844 selection, inactive fields, draft preservation, shared cancellation, schedule-only preservation, 412 feedback, failed status and disabled recovery/management. No horizontal page overflow. |
| `node --check ../WsprryPi-UI/data/wtp-management.js`; `php -l ../WsprryPi-UI/data/views/wtp-controls.php` | PASS. |
| `git diff --check`; `git diff --cached --check` | PASS at final review/staging. |

Sanitizer run used its own `build/wtp-network-sanitize` directory. CMake first
received `-fsanitize=address,undefined -fno-omit-frame-pointer` in both C and C++
flags for Mbed TLS/Pico server. Then the TLS/API/interop targets ran with:

```sh
make wtp-tls-test wtp-network-interop-test SUDO= \
  WTP_NETWORK_BUILD_DIR=build/wtp-network-sanitize \
  WTP_NETWORK_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_NETWORK_LDFLAGS=-fsanitize=address,undefined \
  PICO_SOURCE=/Users/lbussy/GitHub/WsprryPico \
  MBEDTLS_SOURCE=/Users/lbussy/GitHub/pico-sdk/lib/mbedtls
```

PASS for the actual server/core, host adapter, portable library and exercised
application paths. No sanitizer findings. TLS tests regenerated ephemeral
credentials; generated files/captures are ignored and excluded from commits.

Retained development failures: the original host glue lost-reply reconnect
failure described above; an initial backpressure fixture deadline expired before
sustained blocking because of OS buffer growth (bounded receive buffer fixed the
fixture); the initial HTTP fixture serialized an unavailable GPIO backend under
the portable profile (fixed with explicit valid WTP settings); initial screenshot
crops omitted required fields. Those failed runs were not acceptance passes.
Expected macOS `/proc/device-tree`/`/sys/firmware` diagnostics and deliberate lost
ARM cleanup error messages remain in parent test output; they are not hardware
results. The full Linux hardware-disabled semantics profile, installed Apache,
real NSS/mDNS, real credentials/devices, services and RF were not run. Added CI
coverage is checked in; local checks do not establish a remote CI pass.

## Impeccable visual review

Applied `/Users/lbussy/.agents/skills/impeccable/SKILL.md`, existing PRODUCT.md and
DESIGN.md, Operate/new-work guidance and craft floor. The mechanical detector
ran once over affected control/management sources and returned no findings.
Separate finish review confirmed existing typography, flat interior forms,
responsive stacking, explicit identity fields, truthful unavailable status and
nearby conflict feedback. It found and verified the two UI repairs above.

Reviewed ignored evidence under `src/build/wtp-network/ui`: desktop/mobile full
pages, transport/hostname/status/management crops, conflict feedback, failed
mobile status and schedule-success/password crops. The last captures show the
masked nonempty password draft retained after schedule-only success. Dark-theme
and keyboard-focus capture sets were not produced; existing native control and
theme rules were retained. The documenter follow-up found PRODUCT.md/DESIGN.md
still accurate, with no new durable tokens/principles to add. No skill artifacts
or screenshots are committed.

## Documentation Impact

Updated: execution plan and this record; network and shared-API guides; production,
backend, scheduling and status/recovery progress statements; WTP-Client SESSION.md;
README links. USB transport contract and privileged-network policy guide were
reviewed and retained; their core identity/trust rules did not change.

The separate `/Users/lbussy/GitHub/Wsprry_Pi_Docs` repository was inspected read-only
and remains unchanged. Required follow-up under separate authorization:

- `docs/Command_Line_Operations/transmitter_backends.md`: explicit USB/network
  selection, prerequisites and unchanged host scheduling/stop authority.
- `docs/Advanced_Operations/ini_configuration/transmitter_backends.md`: all new
  exact keys, default USB, inactive-value preservation, protected credential
  files, rotation and DHCP/mDNS prerequisites.
- `docs/User_Interface/Setup/Transmitter/index.md`: gated Use Pico/Connection
  workflow, configured versus authenticated observations and standalone drafts.
- `docs/User_Interface/Operations/index.md`: current-job cancellation, historical
  status, failed reads, output-unknown and explicit reconciliation.
- `docs/Advanced_Operations/rest_api.md`: scoped shared resources, host versus
  remote config, If-Match, intent/guard policy, replay and idle-management limits.

## Remaining roadmap and qualification

11.1 host software is implemented and locally validated within these boundaries.
11.2 physical-job concurrent browser servicing, 11.3 Pico mDNS/hostname
certificates and joint DHCP/name acceptance, 11.4 inhibited physical acceptance,
11.5 target resource/contention testing, 11.6 conducted RF and 11.7 final joint
review remain. None is satisfied by software-loopback or sanitizer results.
Commit/push identity and verified remote parity are reported with the completed
change; no claim that all of Phase 11 is complete is made.

## CI access follow-up

The first pushed implementation, `690a0692cd6cd62c99b89a0f1983d73c01624b47`,
passed four jobs in [run 34260402019](https://github.com/WsprryPi/WsprryPi/actions/runs/34260402019),
including Linux semantics, macOS validation and the GCC 13 release build.
The network job failed before tests because its default repository-scoped token
could not check out the private WsprryPico repository. This was a CI wiring gap;
the network and browser steps were skipped, not passed.

The administrator supplied `WSPRRY_PICO_READ_TOKEN` as a WsprryPi Actions secret.
The authenticated checkout in [run 34267169627](https://github.com/WsprryPi/WsprryPi/actions/runs/34267169627)
still received HTTP 403 before network tests. The administrator then made
WsprryPico public. The pinned checkout no longer uses the custom token and keeps
credential persistence disabled. No token value entered the repository. A
successful subsequent network CI run is required to close this follow-up.

The public checkout succeeded in [run 34267548736](https://github.com/WsprryPi/WsprryPi/actions/runs/34267548736).
Its Linux TLS suite (29 cases), 2012-check production/credential-rotation test,
shared API tests and actual Pico TLS interoperability all passed. The browser
step then failed before running because the runner's Node.js could not resolve
the distro-installed `ws` module. The job now uses the UI component's existing
lockfile through `npm ci --ignore-scripts`, matching the established UI CI job.
This dependency repair requires a fresh browser CI pass; the earlier failed
step is retained as a failure, not visual acceptance.

The follow-up is closed by [run 34268677261](https://github.com/WsprryPi/WsprryPi/actions/runs/34268677261)
at `d333c69edc8a65bf59ae52603110996921f82ce1`: all five jobs passed, including
`wtp-network-loopback`, `macos-simulated-profile`, `strict-i2c-profile`,
`non-hardware-validation` and `ubuntu-gcc13-release`. The network job passed its
29 TLS cases, 2012 production/runtime checks, actual Pico interoperability and
rendered desktop/mobile browser workflow. The complete Linux semantics and
GCC 13 warnings-as-errors release build also passed. No physical acceptance is
implied. The custom access-token secret is no longer referenced by this workflow.
