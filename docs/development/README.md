# Development guide

WsprryPi's application, first-party web UI and reusable components are ordinary
content in one Git repository. Follow [AGENTS.md](../../AGENTS.md) and preserve
the [component boundaries](../components/README.md), standalone tests, licenses
and attribution.

## Builds and safe validation

Run application tests from `src`. Inspect a target before executing it; the
ordinary `make test` target can operate hardware. Installation, services, GPIO,
transmissions and other device operations require separate authorization.

On Linux with libgpiod C++ development headers, the hardware-disabled profile is:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
```

The explicit subset for a non-Linux host or Linux without those headers is:

```sh
make semantics-test-portable SUDO=
```

This selects simulated-only coverage with ancillary GPIO disabled. It excludes
tests requiring physical backend capabilities. Do not retry a known broken
macOS C++ toolchain without explicit repair or local-build authorization.
Use an identified Linux environment for affected C++ validation when needed.

Read the [simulation contract](../simulated-backend.md) before exercising the
application without hardware. The [Debian CI workflow](../../.github/workflows/debian-non-hardware.yml)
defines the canonical unprivileged suite. Use [container builds](../container-builds.md)
and the [precompiled-installation contract](../precompiled-installation.md) for
artifact production. Building an executable does not qualify installation or RF.

On its selected branches, the non-hardware workflow starts with a lightweight change-classification job. It inspects the complete Git diff rather than GitHub's bounded path-filter list. Only ordinary documentation skips the full jobs: the root `README.md`, prose under `docs/` or `release_tools/`, and images/PDFs under `docs/`. Other paths, including Markdown in source or test directories, require full validation. The exact policy lives in `scripts/ci/change_scope.py`.

`AGENTS.md`, the support-bundle intake contract, distributed license notices and protected fixture/evidence directories remain checked inputs. Mixed changes, executable bits, symlinks, submodule/type changes and moves across the documentation boundary also require full CI. Pushes use a two-dot comparison; pull requests use the merge-base comparison. New branches, force pushes, empty or unavailable comparisons and classification failures default to full validation. Cancellation still stops dependent jobs. Run the focused regressions from `src` with `make ci-change-scope-test SUDO= CXX=true GPIOD_CPP_HEADER_AVAILABLE=0`.

Concurrency groups include the commit SHA so a documentation-only successor cannot cancel or replace an earlier code-validation run. Different commits can therefore validate concurrently; duplicate runs of the same commit retain cancellation. When documentation becomes a build or regression input, add it to the classifier's checked-input policy before relying on it in tests.

## Maintained contracts and tools

- [WTP execution plans](../wtp-execution-plan.md), [backend](../wtp-backend.md),
  [scheduling](../wtp-scheduling.md), [status/recovery](../wtp-status-recovery.md),
  [USB management](../wtp-usb-management.md) and [network transport](../wtp-network.md).
- [Pico configuration compatibility](../wtp-configuration-compatibility.md),
  [browser API](../wtp-browser-api.md), [Pi endpoint](../wtp-pi-endpoint-contract.md)
  and [Fleet](../wtp-fleet.md).
- [RP1 installation](../rp1-gpclk-dkms-installation.md),
  [provider acceptance](../rp1-provider-acceptance.md),
  [qualification metadata](../rp1-qualification-metadata.md) and
  [Si5351 support limits](../si5351-support.md).
- [Installer/support tools](../../scripts/README.md) and
  [maintainer runbook](../../release_tools/WsprryPi%20Full%20Regression%20Test%20Runbook.md).
- [Release artifact preparation](../../release_tools/Build%20Orchestration.md)
  and [calibration authority and scope](../../release_tools/Calibration%20Scope%20Decisions.md).

The CI rollback regression and its imported helper remain at
`wtp-reply-routing-results/rollback_test.py` and `live_target.py`. The regression
mocks commands and network recovery against a private temporary filesystem;
run it with `python3 -B ../docs/development/wtp-reply-routing-results/rollback_test.py`
from `src`. The helper contains host-specific service/network operations and is
not an ordinary test entry point. Its live commands require separate authorization.

## Work and cleanup

The [release punchlist](../../release_tools/Release%20Punchlist.md) is the selected
work authority. Beta 1 preparation retains its exact candidate identities,
artifacts and unfinished acceptance gates; a later source commit does not
replace that candidate or establish release readiness.

Follow the [repository and work-harness cleanup instructions](repository-cleanup-prompt.md)
for an explicit cleanup request. Inventory exact deletion, rewrite, retention
and hold decisions first. Preserve current dependencies, active work and
recovery assets; retire obsolete prompts and evidence without creating an archive.

## Selected coordinated logging work

The [UDP syslog plan](udp-syslog-plan.md) links the reviewed cross-repository design and coordinated Pi/backend/UI deliverables.

The [UDP logging contract](udp-logging.md) describes the worker package, export-safe journal projection, independent installation status and finite tests. The linked Pico qualification record identifies successful production pairs, physical acceptance and restoration; the combined backlog tracks coordinated publication.
