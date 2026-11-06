# Wsprry Pi Backlog

This is the selected work and release-gate authority. Completed items retain
their status and disposition in this list. Beta 1 acceptance, source identities, publication, and
qualification limits remain in the [release evidence](4.0-Beta-1-Release-Evidence.md),
[source inventory](4.0-Beta-1-Source-Inventory.json), and
[candidate manifest](4.0-Beta-1-Candidate-Manifest.json).

Status key: ✅ Complete · ⏳ In progress · ▧ Not started

<a id="saved-items"></a>

## Ordered backlog

1. ✅ **UI Review.** Review the current application interface using Impeccable,
   including desktop and mobile workflows. Record findings and the validation
   required for any resulting changes. This is a new review; prior Beta review
   acceptance remains bound to its recorded implementation.
2. ✅ **Direct PPS frequency calibration.** RP1 direct capture is complete.
   Pi 1–4 use the existing Chrony host-frequency estimate; optional GPS/PPS
   improves that estimate at the OS level, without application PPS capture.
   Optional Pi 5/RP1 input-only
   GPIO17 capture replaces the host-rate proxy, with bounded qualification,
   per-job correction, residual adjustment, and manual fallback. Thirty
   conducted cases on wspr5's GPIO4/GPIO20 routes passed the declared scoped
   accuracy/trim checks; full Linux, UI, and actual Pi/Pico network/USB suites
   passed, and adversarial findings are closed. Pi 1–4 direct capture remains
   outside this adapter. Merged into `devel`; the release build is installed on
   wspr5 with direct PPS enabled and qualified. No published release implied. See the
   [capture contract](../docs/direct-pps-calibration.md),
   [validation and RF evidence](../docs/direct-pps-validation.md), and
   [scope](Calibration%20Scope%20Decisions.md#direct-pps-disposition-and-acceptance-criteria).
3. ✅ **Alternative RP1 clock parents.** Bounded feasibility review complete;
   retain nominal 200 MHz `pll_sys`. No alternative-parent hardware testing
   selected or physical qualification claimed; see the [parent disposition](Calibration%20Scope%20Decisions.md#rp1-parent-disposition).
4. ✅ **[#411 — Complete selectable hardware profiles and Linux MCP2221A/Si5351 support](https://github.com/WsprryPi/WsprryPi/issues/411).**
   Implementation and scoped Ubuntu/Raspberry Pi OS bench qualification accepted;
   **Ready for Release**. Keep the issue open for its next release boundary,
   including exact release-candidate validation and publication.
5. ✅ **[#419 — Add a native macOS MCP2221A transport beneath the Si5351 backend](https://github.com/WsprryPi/WsprryPi/issues/419).**
   Native transport, web adapter selection/status, unsupported macOS
   Reboot/Shutdown capability and command admission, and operator documentation
   are implemented. Hardware acceptance stands. Final integration requires the
   complete hardware-free matrix, including both current-source Pi/Pico pairs.
   Keep the issue open; Ready for Release remains removed at the maintainer's
   direction. Release labeling and publication are separate decisions.
6. ✅ **Installer Git clone requirement.** Research complete; retain the
   current clone requirement to deliver supporting tooling, configuration,
   service templates, and web assets. Closed for now with no installer changes.
7. ✅ **All-host tooling updates.** Closed November 3, 2026, for the
   maintainer-selected hosts: this Mac (`/Users/lbussy`) and `wspr5`
   (`/home/pi`). Both hosts' user-level `.agents/skills/impeccable` and
   `.codex/skills/impeccable` installations were updated from 4.1.1 to 4.8.0,
   matching the official version endpoint and npm package at execution time.
   `npx --yes impeccable@4.8.0 update --global --yes --no-hooks` succeeded
   on both hosts and installed the platform engine (release 0.1.18).
   The current installer targets `.agents`, including when Codex is selected;
   the legacy `.codex` copies were refreshed from the installed payloads after
   retaining temporary 4.1.1 backups. All four skill metadata checks reported
   4.8.0, all four engine `--version` checks passed with 4.8.0, and all four
   `SKILL.md` SHA-256 values matched
   `fab04b31f946573b00a8559db1fc070f8ea5825254d491c2b13156e5661a86d7`.
   The CLI `check` command inspected project scope and reported no project
   installation; user-level verification used the explicit installed paths.
   Fresh agent sessions are required to load the updated skill instructions.
   Hook manifests, application code, services, and hardware were unchanged.
   Operator documentation was reviewed for impact; this maintainer-tool update
   changes no operator behavior and requires no operator-manual update.
8. ✅ **Pico Type 1 rationale wording.** Closed November 3, 2026, in
   WsprryPico `devel` commit `cb560ae8` (pushed with `[no ci]`). The explanation
   now makes six-character locator retention conditional and states that only
   the first four characters are transmitted. Four- and six-character locator
   previews passed production-handler and authenticated browser regression
   checks; WSPR planner, resource, and schema tests passed. Impeccable review
   included desktop/mobile browser captures and a detector check with no
   findings. The operator manual already describes both locator lengths
   correctly and required no change. Encoding, persistence, and protocol
   schemas are unchanged; validation was hardware-free.
9. ✅ **Pico operator documentation for independent valid-field saves.**
   Closed November 3, 2026. The Pico operator manual's `docs/configure.md`
   covers independent valid-field saves, incomplete progress, explicit
   application, and uncertain-save recovery. The historical P15.14
   documentation reference was superseded by P15.20 numbering.
10. ✅ **Review and Streamline CI.** Required software coverage is retained; the 50% reduction objective was not achieved.
11. ✅ **Review and Streamline Pico CI.**
12. ✅ **Publish new RP1 beta.**
13. ✅ **Publish new Pico beta.** Prepare, validate, and publish the next Pico
   beta under separate release authorization.
14. ▧ **Ensure WsprryPi consumes the latest RP1 beta.** Verify the latest
   published RP1-GPIO-CLK-DKMS beta, update the pinned consumer integration
   where needed, and validate the selected beta before the next WsprryPi beta.
15. ▧ **Ensure WsprryPi consumes the latest Pico devel.**
16. ▧ **Publish new beta.** Prepare, validate, and publish the next beta under
   separate release authorization.

## Final 4.0 stable release gate

Final stable publication remains a separate gate: address Beta feedback,
accept the exact final candidate, repeat affected implementation,
operator-documentation, UI, packaging, and applicable runtime checks, and
complete [#412](https://github.com/WsprryPi/WsprryPi/issues/412) at its proper
publication boundary. Beta acceptance does not establish final stable readiness.

## RP1 dependency order

For future changes, preserve the provider/consumer sequence: prepare the exact
provider candidate, accept it in WsprryPi, integrate accepted changes in both
repositories, freeze and publish the provider, adopt the published artifacts,
and validate affected production integration before application release.
Changed candidates require updated identities and affected requalification.
See the [published-release contract](../docs/rp1-gpclk-dkms-installation.md#published-releases)
and [release evidence](4.0-Beta-1-Release-Evidence.md) for existing acceptance
and its limits. Software tests do not establish physical or RF qualification.

This backlog does not authorize implementation, issue edits, cross-repository
writes, hardware or service operations, commits, pushes, or publication.
