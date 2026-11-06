# RP1 provider fixture verification

The normal installer consumes RP1-DKMS `0.0.1`. See
[installation and ownership](rp1-gpclk-dkms-installation.md) for its source
selection, repeat-install, update, and removal behavior.

The retained [1.0.0](../scripts/tests/fixtures/rp1-release-1.0.0.json) and
[1.0.1](../scripts/tests/fixtures/rp1-release-1.0.1.json) metadata are inputs to
historical package-parser and qualification-attribution regression tests. They
are not current provider selections, installation requirements, or authority to
adopt an old installation. The [qualification metadata](rp1-qualification-metadata.md)
remains bound to its recorded source and artifacts.

Run the deterministic fixture checks from `src`:

```sh
make rp1-release-acceptance-test SUDO=
```

These checks exercise malformed inventories, source and UAPI mismatches,
substituted assets, checksums, and remote-identity refusals. The separate
`scripts/maintainer/accept_rp1_release.py` verifier remains explicitly bound to
its recorded fixture repository. It can rebuild and inspect those inputs without
installing a package, changing ownership records, contacting a Pi, or creating
a release. Its results do not select that provider for normal installation.

Hosted CI exercises the actual update/bootstrap boundary against an exact
RP1-DKMS source commit. Consumer regressions cover route identity, bounded work,
cancellation, ownership, service intent, and cleanup failures. Those fixtures
exercise software contracts; they do not establish installation, reboot,
hardware, timing, GPIO, or RF qualification.

An old provider is manually uninstalled before the first RP1-DKMS installation.
There is no automatic cross-repository migration or retention of that old
installation's recovery inputs.
