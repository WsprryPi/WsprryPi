# Live Fleet browser acceptance

## Result

The remaining assignment creation/editing workflow passed against the actual
installed wspr5 controller and physical wspr2/Pico A targets on October 2, 2026.
Both browser-created schedules and their edited versions completed, with
corresponding signals recorded on wspr5's SDR. All five original assignment
definitions were restored paused and survived an installed-service restart.

The campaign found and repaired a schedule editor concurrency defect. Status
polling previously advanced the revision used by an already-open draft, allowing
it to overwrite another operator's saved schedule. Drafts now retain the revision
and target they were opened against. A stale save returns HTTP 412 and preserves
the draft, with instructions to cancel, reopen and review current settings.
Opening another editor also preserves the original draft authority if the
device catalog cannot be loaded.

This closes the outstanding browser creation/editing acceptance item. Earlier
actual browser Resume, Pause/Cancel, Remove/Cancel and local takeover results
remain in the [managed Fleet report](wtp-managed-rp1-fleet-fix-report.md) and
[native recovery campaign](wtp-native-recovery-results/README.md).

## Executed scope

Work started from clean `devel` at `eb7117eeaab693ba9d94c1c8ede087786797d428`.
The only application component changed is `WsprryPi-UI`: its Fleet schedule
JavaScript and rendered regression test. C++ backends, kernel/provider code,
Pico firmware and the packaged UI manifest were unchanged.

The controller retained its existing installed binary and INI. Its older build
label is not the source identity: [restoration hashes](wtp-live-fleet-browser-results/restoration.json)
bind the installed binary, helper, INI and final repaired JavaScript. The asset
was deployed as a local modification of the existing UI installation. No new
packaged-installation or release-readiness claim is made.

| Output | Physical route | Created job | Edited job |
| --- | --- | --- | --- |
| wspr2 | GPIO4 | TONE, 14,101,100 Hz, 8 s, repeat 120 s, phase 1 s | TONE, 14,101,300 Hz, 6 s, repeat 120 s, phase 13 s |
| Pico A | GP2, `pio-dma-gp2`; firmware `3e1337074003` | TONE, 14,100,100 Hz, 8 s, repeat 120 s, phase 1 s | TONE, 14,100,300 Hz, 6 s, repeat 120 s, phase 13 s |

wspr2, Pico A and the SDR were the existing receiver group. Pico B was not used;
its saved assignment remained paused and the browser showed it offline.
wspr1/wspr4 assignments also remained paused. wspr5 local Enable remained off.
The previously approved frequency permission remained unchanged.

## Live workflow and evidence

1. Backed up the controller's configuration, catalog and assignments privately.
   Established independent read-only Pi WTP and Pico USB observations, plus a
   bounded systemd pause guard. The guard never needed to execute.
2. Reproduced the lost update with two real browser tabs and actual saved
   schedules. Network recording was enabled after this baseline reproduction;
   [baseline controller observations](wtp-live-fleet-browser-results/baseline-reproduction.json)
   establish the overwritten 9-second and subsequent 6-second definitions.
3. Repeated the conflict test after repair. Background refresh did not rebase
   the draft; the actual server rejected its POST with 412. Both desktop and
   mobile drafts retained their values and gave an actionable recovery message.
4. Removed the two backed-up paused assignments and recreated them through
   **Assign a schedule** using their saved profiles. Plain-LAN consent was
   explicitly selected and **Start after saving** remained unchecked.
5. Browser validation blocked a zero-duration save and missing consent.
   The actual server rejected an out-of-period UTC phase and a duplicate
   assignment, preserving the draft. Cancel abandoned edits without saving.
6. Used browser Resume for the created schedules, then Pause and stop,
   Edit schedule and Resume for the changed schedules. Both rounds completed.
   Pausing also canceled each preselected future job before preparation.
   Two stale pause confirmations returned 412; fresh reviewed confirmations
   succeeded. Failed confirmations were not counted as successful pauses.
7. Restored the original definitions through browser forms, restarted the
   installed service with every assignment paused, and verified persistence.
   Closed the temporary browser tabs and removed the guard/observer.

[Browser command/response excerpts](wtp-live-fleet-browser-results/browser-actions.json)
contain 24 observed Fleet POSTs with their real response statuses. Supplemental
end-of-session debugger exports had older entries evicted and are explicitly
marked truncated; this is not represented as a complete transaction ledger.
Critical creation, editing, Resume and conflict requests are retained.

## Physical results

The [four joined WTP/SDR results](wtp-live-fleet-browser-results/acceptance.json)
bind each completed job to its target, requested start, frequency and duration.
Both rounds started at 10:46:01 and 10:48:13 UTC respectively.

| Output / round | Minimum carrier over nearby noise | Median on/off contrast | Result |
| --- | ---: | ---: | --- |
| wspr2 / created | 90.51 dB | 84.26 dB | Corresponding signal observed |
| Pico A / created | 44.96 dB | 38.17 dB | Corresponding signal observed |
| wspr2 / edited | 90.89 dB | 83.39 dB | Corresponding signal observed |
| Pico A / edited | 44.04 dB | 36.89 dB | Corresponding signal observed |

The SDR retained all 112,500,000 requested complex samples, with zero overflows,
timeouts and clipping, verified receiver cleanup and exit 0. This is the agreed
corresponding-signal test. It does not claim decoding, calibrated frequency,
precise edge timing or RF-chain qualification. Pico USB INFO lacks remote
owner/job IDs; its independent active-state observations are combined with the
controller's authoritative WTP completion records rather than inventing IDs.

Raw IQ and full observations remain outside Git under
`/home/pi/wtp-live-fleet-browser-20261002`; gzip was used for the compact evidence
transfer. [Private evidence manifest](wtp-live-fleet-browser-results/private-evidence-manifest.json)
and [observer summary](wtp-live-fleet-browser-results/observer-summary.json)
record their provenance. The controller observer's one connection-refused row
occurred during the intentional service restart; neither target observer failed.

## Restoration

[Final verification](wtp-live-fleet-browser-results/restoration.json) confirms:

- All five original names, target settings, modes, durations, frequencies,
  periods, phases, management ports and revocation generations restored.
- Every assignment paused, no in-flight work, and local output off/unowned.
- Catalog, INI, installed binary and route helper preserved; canonical service
  active after restart, GPS/PPS synchronized.
- Newly consumed slot watermarks retained. Removing/recreating the selected
  assignments creates new contexts; private history was not rewound. The other
  three definitions and watermarks were unchanged.
- Fresh independent LAN proof of inactive/unowned wspr1, wspr2, wspr4 and Pico A.
  Pico B was excluded, so there is no new physical off-state claim for it.
- No campaign timer, observer or receiver process remains.

## Validation and Impeccable review

| Check | Result |
| --- | --- |
| `npm run test:unit` in `WsprryPi-UI` | Passed component unit, PHP, identity and manifest regressions |
| `node tests/wtp_pi_fleet_ui_integration_test.js` in `WsprryPi-UI` | Passed rendered mocked integration, including refreshed Create/Edit conflicts and failed editor-switch target preservation |
| `node tests/wtp_ui_test.js`; `node tests/network_safety_ui_test.js` | Passed focused regressions |
| Python AST parsing, JSON assertions, `git diff --check`, cached diff check | Passed |
| Actual browser -> installed controller -> Pi/Pico -> SDR | Both finite rounds passed |
| Full Linux/backend semantics on this change | Not rerun; only the UI component changed. CI is a separate result |

Impeccable audit/hardening preserved the incumbent layout, semantic form labels,
explicit save behavior and disabled controls. Reviewed actual desktop feedback
and a verified 390 x 844 mobile viewport with no horizontal overflow. The first
screenshot adapter scaled mobile captures incorrectly; browser compositor
captures replaced them. [Mobile editor](wtp-live-fleet-browser-results/mobile-editor.png),
[mobile conflict feedback](wtp-live-fleet-browser-results/mobile-conflict.png),
[desktop feedback](wtp-live-fleet-browser-results/desktop-conflict.jpg) and
[restored selected outputs](wtp-live-fleet-browser-results/fleet-final.png)
are the inspected live evidence. Session-only Fleet visibility was removed when
the test tabs closed; Fleet remains hidden by default.

[Adversarial review and reassessment](wtp-live-fleet-browser-results/adversarial-review.md)
records the repairs and evidence limitations. No unresolved issue remains in
this browser acceptance scope.

## Documentation Impact

- Updated: this development report, execution contract and review/evidence.
- Considered unchanged: `Wsprry_Pi_Docs/docs/User_Interface/Setup/index.md`
  describes explicit saves, paused assignment creation and paused editing;
  `docs/User_Interface/index.md` covers local takeover. The repair enforces the
  existing workflow and supplies its conflict recovery instruction inline.
- No operator documentation or screenshots require replacement for this repair.
  The separate documentation repository was not modified in this campaign.

## Remaining wider work

| Area | Remaining |
| --- | --- |
| Discovery | Link loss, address changes, multihomed recovery and confirmed residual-cache expiry on wspr5's Ethernet/WiFi interfaces |
| CI | On base `eb7117e`, network/browser, full Linux and strict I2C passed; macOS failed in the simulated Pi tone-engine test and GCC 13 was canceled. The browser repair does not resolve that separate Mac failure. New-commit CI must be assessed independently |

Eight-target physical testing remains excluded by the operator's instruction.
