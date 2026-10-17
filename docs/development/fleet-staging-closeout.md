# Fleet staging review and obsolete-worktree closeout

Date: 2026-10-17. Repository/branch: WsprryPi `devel`.

## Outcome and provenance

All nine pending Fleet edits were already integrated by
`3b98d536bb9a32d27dbbfa5a793cb0e5630c39be` ("Stage hidden Fleet tab for Pico
controls"), an ancestor of the reviewed current `devel` head
`57220b3481b1c1e5ee4327958cc8cff60cc0add4`.

The old worktree `/Users/lbussy/GitHub/WsprryPi-Fleet`, branch
`codex/fleet-setup`, remained at
`b5cdc833587c937041fe0b67c568a2fc4704c75a`, 83 commits behind that head.
Its dirty patch and the integrated commit have identical added/removed lines
and stable patch ID `5320e40253bd10547b5874cee351109f8345effe`. Raw patch
metadata differs in blob hashes and hunk positions because the bases differ;
byte-identical patch files are not claimed. Original patch SHA-256:
`e6535eefb827c1b1bd81c412422532e6f86b0bb4cd3b73655874812c93474a93`.

The earlier task report correctly observed nine uncommitted files but did not
check their patch against current history. Treating that as missing release
integration was incorrect. This review established actual inclusion rather than
reapplying older files over later catalog, management, recovery and Type 2/3 work.
No new Fleet capability, endpoint, scheduler, discovery authority, Pico adoption
or compatibility pin is introduced here.

| Old dirty path | Disposition |
| --- | --- |
| `WsprryPi-UI/README.md` | Staging gate already integrated; stale pre-catalog capability description reconciled |
| `WsprryPi-UI/data/index.css` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/data/index.js` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/data/site.js` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/data/views/config.php` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/data/views/wtp-controls.php` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/data/wtp.js` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/tests/wtp_network_ui_integration_test.js` | Already integrated by `3b98d53`; current implementation reviewed |
| `WsprryPi-UI/tests/wtp_ui_test.js` | Already integrated by `3b98d53`; current implementation reviewed |

## Adversarial finding and repair

One P2 finding was reproduced in the current implementation: a malformed
`setup_tab` URL or saved tab value could be interpolated into a CSS selector.
`findTabTriggerBySelector()` then threw a DOM `SyntaxError` instead of falling
back safely. The genuine browser regression failed before repair with
`#fleet-pane"]`, through production `restorePersistedTabState()` and `site.js`.
Selector-shaped input and nonexistent targets are also covered.

The repair enumerates the tab list's declared tab triggers and compares their
literal persisted targets. It preserves the existing disabled/hidden guards and
uses the same target/href priority as the existing persistence helper. No URL or
storage value is executed as CSS. Invalid URL/storage values are cleared; valid
visible Pi I/O restoration still succeeds. The hidden Fleet URL remains unable
to reveal or activate its tab while the session flag is false.

A P3 documentation finding was also repaired: the UI README still described
the original single-Pico staging scope and disclaimed the catalog/selector that
current devel now provides. Its summary now matches the existing known/nearby
device, member-management and schedule controls, while retaining the hidden
session gate and explicit-action requirements. This changes no capability.

After repairs, an in-thread adversarial reassessment challenged malformed and
selector-shaped values, hidden/disabled controls, target/href selection, valid
restoration, session-only flag reset, conditional persistence, feature hiding,
draft preservation and mutation boundaries. The complete network/Fleet browser
regression passed; no unresolved actionable finding remains in this bounded
scope. This is an in-thread assessment, not an independent-agent review.

Final reviewed source SHA-256:

| Input | SHA-256 |
| --- | --- |
| `WsprryPi-UI/README.md` | `7ac7f0753f34394b6e8abf6ee1f0f561fd8298c9639796ce4983dd760e3739e0` |
| `WsprryPi-UI/data/site.js` | `e911fd48641d88d3460c8d87d0eb6669dbe3f6ee76498ab64faac01b7a243b75` |
| `WsprryPi-UI/tests/wtp_network_ui_integration_test.js` | `9612ea7cf8868377e743ce160c53651932a74803f0c897f250fb4c0ba299d7e8` |

## Validation

Passed on the repaired implementation:

- `cd WsprryPi-UI && npm test`: all configured manifest, unit, PHP and
  loopback HTTP groups passed, including WTP visibility, management and draft
  handling. No optional group was skipped by this invocation.
- `cd WsprryPi-UI && npm run test:browser`: all configured process-isolation,
  Test Tone (17 workflows/21 screenshots), integrated console (five pages, four
  modes, both themes and desktop/mobile, 200% text and contrast checks), GPIO,
  CW-duration and support-intake browser groups passed.
- `cd WsprryPi-UI && npm run test:manifest`: final packaged-UI identity checks
  after the README reconciliation passed.
- `node WsprryPi-UI/tests/wtp_network_ui_integration_test.js`: real served PHP/
  Bootstrap page with mocked API responses; malformed URL/storage regression,
  valid restore, hidden default/reset, discovery/editor draft isolation,
  carrier/resource/uncertain-write feedback and five real expected review
  confirmations passed. Desktop/mobile, both themes and a 200% CSS-zoom Fleet
  content-overflow check passed. CSS zoom is not native browser zoom.
- `cd WsprryPi-UI && node tests/wtp_pi_fleet_ui_integration_test.js`: rendered
  in-page confirmations, Escape/cancellation, hiding, stale revisions, duplicate
  prevention, local takeover choices and independent assignment fixtures passed.
- Parent `ui_source_regression_test`: built through the existing Makefile in
  retained Linux image `wsprrypi-item16-gcc13:local`, immutable image
  `sha256:20b4554bcd7bd90f07676e677e805ae121ffaf3c9de30f616dc6a898ff35519b`;
  Ubuntu 24.04.5 ARM64, GCC 13.3.0. Repository source was mounted read-only,
  network disabled and all generated output isolated. It passed.
- `node --check` on both changed JS files and final staged/unstaged whitespace
  checks passed.

The exact Linux invocation was:

```sh
docker run --rm --network none --read-only --tmpfs /tmp   --mount type=bind,src=/Users/lbussy/GitHub/WsprryPi,dst=/workspace,readonly   --mount type=bind,src=/private/tmp/wsprrypi-fleet-closeout/linux,dst=/out   --workdir /workspace/src -e WSPRRYPI_DISABLE_HARDWARE_ACCESS=1   -e WSPRRYPI_REPO_ROOT=/workspace wsprrypi-item16-gcc13:local sh -c   'make /out/bin/ui_source_regression_test BIN_DIR=/out/bin OBJ_DIR_DEBUG=/out/obj DEP_DIR=/out/dep BUILD_METADATA_DIR=/out/generated BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= && /out/bin/ui_source_regression_test'
```

No WsprryPi C++ compilation or linking ran locally on macOS. The full existing
12-job non-hardware workflow is checked on the exact pushed commit separately;
local focused checks are not described as a full local semantics pass.

The three member-resource JSON fixtures used by the network browser test were
reused from retained accepted P15.8 results; they were not regenerated as fresh
native-peer evidence. Current Pi/Pico interoperability source/pins remain
unchanged. No new peer pair or physical qualification is claimed.

## Impeccable and visual evidence

Impeccable 4.1.1 context, hardening, craft-floor and bounded finish guidance were
used. The incumbent Bootstrap/Zephyr console layout and light/dark themes remain
unchanged. The network and Fleet suites generated 100 current captures,
retained with a manifest under `/private/tmp/wsprrypi-fleet-closeout/visual`.
Representative desktop selector, mobile editor, dark member resources, 200% CSS
zoom and mobile focused confirmation images were inspected. These are
conventional Chrome captures with mocked APIs, not live target observations.
The CSS-zoom check covers Fleet content/document overflow, not full native-zoom
or global header certification. Existing screen-reader/physical-device limits
remain unchanged.

The authored-JS detector reported three existing advisory debug colors
(`#1f6feb`, `#9a6700`, `#cf222e`) outside the recorded palette. None is added or
changed by this repair; they retain the earlier integrated-review disposition.
No palette or unrelated UI cleanup was performed.

## Failed attempts and corrections

- Two documentation/fixture edit commands used the UI directory with root-
  relative paths. They refused missing paths before changing their targets; the three misplaced
  ignored JSON copies were moved to the correct output directory and the empty
  accidental directories removed. That attempt is not counted as a test pass.
- The new malformed-value browser test failed before the literal-target repair.
- The first Linux Make invocation left generated-header paths at their default
  read-only source locations. It correctly refused to write; redirecting
  `BUILD_METADATA_DIR`, objects, dependencies and binaries to `/out` fixed the
  invocation without source changes or weaker assertions.
- Headless Chrome emits existing macOS display-link diagnostics; assertions and
  fixture cleanup still pass. Logs retain those messages.

## Cleanup boundary

Preservation is complete: all nine edited tracked files and 29 ignored evidence/
build files were archived to
`/private/tmp/wsprrypi-fleet-closeout/old-fleet-worktree-files.tar.gz`, with every
archive member hash checked against its original. The original patch and a
per-file hash manifest are retained beside it.

The operator explicitly authorized removal of the old Fleet branch/worktree
once the reviewed result is committed and pushed. Its tracked modifications are
proven duplicates of integrated history. Preserve the original patch, nine-file
hash manifest and any ignored evidence/build files outside that checkout before
removing it. Verify that no new edits appeared, then remove only this worktree
and local `codex/fleet-setup` branch. Delete its remote counterpart only if one
exists and is verified integrated. Legacy and unique research/diagnostic
references remain outside this cleanup. Actual cleanup and final remote parity
are reported by the executing task after the push.

## Documentation Impact

Updated: the UI README's current Fleet capability summary, this maintainer
review and the release punchlist's item 13 affected-review reference. The functional changes are restricted to `WsprryPi-UI`;
reusable `src/` components, backend/runtime code and pins are unchanged.

Considered unchanged: valid existing navigation and operator action semantics
remain the same, and
WsprryPi-Docs `docs/User_Interface/Setup/Transmitter/index.md` already describes
selected Pico/member management. The separate Docs repository was inspected
read-only; no new step, visible control or materially inaccurate screenshot is
introduced by rejecting malformed navigation values. No operator-doc update or
publication is required for this bounded repair. Existing P15.14 and Beta-input
adoption/documentation decisions remain separate; item 16 has not started.

No installed service, target installation, GPIO, transmitter hardware or RF was
operated. No release, tag, PR, issue comment or other-conversation message was
created.
