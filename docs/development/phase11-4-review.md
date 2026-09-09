# Phase 11.4 inhibited device acceptance: host record

Status: **OPEN**. The coordinating Pico repository owns the sole case matrix in
`docs/development/phase11-4-plan.md` and consolidated `phase11-4-review.md`.
This record separates actual host software checks, physical preparation and
completed production cases and remaining physical gates. One Pico is available;
two-board acceptance is open.

## Single-board execution continuation

Starting published Pi head was 6ca7d82319ccf3c5776a45975734992299985757;
reviewed runtime and isolated executable remain the exact identities above.
The user authorized all tests, then specifically authorized a bounded pause of
`wsprrypi.service` on wspr5 after automatic approval review rejected service
interruption under the broader grant. The rejection was resolved by that grant.
No permission blocker is asserted for the executed production tests.

The isolated GPIO-free executable SHA-256 remains
`3acd44dd6a8933cc816604a4514d8517e7586a2da40bff628378e080af5c6857`.
Actual production hostname startup, explicit IP plus expected DNS startup and
management GET passed: resolved .47, authenticated wsprrypico-0a60df.local,
full WTP device fd6127d11d6aca42a9905fa3fb1bf1d5, boot
88091dc74738f0070d3d7fe44744a3d4 and inhibited engine. The exact private INI
kept GPIO/LED/amplifier/selectors/fades disabled and used HTTP 31425 with IPv4
loopback WebSocket 31426. Singleton 1234 required the approved service pause.
The installed checkout, executable and INI were not replaced.

One actual finite production QRSS E job, three-second dots at nominal 3,570,100 Hz,
completed as `ea375a1be909a78d0000000000000001`, scheduled UTC ns
1788920524000000000. The reviewed per-run `--allow-unqualified-frequency` option
permitted this inhibited nominal-frequency test; it changed no qualification
policy or installed setting. The ten-minute repeat was bounded by terminating
the candidate before the next slot. Retained `host.last_report.job` proves matching
authoritative completion, inactive device output and successful cleanup. Independent
USB STATUS confirms the terminal record and empty/unowned/inactive state.

Retain all wrapper failures: key-case exception before launch (attempt 1), wrong
network envelope assumption after successful hostname startup (2), expected
unqualified-band rejection before job preparation (3), and a null preparing-job
observer exception causing graceful cancellation of a loaded job (4). Attempt 5's
runtime job completed, but its observer expected the already-cleared current job
instead of `host.last_report.job`; it exited 1. Offline assessment of retained
report plus USB, not another physical run, established the completed outcome.
The observer fixes retain unknown snapshots and do not fabricate device inactivity.

The service was restored after each attempt. Final root-only provider query reports
runtime profile, gpio20 and outputEnabled:false. Its rendered HTTP safety fields
had been Unknown; the authoritative Unix-socket query requires write-side EOF and
supplied the actual admission/cleanup evidence. Final installed binary hash remains
`0c8d2a578a766d2b8292467a0c41babd39ae27196a707d8cc701565815f52bdf`;
INI hash remains `795ce11848a20e03bb82c373e93f068d65bd0630d39761ac96ca2f832a724ebd`.
Service is active, no candidate/capture process remains, and PID preservation is
not claimed after approved pauses. Saved Transmit=false and boot Never persist.

Pico-owned direct tests separately passed six inhibited lifecycle/cancellation/
clock-budget cases, stale-revision Chrome drafts and restoration, server renewal,
wrong-board fail-closed deployment, literal-IP TLS/HTTP clients and changed-value
persistence. Original inhibited image/settings/trust were restored. These are not
production-host fault, principal-rotation or Chrome job-submission results.

Further production restart/fault packets were prepared but not invoked after
baseline reachability failed. Mac TCP timeout and Linux No route to host occurred
before CLAIM in independent fault-driver attempts. Linux captures retain delayed
successful ARP/TLS earlier and unanswered ARP later; NSS was intermittent and failed
after a Console off/on cycle. No TTL-zero goodbye was captured. Missing/untrusted/
expired TLS clients closed before HTTP but produced unexpected record-layer errors;
those remain unresolved. Router administration details, native Chrome access after
relocking and a second Pico also remain missing prerequisites. Broad test authority
and shared Wi-Fi/DHCP do not resolve those physical gaps.

The sole current matrix remains in WsprryPico. Its new
`docs/development/phase11-4-single-board-prompt.md`,
`docs/development/phase11-4-single-board-run.md` and hashed evidence index distinguish
passes, failed attempts, prepared packets and open gates. Raw logs/captures stay in
the coordinating ignored `build/phase11-4-single-board-evidence/` tree.

Repeated adversarial review fixed the observer/evidence distinctions above,
verified source/executable binding and final provider state, and retained all open
physical failures. No Pi runtime/component/CI pin/UI source changed; this update
is documentation only. Pico's four affected deterministic CTest suites passed;
prior Pi test results above are not represented as new tests. No remote CI,
RF, timing, resource or release qualification is claimed. Operator-documentation
follow-ups remain the listed read-only paths; no third repository was edited.

## Historical baseline and preparation

The remaining sections preserve the earlier baseline period. Statements there
that no service action or production invocation occurred do not describe the
subsequently approved continuation above.

## Source and tests

Initial local clean devel and independently read origin/devel:
`2e47641f6ebdff104e32999f5194f2e0dc408e06`. Reviewed runtime remains
`efcc792cb45780c8b87ebfa83838ecb9eb9cdf47`; actual diffs show subsequent test/support
changes. Companion initial head is Pico a9662c3; its runtime remains d8cde03.
No Pi runtime, WTP-Client component, CI pin or UI source changed in this attempt.

Read the repository instructions, 11.1/11.3 reviews, network/browser/production,
configuration and recovery contracts. Inspected parent TLS/resolver/HTTP adapters,
production startup and clock policy, build/test recipes and clean-source gates.

Private raw logs are under the coordinating checkout's ignored
`build/phase11-4-evidence/`. Executed from Pi `src` with existing local tools:

- Protocol, plan, USB, backend, scheduler, status and application focused Make
  targets: PASS (`pi-core.log`), synthetic peers only.
- TLS, production, network runtime, API and network process targets, simulated
  profile with `ANCILLARY_GPIO=0 SUDO=`: PASS (`pi-network.log`); 34 TLS cases,
  1995 production and 2012 credential-rotation checks.
- `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test-portable SUDO=`:
  PASS (`pi-semantics.log`), explicitly portable subset including 23 publication
  tests. Not physical backend coverage.
- Pico-owned clean-pinned build of this exact Pi source against its server/core:
  actual loopback interoperability PASS. First restricted CTest run could not
  start two TLS listeners; permitted rerun passed both. Original failures remain.
  Mac's unavailable alternate loopback address is a retained limitation.

No remote CI result, GPIO/RF operation, target timing or resource qualification
is claimed. Reusable component provenance and companion-source gates remain
intact. Documentation-only Pi changes do not alter the reviewed executable.

## Physical host preparation and isolation

Linux client: wspr5, Debian 13.6 aarch64, kernel 6.18.34+rpt-rpi-2712, wlan1,
observed 192.168.1.117/24. Its real NSS configuration uses
`files mdns4_minimal [NOTFOUND=return] dns`. `getent ahostsv4` resolved the baseline
Pico certified hostname to 192.168.1.47. This proves the NSS utility path, not
production WsprryPi getaddrinfo/TLS/WTP operation. Optional resolver tools were
absent and are not reported as passes.

The independently installed service remains active and unchanged:
`/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini`, executable SHA-256
`0c8d2a578a766d2b8292467a0c41babd39ae27196a707d8cc701565815f52bdf`.
The separate `/home/pi/WsprryPi` checkout remains at
`f210e4d165e0e30f94ee9332e70af45e0367e4cb`. Neither is substituted for the exact
reviewed candidate. No service installation, replacement or restart occurred.

The user explicitly approved isolated source/controller staging and build under
`/home/pi/phase11-4-acceptance`. The first source archive build failed because
release metadata requires Git. A full Git bundle then provided a clean 2e47641
clone in `source-git`. From its `src` directory,
`make release BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` completed successfully.
Original failure: remote `host-build.log`; success: `host-build-git.log`.
The generated name follows the bundle remote's basename:
`source-git/src/build/bin/pi-reviewed-source`, SHA-256
`3acd44dd6a8933cc816604a4514d8517e7586a2da40bff628378e080af5c6857`.
It is an aarch64 executable; it has not been invoked against the device.

A separate owner-only controller identity is staged under `controller/`.
`controller-idle.ini` selects network WTP, full expected device ID
fd6127d11d6aca42a9905fa3fb1bf1d5, port 18443 and private controller paths.
Transmit/enable-on-boot/LED/amplifier/shutdown are disabled, selector entries empty,
PPM zero, ancillary GPIO excluded at build, and web/socket ports are isolated
31425/31426. Review the complete current config and binary hash before requesting
production invocation. Hardware-free networking guards are not bypassed in tests.

## Browser and shorter hostname

The user separately approved the Pico CA and browser identity in the Mac login
keychain, then explicitly approved SSL trust for that same per-device CA after
hostname-scoped trust failed in Chrome. A Pico-owned certificate export repair
adds opt-in macOS PKCS#12 wrapping; TLS/mTLS policy is unchanged. Actual Chrome
loaded the direct Pico page and reported valid/trusted TLS 1.3. This is not
acceptance of Pi's shared browser API/UI or concurrent persistent WTP operation.

The user requested a short MAC-suffix alias. The existing Pico `--hostname`
mechanism supports `wsprrypico-0a60df.local`; a new same-CA server certificate and
inhibited image were prepared and separately approved. See the live migration
result in the joint record. Existing long-name observations do not transfer to
that new image automatically. Update the isolated controller target before a
separately approved invocation. The full WTP device ID remains unchanged.

## Latest executed results and production prerequisite

The separately approved short-name image was flashed and verified on the same
board. One approved Console WIFI OFF/ON followed initial peer reachability
failures; the attempted Mac packet capture could not start because sudo required
administrator authentication. No capture or delivered goodbye is claimed.
Mac mDNS and Chrome subsequently succeeded at
`https://wsprrypico-0a60df.local:18443/`. Chrome's actual certificate viewer and
origin details verified the short SAN, server SHA-256
06496fe4d7a1ab45791d85cb0797fa55f76b8dc7ee931f9c7fa70823fef46016,
expected device CA, 2026-09-08 23:34:01 UTC to 2027-09-08 23:34:01 UTC validity,
and TLS 1.3. Existing browser identity remained usable. This is direct Pico UI
acceptance, not the unexecuted shared Pi path.

Linux NSS succeeded immediately after the Wi-Fi cycle, then returned exit 2
again in `final-linux-nss.*`. The user reported that the name resolves fine;
that report is retained without inferring which client was used. One prompted
wspr5 recheck also returned exit 2 (`user-resolution-recheck.*`). These observations
are compatible with the successful Mac/Chrome result. The isolated Linux path
remains an open failure for this run; no universal current outage is claimed.

The short-name idle configuration is staged separately at
`/home/pi/phase11-4-acceptance/controller-idle-mac.ini`, SHA-256
b425992d6d9d6730eef270dcd8989894c7d25436d1c2b80eeaff81a1e6824ed6.
Source review of `src/main.cpp` established an unconditional singleton port 1234
before ordinary startup. Isolated HTTP/socket ports therefore do not permit
another production instance alongside the installed service. B3 needs an
appropriate separate Linux client or a concrete, separately approved service
pause/restart after inspecting its own authoritative output and restart behavior.
No singleton bypass, existing-service API operation, pause or restart occurred.

Final inventory retained installed PID 239923, active state, original executable
hash and `/home/pi/WsprryPi` revision. Final Pico USB snapshot at 23:55:43 UTC
was inactive/unowned, empty, schedule-disabled with unchanged watermark and
short name active. The later direct-IP/DNS-identity HTTPS read passed; it does
not replace failing Linux NSS or production application acceptance.

## Repeated evidence review and remaining gates

Review kept NSS utility success separate from production resolution and verified
source/executable identities instead of using the installed binary. Restricted SSH
failures and archive build failure remain recorded; retries do not erase them.
No failed connection or browser error is treated as inactive-output evidence.
There is no established Pi runtime defect from the executed cases.

Still required: authorized production invocation, live TLS/WTP/HTTP identity
checks, persistent WTP plus browser observations, finite jobs/cancellation,
response-loss and process/boot recovery, shared revision/draft/secret handling,
real DHCP/link changes, conflict, certificate lifecycle/rejections, persistence
and final cleanup. Each is tracked in the joint matrix; host tests do not close it.
Phases 11.5, 11.6 and 11.7 stay open; Phase 12/13 are outside this task.

## Documentation Impact

Updated this review and `docs/wtp-network.md` with the supported short alias,
renewal requirement and unchanged full WTP identity. Considered unchanged:
production integration, browser API, scheduling/status recovery and historical
11.1/11.3 evidence; no host runtime or UI contract changed.

Wsprry_Pi_Docs was inspected read-only. Required operator follow-up (no edits):

- `docs/Advanced_Operations/ini_configuration/transmitter_backends.md`
- `docs/Command_Line_Operations/transmitter_backends.md`
- `docs/User_Interface/Setup/Transmitter/index.md`
- `docs/User_Interface/Operations/index.md`
- `docs/Advanced_Operations/rest_api.md`
- `docs/User_Interface/Maintenance/network_safety.md`

These need exact certified alias/identity, NSS/trust, finite-job/recovery and
remaining physical limitations. Publication/parity is reported after checks.
