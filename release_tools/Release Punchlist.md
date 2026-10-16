# Wsprry Pi 4.0 Release Punchlist

Selected by the maintainer on October 14, 2026. Review every saved item and
record whether it is completed, already addressed, or explicitly deferred,
with supporting evidence or a reason for deferral. Optional research and
post-release features retain their stated scope; inclusion here requires a
disposition rather than implementation before 4.0.

## Ordered release checklist

Status updated October 16, 2026. Items 1 through 10 are complete within their
recorded scopes. Accepted implementation is integrated into `devel`, and the
immutable RP1 provider release is published. Item 10, WsprryPi production
consumption and validation, is accepted for installation and idle runtime
integration. Production RF operation remains unqualified; the maintainer accepted
the empty `Unavailable` manifest as a bounded limitation and excluded target
rollback rehearsals. Items 11–14 remain open.

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
   WsprryPi production consumption and validation remain open under item 10.
10. ✅ **WsprryPi production integration of the published RP1 release.**
    **Lead: WsprryPi · RP1 item 18.** The
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
    entries). Production RF operation remains unqualified and requires qualifying
    metadata and an appropriate provider publication. Zero RF attempts occurred.
11. ☐ **Documentation performance and remaining Impeccable housekeeping.**
    **Lead: WsprryPi-Docs · RP1 item 19.** Reconcile the saved image-optimization,
    product-metadata and host/tooling reminders; implement justified documentation
    performance and presentation changes before final rendering/screenshots.
    Use current Impeccable tooling for UI work. Any application UI repair belongs
    in a separately scoped WsprryPi change before item 13.
12. ☐ **Consolidated operator-documentation update.**
    **Lead: WsprryPi-Docs · RP1 item 20.** Incorporate item 10's impact record and
    cover settled API/Phase 15.4 resources, Test Tone, calibration boundaries,
    Si5351 disposition/drive/fade, inventory-timeout and RP1 correction,
    installation, migration and recovery behavior. Keep support claims aligned
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
    application/documentation identities and evidence before release packaging.
14. ☐ **Final WsprryPi candidate validation, packaging and 4.0 release.**
    **Lead: WsprryPi · RP1 item 22.** Complete these checkpoints in order:

    - ☐ **14a — Freeze identities and build release artifacts.** After item 13,
      settle version metadata, release notes, compatibility requirements and
      known limitations. Record exact application/documentation commits and
      published provider identities, then build and checksum the release
      artifacts. Any later change requires a new candidate identity and repetition
      of affected acceptance checks, including UI/docs review where applicable.
    - ☐ **14b — Validate the exact candidate and obtain acceptance.** Run required
      CI/regressions and applicable installation/upgrade/service/runtime checks
      against the frozen source and actual release artifacts using item 10's
      acceptance matrix. Record evidence reused, checks repeated, exclusions and
      accepted limitations; repair findings and reassess before acceptance.
    - ☐ **14c — Publish and independently verify.** After candidate acceptance
      and publication authorization, publish WsprryPi 4.0 and its corresponding
      operator-documentation revision through their separate release workflows.
      Verify published tag/commit identities, downloads, checksums and documentation
      availability, then complete release-tracking closeout, including #412.

## Remaining work ownership and cross-reference

| WsprryPi item | RP1 item | Status | Work | Lead repository |
| --- | --- | --- | --- | --- |
| 10 | 18 | ✅ Complete within accepted scope | Published-provider installation/runtime integration; target passes and bounded compatibility/rollback disposition recorded; RF unqualified | `WsprryPi` |
| 11 | 19 | ☐ Not started | Documentation performance and Impeccable housekeeping | `WsprryPi-Docs` |
| 12 | 20 | ☐ Not started | Consolidated operator documentation | `WsprryPi-Docs` |
| 13 | 21 | ☐ Not started | Final integrated application Impeccable review | `WsprryPi` |
| 14 | 22 | ☐ Not started | 14a freeze/package; 14b validate/accept; 14c publish/verify | `WsprryPi` |

The lead repository owns coordination and completion evidence. RP1-GPCLK-DKMS
provides provider-specific support for item 10; a provider defect requires its
own scoped repair and affected requalification. WsprryPi-Docs owns documentation
changes during items 11–14; application UI changes remain in WsprryPi. Keep
cross-repository changes, commits, pushes and publication boundaries separate.
This plan does not authorize implementation, issue edits, cross-repository writes,
hardware/service operations, commits, pushes or publication. The #412/#423
criteria reconciliation was performed under the separately authorized item 10
work; item 13 still requires acceptance of actual integration prerequisites.

## Saved items

| Saved item | Release relevance | Disposition and evidence |
| --- | --- | --- |
| **Test Tone concurrency** | Coordinate complete Start, End, user-stop, expiry, cleanup and restoration transactions, with deterministic race tests. | Complete under release checklist item 4: scheduler ordering, generation fences, nonblocking delivery and watchdog lifecycle validated. See the [review and evidence](../docs/development/test-tone-lifecycle-review.md). Physical qualification remains separate. |
| **Test Tone status duplication** | Keep the frequency preview separate from execution feedback. Include this in the final UI review. | Complete for item 4: accepted/startup/runtime/uncertain and restoration feedback, correlation tests and focused Impeccable review recorded in the [review and evidence](../docs/development/test-tone-lifecycle-review.md). Final integrated UI review (#423) remains pending. |
| **Si5351 readiness failures** | The five comparison investigations were implemented, but intermittent `SYS_INIT` failures remain unresolved in the [qualification record](../docs/development/si5351-results/README.md). Successful later captures did not close that reliability question. | Complete by explicit maintainer acceptance after software repairs and two consecutive inhibited wspr1 checks passed; see [results and disposition](../docs/development/si5351-readiness-review.md). Historical failures remain preserved without claiming their cause was established. |
| **Direct PPS for GPIO frequency calibration** | Optional RF-calibration scope decision; retain Chrony-derived estimates, intrinsic defaults, manual correction and conducted residuals. | Review complete: native PPS RF calibration deferred beyond 4.0. No independent RF counter/capture benefit or new 4.0 requirement established; reopening requires an identified measurement path and measurable improvement. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#direct-pps-disposition-and-reopening-criteria). |
| **RP1 alternative clock parents** | Current output already uses nominal 200 MHz `pll_sys`; historical 50 MHz/XOSC planning is obsolete. | Reconciled: `pll_sys` implemented; genuinely alternative paths through `clk_i2s`, `clk_adc` or `clk_sys` deferred beyond 4.0, subject to ancestry/ownership and coordinated provider/userspace qualification. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#rp1-parent-disposition). |
| **Documentation performance and Impeccable housekeeping** | Build-time lazy loading now exists. Reconcile the older image-optimization, product-metadata, and host-update reminders before carrying them forward. | Pending reconciliation. |

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
  for this bounded closeout. Production RF operation remains unqualified and
  requires qualifying metadata and an appropriate provider publication. Follow the
  [published-release contract](../docs/rp1-gpclk-dkms-installation.md#published-releases).
- ☐ **Operator documentation:** after production RP1 integration, reconcile
  the recorded Si5351 readiness/drive/fade, inventory-timeout, and RP1 correction
  follow-ups alongside API/Phase 15.4, Test Tone and calibration boundaries.
  WsprryPi-Docs leads items 11–12. Update and verify documentation under
  separately authorized cross-repository scope.
- ☐ **Final integrated Impeccable review:** complete
  [#423](https://github.com/WsprryPi/WsprryPi/issues/423) after production RP1
  integration and release-scoped implementation/documentation have settled on
  their integrated branches, with the application on `devel` and no unresolved
  UI-affecting changes expected. First resolve the publication-only #412 tracking
  exception in the issue criteria. WsprryPi leads item 13; repair or explicitly
  accept findings as required by that issue, then complete item 14a–14c's final
  candidate packaging, acceptance, publication and independent verification.

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
8. **Release WsprryPi:** complete final candidate validation and packaging,
   then publish Wsprry Pi 4.0 after final candidate acceptance.

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
