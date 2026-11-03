# Wsprry Pi Backlog

This is the selected work and release-gate authority. Completed items retain
their status and disposition in this list. Beta 1 acceptance, source identities, publication, and
qualification limits remain in the [release evidence](4.0-Beta-1-Release-Evidence.md),
[source inventory](4.0-Beta-1-Source-Inventory.json), and
[candidate manifest](4.0-Beta-1-Candidate-Manifest.json).

Status key: ✅ Complete · ⏳ In progress · ▧ Not started

<a id="saved-items"></a>

## Ordered backlog

1. ▧ **UI Review.** Review the current application interface using Impeccable,
   including desktop and mobile workflows. Record findings and the validation
   required for any resulting changes. This is a new review; prior Beta review
   acceptance remains bound to its recorded implementation.
2. ▧ **Direct PPS frequency calibration.** Retain current
    correction behavior. Reopening requires an identified measurement path and
    measurable improvement; see the [scope and reopening criteria](Calibration%20Scope%20Decisions.md#direct-pps-disposition-and-reopening-criteria).
3. ✅ **Alternative RP1 clock parents.** Bounded feasibility review complete;
   retain nominal 200 MHz `pll_sys`. No alternative-parent hardware testing
   selected or physical qualification claimed; see the [parent disposition](Calibration%20Scope%20Decisions.md#rp1-parent-disposition).
4. ⏳ **[#411 — Complete selectable hardware profiles and Linux MCP2221A/Si5351 support](https://github.com/WsprryPi/WsprryPi/issues/411).**
   First in the retained post-release queue.
5. ▧ **[#419 — Add a native macOS MCP2221A transport beneath the Si5351 backend](https://github.com/WsprryPi/WsprryPi/issues/419).**
   Follows #411.
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
8. ▧ **Pico Type 1 rationale wording.** Address the deferred, nonblocking
   wording follow-up from Pico WSPR Type 2/3 integration.
9. ▧ **Pico P15.14 operator documentation.** Complete the separate Pico
   operator-documentation follow-up for independent valid-field saves.
10. ▧ **Publish new Pico beta.** Prepare, validate, and publish the next Pico
   beta under separate release authorization.
11. ▧ **Publish new beta.** Prepare, validate, and publish the next beta under
   separate release authorization.
12. ▧ **Post-4.0: [#333 — Research Spike: Advanced Scheduling](https://github.com/WsprryPi/WsprryPi/issues/333).**
   Follows #419.
13. ▧ **Post-4.0: [#444 — Track Standard Feld-Hell implementation and qualification](https://github.com/WsprryPi/WsprryPi/issues/444).**
    Follows #333. Includes its separate Si5351 implementation and qualification
    phase and two optional test cleanups.
14. ▧ **Deferred beyond 4.0: Custom-domain TLS.** Address the separate
    documentation custom-domain TLS follow-up.

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
