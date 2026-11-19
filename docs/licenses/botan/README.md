# Botan PAKE build dependency

The phase-6.2 candidate uses Botan only for the approved SPAKE2+ exchange and its cryptographic dependencies. OpenSSL and Mbed TLS retain their existing TLS roles. The application candidate links this provider; phase-6.2 software qualification and operator integration passed for the retained source pair.

[`scripts/pairing_provider.json`](../../../scripts/pairing_provider.json) owns the source revision, archive checksum, requested modules, and license identifier. The upstream [BSD-2-Clause license](BSD-2-Clause.txt) must accompany any binary distribution containing this code. The isolated SDK recipe copies that notice into its output. Ordinary executable builds stage `BOTAN-LICENSE.txt` and the exact `PAIRING-PROVIDER.json` beside the binary; container exports include and verify both files. Installer and maintainer-copy paths publish the upstream notice under `/usr/local/share/licenses/wsprrypi/` before replacing the executable. The [provider qualification record](../../development/issue-473/phase-6-2-provider-qualification.md) records the completed platform/package loading gates. These software checks do not authorize running an installation.

Build a fresh isolated SDK and run the upstream configured vectors:

```sh
python3 scripts/build_pairing_provider.py \
  --root /tmp/wsprry-pairing-provider \
  --cache /tmp/wsprry-pairing-provider-cache --vectors
```

This is a maintainer command. It downloads and verifies the exact pinned source, builds an unmodified minimized static library, and stages headers, library, pkg-config metadata, provenance, and the notice under the selected build root. It installs nothing into the host system. A nonempty build root is refused. The normal Make entry point builds an SDK under a source/toolchain-specific key and exercises the public application-profile fixture alongside existing OpenSSL:

```sh
cd src
make pairing-provider-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
```

This explicit profile is hardware-free on Linux and macOS. The normal default Linux application profile can also run this target; macOS keeps its existing explicit physical-transport selection rules. No application listener or transmission starts. Build caches are separate from system installations and from runtime credentials. The recipe prevents the archive from inheriting a parent application's Git revision as provider provenance.

Use `--cpu arm32` for the maintained ARMv6/VFPv2 hard-float builds, `--cpu arm64` for AArch64, or `--cpu x86_64` for the macOS Intel build. Compile inside the matching existing build environment; these switches do not install a compiler or create a new supported carrier. Native builds use the host architecture.

The upstream module resolver owns the dependency closure. P-256 support must be explicitly included: a build without it can skip the relevant known-answer vectors. The recipe checks required modules and refuses TLS, X.509, HTTP, or Asio modules. Dependencies such as Argon2 and SHA-512 remain when upstream requires them; the application must use only its fixed scrypt/P-256/SHA-256 profile, with no algorithm negotiation or provider-default password mapping.

Before every release, and during monthly maintainer dependency review, inspect the upstream [advisories](https://botan.randombit.net/security.html), [release notes](https://botan.randombit.net/news.html), [support policy](https://botan.randombit.net/handbook/support.html), and changes to the selected module closure. Record the review and actual tests. For an update, change the one reviewed pin/checksum, rebuild fresh SDKs in all four maintained Linux environments and both production macOS architectures, run upstream and application-profile checks, and repeat application packaging/loading and required paired integration. Do not patch cryptographic source, weaken validation, or require operators to repair a dependency. A failed security, maintenance, or packaging gate holds pairing and requires disposition; it cannot select a different suite or manual credentials automatically.

The maintainer process and a patch-update rehearsal demonstrate an update path; they do not certify future releases, promise future reviews have happened, or constitute an independent security audit of the new SPAKE2+ implementation.
