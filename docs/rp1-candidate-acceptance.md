# RP1 provider candidate acceptance

The maintained lock is
[`scripts/tests/fixtures/rp1-candidate-1.0.0.json`](../scripts/tests/fixtures/rp1-candidate-1.0.0.json).
It identifies unpublished provider commit
`51b8012a4ca2f01576805a81f4b50519b5b786d3`, tree
`8574c0fa6f542b84cf8705127b0bd4779b4f5f1d`, product/module/controller
`1.0.0`, Debian package `1.0.0-1`, both UAPIs, both route identities and
overlays, runtime binding v3/readiness v1/update v1, and all seven asset hashes.
The lock is acceptance configuration, not production release metadata.

| Selection | Entry point | Installation and ownership | Runtime and output |
| --- | --- | --- | --- |
| Exact-source development | Normal installer with verified `devel`, `commit:`, or `checkout:` source | Provider-owned exact-source install; WsprryPi v2/v3 ownership evidence | Neutral administration first; route agreement, fresh bounded confirmation and current provider checks remain required |
| Unpublished candidate acceptance | Maintainer command below | Builds and validates assets only; writes no installation record | No runtime activation or output |
| Published stable release | Normal `release` selection | Requires immutable published non-draft/non-prerelease GitHub release and contracted assets; WsprryPi v2/v3 ownership evidence | Installation remains neutral; application and provider output policy still apply |

Neither development nor candidate acceptance claims publication. No selection,
manifest or successful software test establishes hardware, product or RF
qualification. Candidate installation tests, when separately authorized, use
the exact-source development lifecycle with the candidate commit; they do not
inject local packages into normal release selection.

## Rebuild and validate

Use a Linux Debian packaging environment matching the locked provenance's
tool versions and executable hashes. The provider checkout must be clean,
authoritative and detached at the locked commit. Keep generated output outside
every Git checkout, and use a new output directory:

```sh
python3 scripts/maintainer/accept_rp1_candidate.py \
  --provider-source /absolute/clean/detached/provider \
  --output /absolute/new/acceptance \
  --compare-retained /absolute/retained/candidate
```

`--compare-retained` is optional. Retained assets are checked against the lock
and fresh build; their names or directory do not grant trust. The command runs
the provider's reproducible builder and independent validator, rejects missing,
extra, substituted, malformed or duplicate inventory, and applies WsprryPi's
production manifest/package parsers to captured asset bytes. It never calls
the package manager, contacts a target, opens either endpoint or creates a
GitHub release. Normal installation has no candidate selector.

The external `acceptance.json` records source/tree and asset identities,
consumer and controller UAPIs, route identities, runtime contracts, unpublished
classification, consumer commit and working-tree cleanliness, and acceptance
tool/lock/installer hashes. Build logs and provider validation stay beside it.
An acceptance result from modified consumer source explicitly records that fact.

Deterministic refusal tests run with `make rp1-candidate-acceptance-test SUDO=`
from `src`. They use synthetic malformed inputs and do not replace the real
rebuild command. RP1 adapter tests use the real finite-TONE compiler and a
candidate-compatible provider boundary, including full eight-second progress,
chunk-boundary STOP, owner close, service STOP, timeouts and cleanup faults.
They establish software behavior, not execution of kernel DMA or RF output.

## Updates, removal and diagnostics

The `0.9.0` predecessor requires owning runtime preparation and cleanup,
owning package/development removal, and complete absence verification before
clean successor installation. Direct package upgrade and downgrade are refused.
Retain the exact predecessor archive or captured development rollback closure;
version ordering is not a rollback procedure. See
[installation and ownership](rp1-gpclk-dkms-installation.md).

Provider control subprocess and socket exchanges have bounded output and a
single three-second deadline; detached route jobs retain their separate
provider/service lifecycle bounds. Underlying refusal details remain operation failures. Status retains
the last operation's terminal reason, timing, cancellation and cleanup state.
Support bundles collect a bounded identity whitelist from protected ownership
records, separate from provider `inspect`, DKMS and module observations. They
omit full records, paths, tokens and configuration. A recorded release channel
does not prove current publication; diagnostics explicitly say publication and
qualification were not established.

Target installation, service behavior, route switching, eight-second live
TONE and RF checks require their own identified target state and authorization.
Acceptance does not freeze or publish the provider and does not release
WsprryPi. Operator documentation is maintained separately.
