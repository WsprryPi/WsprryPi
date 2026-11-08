# Wsprry Pi Release Tools

These documents define selected release work, acceptance gates and maintainer
validation workflows. Completed execution handoffs are retired under the
[cleanup instructions](../docs/development/repository-cleanup-prompt.md).

- [Wsprry Pi 4.0 Release Punchlist](Release%20Punchlist.md): saved follow-ups,
  release gates, and RP1 dependency order.
- [Calibration Scope Decisions](Calibration%20Scope%20Decisions.md): RP1 direct
  PPS scope, Pi 1–4 host estimation, correction authority, and clock-parent decisions.
- [Release artifact preparation](Build%20Orchestration.md): source/version
  identity, four-target exports and the separate publication boundary.
- [4.0.0 release notes](4.0.0-Release-Notes.md): cumulative operator-facing
  draft since 3.2.1, including a separate section for actual bug fixes.
- [4.0.0 release evidence](4.0.0-Release-Evidence.md): source and issue
  dispositions, post-Beta changes, validation limits, remaining stable gates,
  and the full preserved local and published Beta 1 material.
- Historical Beta 1 preparation: [execution prompt](Item%2016%20Beta%201%20Preparation%20Prompt.md),
  [source inventory](4.0-Beta-1-Source-Inventory.json),
  [candidate manifest](4.0-Beta-1-Candidate-Manifest.json),
  [artifact checksums](4.0-Beta-1-SHA256SUMS), and the
  [published prerelease](https://github.com/WsprryPi/WsprryPi/releases/tag/v4.0.0-beta.1).
  These records retain their original identities; final 4.0.0 publication is separate.


## Shared control first-release gate

The shared Pi/Pico control contract remains at major 1 while its authoritative
Pico `engineering/protocol/control-policy.json` is in `prerelease` phase. Breaking
current-schema changes are allowed before the first shared-control stable release,
with coordinated consumers and successful production network/USB pair checks.
Per-resource version generations and historical compatibility views are unsupported.
Firmware/application SemVer and independent protocol/storage versions are unaffected.

Before publishing the first stable release that includes this control contract,
record the reviewed release transition in the Pico policy using its documented
`check_control_policy.py --record-first-release <actual-stable-version>` procedure,
then vendor that exact policy/checker into Pi and verify:

```sh
python3 scripts/generate_control_contract.py --source /path/to/WsprryPico --check --stable-release
```

The check requires released phase and the immutable canonical schema digest. Pi
non-hardware CI checks the vendored guard; when invoked on a stable SemVer tag,
it also enforces the stable-release gate. Pico stable-publication CI enforces the
same transition. An optimized `make release` candidate build is not a publication
and remains available during prerelease. Existing historical Pi tags do not mark
the first release of this new shared-control contract.
