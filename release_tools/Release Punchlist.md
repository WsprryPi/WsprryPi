# Wsprry Pi 4.0 Release Punchlist

Selected by the maintainer on October 14, 2026. Review every saved item and
record whether it is completed, already addressed, or explicitly deferred,
with supporting evidence or a reason for deferral. Optional research and
post-release features retain their stated scope; inclusion here requires a
disposition rather than implementation before 4.0.

## Ordered release checklist

Status updated October 15, 2026. Items 1 through 8 are complete within their
recorded scopes. Item 9 is in progress in RP1. Accepted implementation
integration remains a prerequisite to provider freeze.

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
7. ✅ **RP1 release-candidate preparation.** Complete. The exact unpublished
   `1.0.0` candidate is bound to RP1-GPCLK-DKMS commit
   `817f3dc6a194f4a92a7f94acf10147169e6439d6`. Release-scoped module/lifecycle
   work, packaging, manifests, hashes, reproducibility, cleanup-first
   `0.9`-to-`1.0` migration, rollback, removal, reinstall and recovery validation
   are complete. The candidate is not frozen or published. See the provider's
   [candidate procedure](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/817f3dc6a194f4a92a7f94acf10147169e6439d6/docs/operator/release-candidate.md)
   and [package lifecycle contract](https://github.com/WsprryPi/RP1-GPCLK-DKMS/blob/817f3dc6a194f4a92a7f94acf10147169e6439d6/docs/operator/package-lifecycle.md).
8. ✅ **WsprryPi acceptance of the exact RP1 candidate.** Complete for WsprryPi
   feature commit `c4736e55e34574349d94d5e974c337760d7f78a2`, incorporating
   `devel` commit `37b41bb20b9d3824c9a555cd730b5361bd085954`, against unpublished
   provider candidate `817f3dc6a194f4a92a7f94acf10147169e6439d6`. All
   [11 CI jobs](https://github.com/WsprryPi/WsprryPi/actions/runs/38835065520)
   passed; 14 fresh finite-TONE conducted cases passed with zero RF retries.
   Final adversarial review has no unresolved findings in scope. See the accepted
   [maintainer requirements](https://github.com/WsprryPi/WsprryPi/blob/c4736e55e34574349d94d5e974c337760d7f78a2/docs/development/rp1-item16-acceptance.md).
   Calibrated RF frequency, power, spectrum and general hardware qualification
   are not claimed. Integration of the accepted implementation branches remains
   separately authorized and pending before provider freeze.
9. ⏳ **RP1 provider freeze and publication.** In progress in RP1. Integration
   of the accepted WsprryPi feature and provider repair branch remains pending.
   After separately authorized integration and applicable revalidation, freeze and publish the
   exact immutable RP1-GPCLK-DKMS `1.0.0` source, Debian package, checksums, inventories,
   compatibility metadata, manifests and provenance. Candidate changes require
   repetition of affected candidate, lifecycle and WsprryPi acceptance checks.
10. ☐ **WsprryPi production integration of the published RP1 release.** Replace
    candidate/development selection with the published immutable provider
    identities. Validate production selection, installation, cleanup-first
    migration, runtime activation, route handling, operation, removal, rollback,
    diagnostics and failure recovery against the exact published artifacts.
11. ☐ **Documentation performance and remaining Impeccable housekeeping.**
    Reconcile reminders and finish selected image-delivery improvements before
    final documentation rendering; use current Impeccable tooling during UI work.
12. ☐ **Consolidated operator-documentation update.** Cover settled API,
    Test Tone, Si5351, inventory-timeout and RP1 correction/installation behavior.
13. ☐ **Final integrated Impeccable review (#423).** Review the settled product,
    repair actionable findings and verify affected documentation/screenshots.
14. ☐ **Final WsprryPi candidate validation, packaging and 4.0 release.** Run final
    regressions, identify artifacts, complete applicable installation/upgrade/
    runtime checks and publish after acceptance.

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
- ☐ **Accepted implementation integration:** separately integrate the accepted
  WsprryPi feature and provider repair branch before provider freeze. Preserve
  exact accepted identities and repeat affected checks if integration changes
  either accepted result. Acceptance does not itself merge either branch.
- ⏳ **RP1 provider freeze and publication:** in progress in RP1. After candidate
  acceptance and application integration, freeze and publish the exact accepted
  provider source and immutable artifacts. The prepared candidate remains unpublished;
  provider publication does not complete WsprryPi production integration or 4.0.
- ☐ **WsprryPi published-provider integration:** after provider publication,
  select and consume its exact immutable artifacts and validate production
  installation, cleanup-first migration, runtime, routes, removal, rollback,
  diagnostics and recovery. Follow the
  [published-release contract](../docs/rp1-gpclk-dkms-installation.md#published-releases).
- ☐ **Operator documentation:** after production RP1 integration, reconcile
  the recorded Si5351 readiness/drive/fade, inventory-timeout, and RP1 correction
  follow-ups in the separate `WsprryPi-Docs` repository. Update and verify
  documentation under separately authorized cross-repository scope.
- ☐ **Final integrated Impeccable review:** complete
  [#423](https://github.com/WsprryPi/WsprryPi/issues/423) after production RP1
  integration and release-scoped implementation/documentation have settled on
  `devel`, with no unresolved UI-affecting changes expected. Repair or explicitly
  accept findings as required by that issue, then complete WsprryPi's final
  candidate packaging and applicable installation/runtime checks before its
  4.0 publication.

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
