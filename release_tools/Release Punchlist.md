# Wsprry Pi Release Punchlist

✅ **RC.1 publication complete.** The six Beta 7 items below remain complete.
The selected RC.1 cycle reuses published Pico RC.3 and RP1 1.0.0; it does not
publish companions or qualify hardware.

- ✅ Current Pi/Pico software support, strict parsing, explicit image planning,
  operator guidance, local production carrier recipes, and submitted-source review.
- ✅ Release-note proofreading and AI Detector review through Grammarly;
  recommended editorial changes applied without changing protected terms.
- ✅ Freeze the RC.1 source and complete exact-tag CI, four-target exports,
  executable identity, dependency, and simulated smoke checks.
- ✅ Publish and verify the matching versioned Read the Docs manual.
- ✅ Verify the six draft assets, publish the prerelease, verify public bytes
  and all four production installer consumers, and record closeout.

Beta 1 acceptance, source identities, publication, and
qualification limits remain in the [release evidence](4.0.0-Release-Evidence.md),
[source inventory](4.0-Beta-1-Source-Inventory.json), and
[candidate manifest](4.0-Beta-1-Candidate-Manifest.json).

Status key: ✅ Complete · ⏳ In progress · ▧ Not started

Latest completed cycle: [WsprryPi 4.0 RC.1](https://github.com/WsprryPi/WsprryPi/releases/tag/v4.0.0-rc.1), application `f88c951dcfcb03abf5cf236a02a2d9688ad49b57`, Pico RC.3 `b4e966797cf016f6b8ed873dbe638522fc4d81c7`, and RP1 `v1.0.0`. All nine exact-tag checks, four exports and simulated smoke checks, six-asset draft/public verification, four production installer consumers, and the versioned manual passed. ARMv6 dependency tracing used the documented emulator adapter. See the [RC.1 evidence](4.0.0-Release-Evidence.md#rc-1-publication). Final stable and hardware qualification remain separate.

Previous completed cycle: [WsprryPi 4.0 Beta 7](https://github.com/WsprryPi/WsprryPi/releases/tag/v4.0.0-beta.7), application source `3db74d7b5311d904a56f8b7ad0543bed4020e6df`, with published Pico RC.2 source `971f7bf9f8075d64ff086f777356bdd06c3d8b53` and unchanged published RP1 RC.2. All nine exact-source tag CI jobs, four executable exports, draft/public byte verification and four production installer-consumer checks passed. The [versioned manual](https://wsprry-pi.readthedocs.io/v4.0.0-beta.7-docs.1/Install/beta7.html) and [release evidence](4.0.0-Release-Evidence.md#beta-7-publication) record the selected cycle and retained qualification limits. Companion releases were reused, not republished. Final stable acceptance remains separate.

<a id="saved-items"></a>

## Completed items

1. ✅ **Increase Fleet capacity to 32 remote devices.** Consolidate the limit
   into one authoritative constant and coordinate runtime, UI, persistence,
   tests, and documentation changes. Follow the
   [implementation prompt](Fleet%2032%20Device%20Implementation%20Prompt.md),
   including repeated adversarial review and both required Pi/Pico integration
   suites.
2. ✅ **Prepare, integrate, validate, and publish the selected RP1 RC.**
   Record the selected version and immutable source revision; accept the exact
   provider candidate in WsprryPi before provider publication, and verify the
   published artifacts and their identities.
3. ✅ **Prepare, integrate, validate, and publish the selected Pico RC.**
   Record the selected version and immutable source revision, validate the
   intended Pi/Pico source pair, and verify the published firmware artifacts.
4. ✅ **Pin and validate WsprryPi against the published RP1 RC.** Pin the
   explicitly selected version and immutable source revision, verify artifact
   identities, and complete affected consumer integration checks.
5. ✅ **Pin and validate WsprryPi against the published Pico RC.** Use the
   selected RC's immutable source revision and complete both required actual
   Pi/Pico network and USB integration suites against the intended sources.
6. ✅ **Validate the final WsprryPi candidate, publish its beta and documentation,
   and verify public downloads.** Record exact application, companion, and
   documentation revisions and artifact checksums. Both companion-consumption
   gates must pass before publication; changed inputs require affected checks
   to be repeated. Verify public artifact identities and served documentation.

Steps 2–6 form a reusable release cycle. RP1 and Pico preparation may proceed
independently; each publication requires its own accepted candidate. Keep the
completed cycle's evidence and identities when starting a new cycle, and reset
statuses only for the newly selected cycle. Publication requires explicit
release authorization.

## Future final 4.0 stable release gate

These requirements apply to a future stable release, not to completion of this
punchlist. Final stable publication remains a separate gate: address Beta feedback,
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
and [release evidence](4.0.0-Release-Evidence.md) for existing acceptance
and its limits. Software tests do not establish physical or RF qualification.

This completed punchlist does not authorize implementation, issue edits, cross-repository
writes, hardware or service operations, commits, pushes, or publication.
