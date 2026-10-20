# Wsprry Pi 4.0 Release Punchlist

This is the selected work and release-gate authority. Items 1–15 are complete
within their accepted scopes. Item 16 remains in progress: 16a–16c are complete,
with 16c accepted under its specific physical-test limitations and documented
service-request cap exception. Publication and announcements in 16d/16e remain
not started and require separate authorization. Cleanup changes no selected
requirement, priority, candidate identity or publication authority.

The current milestone is **WsprryPi 4.0 Beta 1**. Work stops after item 16c.
Items 16d–16e require separate publication authorization. No tag, release/draft,
asset upload, release-branch merge, issue announcement or final stable publication
is authorized by preparation or cleanup.

The [Beta 1 evidence](4.0-Beta-1-Release-Evidence.md),
[source inventory](4.0-Beta-1-Source-Inventory.json) and
[candidate manifest](4.0-Beta-1-Candidate-Manifest.json) bind the selected source,
accepted reuse and remaining gates. Completed development narratives and raw
campaign outputs are retired under the
[cleanup instructions](../docs/development/repository-cleanup-prompt.md).

## Ordered release checklist

Status key: ✅ Complete · ⏳ In progress · ☐ Not started

1. ✅ **WTP/API maturity — Pico configuration contract (#447).** Pico-owned
   preservation, partial updates, explicit resets, capabilities and revision
   handling retain WTP/1 job semantics.
2. ✅ **WTP/API maturity — Pi integration and compatibility (#448).** Adopt the
   update contract and supported version pairs. See the
   [configuration contract](../docs/wtp-configuration-compatibility.md).
3. ✅ **Calibration scope decisions.** Keep current correction behavior and
   RP1's nominal 200 MHz `pll_sys` parent for 4.0. Direct PPS calibration and
   alternative parents are deferred beyond 4.0. Chrony source-age and identity
   concerns retain separate triage. See [scope and reopening criteria](Calibration%20Scope%20Decisions.md).
4. ✅ **Test Tone lifecycle and status feedback.** Scheduler-owned ordering,
   generation fences, watchdog cleanup and truthful execution/restoration
   feedback are accepted in software. Physical qualification remains separate.
   See the [current contract](../docs/bounded-tone.md).
5. ✅ **Shared Pico application, station and hardware resources.** Scoped saves,
   pin inspection and nonpersistent validation are adopted. Physical,
   installed-service and unrestricted security qualification remain separate;
   concurrent Pico web servicing is outside this item's scope. See the
   [member-resource contract](../docs/wtp-browser-api.md#shared-pico-member-resources).
6. ✅ **Si5351 readiness failures.** Accepted by maintainer disposition after
   software repairs and two consecutive inhibited wspr1 checks of 1,000 reads.
   Earlier wspr4 SYS_INIT failures remain unexplained. The acceptance does not
   require or establish RF measurement. See [current limits](../docs/si5351-support.md).
7. ✅ **RP1 release-candidate preparation.** Provider candidate
   `817f3dc6a194f4a92a7f94acf10147169e6439d6` completed release-scoped lifecycle,
   packaging, reproducibility, cleanup-first migration, removal and recovery work.
8. ✅ **WsprryPi acceptance of the exact RP1 candidate.** Consumer
   `c4736e55e34574349d94d5e974c337760d7f78a2` against provider `817f3dc6` was
   accepted for its bounded software and finite-TONE scope. Calibrated RF
   frequency, power, spectrum and general hardware qualification are not claimed.
9. ✅ **RP1 provider freeze and publication.** Immutable
   [1.0.0](https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases/tag/v1.0.0) is
   published at `8c16d3c237cb1267cbedafb5da2bb74132ca5de0`; consumer integration
   is `dc4044ba420d00839c79d72895e3f97e25bea99e`. The seven software assets and
   separate pinned qualification supplement retain their identities.
10. ✅ **WsprryPi production integration of the published RP1 release.**
    Installation and idle runtime integration, neutral activation, both routes'
    selection/recovery and reboot restoration are accepted within their tested
    scope. Automatic selection applies only to positively identified Pi 5/CM5
    with explicit opt-out. Target rollback rehearsals were excluded; the
    provider's original empty `Unavailable` manifest remains accepted unchanged.
    The supplement conveys scoped evidence rather than permission to transmit.
    See [release acceptance](../docs/rp1-provider-acceptance.md).
11. ✅ **Documentation performance and remaining Impeccable housekeeping.**
    Image optimization, product metadata/reminder dispositions and hosted
    publication are accepted. All-host tooling updates and custom-domain TLS
    retain their separate follow-ups.
12. ✅ **Consolidated operator-documentation update.** The independent Docs
    repository covers shared resources, Test Tone, calibration, Si5351/inventory,
    RP1 supplement, installation and recovery. Documentation acceptance does
    not establish application or physical qualification.
13. ✅ **Final integrated Impeccable review (#423).** Closed for reviewed
    implementation `d93c75c`, with affected follow-up under items 14–15.
    #412 remains open for its publication boundary. New implementation changes
    require affected validation; preparation does not publish a release.
14. ✅ **Pico WSPR Type 2/3 management integration and acceptance.** Complete
    within the bounded software integration and operator-documentation scope.
    The nonblocking Pico Type 1 rationale wording follow-up remains deferred.
    See [versioned WSPR formats](../docs/wtp-configuration-compatibility.md#versioned-wspr-formats).
15. ✅ **Save valid fields independently of transmit readiness.** The Pico
    implementation and originating assessment are accepted at `1653034`.
    Valid fields persist independently, invalid/newer drafts remain visible,
    and application is explicit. Pi/Beta adoption is implemented under item 16;
    Pico's broader operator-documentation and physical qualifications retain
    their separate scope. See [station progress](../docs/wtp-browser-api.md#independent-saved-station-progress).
16. ⏳ **WsprryPi 4.0 Beta 1 release documentation and candidate preparation.**
    **Lead: WsprryPi · RP1 item 22.** The current effort stops after 16c.
    Release-note research/drafting in 16a may proceed while item 15's remaining
    Pi/Beta adoption and documentation dispositions are resolved. Its originating
    assessment is complete. Candidate freeze/build and acceptance in 16b–16c
    require acceptance of the selected inputs and an explicit Pi/Beta adoption/
    documentation disposition; item 15's software closure does not establish
    those separate decisions.

    Execution: [comprehensive prompt](Item%2016%20Beta%201%20Preparation%20Prompt.md),
    [release notes draft](4.0-Beta-1-Release-Notes.md),
    [evidence and acceptance gates](4.0-Beta-1-Release-Evidence.md), and
    [complete source inventory](4.0-Beta-1-Source-Inventory.json).
    The user selected the Pi editor changes and separately authorized the Docs
    update, validation, commit and push. Historical physical evidence is reused
    at its recorded identities. The separate October 20 finite wspr5 authority
    and stop-on-failure amendments are recorded in the evidence.

    - ✅ **16a — Research and draft Beta 1 release documentation in Markdown.**
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
      against the exact accepted candidate before publication. The linked draft,
      evidence and inventory now record the complete research and dispositions.
    - ✅ **16b — Freeze identities and build the Beta 1 candidate.** After items
      13–15, select Beta 1 version metadata using the project's existing convention,
      record exact application/documentation commits and published provider
      identities, and build/checksum candidate artifacts. Preserve the researched
      stable-to-Beta comparison range and record later changes. Any changed input
      requires a new candidate identity and repetition of affected acceptance
      checks. Frozen source is `3decc07`, Docs `5214cdf`; all four exports and
      checksums are in the [candidate manifest](4.0-Beta-1-Candidate-Manifest.json).
      This step does not create a public tag or release.
    - ✅ **16c — Validate, accept and hand off Beta 1; stop before publication.**
      Run required CI/regressions and applicable installation/upgrade/service/
      runtime checks against the exact candidate and artifacts using item 10's
      acceptance matrix. Record reused evidence, repeated checks, exclusions and
      accepted limitations; repair findings and reassess. Reconcile release notes
      and compatibility guidance with the actual tested result. Produce the
      accepted candidate identities, checksums, Markdown prerelease body and
      evidence handoff, ready for a separate publication decision. Do not merge
      to the release branch, create a tag/GitHub release or draft, upload assets,
      announce availability, or claim Beta 1 is published.
      **Current disposition:** all functional target acceptance passed. The
      restored predecessor upgrade at `809ef2d6`, repeat at `cea6ac3`, replacement
      explicit restart at `cea6ac3`, and final UI installation at `4ea48c66` each passed
      full five-minute idle checks. Original executable source remains `3decc072`;
      final support/UI is `4ea48c66`. Custom settings and disabled admission/output
      are preserved. Independent reassessment found no remaining scoped finding.
      The operator accepted the exact clean-install/other-three-physical-target/
      reboot/fresh-GPIO4 limitations and separately the documented 20-versus-18
      service-request nonconformance. This does not rewrite the approved limit.
      Exact-support CI `39420958734` passed 14/14, as did frozen-source CI.
      16c is accepted with those specific limitations and the documented cap
      exception. Final documentation-head CI is verified after the scoped record
      push and reported separately. 16d/16e remain not started.
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
| 16 | 22 | ⏳ In progress | 16a–16c complete: original `3decc07` exports and supporting `4ea48c66` each have 14/14 CI passes; finite wspr5 acceptance passed, specific physical-test limitations and documented service-request cap exception accepted. 16d/16e publication remains not started | `WsprryPi` |

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
| **Test Tone concurrency** | Coordinate complete Start, End, user-stop, expiry, cleanup and restoration transactions, with deterministic race tests. | Complete under release checklist item 4: scheduler ordering, generation fences, nonblocking delivery and watchdog lifecycle validated. See the [current contract](../docs/bounded-tone.md). Physical qualification remains separate. |
| **Test Tone status duplication** | Keep the frequency preview separate from execution feedback. Include this in the final UI review. | Complete for item 4: accepted/startup/runtime/uncertain and restoration feedback, correlation handling and focused Impeccable review are accepted; see the [current behavior](../docs/bounded-tone.md). Final integrated UI review (#423) is closed under item 13; changed Pico surfaces require affected review under items 14–15. |
| **Si5351 readiness failures** | The five comparison investigations were implemented, but intermittent `SYS_INIT` failures remain unresolved in the [current contract](../docs/si5351-support.md). Successful later captures did not close that reliability question. | Complete by explicit maintainer acceptance after software repairs and two consecutive inhibited wspr1 checks passed; see [results and disposition](../docs/si5351-support.md). The cause of historical failures remains unestablished. |
| **Direct PPS for GPIO frequency calibration** | Optional RF-calibration scope decision; retain Chrony-derived estimates, intrinsic defaults, manual correction and conducted residuals. | Review complete: native PPS RF calibration deferred beyond 4.0. No independent RF counter/capture benefit or new 4.0 requirement established; reopening requires an identified measurement path and measurable improvement. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#direct-pps-disposition-and-reopening-criteria). |
| **RP1 alternative clock parents** | Current output already uses nominal 200 MHz `pll_sys`; historical 50 MHz/XOSC planning is obsolete. | Reconciled: `pll_sys` implemented; genuinely alternative paths through `clk_i2s`, `clk_adc` or `clk_sys` deferred beyond 4.0, subject to ancestry/ownership and coordinated provider/userspace qualification. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#rp1-parent-disposition). |
| **Documentation performance and Impeccable housekeeping** | Build-time lazy loading now exists. Reconcile the older image-optimization, product-metadata, and host-update reminders before carrying them forward. | Complete under item 11, including measured image improvements, reminder dispositions and verified hosted publication. All-host tooling updates and custom-domain TLS remain separate follow-ups; see the [active release evidence](4.0-Beta-1-Release-Evidence.md). |

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
  documentation acceptance. The [active release evidence](4.0-Beta-1-Release-Evidence.md)
  binds selected source identities, accepted reuse and remaining gates.
  Final `b85ba90` closeout and published planning-only `e3e5008` each passed
  11/11 CI jobs; unchanged reviewed inputs and closure are confirmed in the
  [follow-up verification](https://github.com/WsprryPi/WsprryPi/issues/423#issuecomment-6224818188).
  #412 stays open for publication; other ready issues await the later authorized
  publication notifications. Item 14's affected integration addendum is now
  complete; item 15's Pico implementation and originating assessment are
  complete within their recorded software scope. Pi/Beta adoption is now
  implemented under item 16; its source/build handoff is complete, while final
  candidate installation/service/runtime acceptance remains open.
- ✅ **Pico Type 2/3 companion integration and acceptance:** item 14's complete
  companion integration, eight current/historical pairs, affected Impeccable/
  adversarial repairs and separately published operator documentation are
  accepted within the bounded software scope. See the
  [current contract](../docs/wtp-configuration-compatibility.md)
  for current version negotiation and source-pair limits. The selected candidate
  and acceptance disposition remain in the Beta 1 evidence.
- ✅ **P15.9 supported Pico implementation and originating assessment:** item 15
  closes the original `56821ac`/`29542e4` scope and four originating-review repairs
  at Pico `1653034`, after fresh independent reassessment and software validation.
  The later Pi adoption is tracked separately under item 16. See the
  [current contract](../docs/wtp-configuration-compatibility.md).
- ✅ **P15.9 Pi/Beta adoption and documentation disposition:** the user selected
  Pi station-progress adoption against `1653034`; P15.8 remains `p158`. The Pi
  operator workflow is updated in Docs `5214cdf`. Pico P15.14 and physical
  qualification remain separate; no new hardware operation is authorized.
- ⏳ **Beta 1 release documentation and readiness:** 16a/16b's sourced notes,
  compatibility guidance, exact candidate, four exports and software validation
  are complete. 16c's physical acceptance disposition remains open. The evidence
  handoff records reused checks and remaining gates; publication is separate.
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
