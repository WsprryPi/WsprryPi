# RP1 1.0.1 provider acceptance

The maintained [release lock](../scripts/tests/fixtures/rp1-release-1.0.1.json) binds the published immutable `v1.0.1` provider at commit `b52ab7b796e44d8690b2a91bccb21ae5d8b670df`, tree `a5c7efbe6debdc16e42cd0a2999d57bc095aca62`, product/module/controller `1.0.1`, Debian package `1.0.1-1`, both UAPIs, independent route identities and overlays, runtime binding v3/readiness v1/update v1, and all seven published asset hashes. It is maintained acceptance configuration, not a record of target qualification.

The [bundled qualification contract](rp1-qualification-metadata.md) retains the original `v1.0.0` software and route evidence. The [original software lock](../scripts/tests/fixtures/rp1-release-1.0.0.json) remains an input to its integrity checks. That evidence does not qualify `v1.0.1`; the helper reports the patch as unattributed to the retained evidence. Structural compatibility, readiness, evidence attribution, and application band/mode policy remain separate facts.

| Selection | Entry point | Identity and effects |
| --- | --- | --- |
| Exact-source development | Normal installer with `devel`, `commit:`, or `checkout:` | Exact source identity, separate Experimental ownership; installation and runtime effects require authorization |
| Release verification | Maintainer command below | Rebuilds and validates the locked published assets without installation or runtime activation |
| Published stable release | Normal `release` selection | Requires an immutable published non-draft/non-prerelease release, exact tag/source agreement and contracted assets; installation remains route-neutral |

Repository documentation and test cleanup does not change the published tag or assets. Use the tagged provider checkout for release acceptance; a later `main` or `devel` maintenance commit is a separate source identity even when its source version remains unchanged. Do not replace published assets or carry a source-archive hash forward after changing tracked files. The minimum corrected-runtime ancestor in the installer remains a lifecycle prerequisite, not the selected release pin.

## Rebuild and validate

Use the Linux Debian packaging environment matching the release provenance's
tool versions and executable hashes. The authoritative provider checkout must be
clean and detached at the locked release commit. Keep generated output outside
every Git checkout, and use a new output directory:

```sh
python3 scripts/maintainer/accept_rp1_release.py \
  --provider-source /absolute/clean/detached/provider \
  --output /absolute/new/acceptance \
  --compare-retained /absolute/downloaded/release \
  --verify-publication
```

`--compare-retained` is optional. It checks every retained asset against the lock
and fresh build. `--verify-publication` explicitly enables read-only GitHub access:
it applies the production selector, resolves the remote tag to the locked commit,
and checks release URL, immutability, publication state, the complete asset set,
authoritative asset URLs and GitHub's per-asset digests. Omitting it keeps the
command offline; a matching local release profile alone does not establish
publication or production selectability.

With that option, the command also runs the installer's actual production
release enumeration, downloads, package/inventory validation and exact runtime
source resolution in temporary external staging. It requires the selected
tag, commit, tree and package to match the lock and records
`productionResolverVerified`. It performs no package apply or runtime effect.
This exercises production `auto` and explicit `release` resolution; ordinary
offline selector tests establish their mapping without contacting GitHub.

The command runs the provider's reproducible builder and independent validator
with `--release`, applies WsprryPi's production manifest/package parsers to
captured asset bytes, and exercises exact-source development selection separately.
It never installs a package, changes an ownership record, contacts a target,
opens an endpoint or creates a release. Normal installation has no local-candidate
selector.

External `acceptance.json` identifies consumer/provider revisions, working-tree
state, tool/lock/installer hashes, asset identities, interface checks, reproducible
builds and any independently verified publication. `published` and
`productionSelectable` remain false unless fresh publication verification passes.
Qualification and installation-performed fields remain false in every case.
Build logs and provider validation stay outside Git beside the result.

Run deterministic refusal coverage from `src`:

```sh
make rp1-release-acceptance-test SUDO=
```

The tests reject substituted assets, wrong source identities, malformed inventory,
wrong UAPI/route contracts, unpublished or mutable releases, tag substitution and
wrong remote asset digests. They do not replace the real rebuild or target checks.
CI exercises the provider update/bootstrap boundary at the locked published
commit. Adapter regressions separately cover full finite-TONE plans, progress,
chunk-boundary cancellation, owner close, service stop, timeouts and cleanup faults.

## Installation, recovery, and remaining validation

The `0.9.0` predecessor remains a current migration-test input. Owning runtime preparation and cleanup, owning package/development removal, and absence verification precede clean `1.0.1` installation. This includes removal of an owned `1.0.0` provider before installing `1.0.1`; direct replacement and in-place downgrade are refused. Retain exact predecessor recovery inputs; version ordering is not a rollback procedure. See [installation and ownership](rp1-gpclk-dkms-installation.md).

Publication and source/package verification do not prove actual installation, service behavior, route switching, live output, calibration, spectrum, or RF qualification. Define the applicable acceptance matrix, identify reusable evidence and its applicability, and repeat checks affected by changed inputs. Historical [release evidence](../release_tools/4.0-Beta-1-Release-Evidence.md) binds its selected source/artifact identities and accepted `v1.0.0` integration; it does not establish target acceptance for `v1.0.1`. The [release checklist](../release_tools/Release%20Punchlist.md) owns remaining candidate acceptance and publication gates. Software-only verification does not close installation or runtime acceptance.
