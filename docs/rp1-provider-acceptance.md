# RP1 provider acceptance

The maintainer tool `scripts/maintainer/accept_rp1_release.py` accepts a clean,
authenticated RP1-DKMS checkout and delegates artifact construction and independent
validation to that provider's public tools. The product version comes from the
provider's canonical header and public identity. There is no selected historical
product version or release lock in the application acceptance policy.

Run in a Linux Debian packaging environment with the provider's build prerequisites:

```sh
python3 scripts/maintainer/accept_rp1_release.py \
  --provider-source /absolute/path/to/RP1-DKMS \
  --output /absolute/path/outside-git/acceptance
```

The new output directory contains the build log, immutable asset fingerprints,
provider validation result, and application consumer acceptance. Use `--candidate`
to validate existing assets, `--release` to require release-profile metadata, and
`--compare-retained` to require an identical retained candidate. These operations
install nothing and grant no hardware, timing, GPIO, or RF qualification.

`--verify-publication` requests the provider's public `release-audit` capability
and `verify-release` operation. `--release-tag` selects a specific published tag;
otherwise the provider selects the latest eligible release, including prereleases.
Every accepted asset must match the independently validated local candidate. Missing publication,
unsupported interfaces, different sources/assets, malformed results, and failed
provider commands stop acceptance. A release-profile build alone is not proof of
publication or production eligibility.

The normal installer source policy and cleanup behavior are described in
[installation and ownership](rp1-gpclk-dkms-installation.md). The independently
versioned UAPI and runtime/lifecycle interfaces must satisfy
`config/rp1-provider-requirements.json`; product version numbers do not select a
consumer implementation.

Historical software metadata remains fixture data for qualification attribution.
Provider package-parser rejection fixtures run in RP1-DKMS's offline suite; the
application's `make rp1-release-acceptance-test SUDO=` checks public delegation,
consumer compatibility, source/asset stability, and truthful non-installing results.
The actual cross-repository identity/runtime integration tests use an explicitly
selected provider checkout. None of these tests authorizes installation or RF.
