# Wsprry Pi Backlog

This is the selected work and release-gate authority. Completed items retain
their status and disposition in this list. Beta 1 acceptance, source identities, publication, and
qualification limits remain in the [release evidence](4.0.0-Release-Evidence.md),
[source inventory](4.0-Beta-1-Source-Inventory.json), and
[candidate manifest](4.0-Beta-1-Candidate-Manifest.json).

Status key: ✅ Complete · ⏳ In progress · ▧ Not started

<a id="saved-items"></a>

## Ordered backlog

1. ▧ **Increase Fleet capacity to 32 remote devices.** Consolidate the limit
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
and [release evidence](4.0.0-Release-Evidence.md) for existing acceptance
and its limits. Software tests do not establish physical or RF qualification.

This backlog does not authorize implementation, issue edits, cross-repository
writes, hardware or service operations, commits, pushes, or publication.
