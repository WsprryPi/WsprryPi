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

## Beta 1 publication procedure

For item 16d, the operator selected the developer-operated procedure (Option B)
on October 21, 2026. Use the accepted existing exports; no new Actions publisher
or permanent release architecture is introduced. The retained
[proposal](Four-Target%20Release%20Executable%20Implementation%20Prompt.md)
does not authorize implementing its unselected Actions option.

1. Resolve supporting-source acceptance, freeze the reviewed application and
   Docs commits, and require applicable exact-source CI. Identify the binaries'
   actual executable source separately from later supporting-source repairs.
2. Copy all four accepted exports into fresh external staging using the exact
   flat asset names above. Rehash against the accepted candidate. Declare six
   assets: four executables, `SHA256SUMS`, and `publication-manifest.json`.
   Public checksums name the flat executable files; the public manifest records
   sizes/hashes and exact source/Docs mapping. Keep raw logs and recovery inputs
   private. Do not create self-referential checksum records.
3. Commit/push authorized Pi and Docs changes separately. Create annotated tags
   only at reviewed commits and push each explicit tag ref. Verify remote tag
   objects, peeled commits and trees. Never move a tag or create one implicitly
   from the default branch. Verify the exact Beta Docs publication first;
   preserve RTD stable/latest/default routing.
4. Create the draft with the reviewed body:

   ```sh
   gh release create v4.0.0-beta.1 --repo WsprryPi/WsprryPi \
     --verify-tag --draft --prerelease --latest=false \
     --title 'WsprryPi 4.0 Beta 1' --notes-file /absolute/reviewed/notes.md
   ```

5. Upload only those six files with `gh release upload` and an explicit
   repository/tag. Do not use `--clobber`. On an ambiguous result, inspect actual
   state before retrying. Download every draft asset to a separate directory;
   verify complete inventory, bytes, hashes, notes and links.
6. After all gates pass, publish with
   `gh release edit v4.0.0-beta.1 --repo WsprryPi/WsprryPi --draft=false --prerelease --latest=false`.
   Preserve the repository's existing immutability policy.
7. Independently download every public executable into another new directory.
   Compare bytes/size/SHA-256 directly against the accepted baseline and staged
   packet. Exercise the real fetch helper in each of the four userspaces;
   `--dry-run` does not download. Verify release classification, prior latest
   stable selection, tag/source, Docs build and complete public inventory.
   Supplement with GitHub attestation commands where attestations are available.
8. Record publication facts in the active candidate/evidence/inventory records,
   commit/push closeout separately and verify exact-head CI and remote parity.
   Closeout commits never reidentify binaries or move publication tags.

No target installation, services, GPIO, RF, Pico work, stable promotion or item
16e announcement is authorized by this procedure. Conflicting published tags
or assets require separate direction; do not recreate a release to hide a mismatch.
