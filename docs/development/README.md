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
