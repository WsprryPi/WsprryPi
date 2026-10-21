# Release artifact preparation

WsprryPi provides four container build commands for producing standalone,
dynamically linked executables. The commands export locally; they do not create
tags, upload assets or publish a release. The
[release punchlist](Release%20Punchlist.md) governs selected release work and
acceptance gates. Publication requires its own authorization.

## Source and version identity

Build a release candidate from a clean checkout at an accepted full commit.
Record the commit, tree, branch or detached state, supporting-source identities
and selected version before building. Keep the source fixed while producing
the complete target set.

An unpublished candidate can supply a SemVer without creating a tag:

```sh
./scripts/build-aarch64-trixie-container.sh /absolute/new/output --release-version 4.0.0-beta.1
```

Each wrapper accepts the same version option. Embedded metadata retains the
actual commit and dirty state; the option does not make a modified checkout
clean. Follow the [container build contract](../docs/container-builds.md) for
prerequisites, all four commands, isolated output directories and export checks.

## Target and asset identity

| Build target | Release executable name |
| --- | --- |
| ARMv6 Bookworm | `wsprrypi-armv6-bookworm` |
| ARMv6 Trixie | `wsprrypi-armv6-trixie` |
| AArch64 Bookworm | `wsprrypi-aarch64-bookworm` |
| AArch64 Trixie | `wsprrypi-aarch64-trixie` |

The wrappers export each executable as `wsprrypi` in its own target directory.
The release asset names above are the installer's download contract, not an
upload operation performed by the wrappers. Preserve each export's checksum,
build metadata, target/OS identity, ELF attributes, resolved dependencies,
package inventory and build-image identity. Verify the complete matrix against
the accepted source and version. ARMv7 `armhf` is not the ARMv6 baseline.

Keep generated executables and build evidence outside tracked source. Do not
amend the accepted source commit to include binaries or move a tag after
building. A changed source requires a new candidate identity, affected builds
and acceptance checks. Resolve a release tag explicitly through
`refs/tags/<tag>` and verify its commit against the accepted candidate before
any separately authorized upload or publication.

## Acceptance and publication boundaries

Build and dependency inspection establish artifact properties, not target
installation, service, GPIO, timing or RF correctness. The
[precompiled installation contract](../docs/precompiled-installation.md)
defines deployment checks and recovery limits; live installation remains a
separately authorized operation.

Beta 1's [candidate manifest](4.0-Beta-1-Candidate-Manifest.json),
[checksums](4.0-Beta-1-SHA256SUMS) and
[release evidence](4.0-Beta-1-Release-Evidence.md) bind its accepted artifacts,
reused evidence and remaining gates. Later documentation or supporting-source
changes do not silently replace those identities or complete their acceptance.

The repository has no release-upload workflow. The
[four-target release-process proposal](Four-Target%20Release%20Executable%20Implementation%20Prompt.md)
retains an unresolved choice between Actions orchestration and a
developer-operated process. Neither option is selected by artifact preparation
or cleanup. Preserve the selected candidate until that decision and the
applicable publication gates are resolved.
