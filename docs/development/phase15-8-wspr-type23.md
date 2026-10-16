# P15.8 Pico WSPR Type 2/3 companion

Status: validation in progress. This branch is not integrated into Pi devel.

The operator approved only this companion scope on 2026-10-16: strict native
management/client/UI format negotiation, tests, compatibility pins, associated
documentation and existing feature-branch CI enablement. Base is
`a2437d1bf026e91ea95ac7e2a7a0101e3c4f11a1`, branch
`codex/p15-8-wspr-type23`, isolated worktree `/private/tmp/wsprrypi-p158/source`.
The primary Pi release checkout remains unchanged, including its pre-existing
`release_tools/Release Punchlist.md` edit. Merge/promotion to Pi devel requires
separate operator authorization.

[The compatibility contract](../wtp-configuration-compatibility.md#p158-versioned-wspr-formats)
records the selected versioning. Pico leads encoding, automatic planning,
standalone pair reservation and commissioning. This companion adds no encoder,
transmitter, GPIO, RF engine, scheduler, service or release behavior.
WSPR-Reference remains unchanged. Its existing Linux major regression runner
passes while retaining its documented nonfatal observations. Pico's independent
WSJT-X vectors establish additional packing/hash/receiver observations; neither
suite establishes physical RF or general receiver full-call resolution.

Required validation includes all eight exact clean Pico rows, current native
TLS and consumer station/AP crypto/resource/UI producers, unchanged release Pi
work/refuse checks, Linux-only native/runtime/sanitizer checks, shipped browser
inspection, adversarial reassessment and actual companion-head CI. Results,
repairs and publication identities will be appended after execution. No local
macOS Pi C++ compilation, hardware operation or operator-doc repository edits
are included.

## Documentation Impact

Updated the native configuration compatibility contract and added this bounded
companion/rollout record. Historical P15.1–P15.7 evidence and operator documents
remain unchanged. Pico's P15.8 execution record supplies the exact P15.14 operator
page corrections and screenshot follow-ups. The operator documentation repo
requires separate authorization to update or publish.

## Review and initial local results

The executor performed self-review; no independent reviewer was delegated.
The first actual TLS pair exposed missing versioned native outbound route pairs;
the TLS/consumer allowlists now admit the exact negotiated paths with unchanged
body/revision/authentication checks. Strict plan validation rejects unknown frame
sequences, inconsistent ambiguity/null projections, fractional slot separation
and a versioned descriptor presented without its legacy foundation. Browser
inspection caught a draft replacing the saved ambiguity warning; draft syntax
feedback now retains that warning. Pico's field wrapper received its separately
committed corresponding route repair.

The repaired Pico implementation is
`bcd7f036b9ed57ee0aab080ca8617fccb69b7588`; initial `6bcaf42` is superseded.
All seven historical rows passed with original pins and strict expectations.
Current native TLS/config/resource producers, real station/AP crypto profile
readback, concurrent shipped-browser recovery and journals pass. Unchanged
release Pi `a2437d1` passes Type 1 read/write and explicitly refuses selected
Type 2, Type 3, prefix pairs and ambiguous suffix pairs over both consumer
carriers. Its product files and original clean-source guard were not changed.
The external probe changes only its test-main wrapper and supplies a fresh
profile request digest as required by existing commissioning rules.

Chrome 155.0.8059.40 rendered the actual PHP/JS with genuine source-paired JSON
responses mocked only at HTTP. Twenty desktop/mobile light/dark captures cover
recovery, Type 2, Type 3 and ambiguous pairs, with keyboard Tab, invalid locator,
preserved drafts and no replay. UI producer-to-native/crypto/journal acceptance
is separate and genuine; the rendered mock does not claim a live station.
Impeccable was used before UI implementation. Its one mechanical scan reported
no findings but explicitly degraded to regex because optional HTML parser
modules were absent; visual inspection supplies the rendered evidence.

Linux Docker `wsprrypico-p153-env:local` (GCC 14.2, CMake 3.31.6, Node 20.19.2),
network disabled/no devices, supplies all native compilation. The existing
permission-sensitive TLS rotation fixture first failed on the macOS bind mount;
the unchanged suite is being rerun on the container's native Linux filesystem.
No assertion, deadline or CI timeout was weakened. Logs/builds remain under
`/private/tmp/wsprrypi-p158/results`; browser evidence is under
`/private/tmp/wsprrypi-p158/browser-confirmation`.
