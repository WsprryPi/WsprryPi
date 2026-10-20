# Four Target Release Executable Implementation Prompt

**Proposal only. Select Option A or Option B before implementation.**

Provide all four supported WsprryPi executables as GitHub release assets using
one of two release processes: a fully orchestrated GitHub Actions pipeline, or
developer-operated container builds and release steps. Both processes must
produce the same filenames, verification evidence and release identity records.

Creating, committing or pushing this prompt does not authorize its execution.
Future implementation authorization covers only the selected option and its
approved validation. Release tags, drafts, asset uploads, publication and
announcements require explicit release authorization. No implementation or
publication has been performed by preparing this proposal.

## 1. Select one implementation option

| Decision | Option A: orchestrated GitHub Actions | Option B: developer-operated release |
| --- | --- | --- |
| Build execution | Four isolated matrix jobs on GitHub runners | Developer invokes the four existing Docker wrappers on a suitable build machine |
| Coordination | One authorized release launch coordinates builds, verification, draft assets and publication | Developer follows documented build, verification, upload and publication steps |
| Artifact transfer | Workflow artifacts feed one release job | Developer retains and verifies the exports before uploading |
| Release upload | Automated after every build and required check succeeds | Explicit developer command or documented GitHub upload operation |
| Main acceptance concern | ARMv6 emulation, runner resources and release permissions | Repeatable developer procedure and complete artifact verification |
| Normal CI | Retained, with release-specific checks added where needed | Retained; no automatic release-build or publisher workflow is required |

Obtain the user's selection before writing implementation files. Record the
selected option, accepted source/version, build host or runner, and activation
policy in the implementation plan. Do not implement both release processes as
competing defaults. If Option A proves impractical, preserve its diagnostic
evidence and propose Option B; changing options requires the user's direction.

The word **image** here means the Docker build image containing the source and
compiled workspace. The assets offered to operators are standalone, dynamically
linked executables. Publishing Docker images to GHCR or Docker Hub, producing
bootable Raspberry Pi OS images, and building `.deb` packages are outside this
proposal. No container registry is required for either option.

## 2. Inspect the current repository and release contracts

Before implementing the selected option:

1. Read applicable `AGENTS.md` instructions. Inspect the current branch, parent
   working tree, staged and unstaged changes, untracked files and nested
   component instructions. Run `git status --short --branch`, `git diff --check`
   and `git diff --cached --check`. Preserve all unrelated work.
2. Read the current [release punchlist](Release%20Punchlist.md), candidate
   manifest and acceptance evidence. Treat their identities and gate status as
   current inputs to inspect, not constants copied from this prompt. Keep
   acceptance work and publication work separate.
3. Read [container builds](../docs/container-builds.md),
   [precompiled installation](../docs/precompiled-installation.md),
   [release orchestration](Build%20Orchestration.md), the
   [development guide](../docs/development/README.md),
   [simulation contract](../docs/simulated-backend.md), and
   [non-hardware CI workflow](../.github/workflows/debian-non-hardware.yml).
4. Inspect `scripts/container_build.py`, `containers/build-artifact.sh`, all
   four Dockerfiles and shell wrappers, `scripts/generate_build_metadata.py`,
   `scripts/precompiled_binary.py`, and their existing tests.
5. Verify the repository's actual default branch, workflow availability, token
   policy, release immutability and required checks. Recheck the official
   references below for current behavior before choosing action/tool versions.
6. Identify the smallest file scope for the selected option. Keep C++, UI,
   component interfaces, installer settings, RP1 provider releases and sibling
   repositories outside scope unless a concrete defect requires a separately
   approved expansion. Do not change an installer contract to simplify upload.

The existing release-orchestration guide includes staging binaries, amending a
commit and moving a tag after compilation. Replace that procedure for the
selected release process. Release artifacts belong on the release; the accepted
source commit and any published tag must remain fixed.

## 3. Common artifact and source identity requirements

Implement these requirements for the selected option.

### Maintained targets and release filenames

| Target | Docker platform | Userspace ABI | Required raw release asset |
| --- | --- | --- | --- |
| `armv6-bookworm` | `linux/arm/v6` | 32-bit Raspbian ARMv6/VFPv2 hard-float, `armhf` | `wsprrypi-armv6-bookworm` |
| `armv6-trixie` | `linux/arm/v6` | 32-bit Raspbian ARMv6/VFPv2 hard-float, `armhf` | `wsprrypi-armv6-trixie` |
| `aarch64-bookworm` | `linux/arm64` | 64-bit AArch64, `arm64` | `wsprrypi-aarch64-bookworm` |
| `aarch64-trixie` | `linux/arm64` | 64-bit AArch64, `arm64` | `wsprrypi-aarch64-trixie` |

The installer selects exactly these names within a chosen release tag. Do not
add a version suffix or archive extension to the four raw executable assets,
flatten four exports into one file named `wsprrypi`, or substitute ARMv7 Debian
`armhf` for the ARMv6 baseline. Optional archives may supplement the raw assets.
Bullseye and separate ARMv7 targets remain outside the maintained matrix.

### Build identity and compatibility

- Resolve the selected source to a full immutable commit. For a release tag,
  resolve `refs/tags/<tag>` explicitly and peel annotated tags to their commit.
  Reject an absent, moved or mismatched release tag before uploading.
- Build a clean source snapshot with enough Git objects, refs, tags and index
  state for accurate metadata. Preserve actual branch or detached attribution.
  Do not edit tracked files to manufacture a clean result.
- Validate the release version as the existing SemVer parser requires. Derive
  it from the selected version tag by removing its leading `v`, then pass
  `--release-version`. The current override retains `+<short-commit>`; verify
  that result rather than changing application version semantics incidentally.
- Require consistent executable source/version across all four outputs. Where
  an already accepted release intentionally has different executable and
  supporting-source commits, retain both reviewed identities explicitly. Do not
  silently replace frozen exports or claim that a rebuild is the same artifact.
- Keep current digest-pinned base images and signed package repositories.
  ARMv6 Trixie currently upgrades the pinned Bookworm Raspbian bootstrap. AArch64
  uses Debian userspace baselines. Preserve truthful Raspberry Pi OS compatibility
  limits and package inventories; repositories are not snapshotted, so identical
  source does not promise byte-for-byte identical rebuilds.
- The local build-image ID is evidence of the build used. It is not a published
  registry image address or a promise that another machine can pull that ID.

### Release staging and evidence

Reuse the existing wrappers and exporter. Add a small shared staging/verification
helper only where useful; do not duplicate build logic in workflow YAML or
manual instructions. Use fresh destinations and retain the exporter's refusal
to merge or overwrite completed exports.

Produce a release staging directory containing the four raw assets, an
aggregate `SHA256SUMS`, a machine-readable manifest, and an evidence archive or
equally explicit evidence set. Define the manifest schema and final auxiliary
filenames in the implementation. Include:

- release tag/version and full executable source commit;
- distinct supporting-source identity when applicable;
- per-target OS, userspace architecture, Docker platform and exact build-image ID;
- executable filename, byte count, SHA-256 and compiled metadata;
- package inventory, ELF/ABI and direct-library evidence, resolved dependencies
  and bounded version-check result;
- build environment/tool versions, workflow run or developer build attribution,
  required CI identity/results, and relevant accepted limitations.

Generate checksums for the final release filenames, not the original repeated
`wsprrypi` names inside the exports. Verify all hashes after staging and after
any workflow-artifact transfer. Retain executable permissions across transfers
or restore them explicitly before running checks; archive-based Actions artifact
transfer may normalize permissions. Publish only an explicit file allowlist.
Exclude credentials, Git configuration/objects, host-private paths, unrelated
logs and the full Docker/source workspace from release assets.

## 4. Option A requirements

Implement this section only if Option A is selected.

### Workflow and runner setup

1. Add a dedicated release-executable workflow with the four targets as an
   explicit matrix. Keep ordinary validation separate from publication.
2. Provide a build-only entry point for rollout and troubleshooting. Define a
   trusted version-tag trigger or explicit release launch for the release path.
   Do not publish from branch pushes, pull requests or arbitrary unvalidated
   dispatch inputs. Do not rely on a `release: published` event to attach assets
   after a release has already become immutable.
3. Decide and document activation before enabling release mutations. During
   implementation trials, all release creation/upload/publish operations must
   remain disabled. Once separately authorized, one release launch should
   coordinate the complete selected pipeline without manual per-target work.
4. Install/configure QEMU and Buildx using reviewed, pinned action revisions.
   Verify that both BuildKit build execution and subsequent Docker inspection
   containers support `linux/arm/v6` and `linux/arm64`. Do not treat an advertised
   platform list as proof that the real ARMv6 recipe succeeds.
5. Prefer hosted Ubuntu runners initially. Test ARMv6 cold builds and dependency
   inspection before promising duration, concurrency or resource requirements.
   Pin/document the runner and build tooling. Tune timeouts and parallelism from
   actual results; preserve the exporter's bounded inspection behavior.
6. Keep each job single-platform with the existing `--load` behavior. No combined
   multi-platform manifest or registry publication is needed. Do not upload or
   transfer whole build images between jobs merely to collect executables.
7. Check out the selected source, not whichever branch happens to be current.
   Disable persisted checkout credentials before forming the Docker context.
   Keep ignored staging outputs from contaminating source attribution.
8. Use distinct workflow-artifact names per target. Retain failed-build logs,
   avoid masking any target failure, and make aggregation depend on all four
   successful builds. A cancelled, failed or missing target blocks release.

### Required checks and release coordination

- Require successful software validation for the exact selected source, either
  through an appropriately scoped reusable workflow or verified required runs
  for that full commit. A successful check on a moving branch or older commit
  is insufficient. Handle missing, cancelled and skipped results explicitly.
- Keep build jobs read-only with respect to the repository. Give only the final
  release job the permissions required to upload assets. Use `GITHUB_TOKEN`
  where sufficient; do not expose a write token to source compilation.
- Check current GitHub restrictions before automating release creation/editing.
  A release target whose workflow files differ from the default branch may
  require workflow-authorized credentials that `GITHUB_TOKEN` cannot provide.
  If applicable, propose the smallest supported solution, such as an existing
  developer-created draft or an explicitly approved GitHub App credential.
  Do not silently introduce a broad personal access token or change branches.
- Account for workflow availability on the default/tagged refs and the fact
  that events generated with `GITHUB_TOKEN` generally do not start another
  workflow. Do not depend on a second release-event workflow being triggered by
  the first workflow's token. Use an explicit job/reusable-workflow dependency.
- Serialize release mutations per tag. Define rerun behavior: a matching existing
  asset can be verified and retained; conflicting bytes must stop the run.
  Never use unconditional `--clobber`, delete a release or move a published tag.
- Once release mutation is separately authorized, create or identify the exact
  draft, verify its tag/source, upload the complete allowlist, download the assets
  again and compare hashes/manifest identities. Partial upload leaves a draft
  with a clear failure; it must not result in a published incomplete release.
- Publish only after the recorded acceptance and publication conditions are
  satisfied. A Beta release must be marked as a prerelease; stable/latest
  designation must be intentional. Use the reviewed notes body and preserve the
  separate operator-documentation publication boundary. Verify release state,
  tag identity, asset names, downloads and hashes independently afterward.

Cross-run caching is optional after cold builds work. If added, scope caches by
target/toolchain, test uncached operation and account for cache behavior in
evidence. The current Python exporter invokes Buildx directly, so cache settings
must actually reach that invocation. Do not weaken verification to fit caching.

## 5. Option B requirements

Implement this section only if Option B is selected.

Deliver one precise developer release procedure and the minimum staging/checking
tooling it needs. Do not add an automatic release publisher. The developer must
be able to follow the procedure without reconstructing commands from this
proposal or relying on undocumented files on the original author's machine.

The implemented runbook must give copyable commands, prerequisites, expected
outputs, failure handling and stopping points for these steps:

1. Select the accepted version, full source commit, any distinct supporting
   source, reviewed release notes and acceptance record. Use an isolated clean
   checkout without resetting, stashing or overwriting the developer's work.
2. Verify Docker engine/Buildx, POSIX shell, Python and native/emulated target
   support. Compilation runs inside Linux Docker containers, including when
   the host is macOS. Do not attempt local macOS C++ compilation or linking.
3. Choose absent/empty output directories outside the tracked source. Invoke
   each of the four existing wrappers with the same validated
   `--release-version`. State the exact platform wrapper and destination for
   each command. Capture each exit status and its logs; never rename one target
   as another or accept a failed export because compilation appeared to finish.
4. Run the common staging and verification procedure. Verify four distinct
   targets, final asset names, source/version attribution and aggregate hashes.
   Retain raw evidence and the exact build-image identities through acceptance.
5. Present the complete upload inventory for review. Record that Docker images
   stay on the developer's build machine; the four executables and approved
   evidence are what operators will download from the release.
6. After separate release authorization, create or verify the immutable version
   tag and its peeled commit, and create or identify the draft release. The
   runbook must distinguish a pre-release build from the later authorized tag
   operation. Reconcile tag/source state without rebuilding accepted binaries
   merely to change their attribution or replacing them without fresh acceptance.
7. Provide exact GitHub CLI commands for uploading the four raw executables,
   checksum file, manifest and evidence to that draft. Use explicit filenames
   and a reviewed notes file; make Beta/stable state explicit. Explain matching
   reruns, conflicting assets, authentication failure and partial upload recovery.
8. Download the draft assets to a fresh directory and compare their hashes and
   identities. Only then, under publication authorization, publish and independently
   verify the public release/tag/assets. Do not instruct the developer to upload
   missing assets after publishing an immutable release.
9. Document keeping the accepted exports and recovery evidence until release
   verification succeeds. Cleanup must name only invocation-owned temporary
   containers/directories and must preserve earlier accepted exports.

Developer-operated upload commands must remain explicit release steps, never
side effects of a build, package or validation command. Test command construction
and failure behavior with mocks without mutating GitHub releases.

## 6. Validation required for either selected option

Inspect each existing target before running it. Capture the command, environment,
exit status, warnings, skipped cases and retained evidence. Do not execute this
section while merely preparing or reviewing the proposal.

### Focused automated checks

Use the existing relevant coverage in an identified Linux validation environment,
including:

```sh
python3 -B scripts/tests/container_build_test.py
python3 -B scripts/tests/container_avahi_contract_test.py
python3 -B scripts/tests/build_metadata_regression_test.py
python3 -B scripts/tests/precompiled_binary_test.py
bash scripts/tests/precompiled_installer_test.sh
```

These commands are repository-root entry points; normal Make-based application
tests run from `src` as repository instructions require. Extend existing test
boundaries when practical and add focused tests for new packaging/release logic.
Avoid a broad application or component rewrite solely to test the release path.
`build_metadata_regression_test.py` compiles a C++ round-trip fixture with `g++`;
run it inside the Linux container/runner, never through the local macOS C++
toolchain. Inspect the other tests before choosing a host-side subset.

Cover meaningful success and failure cases for the behavior implemented by the
selected option; matrix and publisher tests apply only where those features exist:

- all four mappings and exact installer-compatible filenames;
- missing, duplicated or extra targets; swapped OS/architecture; empty output;
- wrong commit, dirty source, inconsistent versions, invalid SemVer and moved tags;
- renamed-asset checksum correctness, transfer corruption and missing evidence;
- invalid destinations/symlinks, collisions, partial staging, interruption and
  cleanup without loss of prior accepted exports;
- failed/empty/incomplete/missing-library inspection, timeout and bounded
  simulated version failure; preserve ARMv6 glibc trace-mode handling;
- missing/failed/cancelled/wrong-commit CI and failed matrix jobs;
- publisher disabled in build-only mode; invalid release inputs never reaching
  shell interpolation or the GitHub API;
- exact upload allowlist, conflicting assets, matching reruns, partial upload,
  authentication failure and immutable-release refusal without publication.

For Option A, lint workflow syntax and validate dependency/permission/trigger
behavior with appropriate tooling and fixtures. For Option B, validate the
actual developer command sequence and any shell helper syntax. Verify that
documented commands use the shipped filenames and actual CLI interfaces.

### Actual builds and artifact inspection

Run all four complete recipes in the selected build environment: GitHub runners
for Option A, the documented developer machine/environment for Option B. A
fixture-based exporter test is not evidence of successful real builds.

For every output, verify ELF class/machine, OS/ABI, ARMv6/VFPv2 hard-float where
applicable, direct required libraries including Avahi, complete dependency
resolution, compiled source/version/dirty attribution, package inventory and
SHA-256. Run the executable's bounded `--backend simulated --version` in the
exact build image as an unprivileged user, without network, added capabilities,
hardware devices or production service access. Preserve the existing isolated
glibc dependency-trace check; do not replace it with the ARMv6 loader invocation
that previously failed under QEMU.

Exercise relevant safe software validation for changed code. On Linux with the
required headers, the full semantics profile is
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=` from `src`.
Use the explicitly documented portable subset only where applicable and report
its narrower coverage. Existing exact-source CI may be reused only when its
identity, checks and unchanged-input applicability have been verified.

Start Option A acceptance with a build-only hosted run. Exercise the final
staging path and publisher behavior without a real draft/upload/publication.
For both options, real release upload/download/publication verification is a
later separately authorized acceptance step. Mark that step outstanding until
it is actually performed; mocks do not establish end-to-end release success.

No Pi installation, upgrade, service operation, GPIO state change, firmware
flash, transmission, RF measurement or sudo mutation on a target is authorized
by this prompt. Software/build evidence does not establish physical acceptance.

## 7. Documentation updates required during implementation

Update current maintainer guidance for the selected process; do not leave two
conflicting default release procedures.

- `docs/container-builds.md`: selected automated or developer entry points,
  prerequisites, target matrix, outputs, version inputs, image retention,
  emulation requirements, verification, reruns and current limitations.
- `release_tools/Build Orchestration.md`: replace the binary-staging/amend/retag
  instructions with fixed source identity, release asset preparation, required
  checks, draft verification and separately authorized publication steps. Include
  the actual developer commands for Option B or actual workflow launch/recovery
  steps for Option A. Do not prescribe force-pushing accepted source tags.
- `docs/precompiled-installation.md`: exact raw asset names and tag selection,
  manual download/checksum use, executable permissions, supported OS/ABI and
  supporting-file selection. Preserve the existing installer compatibility and
  refusal behavior. Change "future downloads" claims only after actual assets
  are available and their corresponding validation is recorded.
- `release_tools/README.md`, `docs/development/README.md` and the root README as
  needed: discoverable links to the maintained process without duplicate guides.
- Current release records/punchlist: record only evidence and status changes
  actually established. Do not mark physical or publication gates complete from
  container results, or replace accepted candidate hashes with new builds silently.

Locate related operator guidance in the actual sibling documentation repository
and read its `AGENTS.md`. Sibling edits require explicit cross-repository
authorization. If unavailable, report exact paths/sections needing follow-up
instead of writing there. If authorized, follow its build/render workflow and
keep its review, commit and push separate. Use Impeccable for any affected UI
wording, screenshots or rendered UI documentation; there is no UI redesign in
this release-automation scope.

Check local Markdown links, command spelling, examples, formatting and final
whitespace. Retire this execution prompt only after its selected work is
completed and useful operating instructions have been consolidated, following
the repository's cleanup rules. Do not delete it while either/or selection or
implementation is still pending.

## 8. Review and completion criteria

Perform an adversarial assessment of the selected implementation. Challenge
wrong-source publication, tag movement, version drift, target substitution,
partial releases, silent overwrite, failed-check bypass, hidden network/secret
dependencies, swallowed exits, unbounded emulation, stale evidence and unsupported
hardware claims. Repair in-scope findings, rerun affected checks and reassess.
Do not call the unselected option implemented or silently expand into it.

The implementation handoff must identify:

1. The selected option, files changed and the actual build/release workflow.
2. Four verified artifacts and their exact source/version/hash/ABI identities.
3. Commands and tests run, results, warnings, skips, ARMv6 runner findings and
   remaining validation, including any nonpublishing or real-release gap.
4. Documentation Impact: updated files, considered-but-unchanged guidance and
   exact outstanding operator-documentation work.
5. Release permissions and activation state, required credentials if any, and
   whether any tag, draft, upload or publication actually occurred.
6. Current branch, working-tree state, preserved unrelated edits, component paths
   changed, and commit/push status. No component changes are expected.

Commit or push implementation changes only when that later execution request
authorizes it. If authorized, stage only the selected scope, review the complete
staged diff, commit at the approved boundary, push only the approved branch and
independently verify the remote commit. Never include generated binaries or
local skill/runtime artifacts in Git.

The present authorization is limited to creating, reviewing, committing and
pushing this proposal document. Stop after that documentation handoff. Do not
select an option, implement automation, run builds, create tags/releases/drafts,
upload artifacts or publish anything now.

## Official references to verify before implementation

- [Docker multi-platform builds in GitHub Actions](https://docs.docker.com/build/ci/github-actions/multi-platform/)
- [Docker QEMU setup action](https://github.com/docker/setup-qemu-action)
- [Docker Buildx setup action](https://github.com/docker/setup-buildx-action)
- [GitHub workflow events and manual dispatch](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)
- [GitHub Actions artifact transfer and permissions](https://github.com/actions/upload-artifact)
- [GitHub release creation and token restrictions](https://docs.github.com/en/rest/releases/releases#create-a-release)
- [GitHub release asset API](https://docs.github.com/en/rest/releases/assets)
- [GitHub immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
- [GitHub CLI release creation](https://cli.github.com/manual/gh_release_create)
- [GitHub CLI asset upload and overwrite behavior](https://cli.github.com/manual/gh_release_upload)
- [GitHub CLI release editing and publication](https://cli.github.com/manual/gh_release_edit)
