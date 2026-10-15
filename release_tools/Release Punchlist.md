# Wsprry Pi 4.0 Release Punchlist

Selected by the maintainer on October 14, 2026. Review every saved item and
record whether it is completed, already addressed, or explicitly deferred,
with supporting evidence or a reason for deferral. Optional research and
post-release features retain their stated scope; inclusion here requires a
disposition rather than implementation before 4.0.

## Ordered release checklist

Status updated October 15, 2026. Items 1, 2 and 3 are complete; item 4 is in progress.

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
4. ⏳ **Test Tone lifecycle and status feedback.** **In progress.** Address
   concurrency and status duplication together, including Start/End/user-stop/
   restoration races and distinct frequency-preview versus execution feedback.
5. ☐ **Si5351 readiness failures.** Characterize and resolve or explicitly
   disposition intermittent `SYS_INIT` failures, then repeat the necessary
   guarded-default qualification against the settled Test Tone behavior.
6. ☐ **RP1 release candidate preparation.** Finish release-scoped
   module/lifecycle work, packaging and the release pipeline; identify the
   exact candidate and its UAPI, compatibility metadata, manifests and hashes.
7. ☐ **WsprryPi acceptance of the RP1 candidate.** Verify that exact candidate
   and disposition historical provider-rejection/eight-second TONE failures;
   repair integration defects before provider freeze.
8. ☐ **RP1 publication.** Publish the immutable provider release and verify
   production selection and consumption of those exact artifacts.
9. ☐ **Documentation performance and remaining Impeccable housekeeping.**
   Reconcile reminders and finish selected image-delivery improvements before
   final documentation rendering; use current Impeccable tooling during UI work.
10. ☐ **Consolidated operator-documentation update.** Cover settled API,
    Test Tone, Si5351, inventory-timeout and RP1 correction/installation behavior.
11. ☐ **Final integrated Impeccable review (#423).** Review the settled product,
    repair actionable findings and verify affected documentation/screenshots.
12. ☐ **Final candidate validation, packaging and 4.0 release.** Run final
    regressions, identify artifacts, complete applicable installation/upgrade/
    runtime checks and publish after acceptance.

## Saved items

| Saved item | Release relevance | Disposition and evidence |
| --- | --- | --- |
| **Test Tone concurrency** | Deferred P3: coordinate complete Start, End, and user-stop transactions, with deterministic race tests. Current WebSocket locking addresses part of this; reassess the broader scheduler follow-up. | In progress under release checklist item 4. |
| **Test Tone status duplication** | Keep the frequency preview separate from execution feedback. Include this in the final UI review. | In progress under release checklist item 4; final integrated UI review remains pending. |
| **Si5351 readiness failures** | The five comparison investigations were implemented, but intermittent `SYS_INIT` failures remain unresolved in the [qualification record](../docs/development/si5351-results/README.md). Successful later captures did not close that reliability question. | Pending qualification disposition. |
| **Direct PPS for GPIO frequency calibration** | Optional RF-calibration scope decision; retain Chrony-derived estimates, intrinsic defaults, manual correction and conducted residuals. | Review complete: native PPS RF calibration deferred beyond 4.0. No independent RF counter/capture benefit or new 4.0 requirement established; reopening requires an identified measurement path and measurable improvement. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#direct-pps-disposition-and-reopening-criteria). |
| **RP1 alternative clock parents** | Current output already uses nominal 200 MHz `pll_sys`; historical 50 MHz/XOSC planning is obsolete. | Reconciled: `pll_sys` implemented; genuinely alternative paths through `clk_i2s`, `clk_adc` or `clk_sys` deferred beyond 4.0, subject to ancestry/ownership and coordinated provider/userspace qualification. See [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md#rp1-parent-disposition). |
| **Documentation performance and Impeccable housekeeping** | Build-time lazy loading now exists. Reconcile the older image-optimization, product-metadata, and host-update reminders before carrying them forward. | Pending reconciliation. |

## Release gates

- ☐ **RP1 publication and candidate verification:**
  [#412](https://github.com/WsprryPi/WsprryPi/issues/412) is implementation-complete.
  Publish an eligible immutable provider release and verify WsprryPi against
  its exact artifacts. At selection of this punchlist,
  [RP1-GPCLK-DKMS had no published release](https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases).
  Complete the correction-documentation work and record the disposition of
  historical provider-rejection exception-containment and eight-second finite
  TONE failures. Cite subsequent fixes and applicable evidence first; link a
  focused regression if a defect remains. Two-second brackets alone do not
  resolve the eight-second observation.
- ☐ **Operator documentation:** reconcile the recorded Si5351
  readiness/drive/fade, inventory-timeout, and RP1 correction follow-ups in the
  separate `WsprryPi-Docs` repository. Update and verify documentation under
  separately authorized cross-repository scope.
- ☐ **Final integrated Impeccable review:** complete
  [#423](https://github.com/WsprryPi/WsprryPi/issues/423) after release-scoped
  implementation and documentation work has settled on `devel`, with no
  unresolved UI-affecting changes expected. Repair or explicitly accept
  findings as required by that issue, then complete exact-candidate packaging
  and applicable installation/runtime checks before publication.

## RP1 dependency order

Finish the release-scoped RP1 work first, with one bounded WsprryPi acceptance
pass before provider publication:

1. **RP1-GPCLK-DKMS candidate:** finish module/lifecycle acceptance, packaging,
   and the release pipeline. Prepare an exact candidate with identified
   source, UAPI, compatibility metadata, manifests, and hashes.
2. **Cross-repository acceptance:** validate current WsprryPi `devel` against
   that exact candidate through the supported development/test path. Repair
   integration defects before the provider freeze. Changes to the candidate
   require updated identities and repetition of affected checks.
3. **RP1-GPCLK-DKMS publication:** freeze and publish the immutable provider
   release with its package, checksums, installation manifest, inventory,
   compatibility, and provenance. Keep provider and application review,
   qualification, and publication boundaries separate.
4. **WsprryPi release:** verify production selection and consumption of that
   published artifact, complete applicable installation/runtime checks and
   operator documentation, perform final #423 review, then package and release
   Wsprry Pi 4.0.

RP1 acceptance can use unreleased WsprryPi `devel`; it does not require a
published WsprryPi release. The dependent production WsprryPi release requires
the published RP1 artifact. Follow the
[provider release contract](../docs/rp1-gpclk-dkms-installation.md#published-releases)
and preserve fail-closed identity and compatibility checks throughout.

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
