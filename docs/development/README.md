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

## Production application and browser regressions

The Linux application regression builds the current source with only simulated and WTP backends and ancillary GPIO disabled. It starts the explicit simulated backend in a private network namespace, verifies canonical INI values and safe refusal of case-mismatched keys and DEFAULT values used in place of required canonical keys through the actual application, and runs finite browser-read profiles with synthetic schedule time. Reads must preserve both saved INI bytes and exposed configuration. It opens no transmitter device and supplies no RF qualification.

```sh
make -C src production-browser-regression-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= TEST_NETNS_RUNNER="sudo unshare --net"
make -C src tls-observer-test SUDO=
```

The TLS observer regression uses fresh loopback certificates, concurrent local streams and strict validation of record identity, ordering, partial writes and corrupt/truncated input. Both historical observer record versions remain accepted explicitly. The production network CI lane runs these checks and the rendered USB profile editor, including custom uncertainty values, explicit preset application, logging refusal feedback and desktop/mobile light/dark layouts.

Both production network and USB pairs exercise all four GPIO drive strengths and a byte-identical Pi management producer from revision `411883266bfe4bcbf845b3b8f1caabd74851c23e`, before drive-field support. The checked fixture's hash and provenance are verified before execution. Engineering TLS and USB accept its complete-station Save while preserving the separate drive setting. Consumer-Radio refuses its profile-owned station writes without changing Pico journal bytes, generation or cursor and preserves drive during valid unrelated saves. Invalid strengths must fail without mutation. The USB CI invocation sets `WSPRRYPI_GPIO_DRIVE_REQUIRED=1`, so missing drive support cannot silently bypass these cases. These checks establish software interoperability, not physical pad current or RF output. Installed-journal resume checks and hardware probes remain explicit opt-in tools.

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

## CI ownership, synchronization and build reuse

The non-hardware workflow retains nine expanded contexts: scope admission, the simulated macOS profile, native Si5351 ARM and Intel profiles, strict Linux I2C, six sequential GCC release profiles, Debian full validation, and the production network and USB software pairs. Platform, capability and carrier contexts provide different evidence; a portable or mocked subset does not replace the full profile or either actual pair.

The scope job resolves the selected Pico revision once. Both carrier jobs require and verify that immutable revision before building. Manual dispatch accepts `pico_ref`; select the intended committed companion explicitly. The network job also checks out its pinned Mbed TLS dependency, and Debian verifies the pinned RP1 provider. Missing, malformed or mismatched source selections fail without substituting another checkout.

`make ci-focused-state-test ci-network-policy-test ci-support-lifecycle-test SUDO=` runs the focused parent state, network policy and support lifecycle owners. `make ci-installer-offline-test SUDO=` runs the offline installation owners. Their workflow execution retains the named syscall traces and final fail-closed hardware-access audit. The audit covers those named processes, not every program or physical hardware behavior. `scripts/tests/ci_execution_graph_test.py` checks execution ownership and source-selection refusal; `scripts/tests/network_build_test.py` exercises real compiler input changes, failure and recovery.

Tests establish readiness and completion through acknowledgments, held transactions, actual lock contention, parser state or terminal results. Operation deadlines, bounded failure guards, logical test clocks and observed duration have separate purposes. A sleep or absence of a marker does not prove entry into a race or completion of cleanup. Preserve the primary failure and secondary cleanup errors, retain ownership of unreaped children, and refuse reuse while cleanup remains unresolved. Fixed-delay manual fixtures are not ordinary-CI timing qualification.

The UI browser fixtures share CDP/startup/HTTP and owned-process primitives in `WsprryPi-UI/tests/browser_harness.js` and `browser_fixture_process.js`. Their fault checks run with `node --test tests/browser_harness_test.js` and `node tests/browser_fixture_process_test.js` from `WsprryPi-UI`. Scenario assertions and production carrier adapters remain independent of the shared plumbing; the dependency-free USB lane must not acquire an undeclared browser package.

Compatible network test binaries share object compilation through `scripts/network_build_rules.mk` and `scripts/build_identity.py`. Reuse depends on compiler-selected inputs, flags, toolchain, dependency contents and output identity; missing or unstable identity refuses reuse. Required test executables still run on unchanged builds. Different architectures, profiles, source revisions and cryptographic contexts do not share artifacts without equivalent input identity. Build budgets respect the parent jobserver and available resources.

Software checks do not qualify physical USB, installation, services, GPIO, transmitter timing or RF. Native inventory delays and historical tracer failures without a demonstrated cause remain diagnostic limitations; a later successful run alone does not establish a repair. Keep manual/component entry points and recovery assets unless their present duties and equivalent replacements are established.

The production TLS pair uses a logical UTC epoch advanced by one monotonic clock;
it does not repeatedly correlate independently sampled host wall/steady clocks.
Its future scheduled-job case selects a full UTC second at least 60 seconds ahead.
The actual Pico dry-run engine retains its UTC-second launch window and 1-ms
uncertainty policy. `wtp_fixture_clock_test` checks exact projection, a valid
start, and narrow-window, late-start, and inconsistent-clock refusals without
sleeping. Host results do not qualify UTC accuracy or physical launch timing.

The USB pair lane uses the named Ubuntu 24.04 runner's installed CMake, Python,
Node, C++ compiler, Make, and OpenSSL development tools. It checks executable
availability, records versions and OpenSSL flags, then performs the actual CMake
configure, compiled endpoint/client builds, and production pair checks. Missing
or unusable tools fail the lane; setup does not silently install alternative
CMake or Node packages. These image tools are inventoried inputs, not a claim of
byte-reproducible system libraries.

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
