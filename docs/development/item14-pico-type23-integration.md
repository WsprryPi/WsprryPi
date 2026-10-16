# Item 14: Pico Type 2/3 integration acceptance

October 16, 2026. Integration and operator-documentation work executed under the
maintainer's explicit instruction to assess adversarially, repair, commit and
promote to devel after validation. The later explicit Docs reply also authorizes
its devel promotion and hosted publication. P15.9 remains unexecuted and outside
Beta 1 scope; item 16 freeze/build is not started by this slice.

## Source identity and scope

The initial clean Pi devel checkpoint, its upstream and independent remote query
all matched `af4ce3138177ac488421e7d45808e5c31a54ec4f`. The isolated managed
worktree is `/Users/lbussy/.codex/worktrees/step14-pico-type23-integration/WsprryPi`,
branch `codex/step14-pico-type23-integration`. It merges the **complete** accepted
companion history, `41c1dbd` through `6d5e73430bfbacdb8cfc29c3fe41089aa8ae5fcb`,
with both histories retained. Initial implementation merge commit is
`f6b5158bd3c3bdf38b28ae4f649d0404f11cfc25`. Companion base is
`a2437d1bf026e91ea95ac7e2a7a0101e3c4f11a1`; its original branch/checkpoint and
existing worktrees remain untouched. No textual conflict occurred.

Pico tested product pin stays **`804e8a06585ec5a0e650e69fcd71ada398b6cc9f`**.
Its initially observed read-only devel/report head was
`5c7585c22f7d46e120fc4be4720c84a9e8dbecd6`, matching its independent remote.
Concurrent later Pico source/roadmap changes are preserved and never used as
compatibility inputs. Mbed TLS is clean and pinned to
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.

Docs starts clean at `b377bc48bafe7310a5e5e6ef04db7a7e8ace9427`, with accepted
previous content `166639f21ba3e13f24db2fae97928069cc9069ab`. Its separate worktree
`/private/tmp/wsprrypi-step14-docs`, branch `codex/step14-pico-type23-docs`,
commits operator content at `7dcac4a08be02747a61adcc0b633a6865293265c` and final
record at `ee689f0c7f5bbbb4f961ec81fd1b1cfbf98f9a79`. That exact head is promoted
to Docs devel and independently verified remote. No force push, PR, tag, release,
announcement, other-chat message or target hardware operation occurs.

`WsprryPi-UI` is the only component modified. Parent changes include native
`src/wtp_integration/{configuration,consumer_radio,network_http}.cpp`, native
fixtures/tests, Makefile/CMake/pins, compatibility contract and non-hardware CI.
No reusable `src/` component implementation, WTP/1, encoder, scheduler or local
RF authority is changed. All component trees remain ordinary parent content.

## Integrated behavior

The adapter requires the exact additive application_resources_v2 descriptor
with its legacy foundation, then selects application/2, station/2, config/2 and
config/update/2 on engineering mTLS or explicit authenticated consumer station/AP.
Frozen application/1, station/1 and wspr_type1 remain supported. Unsupported or
malformed discovery refuses; there is no route/carrier fallback or write replay.
Native admission retains canonical Host/Origin/intent, limits, revision, fresh
member/boot/profile context, profile-owned identity and single transactional save.

The Pico selects ordinary/four Type 1, ordinary/six Type 1 using first four on
air while retaining all six, compound/four Type 2 without locator, compound/six
Type 2 then Type 3, and bracketed/six Type 3 single. Bracketed/four and unsupported
syntax refuse. W1A prefix and bracketed W1A, suffix ambiguity, alternate spelling
and requested-full-call hashing are retained. Exact reported powers, including
zero, are message metadata. Complete atomic legacy/update semantics preserve
omission/null/false/zero, credentials, flags, schedules, pins and watermark.
Readable unchanged unsupported identity can survive an unrelated save.

The station hint is generic before negotiated readback, truthful Type 1-only for
old firmware, and automatic Type 1/2/3 for current firmware. Context invalidation
returns it to undiscovered guidance. It remains associated with all station
fields and does not claim that a save or draft hint proves readiness, enablement,
inactive output or transmission. No durable incomplete draft, partial success or
new apply lifecycle is added.

## Adversarial findings and repairs

A fresh independent read-only reviewer, `/root/p158_adversarial`, inspected all
16 original companion paths, native/Pico contracts, current repairs, tests and
visual evidence. Executor self-review is separate from that review.

| Finding | Severity / trigger | Repair and regression |
| --- | --- | --- |
| F00: static station hint still claims Type 1-only | P2; current firmware loaded | Generic initial PHP hint plus negotiated helper; unit checks field associations and old/new text, actual resource producers assert negotiated text |
| F01: missing current Type 3 file silently skips feature browser coverage | P2; generated/copy input absent | Required eight-file application/2 set; missing/wrong-version fixture throws, unit regression; all eight real source-paired scenarios render |
| F02: unknown baseline schema treated as historical | P2; application/99 fixture | Explicit /1 or /2 admission and unexpected-schema regression |
| F03: Docs asserts universal 409 unsupported format | P2; shared station write returns 400 | Preserve endpoint-specific status/error; native API/interop assertions remain exact |
| F04: generic example hardcodes station/1 | P2; current adapter requires negotiated /2 | Explicit legacy label and loaded-schema instruction beside example; strict rendered/API review |
| F05: recovery fixture did not reconcile management selection after retirement | Test/evidence; later status observation invalidates loaded revision | Mock explicit engineering selection and perform management read before station loads, retaining real invalidation and deadlines; expanded browser run passes |
| F06: enlarged-text image clip cuts feedback tail | Evidence; Chrome region framing | Add capture padding only; confirmation images include full hint, controls and feedback; no product layout change |

### Known nonblocking Pico follow-up (F07)

Independent reassessment confirms a **P3 wording defect** at pinned Pico
`804e8a0`, `src/application/resources.cpp:66–67`: the Type 1 rationale
unconditionally says “All six saved locator characters are retained.” The real
source-paired consumer station `AA0NT / EM18 / 37` stores four characters. Its
station tuple, Type 1 plan and frame count are correct; no persistence/planning
failure is demonstrated. Pi faithfully displays the remote explanation.

This slice explicitly keeps Pico read-only and the current implementation pin
fixed. A future Pico follow-up requires separate authorization and should make that
sentence conditional or say “The saved locator is retained unchanged,” with four-/six-
character Type 1 rationale regressions and a reviewed compatibility-pin handoff.
No Pico edit, repin or other-chat message is made here. F07 is disclosed as a
known deferred Pico copy issue and is not a Pi integration blocker. Acceptance
means **no unresolved in-scope Pi/Docs integration defect**, rather than claiming
all Pico findings are fixed.

Source/docs reassessment closes F00–F04; independent visual reassessment also
checks F05/F06 and final acceptance evidence. No originating-conversation or
physical/security acceptance is implied by this independent software review.

## Compatibility and native validation

All compatibility sources are clean immutable native Linux clones, checked by
the existing full-SHA CMake guards. Historical pins and original expectations
are unchanged:

| Profile | Exact Pico pin |
| --- | --- |
| legacy | `95aac22bf7cc31b67fe5773761708d743d8a6473` |
| extended | `dfffdd5220db8b7cf1acf5e74950dbd294283c55` |
| p152 | `87144d1364f36d04bfe0a714bd1c2f6deb974302` |
| p153 | `d6208dbc88005383041c5ec306b738db7ec22796` |
| p154 | `ddb00dd7960373cec271f7816c0e1b10117416e9` |
| p156 | `792b5b2f60e381a6bd8e7a7726eeffe24fc08519` |
| p157 | `26ae596b540e92f25da37f978321ba1ec5934bdc` |
| current | `804e8a06585ec5a0e650e69fcd71ada398b6cc9f` |

The identified disposable image is `wsprrypico-p153-env:local`, immutable ID
`sha256:c7f2a891a6fff72e84bd1f2130b934881565f8b9ab489b20f35071f321ad219a`:
Debian GCC 14.2, aarch64 Linux, CMake 3.31.6, Node 20.19.2, PHP 8.4.24,
libgpiodcxx 2.2.1. Native containers have network=none, no passed devices and no
privileged mode. Read-only host inputs are copied to native Linux workspaces;
permission-sensitive fixtures never run on macOS bind-mount semantics. No Pi
C++ compilation/linking is attempted on macOS.

Commands from Pi `src`, with separate profile outputs:

```sh
make wtp-config-interop-test SUDO= BUILD_METADATA_DIR=/out/metadata \
  PICO_COMPAT_PROFILE="$profile" PICO_SOURCE="/peers/$profile" \
  MBEDTLS_SOURCE=/deps/mbedtls WTP_NETWORK_BUILD_DIR="/out/$profile"
make wtp-network-interop-test SUDO= BUILD_METADATA_DIR=/out/metadata \
  PICO_SOURCE=/peers/current MBEDTLS_SOURCE=/deps/mbedtls \
  WTP_NETWORK_BUILD_DIR=/out/current
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-network-runtime-test \
  wtp-scheduler-test wtp-application-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
```

All eight local rows pass (`interop-<profile>.log`); the actual current network
suite passes (`network.log`), including changed IPv4 with stable TLS identity,
production STATUS keepalive beyond idle timeout, a 60-second scheduled wait,
partial I/O/replay, foreign ownership, reboot/device mismatch and same-session
recovery. Config interop includes `wtp-config-test`, API/production UI producers,
current TLS, real consumer crypto/resource/journal checks and full-capacity recovery.
Focused native runtime passes **34 TLS cases, 358 production checks, 1514
scheduler checks and 1337 application checks**. Expected refusal/cleanup log
messages exercise injected failures, not actual transmitter operation.
ASan/UBSan/leak detection passes native config/API/client, actual consumer
profile crypto and all resource/UI producers:

```sh
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
make wtp-config-test wtp-api-test wtp-config-client-build SUDO= \
  BUILD_METADATA_DIR=/out/metadata WTP_NETWORK_BUILD_DIR=/out/sanitize \
  WTP_NETWORK_CXXFLAGS='-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_NETWORK_LDFLAGS='-fsanitize=address,undefined'
cmake -S tests/network -B /out/sanitize -DPICO_SOURCE=/peers/current \
  -DMBEDTLS_SOURCE=/deps/mbedtls \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build /out/sanitize --parallel 2
/out/sanitize/consumer_interop /peers/current/docs/protocol/Consumer-Radio-v1-vectors.json
node ../WsprryPi-UI/tests/wtp_resources_interop_test.js /out/sanitize
```

The maintained Pico release probe is unchanged and still hard-pinned to clean
Pi `a2437d1`. Fresh run against current Pico passes twelve consumer station/AP
cases: Type 1 read/unrelated save preserves identity and watermark; selected
new formats (including W1A) explicitly refuse without mutation/substitution.
For refreshed pre-integration Pi `af4ce31`, the **entire src tree**, including
product headers, reusable dependencies and native build files, is identical to
`a2437d1`. The probe's full native product/dependency closure is therefore
identical; its result is justified reuse for this native comparison, not a fresh
historical run relabeled as current. Accepted companion `6d5e734` is a fresh
clean comparison checkout; its own configuration producer passes against current
Pico using the candidate native binaries whose entire src closure matches that
companion. No probe/source guard is weakened or retargeted.

Reproduction of the immutable release and companion comparisons in the same
native Linux environment:

```sh
cmake -S /probe -B /out/release-probe -DOLD_PI=/work/frozen \
  -DPICO_SOURCE=/peers/current -DMBEDTLS_SOURCE=/deps/mbedtls \
  -DPAIR_BUILD=/out/current
cmake --build /out/release-probe --parallel 2
/out/release-probe/release_p158
node /work/companion/WsprryPi-UI/tests/wtp_configuration_interop_test.js \
  /out/current current /peers/current
```

## Affected Impeccable review and browser validation

Impeccable 4.1.1 context ran once for `data/views/wtp-controls.php`; Clarify,
Audit and craft-floor were read before edits. PRODUCT/DESIGN's Operate interface
and incumbent layout/styles remain. The existing step 13 inventory is retained:
**123 of 131 inputs unchanged**, eight affected inputs recorded in
`item14-affected-inputs.json`, plus the two new fail-closed fixture test/helper
files. The CSS, contrast, focus, enlarged-text, modal, logs, shell, browser-router
and trace-audit repairs stay included. All eight affected inputs receive fresh
coverage; no historical evidence is overwritten.

On macOS, Node 26.12.0/PHP 8.5.11/Chrome 155.0.8059.40:

```sh
# From WsprryPi-UI
npm ci --ignore-scripts
npm test
npm run test:browser
node tests/wtp_pi_fleet_ui_integration_test.js
node tests/wtp_network_ui_integration_test.js
WSPRRYPI_RUN_RECOVERY_BROWSER=1 node tests/wtp_recovery_ui_integration_test.js \
  /private/tmp/wsprrypi-step14/current-build \
  /private/tmp/wsprrypi-step14/browser-final-current
# From parent root
python3 scripts/tests/no_transmitter_hardware_audit_test.py
sh -n scripts/ci/assert-no-transmitter-hardware.sh
git diff --check
git diff --cached --check
```

All pass. Default browser tests retain step 13's 17 Test Tone workflows, five
pages/four modes, both themes, desktop/mobile, 200% text, contrast/descriptions,
first-log-record clearance, conditional GPIO, CW latch and 21 private-support
scenarios. Fleet covers explicit takeover/modal and stale/duplicate behavior.
Network uses fresh genuine current-pair envelopes to render old/new resource,
profile-owned read-only, storage fault, stale revision and uncertain save states.

Expanded recovery emits **72 captures**: initial lost-reply/readback/retirement
states plus Type 1, ordinary-six, Type 2, Type 3, compound pair, W1A pair,
bracketed W1A and ambiguity, each at 1440x1100 / 390x844, light/dark and
100/200% text. Assertions cover keyboard Tab/focus, hint-field association,
no page overflow, invalid bracket locator, unsupported preserved draft,
declined actions sending nothing, password scrubbing and zero save replay.
The fixture reconciles management state before fresh revisioned loads. A further
**16 captures** use fresh p157 application/1 envelopes, verify the old Type 1
hint/refusal, and retain draft, keyboard, both themes/viewports and 200% coverage
(`browser-final-legacy.log`). Both final rendered runs pass.

Real PHP/JS rendering mocks HTTP only. Separate actual native API/TLS/crypto/
journal tests establish carrier behavior; neither establishes a physical device.
Representative full-feedback enlarged/mobile/dark and normal/desktop/light,
W1A/bracket/pair/ambiguity, read-only/fault/recovery images were inspected.
Source detector returns []/exit0; rendered Docs detector explicitly degrades to
regex because optional parser modules are unavailable. No full computed-style,
assistive-technology or physical-browser certification is claimed.

Failed intermediate attempts are retained, not counted as passes: an initial
missing parent build-fixture destination, a wrong-directory patch command,
stale management-state fixture sequencing, clipped evidence framing, and a
runtime container missing its read-only Git metadata. Each was corrected and
the affected command passed. No assertion, timeout, source guard, hardware audit
or deadline was weakened.

## Documentation Impact

Updated operator Docs: Transmitter member guidance, REST API, host WSPR setup
and WSPR INI scope notes, with separate maintainer record/commits. Considered
unchanged: existing recovery/troubleshooting guidance and screenshots retain
accurate controls and no-replay procedures; no image replacement is needed.
Default/Pico gating is preserved, and no P15.9 feature is described as available.

Docs four behavioral tests and six strict default/Pico HTML/EPUB/LaTeX builds
pass without Sphinx warnings. Actual PDFs are 134/146 pages, zero TeX errors or
LaTeX/package warnings; incumbent whole-book hbox advisories are default 54/68,
Pico 54/90, with 14 underfull vboxes in each. Affected PDF pages 40/41/112/113,
EPUB presence/gating and built desktop/mobile/200% HTML were inspected. At
390px / 200% API text, document width remains 390px. See the separate
[Docs acceptance record](https://github.com/WsprryPi/WsprryPi-Docs/blob/ee689f0c7f5bbbb4f961ec81fd1b1cfbf98f9a79/maintainer/item14-pico-type23-docs-review.md).

No automatic hosted build appeared after the approved push (fresh public API,
no cached response; correct project repository, empty classic GitHub hooks).
As in the prior accepted Docs workflow, the existing authenticated Firefox
session's **Rebuild version** retries exactly devel. No hosting, integration,
version, alias, credential or visibility settings change. Actual publication
verification is complete: [RTD build 35202029](https://app.readthedocs.org/projects/wsprry-pi/builds/35202029/)
checks out exact `ee689f0c7f5bbbb4f961ec81fd1b1cfbf98f9a79` and succeeds in
58 seconds. All 16 raw-log commands exit zero; there are zero Sphinx warnings or
TeX errors. Three PDF passes resolve the normal first-pass references; 131
initial-pass LaTeX/package warnings are retained in the raw log, and the final
PDF pass has zero such warnings. Existing box advisories remain. Published
HTML and copied Transmitter source match the local default output; PDF has
134 pages and EPUB opens correctly. The hosted build intentionally retains
**Pico-hidden default gating**: the new member guidance stays hidden from HTML,
copied source, PDF and EPUB until that pre-existing visibility flag is enabled.
Pico-enabled local output (146 pages) is reviewed separately; no flag change or
claim that the new Pico guide is publicly visible is made.

## CI, promotion and final disposition

Initial exact-implementation [CI 38974181065](https://github.com/WsprryPi/WsprryPi/actions/runs/38974181065)
passes all **12/12 jobs** at `f6b5158`, including seven historical rows, pinned
current API/TLS/resource/crypto/browser checks, full Debian semantics with
libgpiod headers, macOS hosted portable profile, strict I2C, GCC 13 release,
component regressions and explicit simulators. Its source-paired browser emits
72 captures. All completed job conclusions and logs are inspected. The log has
40 nonfatal host warnings: 29 package man-page alternatives and 11 make jobserver
notices. Expected injected refusal/default-value diagnostics, non-device installer
fixtures and explicit opt-outs are not unrun required jobs.

The unchanged fail-closed audit/regression passes: all five actual trace
arguments are present; the script requires regular/readable/nonempty files
before grep; completion has no missing/unreadable/grep error and no prohibited
transmitter access. All five synthetic audit regressions pass. Full semantics is
explicitly `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=` in the
checked-in Debian workflow, not the portable subset.

Initial implementation is promoted to devel at `f6b5158` with independent remote
parity. [Devel CI 38975937376](https://github.com/WsprryPi/WsprryPi/actions/runs/38975937376)
passes all **12/12 jobs** for that exact promoted head before checklist closure. The later closeout
commit changes only this record/checklist/prompt metadata and browser evidence
framing plus explicit historical rendering; product PHP/JS/C++ is unchanged.
Its own final feature/devel exact-head CI and parity are verified in the final
handback after push, rather than claiming its eventual SHA from inside itself.

Independent final reassessment confirms F00–F06 repaired, native/source/browser
and hosted evidence inspected, 123 unchanged/eight affected item 13 inputs,
exact first CI/pin and all five traces. It independently reruns the fixture,
management, JavaScript syntax, five audit regressions and shell syntax checks.
No unresolved in-scope Pi/Docs integration defect remains; F07 stays explicitly
deferred in read-only Pico. Remaining exact-head verification is
not replaced by that judgment.

Raw logs, source-paired envelopes, generated outputs and all ordinary captures
stay under `/private/tmp/wsprrypi-step14` or ignored build trees, outside commits.
Only the maintainer record and affected-input hashes are retained in Git. The
first native container is retired after its eight rows/network pass; focused
runtime and sanitizer results come from their separate completed containers,
so redundant repeat phases are not counted or required. Task-owned test/browser
processes and disposable containers are retired after evidence collection;
unrelated containers/worktrees and concurrent Pico edits remain preserved.

A portable subset is never described as full Linux semantics. Physical GPIO,
MMIO, DMA, mailbox, I2C, RF, frequency/power, installation, target services,
full security certification and originating Pico conversation acceptance remain
outside this software integration. No current result weakens those boundaries.
