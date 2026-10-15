<!-- SPDX-License-Identifier: MIT -->

# Item 16 candidate acceptance record

Status: **PARTIAL — renewed target acceptance remains required.** No provider
freeze, publication, WsprryPi merge or release is authorized by this record.

## Exact source and policy

The feature branch is `codex/rp1-1.0-candidate-acceptance`, based on synchronized
WsprryPi `devel` commit `1090d1af6095757fec039e39e09fc5ad70a5706f`.
Implementation and validation use `/private/tmp/wsprrypi-rp1-1.0-acceptance`;
the primary checkout and concurrent Si5351/WTP work remain separate.

The accepted software inputs now identify unpublished provider commit
`840afebc667cf9202701246b84d437d95581cdd3`, tree
`9ae4e4a1a5e432f3f722c7bbfae7563d3d1a39f8`, product/module/controller
`1.0.0`, Debian version `1.0.0-1`, binding v3/readiness v1/update v1.
The [candidate lock](../../scripts/tests/fixtures/rp1-candidate-1.0.0.json)
records every asset, UAPI and overlay hash. GPIO4 and GPIO20 retain distinct
`v1.0.0-rp1-gpio4` and `v1.0.0-rp1-gpio20` identities. Development installation,
software candidate acceptance and production consumption remain distinct;
production still refuses an unpublished release.

Provider-owned repairs required new candidates rather than consumer workarounds:
passive package data survives exact inverse runtime removal, orphaned runtime
helpers/UAPI directories are refused, and interrupted inverse removal resumes
without trying to observe an already removed binding. The latter regression
now reads the binding from the mutating filesystem; the former fixture kept a
synthetic binding alive and concealed the failure. Foreign or unretired journals,
changed bytes, unsafe modules/endpoints and stale plans remain refusals.

## Target campaign and disposition

The approved exact-input campaign used consumer
`baa1459a4cd1456613b24430e826b286094dffb6` and provider
`51b8012a4ca2f01576805a81f4b50519b5b786d3` on `wspr5`, kernel
`6.18.34+rpt-rpi-2712`. Cleanup-first source-development migration from exact
`0.9.0` source `924c7e546ab82a40497339c4baeb813f95f3408e` completed before
installation and neutral runtime activation. No reboot or boot edit occurred.

The maintainer confirmed GPIO4 and GPIO20 connected through proper attenuation
to the SDR. The campaign used TONE at 14,097,100 Hz, 2 mA, transient manual
0 PPM plus the intrinsic -46.245 PPM baseline. GPSDO LBE-1421 outputs and PPS
were disabled and verified; no GPSDO reference output or frequency calibration
was performed. Receiver: SDRplay RSP1B, 250 ksps CF32, center 14,087,100 Hz,
200 kHz bandwidth, 20 dB gain, AGC/bias off. FFT windows were 32.768 ms and
had no hardware timestamp or independent frequency calibration.

| GPIO4 case | SDR carrier duration | Result |
| --- | --- | --- |
| Full eight-second finite program | 8.031744 s | PASS on those exact inputs |
| STOP near start | 1.032192 s | PASS on those exact inputs |
| STOP requested at 3.99 s | 8.031744 s | FAIL; End was processed near 7.75 s |

The failed batch stopped after three programs: 24 programmed RF seconds, within
the approved maximum of 14 programs/112 programmed seconds. GPIO20, later STOP
cases, owner-close and service-stop were not run. Terminal cleanup and passive
GPIO/clock/DMA/owner/lease quiescence were verified after all three cases.
No results transfer to the repaired commits or the untested route.

The external helper polled status every 50 ms while synchronous replies took
about 110 ms, placing End behind queued observations. The consumer repair retains
partial frames, bounds frame/queue storage and coalesces consecutive pending
status observations without reordering control commands. Hardware-free socket
regressions cover split headers/payloads, a backlog before End followed by Start,
half-close and oversized frame rejection. Revised qualification helpers allow
one outstanding status query and record STOP send completion.

Failure cleanup used provider-owned recovery and exact inverse removal. When the
public update wrapper refused interrupted removal, its direct owning removal
verb completed cleanup. The captured application/configuration/service files
were restored; exact original `0.9.0` source was reinstalled through its owning
installer. Both module file hashes match the original backup. The application
is active/enabled and idle, saved GPIO20/correction restored, GPSDO outputs off,
modules/endpoints absent. Runtime deployment remains absent; historical runtime
journals were not replayed, and fresh source-installation ownership is retained.

## Verification and remaining gate

The candidate procedure performs two reproducible builds and independent
validation. Provider offline checks, all disposable package/DKMS lifecycle
scenarios, consumer installer/candidate/diagnostic tests, real cross-provider
contract tests, Linux semantics, transmitter component tests, GCC13 release
profiles, simulator audits, and UI/browser regressions form separate software
evidence. The initial consumer commit's ten hosted CI jobs passed; a follow-up
commit must pass its own hosted run. Software evidence does not qualify RF.

The retained external evidence root is
`/private/tmp/wsprrypi-item16-evidence`; `validation-index.json` maps exact
commands, outcomes and evidence hashes. `wspr5-rf-campaign-manifest.json` seals
capture hashes and records the failed case explicitly. Private configuration
and raw IQ remain in the protected target evidence directory.

A revised consolidated plan must bind the repaired consumer/provider commits
before any additional target migration or RF acceptance. Target tests must
repeat on those inputs and independently cover both routes. No successful
software check closes this gate. After acceptance, integration into `devel`
requires separate authorization; only then may later provider freeze/publication
work be considered.

## Documentation and licensing

Maintainer documentation covers candidate selection, installation/ownership,
bounded Tone, diagnostics and this record. Separate operator documentation still
needs the 1.0 selection/version gate, cleanup-first/pending-removal recovery,
identity diagnostics and terminal failure behavior. That sibling repository was
reviewed read-only and was not edited without cross-repository authorization.

The existing UI presentation remains unchanged. Impeccable review and the browser
fixture exercise literal provider failures, draft preservation and available
End/recovery controls at desktop/mobile sizes, light/dark themes and mobile
200% zoom. Local Impeccable design-sidecar drift remains a separate follow-up.
Existing licenses/notices remain intact. New independent tooling/tests/docs use
MIT; no provider kernel source or canonical UAPI is vendored into WsprryPi.
