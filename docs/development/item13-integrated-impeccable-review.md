# Item 13: final integrated Impeccable review

October 16, 2026. Status: integrated UI review accepted for implementation
`d93c75c`, with corrected exact-candidate CI verified. At record preparation,
final record-commit CI/parity and scoped issue closure remain required; their
completed results belong in the #423 closeout linked below.
This is the integrated UI gate for the reviewed inputs, not Beta 1 packaging,
installation acceptance, publication, general accessibility certification or RF
qualification. Item 14 has not started.

## Candidate and prerequisites

Initial application `devel`, local upstream and independently queried
`origin/devel` all resolve to `587c7b137e982dca012701c0a5e85422bf720d73`.
The index was empty; only `release_tools/Release Punchlist.md` was modified.
Both staged/unstaged whitespace checks passed. The authorized item 12 closeout
and subsequently observed item 13 in-progress wording are retained. No reset,
stash, branch change, history rewrite or force push is used.

The complete tree has ordinary tracked components, no mode-160000 gitlinks and
no nested AGENTS.md affecting the changes. `WsprryPi-UI` is the only component
modified. No reusable `src/` component or parent application implementation is
changed. The parent CI workflow gains the existing opt-in recovery browser test
and the trace audit repair, with focused parent script/tests described in F13.

| Prerequisite | Exact source/evidence inspected | Decision |
| --- | --- | --- |
| Items 10–12 accepted in scope | [Item 10 acceptance matrix](rp1-item10-production-integration.md), including installed consumer `8d5247a`/helper `aae72f1`, independent GPIO4/GPIO20 supplement status, original empty manifest and retained target/RF limitations; docs item 11/12 records below | Satisfied; original accepted exclusions are retained, not reopened or presented as new tests |
| Release-scoped implementation/integration accepted | [Test Tone ownership/review](test-tone-lifecycle-review.md), implementation `fbb80fa2d1f654c10b8fcf695390144e945e0dd1`; [P15.4 acceptance](phase15-4-shared-resources.md), implementation `085ef702b43be57b13baf3637651cb36084d985d` plus fixture repair `f51d7b0686769f5e7dc574992dfde7e338f275cf`; [Si5351 disposition](si5351-readiness-review.md); [#448 software matrix](issue448-review.md); inventory `402c0c2`/`3bee319`; recovery `a6e5137a19f50f9b469dafaf18098bdaf400e44e` | Accepted source and tests are included in initial `devel`; acceptance is established by records and implementation, not checkboxes |
| Application candidate integrated on devel | HEAD/upstream/`git ls-remote` equality above; `git merge-base --is-ancestor` for Test Tone, P15.4, recovery, manifest, network safety, private support, band preferences and RP1 supplement milestones | Satisfied; later source changes here require their own review/tests |
| No expected unfinished UI work | Current #423/#412 bodies/comments, relevant Ready for Release completion evidence, selected punchlist dispositions and maintainer's item 13 instruction | Satisfied before assessment; findings from this review are repaired here. Deferred Pico/calibration/alternate-parent/Standard Feld/macOS work stays deferred |
| Hardware-free execution available | [Simulator contract](../simulated-backend.md), real PHP views, guarded loopback browser fixtures, mocked API/WS/SSE, current non-hardware CI | Satisfied; no installed application or transmitter is used |

Additional candidate-inclusion checks explicitly resolve and confirm ancestry
for manifest integration `433e62cc18323db34b4b200d32a398b7e72f2831`, network
safety `ad2d0cb89af7d3df8ad5e69c3d3cec27411431ec`, private support
`5c9cca004443c59ea660971f76a4d53d3881dd3d`, canonical band preferences
`bf6d83de4bf9e4c49f699ddfd8c9a222553fa98f` and RP1 supplement
`cc571d66aa0605cba794ace09cf9b2351ebe63f2`. Relevant source, tests, issue
completion comments and accepted operator procedures were inspected. Historical
submodule integration evidence is interpreted through today's ordinary trees.
The 26 open Ready for Release issues were inspected read-only; their readiness
label alone was not used as acceptance. Older #352's public-attachment proposal
is superseded by integrated #414's encrypted private workflow. Qualification
issues retain their recorded band/mode exclusions, rather than requiring every
possible combination to be qualified for this UI gate.

### Issue sequencing reconciliation

#423 originally demanded all selected issues be closed, while the actual
`Ready for Release` label means “In devel and pending next release.” The
maintainer's explicit item 13 direction authorizes accepted implementation to
remain tracked by those open issues pending actual Beta publication. Only
#423's sequencing text was corrected: accepted integrated implementation with
verified evidence/inclusion satisfies that prerequisite. Unfinished work,
unaccepted integration/target acceptance and expected UI changes still block.
#412 retains its explicit publication-only exception and remains open. Its stale
“acceptance in progress” summary does not override item 10's later acceptance
matrix. No other issue is changed, closed or notified; no Beta availability is
announced. #423 closes only after the final evidence-linked acceptance comment.

### Documentation identity

The actual sibling is `WsprryPi-Docs`; its AGENTS.md was read before read-only
inspection. Clean docs `devel`, its upstream and independent remote all equal
`b377bc48bafe7310a5e5e6ef04db7a7e8ace9427`.

- Accepted operator content: `166639f21ba3e13f24db2fae97928069cc9069ab`.
- [Item 12 acceptance record](https://github.com/WsprryPi/WsprryPi-Docs/blob/b377bc48bafe7310a5e5e6ef04db7a7e8ace9427/maintainer/item12-operator-docs-review.md).
- [Item 11 record](https://github.com/WsprryPi/WsprryPi-Docs/blob/8d6d037f7e1ae1bb9f0a525735ddcf5a6ee57d3a/maintainer/item11-docs-performance-review.md).
- Item 12 reviewed application `a2437d1bf026e91ea95ac7e2a7a0101e3c4f11a1`;
  `git diff a2437d1 HEAD --name-only` initially contains only the release checklist.
- `166639f..b377bc4` changes only the verification record. RTD build
  [35197867](https://app.readthedocs.org/projects/wsprry-pi/builds/35197867/)
  verified exact content HTML/PDF/EPUB. This review reuses the accepted hosted
  evidence; it does not independently rebuild/re-publish docs or require the
  verification-only commit to be hosted.

No subsequent pre-review source/content change invalidated those baselines.
The UI repairs below receive their own documentation-impact review.

## Source and integration orientation

Read current PRODUCT.md, DESIGN.md, app_state.php, index.php, shared shell,
navigation, asset/theme/identity loading, all five view sources and their JS,
including Fleet/member resources, local ownership and scheduling. Trace points:

| Operator action | Actual implementation boundary reviewed | Required truth |
| --- | --- | --- |
| Host station/mode/pin edits | `site.js` population → `index.js` validation/payload/autosave → parent config parsing/validation/serialization/persistence and deferred scheduler consumption | Invalid/custom drafts remain; saved is not runtime-active; cross-scope fields do not autosave |
| Test Tone selection/Start/End | `site.js` catalog/preview/correlated reply → `web_socket_commands.cpp` → scheduler lifecycle mutex/generation, cleanup/restoration result → status renderer | Preview is nonpersistent; acceptance is not activity; stopped does not imply all RF off; restoration failure remains separate |
| Member station/hardware | `wtp-management.js` → browser API/coordinator/actual pinned interoperability fixtures | Separate explicit saves; ETag/selection/boot context; nonpersistent pin validation; profile-owned station read-only; uncertain write requires readback without replay |
| Local Enable/Fleet | `local-wtp.js`, `fleet-schedules.js`, WTP browser/runtime bridge and fleet tests | One member application/scheduler authority; takeover is explicit; unknown output inhibits; independent assignments and retirement |
| RP1 route | `index.js` route controller → route API/service/qualification fields | Saved/active/structural compatibility/scoped evidence/application eligibility/readiness/ownership/output authorization are distinct; no fallback |
| Maintenance/update/support | `maintenance.js`, update/identity helpers in `site.js`, network safety and encrypted-support contracts | Requested/configured/active distinct; remote update requires ancestry/release evidence; readable review precedes encryption; opening handoff is not upload confirmation |
| Logs/spots | real view scripts, mocked SSE and demo reports; source stream/proxy contracts | Explicit loading/empty/failure, complete technical values, bounded reconnect/cleanup; no external data or journal process in this review |

Chrony/UTC timing remains distinct from RF-clock calibration. Item 3 is a scope
decision, Si5351 item 6 is bounded readiness acceptance, and RP1's original
empty `Unavailable` manifest remains unchanged. GPIO4 evidence never transfers
to GPIO20. Detailed RP1 diagnostics remain hidden in normal operation. No
band/mode policy, inhibition, authorization, persistence contract or backend
fallback is weakened. Simulation stays explicit and is never a hardware fallback.

## Impeccable method and initial assessments

Version **4.1.1**, `/Users/lbussy/.agents/skills/impeccable/SKILL.md`.
`context.mjs --target WsprryPi-UI/data/index.php` ran exactly once. Critique,
audit and Operate references were loaded; craft-floor was loaded immediately
before UI repairs. PRODUCT/DESIGN's slate shell, local fonts, neutral panels and
light/dark model govern; no redesign or collateral/sidecar refresh is performed.

Method: dual-agent, isolated self-contained prompts, read-only agents and
separate fresh IAB tabs:

- **A:** `/root/assessment_a`, source/browser design and operator-workflow
  assessment; no detector or B findings consumed.
- **B:** `/root/assessment_b`, deterministic detector and browser audit. Its
  findings were withheld until A finished. Lead alone owns repairs.

Initial Nielsen scores: status 3, real-world match 4, control/freedom 3,
consistency 3, error prevention 3, recognition 3, efficiency 2, aesthetics 3,
error recovery 3, help 3: **30/40**. Initial technical scores: accessibility 2,
performance 3, responsive 2, theming 3, integrity 3: **13/20**. These initial
scores are not inflated into certification after repairs. Both assessments
identify a coherent product-specific instrument and concrete defects, rather
than category-interchangeable styling. Expert/keyboard-low-vision/mobile
personas were considered. Dense technical tables and five related WSPR plan
choices are intentional expert-operating structure; no unsupported simplification
is introduced. Literal backticks in existing technical examples are an editorial
observation, not a functional defect or a reason to rewrite accepted copy.

Performance audit is bounded source/asset inspection: page-specific CSS/JS are
selected from active view metadata, view scripts load after page markup, font
faces are local with `font-display: swap`, and versioned asset URLs/cache-identity
regressions remain covered. The repairs add no vendor dependency, image/font
payload or production request polling. Large incumbent shared/Setup scripts
remain (282,153/206,637 source bytes); this review found no new performance defect
requiring a scoped repair. No Lighthouse/Core Web Vitals, slow-network benchmark
or target-Pi browser performance qualification is claimed.

### Detector evidence and limits

The exact invocation is retained in
[item13-integrated-impeccable-evidence/detector-command.json](item13-integrated-impeccable-evidence/detector-command.json).
It passes 44 explicit first-party markup/source targets (33 PHP, 11 JS),
excluding vendor assets and `view_diag_logs.php`. Explicit PHP arguments matter:
the directory walker does not recognize generic PHP. CSS is inspected through
source and computed browser styles, not misleading CSS-only detector targets.

Initial CLI exit **2**, valid JSON, empty stderr, **3 advisory
`design-system-color`** findings: `site.js` LOG_LEVEL_STYLES at initial lines
3650/3652/3653, values `#1f6feb`, `#9a6700`, `#cf222e`. They format developer
console messages, not operator surfaces; justified false positives, no repair.
This version lacks the separate advisory flag expected by its exit partitioner,
so actual exit 2 is recorded rather than called a clean exit. Regex source
coverage does not calculate contrast, execute PHP/cascade or prove accessibility.

B's browser detector ran on all five views at desktop 1440×900 light and mobile
390×844 dark. Mutable CDP preflight changed title, appended a script and verified
execution. Injection of `detect.js` succeeded and yellow overlays/console output
were captured. Subagent `visibility.set(true)` returned “IAB visibility is not
supported in a subagent thread”; no human-visible overlay deliverable is claimed.
The owned overlay server on 18424 was stopped, its runtime directory removed,
metrics cleared and B's tab closed.

Ten snapshots yielded **53 repeated raw detections**, not 53 defects:
cramped-padding 2, thin-border/wide-shadow 10, nested-cards 4, text-occlusion 18,
skipped-heading 10, low-contrast 4, gray-on-color 5. Dispositions:

- Bootstrap tab baseline padding, segmented mode controls and flat Spots toolbar
  are deliberate incumbent components.
- Temporary About popup elevation is allowed by DESIGN; its actual sizing bug
  is repaired separately.
- Signal text on slate has about 7.68:1 contrast; “gray on color” is false here.
- Closed details/hidden modal headings are not visible content. The visible
  Maintenance heading skip is a real finding and repaired.
- Inspector labels detect their own occlusion/overflow. IAB emulated screenshots
  also clip despite correct DOM dimensions. Conventional Chrome captures below
  provide final visual evidence; tool artifacts are not blamed on the product.
- Disabled Connecting buttons are contrast-exempt; enabled action contrast and
  hover/focus states receive real computed checks, not that exemption.

`view_diag_logs.php` is excluded from detector/injection/automated styling and
untouched. Manual source inspection covers its diagnostic controls, SSE and
self-contained/CDN dependency boundary. No shared-shell or offline-reliability
claim is made for that explicitly excluded developer diagnostic.

## Surface/state coverage matrix

This inventories the complete first-party console; rows describe sampled evidence,
not every combinatorial state on every device. Existing focused tests retain
production handlers; current interoperability CI regenerates actual peer inputs.

| Surface | Representative states/actions | Evidence |
| --- | --- | --- |
| Shared shell/navigation/theme/identity | loading/disconnected/reconnect, desktop/menu collapse, active navigation, About open/closed, build IDs, mismatch refresh, both themes, keyboard bypass/focus | Manifest/identity unit+HTTP tests, responsive source checks, integrated render/contrast/description/About checks; A/B source/browser |
| Operation/status/local authority | initial/unavailable/retained values, enable/disable/Stop, ownership/takeover choices, finish-current/end-now/cancel, output unknown and explicit recovery | Existing CW/GPIO browser integration, Fleet/local-takeover suite, WTP parent CI; mobile runtime-before-reboot layout check |
| Setup WSPR | populated/placeholder/invalid station, custom frequencies/band preferences, preview vs autosave, validation/pending/saved, refresh/context and invalid-draft preservation | Unit band-frequency correlation, CW/GPIO real-handler browser suites, parent UI/config semantics; integrated WSPR render |
| Setup QRSS/FSKCW/DFCW | four mode render states, custom dot/shift/repeat/gap values, validation, mode guard/cancel, busy/duration latch, timing persistence | CW timing unit and duration-latch browser suites; parent semantics; four-mode visual samples |
| Transmitter/Pi I/O/RP1 | enabled/unsupported adapters, saved vs active route, conflicts, retained hidden values, progress/uncertain transaction, cancellation/recovery, compact qualification status | GPIO/browser and RP1 UI unit regressions; parent RP1 diagnostics/planner/lifecycle/route tests; A source/browser; hidden details not forced into operator view |
| Fleet/member configuration | hidden development gate, empty/populated catalogs, unsupported transport/resources, Type 1 station restriction, profile-owned fields, Load/Validate/separate Save, saved vs active/null/restart, ETag/context conflicts, storage/application faults, late replies and uncertain writes | WTP unit, network/resource/fleet and recovery browser suites; fresh current and historical peer interoperability in Linux CI |
| Maintenance Test Tone | custom/band preview, invalid draft, accepted/starting/active, committed differences, pending End, stale generations/replies, timeout/disconnect/reconnect, failed cleanup/restoration, explicit End, modal keyboard dismissal/focus return | 17 production-handler browser workflows, parent request/plan/response/lifecycle tests; 21 tone images plus integrated keyboard coverage; no simulated TONE worker claimed |
| Maintenance network safety/update/recovery | requested/configured/active unknown, privileged controls mocked; version missing/malformed/fetch failure/disabled; reset/repair confirmation; update release/ancestry/unknown distinctions | Network safety unit, update/source regressions, integrated actual version-load branches and button-state contrast; no privileged mutation |
| Support bundle/private intake | initial/create/cancel/failure, readable review/download/consent, encrypted download/receipt, unavailable/disabled/upgrade/trust failure, GitHub/non-GitHub continuation, opening vs reported upload, clipboard fallback | Support unit+harness and 21-scenario real browser suite; support runtime/crypto software CI; all data/endpoints synthetic; no upload/message |
| Logs | initial/empty/connecting/disconnected/reconnect, populated real SSE-handler row, long service/message values, first-row clearance, controls and non-color severity/status | Responsive unit/source, integrated fake SSE through production handler, all viewport/theme/text scales; source stream lifecycle review; no journalctl |
| Spots | loading/empty/failure/retry, source failover, populated demo, long technical table, sorting/refresh and external QRSS link labeling | Spots unit/PHP menu tests, integrated demo and A/B empty states; proxy never contacts an external service |

Conventional Chrome review uses **1440×1000 desktop and 390×844 mobile**, both
light/dark, and **200% root text sizing** in each of those combinations. This is
text-enlargement stress coverage, not a claim of physical phone testing or native
browser zoom equivalence. Network/recovery fixtures additionally sample
1280×900 and 390×844 in both themes. Computed text contrast composites actual
ancestor backgrounds and foreground alpha, skips disabled/closed-details
controls, and waits for
Bootstrap transitions before sampling normal/hover/active/focus states.
Descriptions resolve, status meaning uses text as well as color, and production
status regions retain polite announcements. Browser keyboard event coverage
includes Test Tone open/Escape/focus return and the existing modal/Fleet recovery
paths, plus enlarged-menu open, theme-control reachability, scroll and keyboard
close/focus in both themes. The smallest sampled enabled-button/tab text ratio
is 4.84:1. This is semantic/browser inspection, not actual VoiceOver/NVDA testing.

## Finding, repair and reassessment ledger

| ID | Severity; trigger/location | Repair and operator effect | Verification/disposition |
| --- | --- | --- | --- |
| F01 | Review-isolation P1: browser fixtures initially load production transports before mocks | Shared test-only PHP router refuses app/privileged/journal/spot endpoints and CSP restricts connections to the fixture origin | Real Chrome fetch/XHR attempt against owned foreign-port sentinel receives zero requests; endpoint 503 checks; all seven fixtures use router |
| F02 | P1: enabled warning/secondary/primary outline actions fail text contrast, especially dark recovery | Shared theme-aware button foreground/border/interaction tokens; warning fill uses dark text; explicit keyboard outlines | Computed 4.5:1 minimum on sampled enabled text buttons in both themes and hover/active/focus states; log override repaired rather than masking hover failure |
| F03 | P1: absolute log toolbar/status cover first ordinary records; light hover text disappears | Non-scrolling flex control row precedes scroller; remove competing custom button fill | Real production SSE-handler row geometry clears controls at both sizes/themes and 200%; hover regression |
| F04 | P2: failed/invalid local version leaves update panel Checking | Both failure branches render bounded unavailable/failure feedback, retaining disabled preference | Actual HTTP failure and malformed-success branches; no false update availability |
| F05 | P2: open mobile About panel shrinks to 37.625px and extends beyond viewport | Explicit viewport-bounded popup width anchored to summary's right edge | Open-state measured width/bounds and screenshots, both themes and narrow layout |
| F06 | P2: missing description IDs and visible Maintenance heading skip | Operation descriptions reference actual state detail; Setup tablist gets a real label; recovery headings become peer h2 without visual restyle | Rendered description resolution, source/DOM heading review |
| F07 | P2: mobile runtime actions follow infrequent reboot choices | Runtime controls precede reboot group in source/mobile, desktop grid placement retained | Mobile geometry and keyboard/source order; no setting/persistence change |
| F08 | P2: programmatically opened Test Tone lacks trigger focus restoration | Reuse modal instance and restore trigger focus on dismissal, settling its local loading presentation; extend the existing fake DOM with native one-time event support | Real keyboard Space/Escape event workflow and full unit suite; Start is never invoked by that test |
| F09 | Adversarial P1: 200% log buttons overlap despite a document-width check | Auto-fit the control grid by actual available width; remove the minimum button width | Individual text/control bounds and non-overlap assertions, enlarged mobile screenshot |
| F10 | Adversarial P1: active dark Setup tab has 2.66:1 contrast; initial helper omitted nav-link text | Theme emphasis/body tokens for tabs and CW selectors; sample nav links with foreground alpha compositing | Final sampled enabled normal/interaction text contrast minimum 4.84:1; desktop/mobile and both themes |
| F11 | Adversarial P2: enlarged shell status/clocks clip inside overflow-hidden containers | Mobile masthead uses a full-width status row; clocks wrap labels and values without intrinsic minimum widths | Range/text and parent bounds at 200%, not just document scroll width; conventional Chrome screenshots |
| F12 | Adversarial follow-up P2: at mobile 200%, fixed expanded navbar leaves theme control below viewport (886–914px on 844px viewport) | Cap the mobile navbar at the dynamic viewport height and allow vertical scroll | Real keyboard open, focused theme reachability, scroll back to toggler, keyboard close/focus in both themes; open-menu captures |
| F13 | P1: completed CI log shows five missing trace arguments with leading spaces, yet hardware-access audit reports success | Use a literal YAML shell block, reject missing/empty/unreadable regular traces, and fail on grep errors | Five synthetic-trace tests cover clean paths with spaces, missing/empty/directory/prefixed-space inputs, prohibited paths and grep exit2; corrected CI38952419761 independently checks five nonempty real traces without read errors; no prohibited access observed |

A fresh isolated read-only reviewer, `/root/adversarial`, challenged the complete
source diff, fixtures, screenshots, prerequisites and acceptance record after the
initial repairs. Its three confirmed findings (F09–F11) were repaired and the
affected unit/browser suites repeated. Reassessment confirms the repairs and no
additional confirmed actionable implementation defects. The reviewer could not
use its child browser surfaces during reassessment; it inspected conventional
Chrome evidence and actual test logs instead. It requested a final expanded-menu
check at 200% text size. That check confirmed F12; after repair, both theme
captures and actual keyboard reachability assertions pass. A final independent
reassessment reviewed the current CSS/helper, those two images and all final
local logs, and reports no unresolved actionable findings in the bounded scope. This is an
independent agent review, not the lead executor self-review.

Initial false positives, incumbent editorial conventions and pre-existing bounded
qualification/device/performance limitations are recorded above. They are not
silently waived actionable defects. No new hardware-support claims, Pico feature,
calibration method, clock parent, Standard Feld behavior or deferred macOS
capability work is introduced.

## Validation and evidence boundaries

Local host: macOS 27.0.1, Node v26.12.0, PHP 8.5.11, Python 3.15.0
and Chrome 155.0.8059.40. No local Mac C++ compilation or
linking. Available existing Debian ARM64 images were inspected with disposable
`--network none` containers; they lack the complete Node/PHP/Chromium/strace
runtime, so they are not claimed as a full semantics pass. Podman was stopped
and remained stopped. Linux application/component validation is supplied by the
identified final GitHub CI environment, not by a portable subset or earlier run.

Commands from `WsprryPi-UI` unless noted:

```sh
npm ci --ignore-scripts
npm test
TEST_TONE_EVIDENCE_DIR=/tmp/wsprrypi-item13/tone-final \
  WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR=/tmp/wsprrypi-item13/support npm run test:browser
node tests/wtp_pi_fleet_ui_integration_test.js
WSPRRYPI_UI_CAPTURE=0 node tests/wtp_network_ui_integration_test.js
WSPRRYPI_RUN_RECOVERY_BROWSER=1 node tests/wtp_recovery_ui_integration_test.js \
  ../src/build/wtp-network /tmp/wsprrypi-item13/recovery-final
```

Local network/recovery rendering reuses nine explicit synthetic envelopes from
the accepted P15.4 fixture record, with hashes retained in temporary provenance.
It is browser/UI evidence only, not a fresh peer-interoperability pass. Final
Linux CI regenerates current resource envelopes through actual pinned consumers
before network/resource and recovery browser checks. The existing recovery test
is now included explicitly in that workflow instead of silently skipped.

All six final local commands above passed. The additional parent command
`python3 scripts/tests/no_transmitter_hardware_audit_test.py` passes all five
synthetic tests, and `sh -n scripts/ci/assert-no-transmitter-hardware.sh` passes. The default browser command includes
17 Test Tone workflows, integrated five-page/four-mode review (20 normal
page/theme/viewport samples plus 200% checks, 38 integrated screenshots),
conditional GPIO controls, CW duration latch and private-support intake. The
support suite uses 21 scenarios; recovery retains eight images. `npm ci` used the
lockfile, installed one pinned development dependency, and reported no
vulnerabilities. Changed JavaScript syntax/PHP lint and final `git diff --check`
passed. The final detector rerun retains exactly the same three justified console
color false positives, exit 2, valid JSON and empty stderr.

Failed intermediate checks were repaired and repeated: the audit test
PATH-stub shell lookup (fixed by invoking `/bin/sh` explicitly), description resolution,
log hover, dark tabs/inactive CW/navigation contrast, fake-DOM native event
support, 200% shell/control bounds and expanded-menu reachability. An
animation-finished wait stalled in emulation and was replaced by finite sampling
and evaluation deadlines; only the verified stalled-run Node/PHP/Chrome group
was retired. A sandbox-denied loopback launch and two wrong-directory commands
ran no tests and were repeated correctly. None of those attempts is counted as
a pass. The final unit/default-browser/Fleet/network/recovery logs all exit 0.

The reference `.github/workflows/debian-non-hardware.yml` is the exact command
inventory at the reviewed SHA. On its identified Ubuntu runner, from `src`, the
full profile runs in `debian:trixie-slim` with libgpiod development headers
and is explicitly:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
```

The same workflow also runs parent UI/source/config/API/RP1/lifecycle tests, real pinned peer
configuration/network/resource interoperability, historical compatibility,
simulated QRSS/WSPR, transmitter contracts and file-access audits. Test Tone
lifecycle uses the existing suppressed-execution typed fixture: simulated TONE
is unsupported and is not falsely claimed as executed. Simulation software
checks do not qualify GPIO/MMIO/DMA/mailbox/I2C/RF or timing.

## Documentation Impact

Updated here: this maintainer record, reproducible UI test coverage and release
checklist. F13 repairs CI evidence integrity only and changes no operator
procedure. Operator documentation is read-only. Considered current docs sections:
`User_Interface/Operations/index.md`, `User_Interface/Logs/index.md`,
`User_Interface/Maintenance/index.md`, `User_Interface/Setup/index.md`,
`User_Interface/Setup/Transmitter/index.md`, plus item 12's REST/WebSocket,
calibration and RP1 procedures. Repairs retain controls, action meanings,
configuration semantics, normal desktop ordering, qualification terminology and
all support/privacy consent boundaries. Existing component screenshots still
identify the same controls/procedures; color/readability and the log control
clearance do not make those instructions materially inaccurate. Mobile control
ordering has no positional instruction requiring correction. No blocking
operator-documentation correction or sibling write is required; optional image
refresh can accompany a future separately authorized docs update. Item 11's
image/loading/style changes remain intact.

Accepted item 11/12 limitations remain separate: bounded real-device/accessibility
coverage, degraded docs detector coverage, TeX box advisories, custom-domain TLS
and all-host Impeccable updates. No new publication requirement is invented for
the verification-only docs record.

## Reviewed implementation and closeout boundary

Final reviewed implementation:
**`d93c75c4f1490df9108856403710e692b9c2d3dd`** on `devel`.
UI repair commit `090a15a93cc2f98545615b5d9b59baeae173a4de` contains the reviewed
console and fixture changes; `d93c75c` adds only the necessary F13 CI audit/test
repair. No UI source changed between them. The fresh adversarial reviewer
independently reran all five audit tests, reviewed the three-file correction and
found no actionable code defect. The final input inventory has 131 hashes,
including both parent audit files.
Before F13, a read-only acceptance-record audit independently checked all 129 input
hashes, all 15 retained images against current captures, 20 measurement samples,
38 integrated captures, contrast minimum and documentation-impact dispositions;
it found no actionable record correction.

Local HEAD, upstream and independent `git ls-remote origin refs/heads/devel`
matched immediately after its non-force push. The two implementation commits contain the UI
component, its test fixtures/regressions, parent CI additions and F13 audit/test.
The evidence/checklist closeout preserves the authorized existing item 12 update.

[Implementation CI 38950255973](https://github.com/WsprryPi/WsprryPi/actions/runs/38950255973)
completed with all 11 jobs green. Completed-log inspection nevertheless exposed
F13: the final audit received arguments with leading spaces, printed missing-file
errors and still returned success on grep exit 2. This run proves its other tests,
including full semantics/browser/interoperability, but **does not prove the
transmitter hardware-access audit**. It is retained as `initial-ci.json`, not
substituted for corrected-audit evidence. The focused parent CI/script repair was revalidated on the corrected candidate
before marking checklist item 13 complete.
That log also contains 39 nonfatal host warnings: 10 make jobserver-mode notices
and 29 package man-page alternative notices. Informational skip messages cover
CMake compiler probes, deliberately unscheduled/zero-frequency cases, Si5351
dry-run and explicit installation opt-out; they are not omitted required test
jobs. No such message is described as hardware or installation qualification.

The retained [evidence index](item13-integrated-impeccable-evidence/README.md)
links representative conventional Chrome captures and raw detector evidence.
`reviewed-inputs.json` hashes the tracked UI component, non-hardware workflow
and both parent audit files.
The corrected candidate passed all **11 actual jobs** in
[CI 38952419761](https://github.com/WsprryPi/WsprryPi/actions/runs/38952419761).
Every job conclusion and the completed logs were inspected. The regression step
passes all five synthetic tests; the actual audit successfully inspects all five
nonempty traces with no missing-file/grep-read errors and no prohibited access
observed. `implementation-ci.json` and `corrected-audit.txt` retain that proof.
The corrected run has the same 39 nonfatal warning notices described above;
no required job is skipped, failed or cancelled. The full Linux semantics,
component/parent, UI/browser, current/historical peer interoperability, explicit
simulators and hardware-access software gates pass at this exact implementation.

The later evidence/checklist-only commit has no differences in these reviewed
inputs. Its own exact-head CI, final remote parity and issue closure are verified
before #423 closes and recorded in the [#423 closeout](https://github.com/WsprryPi/WsprryPi/issues/423)
and final handback. This record deliberately does not create another commit just
to record its own eventual identity/CI result.

### Cleanup

The lead's synthetic PHP fixture was PID 25867 on loopback port 18423. Its exact
command was verified before SIGTERM; the parent shell exited. B's overlay fixture
on 18424 was stopped and its generated directory removed. Browser test fixtures
allocate ephemeral loopback ports and isolated per-run Chrome profiles; the
scripts close their CDP connection/browser and owned children/groups in finally
blocks. Their numerical ephemeral ports are not retained. A post-suite process
inspection found none of this task's PHP/Chrome/test processes. The IAB tab
inventory is empty; no task tab or per-tab emulation remains.

Exactly the nine task-copied synthetic resource envelopes were removed after
checking their original SHA-256 provenance. Existing ignored build/UI output was
preserved. The stalled-run profile `/tmp/wsprrypi-tone-browser-37829` was removed
if present. Temporary evidence remains outside Git under `/tmp/wsprrypi-item13`;
only selected synthetic captures/JSON enter this record. Pinned ignored
`WsprryPi-UI/node_modules` remains available for ordinary testing. No cache,
private support content, skill/runtime directory or build tree is staged.

The sibling docs checkout remains clean on `devel` at `b377bc4`; no sibling write
or publication occurred. No Pi, transmitter, GPIO, I2C, physical tone, installed
application, service or target configuration was operated. No release/tag/PR,
asset upload, release merge, Beta announcement or item 14 artifact was created.

No unresolved actionable review finding remains. Local and corrected CI software
validation, visual confirmation, independent reassessment and documentation
impact are accepted for `d93c75c`. Final record-commit CI/parity and the scoped
#423 acceptance/closure are reported in its linked closeout above. #412 remains
open; no other Ready for Release issue is changed or notified. Item 14 remains
the next separately authorized gate.
