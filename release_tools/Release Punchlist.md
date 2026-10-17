# Wsprry Pi 4.0 Release Punchlist

Selected by the maintainer on October 14, 2026. Review every saved item and
record whether it is completed, already addressed, or explicitly deferred,
with supporting evidence or a reason for deferral. Optional research and
post-release features retain their stated scope; inclusion here requires a
disposition rather than implementation before 4.0.

## Ordered release checklist

Status updated October 17, 2026. Items 1 through 15 are complete within their
recorded scopes. Accepted implementation is integrated into `devel`, and the
immutable RP1 provider release is published. Item 10, WsprryPi production
consumption and validation, is accepted for installation and idle runtime
integration. Existing documented RF qualification and its limitations are
preserved; the maintainer accepted the provider's empty `Unavailable` manifest
as the original release state and excluded target rollback rehearsals. The
[separate qualification supplement](../docs/rp1-qualification-metadata.md) records
accepted route evidence without changing those assets or application policy.
Its installed consumer, cache, independent both-route status, API/UI and support
collector are now verified on wspr5.
No new RF qualification is claimed. Items 11–12 are complete, including verified
hosted builds and publication. Item 13 is complete for reviewed implementation
`d93c75c` on `devel`, with #423 closed and final closeout CI verified;
item 14 is complete within its bounded software integration scope. The full
Pico Type 2/3 companion is integrated with affected Impeccable review,
independent adversarial repairs, exact-source compatibility and promoted-head
CI. Docs devel and its hosted default-gated build are verified. Item 15/P15.9
is closed for the supported Pico software implementation and originating
post-execution assessment. Four findings were repaired and independently
reassessed at Pico `1653034`. Pi adoption and P15.14 operator documentation
remain separate; no Pi release adoption or physical qualification is claimed.
Beta 1 preparation is item 16 and has not started.

The current milestone is **WsprryPi 4.0 Beta 1**, not final 4.0 stable
publication. The maintainer's October 16 direction is to prepare a reviewable
Beta 1 candidate and Markdown release documentation, with a hard stop after
item 16c. Items 16d–16e are later publication/announcement gates, not authorized
by this planning update. No tag, GitHub release/draft, asset upload, issue
announcement or final stable publication is to be performed now.

Status key: ✅ Complete · ⏳ In progress · ☐ Not started

1. ✅ **WTP/API maturity — Pico configuration contract (#447).** Complete.
   Establish Pico-owned preservation, partial updates, explicit resets,
   capabilities and revision handling while retaining WTP/1 job semantics.
2. ✅ **WTP/API maturity — Pi integration and compatibility (#448).** Complete.
   Adopt the update contract and verify supported version pairs, providing
   the tested handoff that unblocks further Pico development.
3. ✅ **Calibration scope decisions.** Complete. Retain current correction
   behavior and RP1's implemented 200 MHz `pll_sys` parent for 4.0. Defer direct
   PPS calibration and alternative-parent experiments beyond 4.0; neither
   absence blocks release. This closes the scope decision, not RF calibration.
   Existing Chrony source-age and source-identification concerns remain recorded
   for separate triage. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md).
4. ✅ **Test Tone lifecycle and status feedback.** Complete. Scheduler-owned
   ordering, generation fencing, watchdog cleanup and truthful execution/
   restoration feedback are implemented and software-validated. See the
   [review and evidence](../docs/development/test-tone-lifecycle-review.md).
   Physical qualification remains separate; the final integrated UI review
   is closed under item 13.
5. ✅ **WsprryPico — Implement Phase 15.4: adopt shared application, station and
   hardware resources.** Complete within the authorized software scope. Shared
   member resources, scoped saves, pin inspection and nonpersistent validation
   are adopted and software-validated. See the
   [acceptance record](../docs/development/phase15-4-shared-resources.md).
   Physical, installed-service and unrestricted security qualification remain
   separate; P15.5 concurrent web servicing is not included.
6. ✅ **Si5351 readiness failures.** Complete by explicit maintainer
   disposition after software repairs, Linux validation and two consecutive
   inhibited wspr1 I2C checks (1,000 reads each) passed. The maintainer requested
   closure if the identical repeat passed; it did. See the
   [readiness review and acceptance](../docs/development/si5351-readiness-review.md).
   Historical wspr4 `SYS_INIT` observations remain retained and unexplained;
   closure accepts the bounded I2C evidence without requiring RF measurement.
7. ✅ **RP1 release-candidate preparation.** Complete. The original unpublished
   `1.0.0` candidate was bound to RP1-GPCLK-DKMS commit
   `817f3dc6a194f4a92a7f94acf10147169e6439d6`. Release-scoped module/lifecycle
   work, packaging, manifests, hashes, reproducibility, cleanup-first
   `0.9`-to-`1.0` migration, rollback, removal, reinstall and recovery validation
   are complete. Subsequent freeze and publication are recorded in item 9.
   See the provider's
   [candidate procedure](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/817f3dc6a194f4a92a7f94acf10147169e6439d6/docs/operator/release-candidate.md)
   and [package lifecycle contract](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/817f3dc6a194f4a92a7f94acf10147169e6439d6/docs/operator/package-lifecycle.md).
8. ✅ **WsprryPi acceptance of the exact RP1 candidate.** Complete for WsprryPi
   feature commit `c4736e55e34574349d94d5e974c337760d7f78a2`, incorporating
   `devel` commit `37b41bb20b9d3824c9a555cd730b5361bd085954`, against then-unpublished
   provider candidate `817f3dc6a194f4a92a7f94acf10147169e6439d6`. All
   [11 CI jobs](https://github.com/WsprryPi/WsprryPi/actions/runs/38835065520)
   passed; 14 fresh finite-TONE conducted cases passed with zero RF retries.
   Final adversarial review has no unresolved findings in scope. See the accepted
   [maintainer requirements](https://github.com/WsprryPi/WsprryPi/blob/c4736e55e34574349d94d5e974c337760d7f78a2/docs/development/rp1-item16-acceptance.md).
   Calibrated RF frequency, power, spectrum and general hardware qualification
   are not claimed. The accepted consumer and provider repair are integrated
   into their respective `devel` branches.
9. ✅ **RP1 provider freeze and publication.** Complete.
   [RP1-GPCLK-DKMS 1.0.0](https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases/tag/v1.0.0)
   is published and immutable at provider commit
   `8c16d3c237cb1267cbedafb5da2bb74132ca5de0`; accepted consumer integration is
   on WsprryPi `devel` at `dc4044ba420d00839c79d72895e3f97e25bea99e`.
   All seven published assets matched independent rebuilds and checksums.
   Provider CI (2/2), consumer CI (11/11), 22 disposable Linux package/DKMS
   lifecycle scenarios and final adversarial review passed. Item 8's 14 conducted
   RF cases were reused with unchanged relevant executable/build inputs and
   matching Debian package members; no new target or RF operation occurred.
   WsprryPi production consumption and validation are accepted separately under item 10.
10. ✅ **WsprryPi production integration of the published RP1 release.**
    **Lead: WsprryPi.** The
    [acceptance matrix and evidence](../docs/development/rp1-item10-production-integration.md)
    bind the published immutable `1.0.0` identities, software checks, evidence
    reuse, target results and accepted exclusions. Fresh downloads, independent
    rebuilds and the actual production resolver validate the published bytes;
    development selectors and fail-closed policy remain intact. The maintained
    [release acceptance workflow](../docs/rp1-provider-acceptance.md) now checks
    production resolution explicitly. Maintainer publication guidance and
    #412/#423 criteria are reconciled; #412 remains open and #423 is closed
    under item 13. The maintainer confirmed automatic installation on
    positively identified Pi 5/CM5 with
    explicit opt-out. Operator-documentation follow-ups are recorded for item 12.
    On wspr5, consumer `8abb0d1` and published provider `8c16d3c` passed
    cleanup-first development-to-release migration, normal installation,
    neutral activation, both routes' idle selection/recovery/removal/reselection,
    and independent GPIO4/GPIO20 automatic restoration across two reboots.
    Upgraded versions remain installed with GPIO20 idle, transmission disabled
    and boot transmission set to Never. The maintainer explicitly excluded
    target rollback rehearsals; unchanged package reinstall/interruption/inverse
    coverage is retained from software and disposable lifecycle evidence, not
    claimed as fresh target tests. **Accepted scope:** installation/runtime
    integration with the published manifest unchanged (`Unavailable`, no target
    entries). Existing application band/mode RF qualification remains valid
    within its recorded scope; an empty provider manifest does not erase it.
    The separately published `qualification-v1.0.0-r1` supplement is now installed
    and verified on wspr5: application binary `8d5247a`, repaired helper `aae72f1`,
    exact pinned cache/receipt, independent GPIO4/GPIO20 evidence, detailed API,
    installed UI and support collector. Wrong-route/build and missing/invalid/stale
    evidence checks pass without changing provider authority or application policy.
    The original provider manifest remains empty. Prior migration/reboot/recovery
    evidence is retained because its bound inputs are unchanged. No new RF,
    reboot or rollback rehearsal occurred during the supplement closeout.
11. ✅ **Documentation performance and remaining Impeccable housekeeping.**
    **Lead: WsprryPi-Docs · RP1 item 19.** Complete. Implementation `bb42bc9`
    repairs authored image sizing/loading, cache invalidation and mobile
    breadcrumbs; three losslessly optimized PNGs save 367,799 bytes with decoded
    pixels and metadata preserved. Saved product/design reminders are reconciled.
    Local default/Pico builds, regression tests, focused Impeccable review and
    adversarial reassessment passed within recorded coverage and limitations.
    [RTD build 35197280](https://app.readthedocs.org/projects/wsprry-pi/builds/35197280/)
    succeeded for `dad196e706968d48b045c84a550ec4342130196b`, including HTML, PDF
    and EPUB. Canonical devel publication was verified against source asset hashes
    and image-loading attributes. See the
    [completion record](https://github.com/WsprryPi/WsprryPi-Docs/blob/8d6d037f7e1ae1bb9f0a525735ddcf5a6ee57d3a/maintainer/item11-docs-performance-review.md).
    Commit `8d6d037` updates only that record; publication of this newer record-only
    commit is not claimed. All-host Impeccable updates and custom-domain TLS
    remain separately recorded follow-ups. Consolidated operator documentation,
    integrated application review and Beta 1 preparation are separate gates.
12. ✅ **Consolidated operator-documentation update.**
    **Lead: WsprryPi-Docs · RP1 item 20.** Complete. Content commit
    `166639f21ba3e13f24db2fae97928069cc9069ab` reconciles host/member settings,
    persistence, Test Tone lifecycle/restoration, UTC versus RF calibration,
    Si5351 readiness/drive/fades, bounded inventory/cache behavior and RP1
    installation/activation/authorization/recovery. Examples and screenshots
    match the reviewed application `a2437d1`; checklist-only `587c7b1` is
    incorporated into the scope review. Qualification/band dispositions,
    navigation, Pico gating and item 11's image/style behavior are preserved.
    All four tests, strict default/Pico HTML, EPUB/LaTeX and actual PDF builds,
    schema/example/link checks and bounded Impeccable checks passed; actionable
    in-thread adversarial findings were repaired. Recorded TeX advisories,
    degraded detector coverage and accessibility/device-review limits remain.
    [RTD build 35197867](https://app.readthedocs.org/projects/wsprry-pi/builds/35197867/)
    succeeded for the exact content commit; canonical pages, copied sources,
    assets and hosted PDF/EPUB were verified. See the
    [acceptance record](https://github.com/WsprryPi/WsprryPi-Docs/blob/b377bc48bafe7310a5e5e6ef04db7a7e8ace9427/maintainer/item12-operator-docs-review.md).
    Commit `b377bc4` records verification only; publication of that newer
    record-only commit is not claimed. The documentation prerequisite for
    item 13 is satisfied, subject to its fresh candidate/prerequisite checks.
    No new hardware/RF qualification or Beta 1 readiness is claimed; integrated
    UI review and Beta preparation remain separate gates. Custom-domain TLS and
    all-host Impeccable follow-ups stay separate.
13. ✅ **Final integrated Impeccable review (#423).**
    **Lead: WsprryPi · RP1 item 21.** Complete;
    [#423 is closed with evidence](https://github.com/WsprryPi/WsprryPi/issues/423#issuecomment-6223312301)
    for reviewed implementation
    `d93c75c4f1490df9108856403710e692b9c2d3dd` on `devel` (UI repairs `090a15a`,
    followed by the fail-closed CI trace-audit repair). Prerequisite evidence
    and candidate inclusion verified items 10–12 and accepted release-scoped
    integration. #423's sequencing now permits accepted Ready for Release issues
    to remain open pending publication; #412's publication-only exception remains.
    Impeccable 4.1.1 isolated critique/audit and fresh adversarial reassessment
    completed; bounded findings were repaired and revalidated, with no unresolved
    actionable finding. All five views/four modes, representative operator states,
    desktop/mobile, both themes, keyboard/focus and 200% text were reviewed.
    Local UI/browser/Fleet/network/recovery suites and exact-implementation
    [CI 38952419761](https://github.com/WsprryPi/WsprryPi/actions/runs/38952419761)
    passed. Final evidence/checklist commit `b85ba90` passed
    [all 11 closeout CI jobs](https://github.com/WsprryPi/WsprryPi/actions/runs/38954297998).
    Planning-only commit `e3e5008` was subsequently pushed and passed
    [all 11 exact-head CI jobs](https://github.com/WsprryPi/WsprryPi/actions/runs/38966261503);
    the [follow-up verification](https://github.com/WsprryPi/WsprryPi/issues/423#issuecomment-6224818188)
    confirms #423 remains closed. At that item 13 checkpoint, all 131 reviewed
    UI/workflow/audit inputs retained their accepted hashes. Item 14 records
    the later 123 unchanged/eight affected input addendum. See the
    [maintainer acceptance record](../docs/development/item13-integrated-impeccable-review.md)
    and retained visual/detector evidence. At that item 13 checkpoint, operator
    docs remained unchanged at accepted content `166639f`/verification record
    `b377bc4`; no blocking correction was required. This UI gate does not qualify physical devices, assistive
    technology, installation, services, GPIO, timing or RF, and does not establish
    Beta publication readiness. Physical-device, actual screen-reader and native
    browser-zoom coverage remain outstanding. Later UI-affecting changes require
    affected review/documentation checks again; items 14–15 record the Pico
    Type 2/3 integration and valid-field-saving follow-ups. Item 14 records the
    completed affected integration review below; item 15's supported Pico
    implementation and originating assessment are complete within the recorded
    software scope. Pi/Beta adoption remains separate. Item 16 has not started.
14. ✅ **Pico WSPR Type 2/3 management integration and acceptance.**
    **Lead: WsprryPi · Documentation: WsprryPi-Docs.** Closed within the
    bounded software scope. The full accepted companion through `6d5e734`,
    including its implementation, tests and immutable Pico `804e8a0` pin, is
    integrated on `devel` at reviewed implementation `f6b5158`, with final
    closeout `a84010d6c16ac416d52989053ac1f273390a8ab5`. All
    [12 final feature-head CI jobs](https://github.com/WsprryPi/WsprryPi/actions/runs/38977319372)
    and [12 final devel-head jobs](https://github.com/WsprryPi/WsprryPi/actions/runs/38977334299)
    passed on that exact closeout head. Eight exact local compatibility pairs,
    genuine TLS/consumer crypto/
    resource/journal/recovery, runtime and sanitizer checks pass. Impeccable
    and fresh independent adversarial reassessment close all bounded findings;
    72 current plus 16 historical rendered captures cover both themes,
    desktop/mobile, keyboard and 200% text. Step 13's unchanged inputs and
    guarded fixtures/fail-closed trace audit are preserved.
    Separate Docs commits `7dcac4a`/`ee689f0` are promoted to Docs `devel`;
    [RTD build 35202029](https://app.readthedocs.org/projects/wsprry-pi/builds/35202029/)
    verifies exact `ee689f0`, HTML/copied sources/PDF/EPUB, and unchanged
    Pico-hidden hosted gating. Strict local Pico-enabled outputs are reviewed
    separately. No P15.9, freeze/build, release, installation, target services
    or physical RF qualification is included. The record retains the nonblocking
    Pico Type 1 rationale wording follow-up; no Pico edit or repin is included.
    See the
    [acceptance and item 13 affected-review addendum](../docs/development/item14-pico-type23-integration.md)
    and [retained execution prompt](P15.8%20Pi%20Integration%20Prompt.md).
    The evidence/test-fixture closeout changes no product code; its own final
    feature/devel CI and local/upstream/independent remote parity are verified.
    The nonblocking Pico wording defect remains separately deferred; it does
    not reopen this bounded integration gate. Item 15's Pico implementation is
    complete within its separate recorded scope; item 16 has not started.
15. ✅ **P15.9 — Save valid fields independently of transmit readiness.**
    **Lead: WsprryPi coordination · Implementation: WsprryPico.** Closed within
    the supported Pico software and originating-assessment scope. Original
    product `56821ac`/execution record `29542e4` and later P15.12 integration
    `227e73c` were reviewed in current Pico `156dfa1`; four findings were repaired
    and closed at published Pico `165303458e49feaac1de54c8344058f56578d533`.
    Valid independent fields persist as durable progress; invalid/newer browser
    edits remain visible. Applying saved settings is explicit; saving does not
    establish readiness or activate output. The originating assessment repaired
    storage-fault admission, delayed rejected/cleared-value feedback, native
    reported-power clearing and authoritative incomplete saved-state readback.
    Fresh independent reassessment found no unresolved actionable findings.
    See the [coordinating acceptance record](../docs/development/item15-pico-valid-field-saves-review.md),
    [immutable originating review](https://github.com/WsprryPi/WsprryPico/blob/165303458e49feaac1de54c8344058f56578d533/docs/development/phase15-9-post-execution-review.md),
    [historical execution evidence](https://github.com/WsprryPi/WsprryPico/blob/29542e40e131b3e0faa5be5522d220f906de4b0f/docs/development/phase15-9-valid-field-saves.md)
    and [retained prompt](P15.9%20Valid%20Field%20Saves%20Prompt.md).
    Fresh validation passed 200 executed host groups (201 registered; one
    optional schema group skipped), 13 affected ASan/UBSan groups, 43 shared
    browser assertions, 17 bootstrap Node groups, both firmware builds and
    genuine production carrier/browser workflows. Progress schema/session
    checks passed 44/35 assertions. Impeccable used 41 desktop/mobile/zoom
    captures with representative visual inspection and keyboard checks. Exact
    commands, failed attempts, warnings, skips and limits are retained in the
    review. Pages checks are publication-only; no firmware/software-test CI
    pass is claimed.
    Historical unchanged Pi pairs against Pico product `56821ac` passed for
    release `a84010d` and accepted companion `6d5e734`. They are not fresh
    compatibility evidence for later P15.12 or these repairs. Pi source,
    components, UI, tests, workflows, compatibility pins and historical rows
    were not changed; the current Pi Pico pin remains P15.8 `804e8a0`.
    **Still separate:** Pi/Beta adoption disposition, P15.14 operator
    documentation and P15.13 physical qualification. This closes the originating
    assessment; it does not establish Pi release adoption or Beta inclusion.
    Select and accept the applicable adoption/documentation inputs before any
    affected item 16b freeze. Any Pi changes or pin advance require separate
    authorization; preserve historical expectations and retain P15.8 as `p158`
    if the current pin advances, with nine compatibility rows. Item 16 has not
    started. No hardware operation occurred.
16. ☐ **WsprryPi 4.0 Beta 1 release documentation and candidate preparation.**
    **Lead: WsprryPi · RP1 item 22.** The current effort stops after 16c.
    Release-note research/drafting in 16a may proceed while item 15's remaining
    Pi/Beta adoption and documentation dispositions are resolved. Its originating
    assessment is complete. Candidate freeze/build and acceptance in 16b–16c
    require acceptance of the selected inputs and an explicit Pi/Beta adoption/
    documentation disposition; item 15's software closure does not establish
    those separate decisions.

    - ☐ **16a — Research and draft Beta 1 release documentation in Markdown.**
      Establish the previous published stable WsprryPi baseline and the intended
      Beta 1 source range. Enumerate all issues marked ready for release and
      closed issues relevant to that range, using the actual labels, project
      statuses, milestones and recorded dispositions with complete pagination.
      Reconcile every candidate issue against commit/merge history and all
      relevant internal documentation, including development/acceptance records,
      protocol/configuration contracts, component READMEs and operator-docs
      handbacks. Issue closure or a ready status alone does not prove inclusion;
      record included, duplicate, superseded, excluded and unresolved dispositions.
      Prepare `release_tools/4.0-Beta-1-Release-Notes.md` for the eventual GitHub
      prerelease body, with an auditable companion
      `release_tools/4.0-Beta-1-Release-Evidence.md` mapping claims to issues,
      commits and evidence. Cover what was added, changed and fixed; removals
      and deprecations; backward-compatibility breaks or risks; defaults, API/WTP,
      configuration, platform/backend and installation changes; upgrade/migration
      and rollback guidance; dependencies, qualification limits and known issues.
      Distinguish confirmed breaks from uncertain compatibility and state what
      the operator must do. Deduplicate overlapping fixes, retain useful issue
      links, and keep private paths/bench details out of the public notes.
      Review omissions and unsupported claims adversarially; refresh the draft
      against the exact accepted candidate before publication. These are planned
      deliverables, not files already created by this checklist update.
    - ☐ **16b — Freeze identities and build the Beta 1 candidate.** After items
      13–15, select Beta 1 version metadata using the project's existing convention,
      record exact application/documentation commits and published provider
      identities, and build/checksum candidate artifacts. Preserve the researched
      stable-to-Beta comparison range and record later changes. Any changed input
      requires a new candidate identity and repetition of affected acceptance
      checks. This step does not create a public tag or release.
    - ☐ **16c — Validate, accept and hand off Beta 1; stop before publication.**
      Run required CI/regressions and applicable installation/upgrade/service/
      runtime checks against the exact candidate and artifacts using item 10's
      acceptance matrix. Record reused evidence, repeated checks, exclusions and
      accepted limitations; repair findings and reassess. Reconcile release notes
      and compatibility guidance with the actual tested result. Produce the
      accepted candidate identities, checksums, Markdown prerelease body and
      evidence handoff, ready for a separate publication decision. Do not merge
      to the release branch, create a tag/GitHub release or draft, upload assets,
      announce availability, or claim Beta 1 is published.
    - ☐ **16d — Later: publish Beta 1 as a GitHub prerelease and verify.** Only
      after separate authorization, publish the accepted Beta 1 and corresponding
      documentation through their separate workflows. Use the reviewed Markdown
      body, mark it as a prerelease, and independently verify tag/source identities,
      downloads, checksums, notes and documentation. This is not final 4.0 stable
      publication. Do not perform 16e until actual Beta availability is verified.
    - ☐ **16e — Later: update every open issue marked ready for release.**
      After 16d, freshly enumerate the complete open ready-for-release set using
      the project's actual readiness markers. Reconcile it against the accepted
      Beta scope and update every such issue with the verified Beta 1 prerelease
      URL and accurate availability/testing guidance. Do not imply an unshipped
      fix is included. Preserve issue states; do not automatically close issues.
      Avoid duplicate announcements, record issue/comment URLs and outcomes, and
      resolve any failed or omitted updates before declaring this gate complete.

## Remaining work ownership and cross-reference

| WsprryPi item | RP1 item | Status | Work | Lead repository |
| --- | --- | --- | --- | --- |
| 10 | 18 | ✅ Complete within accepted scope | Published-provider installation/runtime integration; installed supplement and both-route status accepted; prior target passes/rollback exclusion and RF scope preserved | `WsprryPi` |
| 11 | 19 | ✅ Complete | Documentation performance and Impeccable housekeeping; hosted build/publication verified | `WsprryPi-Docs` |
| 12 | 20 | ✅ Complete | Consolidated operator documentation; exact-revision hosted build/publication verified | `WsprryPi-Docs` |
| 13 | 21 | ✅ Complete; #423 closed | Integrated application Impeccable review accepted for `d93c75c`; final closeout/planning-head CI each passed 11/11; bounded validation and qualification limits retained | `WsprryPi` |
| 14 | — | ✅ Closed within bounded software scope | Full companion integrated at final `a84010d`; feature/devel CI each passed 12/12; eight exact compatibility pairs, affected Impeccable/adversarial repairs and published Docs accepted; nonblocking Pico wording follow-up retained | `WsprryPi`; affected operator docs: `WsprryPi-Docs` |
| 15 | — | ✅ Pico software and originating assessment complete | Pico `1653034` closes four findings after fresh independent reassessment; 200 executed host groups, 13 sanitizer groups, both firmware builds and genuine browser checks passed. Pi adoption, P15.14 docs and physical qualification remain separate | `WsprryPi` coordination; `WsprryPico` implementation |
| 16 | 22 | ☐ Not started | Beta 1 release notes, candidate build and acceptance; stop after 16c; later 16d prerelease publication and 16e issue announcements | `WsprryPi` |

RP1 item numbers above are historical cross-references; the current provider
ROADMAP has no numbered application release gates.

The lead repository owns coordination and completion evidence. RP1-GPCLK-DKMS
provides provider-specific support for item 10; a provider defect requires its
own scoped repair and affected requalification. WsprryPi-Docs owns documentation
changes during items 11–16; application UI changes remain in WsprryPi. Keep
cross-repository changes, commits, pushes and publication boundaries separate.
This plan does not authorize implementation, issue edits, cross-repository writes,
hardware/service operations, commits, pushes or publication. The #412/#423
criteria reconciliation was performed under the separately authorized item 10
work. Item 13's acceptance remains bound to its reviewed candidate; items 14–15
require acceptance of their selected release integration and affected checks
before item 16's candidate freeze. Saving item 15's prompt does not execute it
or grant its separately required Pi implementation/integration authority.

## Later final 4.0 stable release

Final 4.0 stable publication is outside the current Beta 1 milestone. After
Beta feedback and any resulting changes, establish a separate final-candidate
acceptance/publication decision, repeat affected implementation, documentation,
UI, packaging and applicable runtime checks, and complete the remaining release
tracking requirements, including #412 at its proper publication boundary.
Beta preparation or announcement does not itself close that stable release gate.

## Saved items

| Saved item | Release relevance | Disposition and evidence |
| --- | --- | --- |
| **Test Tone concurrency** | Coordinate complete Start, End, user-stop, expiry, cleanup and restoration transactions, with deterministic race tests. | Complete under release checklist item 4: scheduler ordering, generation fences, nonblocking delivery and watchdog lifecycle validated. See the [review and evidence](../docs/development/test-tone-lifecycle-review.md). Physical qualification remains separate. |
| **Test Tone status duplication** | Keep the frequency preview separate from execution feedback. Include this in the final UI review. | Complete for item 4: accepted/startup/runtime/uncertain and restoration feedback, correlation tests and focused Impeccable review recorded in the [review and evidence](../docs/development/test-tone-lifecycle-review.md). Final integrated UI review (#423) is closed under item 13; changed Pico surfaces require affected review under items 14–15. |
| **Si5351 readiness failures** | The five comparison investigations were implemented, but intermittent `SYS_INIT` failures remain unresolved in the [qualification record](../docs/development/si5351-results/README.md). Successful later captures did not close that reliability question. | Complete by explicit maintainer acceptance after software repairs and two consecutive inhibited wspr1 checks passed; see [results and disposition](../docs/development/si5351-readiness-review.md). Historical failures remain preserved without claiming their cause was established. |
| **Direct PPS for GPIO frequency calibration** | Optional RF-calibration scope decision; retain Chrony-derived estimates, intrinsic defaults, manual correction and conducted residuals. | Review complete: native PPS RF calibration deferred beyond 4.0. No independent RF counter/capture benefit or new 4.0 requirement established; reopening requires an identified measurement path and measurable improvement. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#direct-pps-disposition-and-reopening-criteria). |
| **RP1 alternative clock parents** | Current output already uses nominal 200 MHz `pll_sys`; historical 50 MHz/XOSC planning is obsolete. | Reconciled: `pll_sys` implemented; genuinely alternative paths through `clk_i2s`, `clk_adc` or `clk_sys` deferred beyond 4.0, subject to ancestry/ownership and coordinated provider/userspace qualification. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#rp1-parent-disposition). |
| **Documentation performance and Impeccable housekeeping** | Build-time lazy loading now exists. Reconcile the older image-optimization, product-metadata, and host-update reminders before carrying them forward. | Complete under item 11, including measured image improvements, reminder dispositions and verified hosted publication. All-host tooling updates and custom-domain TLS remain separate follow-ups; see the pinned item 11 completion record. |

## Release gates

- ✅ **WsprryPi exact-candidate acceptance:** the source pair in item 8 passed
  software/CI and bounded conducted finite-TONE acceptance, including the
  historical provider-rejection and eight-second lifecycle follow-ups.
  Preserve the accepted scope and the ownership boundary established by
  [#412](https://github.com/WsprryPi/WsprryPi/issues/412).
- ✅ **Accepted implementation integration:** the accepted WsprryPi consumer
  and provider repair are integrated into their respective `devel` branches.
  Preserve the accepted runtime, UAPI and route behavior; repeat affected
  checks when source or artifact identities change.
- ✅ **RP1 provider freeze and publication:** the immutable
  [1.0.0 release](https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases/tag/v1.0.0)
  and all seven published assets were independently verified; exact source and
  integration identities are recorded in item 9. WsprryPi production integration
  and its 4.0 release remain separate gates.
- ✅ **WsprryPi published-provider integration:** item 10 accepts the exact
  published artifacts for installation and idle runtime integration after
  wspr5 migration, installation, neutral activation, independent both-route
  recovery/removal and automatic reboot restoration passed. Software/disposable
  lifecycle evidence is retained; target rollback rehearsals were explicitly
  excluded. The maintainer accepts the unchanged empty `Unavailable` manifest
  as the original release state. Existing RF qualification retains its recorded scope;
  the separately pinned supplement and installed consumer report verified scoped
  evidence for each route without creating a provider permission gate. No new RF
  qualification is claimed. Follow the
  [published-release contract](../docs/rp1-gpclk-dkms-installation.md#published-releases).
- ✅ **Operator documentation:** WsprryPi-Docs completed items 11–12, including
  the consolidated API/Phase 15.4, Test Tone, calibration, Si5351/inventory and
  RP1 supplement/installation/recovery guidance. Exact-revision local/hosted
  validation and publication are recorded in item 12. This satisfies the docs
  prerequisite for item 13; integrated application review and Beta 1 acceptance
  remain separate gates.
- ✅ **Final integrated Impeccable review:**
  [#423](https://github.com/WsprryPi/WsprryPi/issues/423) is closed for reviewed implementation
  `d93c75c` after production RP1 integration and release-scoped implementation/
  documentation acceptance. The [item 13 record](../docs/development/item13-integrated-impeccable-review.md)
  retains repairs, independent reassessment, exact-head CI and bounded evidence.
  Final `b85ba90` closeout and published planning-only `e3e5008` each passed
  11/11 CI jobs; unchanged reviewed inputs and closure are confirmed in the
  [follow-up verification](https://github.com/WsprryPi/WsprryPi/issues/423#issuecomment-6224818188).
  #412 stays open for publication; other ready issues await the later authorized
  publication notifications. Item 14's affected integration addendum is now
  complete; item 15's Pico implementation and originating assessment are
  complete within their recorded software scope. Pi/Beta adoption remains
  separate. Item 16 has not started.
- ✅ **Pico Type 2/3 companion integration and acceptance:** item 14's complete
  companion integration, eight current/historical pairs, affected Impeccable/
  adversarial repairs and separately published operator documentation are
  accepted within the bounded software scope. See the
  [acceptance addendum](../docs/development/item14-pico-type23-integration.md)
  for exact identities, CI, preserved evidence and the Pico wording follow-up.
- ✅ **P15.9 supported Pico implementation and originating assessment:** item 15
  closes the original `56821ac`/`29542e4` scope and four originating-review repairs
  at Pico `1653034`, after fresh independent reassessment and software validation.
  Pi remains unchanged and unadopted. See the
  [coordinating acceptance](../docs/development/item15-pico-valid-field-saves-review.md).
- ☐ **P15.9 Pi/Beta adoption and documentation disposition:** select the
  applicable software/docs inputs before affected item 16b. The originating
  assessment is closed; no new hardware operation is authorized or implied.
- ☐ **Beta 1 release documentation and readiness:** complete 16a–16c's sourced
  Markdown notes, compatibility/migration guidance, exact candidate and acceptance
  evidence. Stop with a reviewable handoff; no release or announcement is authorized.
- ☐ **Later Beta 1 publication and issue announcements:** after separate
  publication authorization, complete 16d verification before updating every
  open ready-for-release issue under 16e. Preserve issue states and audit coverage.
  Final 4.0 stable acceptance/publication remains a later separate gate.

## RP1 dependency order

Keep provider preparation, consumer acceptance, provider publication and
production consumption as separate gates:

1. **Prepare the provider candidate:** identify and lifecycle-test the exact
   RP1-GPCLK-DKMS candidate, including its source, package, UAPI, compatibility
   metadata, manifests and hashes.
2. **Accept the candidate in WsprryPi:** validate current WsprryPi `devel`
   against those exact candidate identities through the supported
   development/test path; repair application-owned integration defects.
3. **Integrate accepted implementation changes:** integrate the accepted
   WsprryPi candidate-consumer changes into `devel` and the accepted provider
   repairs into its release line before provider freeze. Repeat affected checks
   if either integrated result changes.
4. **Freeze and publish the provider:** freeze and publish the exact accepted
   RP1-GPCLK-DKMS source and immutable artifacts through its own release process.
5. **Adopt the published provider:** update WsprryPi production selection and
   consumption to the exact published artifacts and immutable identities.
6. **Validate production integration:** complete applicable installation,
   cleanup-first migration, runtime activation, route/operation, removal,
   rollback, diagnostics and recovery checks against those published artifacts.
7. **Complete documentation and final UI review:** finish documentation
   performance/housekeeping and consolidated operator documentation, then the
   final integrated Impeccable review (#423) of the settled product.
8. **Integrate the Pico Type 2/3 companion:** after item 13 closeout, complete
   item 14's separately authorized integration and compatibility checks, then
   repeat affected UI/documentation review and adversarial acceptance. Preserve
   the accepted RP1 and unchanged UI evidence; record the new candidate identities.
9. **Deliver P15.9 valid-field saves:** item 15's supported Pico implementation
   and originating assessment are complete after four repaired findings and
   fresh independent reassessment. Pi/Beta adoption/documentation dispositions
   remain separate. Accept selected inputs and repeat affected compatibility/
   UI/docs checks before freeze; Pi adoption is not yet claimed.
10. **Prepare WsprryPi Beta 1:** research all relevant ready/closed issues,
    internal documentation and commit history; write sourced Markdown release
    notes and compatibility guidance. After items 13–15 are accepted, build and
    accept the exact Beta 1 candidate, then stop before publication (16a–16c).
11. **Later publish and announce Beta 1:** after separate authorization, verify
    the GitHub prerelease and artifacts, then update every open ready-for-release
    issue with its availability link and accurate scope (16d–16e).
12. **Later final stable release:** resolve Beta feedback, repeat affected checks
    and obtain separate final 4.0 candidate/publication acceptance.

Candidate acceptance can use unreleased WsprryPi `devel`; it does not require a
published WsprryPi release. Acceptance and integration precede provider freeze;
published-provider consumption follows provider publication. The DKMS and
WsprryPi releases remain separate review and publication boundaries. RP1
completion does not complete documentation, UI, packaging or WsprryPi 4.0 gates.

A candidate change after acceptance invalidates affected identities and requires
updated provenance and repetition of affected candidate, lifecycle and consumer
acceptance checks before freeze. Follow the
[provider release contract](../docs/rp1-gpclk-dkms-installation.md#published-releases)
and preserve fail-closed identity and compatibility checks throughout.
Hardware-free tests do not establish target, GPIO, transmission, SDR or RF
qualification.

## Post-release scope

The recorded post-4.0 ordering remains
[#411](https://github.com/WsprryPi/WsprryPi/issues/411) →
[#419](https://github.com/WsprryPi/WsprryPi/issues/419) →
[#333](https://github.com/WsprryPi/WsprryPi/issues/333) →
[#444](https://github.com/WsprryPi/WsprryPi/issues/444).
Standard Feld work is deferred until after 4.0 under #444, including its
separate Si5351 implementation/qualification phase and two optional test
cleanups. These are outside the 4.0 release punchlist.
Optional RP1 clock experiments and other future features stay outside the
release-scoped provider closeout.

## Completion record

Before release, replace pending dispositions with decisions and evidence,
record the exact provider and application commits/artifacts and documentation
revision, and link final automated, visual, installation/runtime, and
applicable hardware qualification results. Preserve the distinction between
software tests and physical qualification. Live operations remain subject to
the applicable repository instructions and explicit authorization.
