# RP1 provider acceptance

The maintainer tool `scripts/maintainer/accept_rp1_release.py` accepts a clean, authenticated RP1-GPIO-CLK-DKMS checkout and delegates artifact construction and independent validation to that provider's public tools. The product version comes from the provider's canonical header and public identity.

Run in a Linux Debian packaging environment with the provider's build prerequisites, a C++ compiler, and Node.js. For prepublication acceptance, identify the clean WsprryPi source supplying the proposed release UI and executable.

```sh
python3 scripts/maintainer/accept_rp1_release.py \
  --provider-source /absolute/path/to/RP1-GPIO-CLK-DKMS \
  --consumer-source /absolute/path/to/WsprryPi \
  --release \
  --output /absolute/path/outside-git/acceptance
```

Use `--candidate` for existing assets and `--compare-retained` to require byte identity with a retained candidate. With `--release`, the tool derives the drive choices from the selected Pi source and requires 2, 4, 8, and 12 mA in the UI, CLI, and independently validated provider capabilities for each of GPIO4 and GPIO20. The `rp1DriveSelection` record in `acceptance.json` identifies the Pi commit and tree, provider tag, package digest, and route results. Check those identities against the sources and package selected for release. Generic package validation can run without `--release`. Its result lacks the drive-selection record required for release acceptance.

After provider publication, `--verify-publication` repeats the drive check and requests the provider's public `release-audit` operation for the tag pinned by the selected Pi installer. If supplied, `--release-tag` must match that pin. The verified published assets must match the same independently validated local candidate byte for byte. Missing drive evidence, unsupported settings, mismatched identities, and unavailable publication stop release acceptance. Offline checks establish software compatibility. Installation, GPIO, timing, and RF qualification require separate evidence.

The normal installer source policy and cleanup behavior are described in [installation and ownership](rp1-gpclk-dkms-installation.md). The independently versioned UAPI and runtime/lifecycle interfaces must satisfy `config/rp1-provider-requirements.json`; product version numbers do not select a consumer implementation.

Run `make -C src rp1-release-acceptance-test rp1-drive-cli-test rp1-gpclk-linux-provider-test SUDO=` for the release comparison, normal CLI option parsing in the hardware-free fixture, and adapter refusal against a provider that advertises only 2 mA.
