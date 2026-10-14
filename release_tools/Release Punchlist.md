# Wsprry Pi 4.0 Release Punchlist

Selected by the maintainer on October 14, 2026. Review every saved item and
record whether it is completed, already addressed, or explicitly deferred,
with supporting evidence or a reason for deferral. Optional research and
post-release features retain their stated scope; inclusion here requires a
disposition rather than implementation before 4.0.

## Saved items

| Saved item | Release relevance | Disposition and evidence |
| --- | --- | --- |
| **Test Tone concurrency** | Deferred P3: coordinate complete Start, End, and user-stop transactions, with deterministic race tests. Current WebSocket locking addresses part of this; reassess the broader scheduler follow-up. | Pending reassessment. |
| **Test Tone status duplication** | Keep the frequency preview separate from execution feedback. Include this in the final UI review. | Pending final UI review. |
| **Si5351 readiness failures** | The five comparison investigations were implemented, but intermittent `SYS_INIT` failures remain unresolved in the [qualification record](../docs/development/si5351-results/README.md). Successful later captures did not close that reliability question. | Pending qualification disposition. |
| **Direct PPS for GPIO frequency calibration** | Revisit near release. This remains an optional design decision, with no selected implementation or mandatory release gate. Distinguish RF-clock measurement from host PPS timing and avoid double application of correction. | Pending review decision. |
| **RP1 alternative clock parents** | Saved research into `pll_sys`, `clk_i2s`, `clk_adc`, and `clk_sys`: accuracy, resolution, noise, ownership, and cleanup. Future work. | Confirm and record deferral. |
| **Documentation performance and Impeccable housekeeping** | Build-time lazy loading now exists. Reconcile the older image-optimization, product-metadata, and host-update reminders before carrying them forward. | Pending reconciliation. |

## Release gates

- [ ] **RP1 publication and candidate verification:**
  [#412](https://github.com/WsprryPi/WsprryPi/issues/412) is implementation-complete.
  Publish an eligible immutable provider release and verify WsprryPi against
  its exact artifacts. At selection of this punchlist,
  [RP1-GPCLK-DKMS had no published release](https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases).
  Complete the correction-documentation work and record the disposition of
  historical provider-rejection exception-containment and eight-second finite
  TONE failures. Cite subsequent fixes and applicable evidence first; link a
  focused regression if a defect remains. Two-second brackets alone do not
  resolve the eight-second observation.
- [ ] **Operator documentation:** reconcile the recorded Si5351
  readiness/drive/fade, inventory-timeout, and RP1 correction follow-ups in the
  separate `WsprryPi-Docs` repository. Update and verify documentation under
  separately authorized cross-repository scope.
- [ ] **Final integrated Impeccable review:** complete
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
