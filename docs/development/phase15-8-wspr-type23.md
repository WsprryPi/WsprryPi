# P15.8 Pico WSPR Type 2/3 companion

Status: software implementation and local validation complete. CI/publication
are tracked in the lead Pico execution record. This branch is not integrated
into Pi devel.

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

## Final local gate and source identities

Production implementation is `41c1dbd42620da6d5595b3ca0171c15ad6c9ee5f`;
subsequent changes only stabilize the browser fixture/evidence. Pico current
is finally pinned to `0c02423d3ea0f6e89507a74ed5a2be9b00895ee0`; later lead-repo
review/report commits do not replace that implementation pin. Historical rows
remain legacy `95aac22`, extended `dfffdd5`, p152 `87144d1`, p153 `d6208db`,
p154 `ddb00dd`, p156 `792b5b2`, p157 `26ae596` (full identities in pin files).

Final Linux native units/API/UI producers pass. ASan/UBSan with leak detection
passes the final native validators, actual consumer profile/crypto cases and
resource/UI producers for TLS, consumer station and AP. Logs:
`sanitize-final-{native,build,consumer,resources}.log`. Linux native-filesystem
runtime passes 34 TLS cases, 358 production runtime checks, 1,514 scheduler checks
and 1,337 application checks (`runtime-localfs.log`); simulated backend, no devices
or RF, no timeout changes. The original bind-mount failure remains recorded.

The maintained release probe is in the lead repo under
`tests/release_pi_compatibility/`: it verifies exact clean Pi/Pico/Mbed TLS
identities, compiles unchanged release Pi product files, reuses only test doubles
from the original fixture and links the freshly built current Pico fixture.
Its ten station/AP cases pass (`release-probe-maintained-test.log`). Type 1
includes a real unrelated-settings save and preserved watermark; every new
format is an explicit `unsupported_station_encoding` refusal with no mutation.

The browser confirmation passes all twenty captures
(`browser-confirmation-framed.log`), with visible-field assertions after viewport
layout settles. It corrects only screenshot framing under the incumbent fixed
header. Product UI received one bounded repair batch; no redesign/polishing loop
was performed. The lead repo's generated page also passes its twelve-capture
real HTTP/crypto commissioning confirmation.

During execution the operator independently advanced Pi devel to
`587c7b137e982dca012701c0a5e85422bf720d73`; GitHub comparison shows only
`release_tools/Release Punchlist.md` changed. Therefore reviewed native product
blobs are unchanged from the probe's exact `a2437d1` base. The primary checkout
now has that HEAD and a further dirty Punchlist edit; neither was altered by this
execution. This companion retains its approved base and is not rebased/merged.

Publication order is constrained by CI's exact cross-repository checkout:
after local old/new compatibility gates pass, push the tested Pico implementation
so its immutable SHA is reachable, then push this approved Pi feature head and
run its existing required workflow. The frozen Type 1 contract and tested old-Pi
refusals make this staged rollout safe; Pi devel is not a publication target.
CI run/head/results and final remote parity are recorded in the lead Pico
`docs/development/phase15-8-wspr-type23.md` after the workflow completes. Feature
branch validation does not mean the release branch adopted the new capability.


A final Pico-only UI reassessment corrected application/2's null-plan empty
state, which had displayed the old Type-1-only fallback label. Pico production
identity is now `99ce69670e3c44182216f847b245a3f62a13eca2`; its portable C++,
protocol, planner and scheduler are unchanged from `bcd7f03`. The current pin and
clean-source interop/firmware/browser checks were refreshed. All seven historical
rows passed again with the final native client (`final-pairs.log`, seven
`FINAL PASS` records); their source pins and original expectations were not
changed. The current UI-source retest uses `current-ui-*` and `sanitize-ui-*`
logs. No Pi production code changed after `41c1dbd`.


Final companion current pin is `0c02423d3ea0f6e89507a74ed5a2be9b00895ee0`.
Pico's last UI-only repair preserves an unchanged legacy unencodable identity
when saving unrelated settings, matching the existing API's preservation rule;
a changed identity still requires authoritative planning. Its shipped-script
behavioral regression passes. C++/protocol/encoder/scheduler blobs remain the
same as `bcd7f03`. Final-source local checks use `final0c-*` logs. The native
host network test at the preceding UI identity `99ce696` conservatively reported
one missed start during concurrent builds (elapsed 158668 ns, allowance 1 ms;
mapping error was not logged). The unchanged test passed after other compilation
finished (`current-ui-network-quiet.log`); no timing assertion was weakened.
The final pinned source is also exercised by companion-head CI.


The post-publication grammar reassessment found and repaired a Pico prefix
selection defect: a valid three-character prefix can also be a valid Type 1
base call (`W1A`). The old suffix branch returned before trying the valid right-
hand base call. Final Pico source is now
`804e8a06585ec5a0e650e69fcd71ada398b6cc9f`; `0c02423` is superseded. Pi's
unchanged reference and pinned independent WSJT-X agree on all 162 Type 2
symbols for `W1A/AA0NT`; cold/contextual Type 3 observations and full-call hash
vectors are added. Original Pico binary reproduction reports both the pair and
explicit compound Type 3 refused; repaired planner/resource/real commissioning
checks pass. Native station/AP profile and UI/journal cases now cover both
`W1A/AA0NT` and `<W1A/AA0NT>`. No Pi product code changed. The earlier CI run
38948276130 at `28ee735` is superseded; a new feature-head run is required.
