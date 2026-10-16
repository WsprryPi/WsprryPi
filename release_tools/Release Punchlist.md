# Wsprry Pi 4.0 Release Punchlist

Selected by the maintainer on October 14, 2026. Review every saved item and
record whether it is completed, already addressed, or explicitly deferred,
with supporting evidence or a reason for deferral. Optional research and
post-release features retain their stated scope; inclusion here requires a
disposition rather than implementation before 4.0.

## Ordered release checklist

Status updated October 16, 2026. Items 1 through 11 are complete within their
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
No new RF qualification is claimed. Item 11 is complete, including verified
hosted build and publication. Item 12 is in progress in WsprryPi-Docs;
items 13–14 have not started.

The current milestone is **WsprryPi 4.0 Beta 1**, not final 4.0 stable
publication. The maintainer's October 16 direction is to prepare a reviewable
Beta 1 candidate and Markdown release documentation, with a hard stop after
item 14c. Items 14d–14e are later publication/announcement gates, not authorized
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
   Physical qualification and the final integrated UI review remain open.
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
    #412/#423 criteria are reconciled; both issues remain open. The maintainer
    confirmed automatic installation on positively identified Pi 5/CM5 with
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
    remain separately recorded follow-ups. Items 12–14 remain open.
12. ⏳ **Consolidated operator-documentation update.**
    **Lead: WsprryPi-Docs · RP1 item 20.** In progress. Incorporate item 10's impact record and
    cover settled API/Phase 15.4 resources, Test Tone, calibration boundaries,
    Si5351 disposition/drive/fade, inventory-timeout and RP1 correction,
    installation, migration and recovery behavior. Include supplement cache/refresh,
    evidence-status meanings, structural compatibility versus external qualification,
    the installed consumer diagnostic and frozen packaged diagnostic limitations;
    preserve the application band table. Keep support claims aligned
    with the acceptance matrix. Build/render the documentation, verify links and
    replace materially inaccurate screenshots after item 11's changes settle.
13. ☐ **Final integrated Impeccable review (#423).**
    **Lead: WsprryPi · RP1 item 21.** Begin after items 10–12 and release-scoped
    implementation are accepted on their respective integrated branches, with
    the application candidate on `devel` and no expected UI-affecting changes.
    Confirm the publication-only tracking exception from item 10 is reflected
    in the issue criteria before starting. Review the complete settled product,
    repair actionable findings, explicitly disposition accepted findings and
    repeat affected tests/visual checks. Coordinate only affected documentation
    and screenshot repairs with WsprryPi-Docs. Record the exact reviewed
    application/documentation identities and evidence before Beta 1 packaging.
    Acceptance applies to that reviewed candidate; later Beta/stable changes
    require repetition of affected UI/documentation checks.
14. ☐ **WsprryPi 4.0 Beta 1 release documentation and candidate preparation.**
    **Lead: WsprryPi · RP1 item 22.** The current effort stops after 14c.
    Release-note research/drafting in 14a may proceed alongside items 12–13;
    candidate freeze/build and acceptance in 14b–14c follow item 13.

    - ☐ **14a — Research and draft Beta 1 release documentation in Markdown.**
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
    - ☐ **14b — Freeze identities and build the Beta 1 candidate.** After item 13,
      select Beta 1 version metadata using the project's existing convention,
      record exact application/documentation commits and published provider
      identities, and build/checksum candidate artifacts. Preserve the researched
      stable-to-Beta comparison range and record later changes. Any changed input
      requires a new candidate identity and repetition of affected acceptance
      checks. This step does not create a public tag or release.
    - ☐ **14c — Validate, accept and hand off Beta 1; stop before publication.**
      Run required CI/regressions and applicable installation/upgrade/service/
      runtime checks against the exact candidate and artifacts using item 10's
      acceptance matrix. Record reused evidence, repeated checks, exclusions and
      accepted limitations; repair findings and reassess. Reconcile release notes
      and compatibility guidance with the actual tested result. Produce the
      accepted candidate identities, checksums, Markdown prerelease body and
      evidence handoff, ready for a separate publication decision. Do not merge
      to the release branch, create a tag/GitHub release or draft, upload assets,
      announce availability, or claim Beta 1 is published.
    - ☐ **14d — Later: publish Beta 1 as a GitHub prerelease and verify.** Only
      after separate authorization, publish the accepted Beta 1 and corresponding
      documentation through their separate workflows. Use the reviewed Markdown
      body, mark it as a prerelease, and independently verify tag/source identities,
      downloads, checksums, notes and documentation. This is not final 4.0 stable
      publication. Do not perform 14e until actual Beta availability is verified.
    - ☐ **14e — Later: update every open issue marked ready for release.**
      After 14d, freshly enumerate the complete open ready-for-release set using
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
| 12 | 20 | ⏳ In progress | Consolidated operator documentation | `WsprryPi-Docs` |
| 13 | 21 | ☐ Not started | Integrated application Impeccable review for the Beta 1 candidate | `WsprryPi` |
| 14 | 22 | ☐ Not started | Beta 1 release notes, candidate build and acceptance; stop after 14c; later 14d prerelease publication and 14e issue announcements | `WsprryPi` |

RP1 item numbers above are historical cross-references; the current provider
ROADMAP has no numbered application release gates.

The lead repository owns coordination and completion evidence. RP1-GPCLK-DKMS
provides provider-specific support for item 10; a provider defect requires its
own scoped repair and affected requalification. WsprryPi-Docs owns documentation
changes during items 11–14; application UI changes remain in WsprryPi. Keep
cross-repository changes, commits, pushes and publication boundaries separate.
This plan does not authorize implementation, issue edits, cross-repository writes,
hardware/service operations, commits, pushes or publication. The #412/#423
criteria reconciliation was performed under the separately authorized item 10
work; item 13 still requires acceptance of actual integration prerequisites.

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
| **Test Tone status duplication** | Keep the frequency preview separate from execution feedback. Include this in the final UI review. | Complete for item 4: accepted/startup/runtime/uncertain and restoration feedback, correlation tests and focused Impeccable review recorded in the [review and evidence](../docs/development/test-tone-lifecycle-review.md). Final integrated UI review (#423) remains pending. |
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
- ⏳ **Operator documentation:** in progress in WsprryPi-Docs. Reconcile
  the recorded Si5351 readiness/drive/fade, inventory-timeout, and RP1 correction
  follow-ups alongside API/Phase 15.4, Test Tone and calibration boundaries.
  WsprryPi-Docs leads items 11–12. Update and verify documentation under
  separately authorized cross-repository scope.
- ☐ **Final integrated Impeccable review:** complete
  [#423](https://github.com/WsprryPi/WsprryPi/issues/423) after production RP1
  integration and release-scoped implementation/documentation have settled on
  their integrated branches, with the application on `devel` and no unresolved
  UI-affecting changes expected. Confirm the publication-only #412 tracking
  exception in the issue criteria. WsprryPi leads item 13; repair or explicitly
  accept findings as required by that issue, then complete Beta 1 candidate
  packaging and acceptance under 14b–14c. Release-note research may begin earlier.
- ☐ **Beta 1 release documentation and readiness:** complete 14a–14c's sourced
  Markdown notes, compatibility/migration guidance, exact candidate and acceptance
  evidence. Stop with a reviewable handoff; no release or announcement is authorized.
- ☐ **Later Beta 1 publication and issue announcements:** after separate
  publication authorization, complete 14d verification before updating every
  open ready-for-release issue under 14e. Preserve issue states and audit coverage.
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
8. **Prepare WsprryPi Beta 1:** research all relevant ready/closed issues,
   internal documentation and commit history; write sourced Markdown release
   notes and compatibility guidance. After the integrated review, build and
   accept the exact Beta 1 candidate, then stop before publication (14a–14c).
9. **Later publish and announce Beta 1:** after separate authorization, verify
   the GitHub prerelease and artifacts, then update every open ready-for-release
   issue with its availability link and accurate scope (14d–14e).
10. **Later final stable release:** resolve Beta feedback, repeat affected checks
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
