# Development guide

WsprryPi's application, first-party web UI and reusable components are ordinary content in one Git repository. Follow [AGENTS.md](../../AGENTS.md) and preserve the [component boundaries](../components/README.md), standalone tests, licenses and attribution.

## Builds and safe validation

Run application tests from `src`. Inspect a target before executing it; the ordinary `make test` target can operate hardware. Installation, services, GPIO, transmissions and other device operations require separate authorization.

On Linux with libgpiod C++ development headers, the hardware-disabled profile is:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
```

The explicit subset for a non-Linux host or Linux without those headers is:

```sh
make semantics-test-portable SUDO=
```

This selects simulated-only coverage with ancillary GPIO disabled. It excludes tests requiring physical backend capabilities. Do not retry a known broken macOS C++ toolchain without explicit repair or local-build authorization. Use an identified Linux environment for affected C++ validation when needed.

Read the [simulation contract](../simulated-backend.md) before exercising the application without hardware. The [Debian CI workflow](../../.github/workflows/debian-non-hardware.yml) defines the canonical unprivileged suite. Use [container builds](../container-builds.md) and the [precompiled-installation contract](../precompiled-installation.md) for artifact production. Building an executable does not qualify installation or RF.

On its selected branches, the non-hardware workflow starts with a lightweight change-classification job. It inspects the complete Git diff rather than GitHub's bounded path-filter list. Only ordinary documentation skips the full jobs: the root `README.md`, prose under `docs/` or `release_tools/`, and images/PDFs under `docs/`. Other paths, including Markdown in source or test directories, require full validation. The exact policy lives in `scripts/ci/change_scope.py`.

`AGENTS.md`, the support-bundle intake contract, distributed license notices and protected fixture/evidence directories remain checked inputs. Mixed changes, executable bits, symlinks, submodule/type changes and moves across the documentation boundary also require full CI. Pushes use a two-dot comparison; pull requests use the merge-base comparison. New branches, force pushes, empty or unavailable comparisons and classification failures default to full validation. Cancellation still stops dependent jobs. Run the focused regressions from `src` with `make ci-change-scope-test SUDO= CXX=true GPIOD_CPP_HEADER_AVAILABLE=0`.

Concurrency groups include the commit SHA so a documentation-only successor cannot cancel or replace an earlier code-validation run. Different commits can therefore validate concurrently; duplicate runs of the same commit retain cancellation. When documentation becomes a build or regression input, add it to the classifier's checked-input policy before relying on it in tests.

## Hardware-free controller build

The explicit controller build reuses the controller role and remote-management runtime with no physical or simulated local output. Build from `src`:

```sh
make -j2 debug BUILD_PROFILE=controller SUDO=
./build/bin/profiles/controller/wsprrypi_debug --version
make controller-build-profile-test controller-profile-entry-test controller-transmitter-boundary-test BUILD_PROFILE=controller SUDO=
```

The profile requires empty `BACKENDS`, `ANCILLARY_GPIO=0`, and no explicit `SI5351_TRANSPORT`. Unset a transport selected in the shell before building this profile. Ordinary transmitter builds retain their backend validation and default role. The controller executable requires `--controller-only` for normal startup; safe `--help`, `--version`, and `--list-backends` do not construct a transmitter. An empty backend listing is intentional. Legacy transmitter run, debugger, install, and uninstall targets refuse this profile before building or operating an ordinary transmitter alias.

Controller objects, generated headers, link maps, license notices, and executables use isolated `controller` paths. Its artifact does not replace the ordinary `build/bin/wsprrypi` or `build/bin/wsprrypi_debug` aliases. The explicit `scripts/controller_sources.mk` manifest admits sources. Parent no-output adapters refuse local transmitter, PPS capture, and RP1 route operations; the controller excludes the local WTP execution runtime while retaining shared identity and private-state validation.

The [controller browser route inventory](controller-browser-routes.md) records the role bootstrap, navigation, HTTP methods, WebSocket commands, and hardware-free browser checks.

A build is not a qualified installation. The [platform matrix](issue-475-platform-matrix.md) records current #475 evidence and outstanding native gates. The finite `src/tests/controller_profile_production_test.py` gate and reviewed `scripts/tests/controller_native_namespace.sh` isolation launcher require an explicitly authorized isolated native Linux amd64 environment, private OS home, hidden host service sockets, and ordinary unprivileged execution. It uses the actual binary and host identity without fixture identity or hardware-suppression environment, checks empty enrollment and restart, refuses local operations, and audits syscall traces. Do not run it against an interactive account's unisolated home or live transmitters. Native path/transport adapters, browser acceptance, and installed services belong to later authorized slices.

## Production application and browser regressions

The Linux application regression builds the current source with the explicit simulated backend and ancillary GPIO disabled. It starts the explicit simulated backend in a private network namespace, verifies one-time migration and refusal of invalid source configuration through the actual application, and runs finite browser-read profiles with synthetic schedule time. A separate frontend serves reads, which must preserve the SQLite/WAL image, retired source backup, and exposed configuration. It opens no transmitter device and supplies no RF qualification.

```sh
make -C src production-browser-regression-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= TEST_NETNS_RUNNER="sudo unshare --net"
make -C src tls-observer-test SUDO=
```

The TLS observer regression uses fresh loopback certificates, concurrent local streams and strict validation of record identity, ordering, partial writes and corrupt/truncated input. Both historical observer record versions remain accepted explicitly. The production network CI lane runs these checks and the rendered USB profile editor, including preserved explicit connection values, management-only selection, logging refusal feedback and desktop/mobile light/dark layouts.

Both production network and USB pairs exercise all four GPIO drive strengths and a byte-identical Pi management producer from revision `411883266bfe4bcbf845b3b8f1caabd74851c23e`, before drive-field support. The checked fixture's hash and provenance are verified before execution. Engineering TLS and USB accept its complete-station Save while preserving the separate drive setting. Consumer-Radio refuses its profile-owned station writes without changing Pico journal bytes, generation or cursor and preserves drive during valid unrelated saves. Invalid strengths must fail without mutation. The USB CI invocation sets `WSPRRYPI_GPIO_DRIVE_REQUIRED=1`, so missing drive support cannot silently bypass these cases. These checks establish software interoperability, not physical pad current or RF output. Installed-journal resume checks and hardware probes remain explicit opt-in tools.

## Maintained contracts and tools

- [Native execution plans](../wtp-execution-plan.md), [Pico management](../wtp-backend.md), [scheduling](../wtp-scheduling.md), [status/recovery](../wtp-status-recovery.md), [USB management](../wtp-usb-management.md) and [network transport](../wtp-network.md).
- [Pico configuration compatibility](../wtp-configuration-compatibility.md), [browser API](../wtp-browser-api.md), [Pi authority](../wtp-pi-endpoint-contract.md) and [Fleet](../wtp-fleet.md).
- [RP1 installation](../rp1-gpclk-dkms-installation.md), [provider acceptance](../rp1-provider-acceptance.md), [qualification metadata](../rp1-qualification-metadata.md) and [Si5351 support limits](../si5351-support.md).
- [Installer/support tools](../../scripts/README.md) and [maintainer runbook](../../release_tools/WsprryPi%20Full%20Regression%20Test%20Runbook.md).
- [Release artifact preparation](../../release_tools/Build%20Orchestration.md) and [calibration authority and scope](../../release_tools/Calibration%20Scope%20Decisions.md).

The CI rollback regression and its imported helper remain at `wtp-reply-routing-results/rollback_test.py` and `live_target.py`. The regression mocks commands and network recovery against a private temporary filesystem; run it with `python3 -B ../docs/development/wtp-reply-routing-results/rollback_test.py` from `src`. The helper contains host-specific service/network operations and is not an ordinary test entry point. Its live commands require separate authorization.

## CI ownership, synchronization and build reuse

The non-hardware workflow runs scope admission, simulated macOS and native Si5351 checks on Apple silicon, strict Linux I2C, GCC release builds, Debian full validation, and the production network and USB software pairs. Each platform, capability, and carrier check provides evidence for its own test configuration.

The scope job resolves the selected Pico revision once. Both carrier jobs require and verify that immutable revision before building. The default companion is the committed Pico revision in the vendored control contract's provenance. Regenerate the vendored contract from the committed Pico candidate when updating the pair. Manual dispatch accepts an explicit `pico_ref` override. The network job also checks out its pinned Mbed TLS dependency, and Debian verifies the pinned RP1 provider. Missing, malformed, or mismatched source selections fail without substituting another checkout.

`make ci-focused-state-test ci-network-policy-test ci-support-lifecycle-test SUDO=` runs the focused parent state, network policy and support lifecycle owners. `make ci-installer-offline-test SUDO=` runs the offline installation owners. Their workflow execution retains the named syscall traces and final fail-closed hardware-access audit. The audit covers those named processes, not every program or physical hardware behavior. `scripts/tests/ci_execution_graph_test.py` checks execution ownership and source-selection refusal; `scripts/tests/network_build_test.py` exercises real compiler input changes, failure and recovery.

Tests establish readiness and completion through acknowledgments, held transactions, actual lock contention, parser state or terminal results. Operation deadlines, bounded failure guards, logical test clocks and observed duration have separate purposes. A sleep or absence of a marker does not prove entry into a race or completion of cleanup. Preserve the primary failure and secondary cleanup errors, retain ownership of unreaped children, and refuse reuse while cleanup remains unresolved. Fixed-delay manual fixtures are not ordinary-CI timing qualification.

The UI browser fixtures share CDP/startup/HTTP and owned-process primitives in `WsprryPi-UI/tests/browser_harness.js` and `browser_fixture_process.js`. Their fault checks run with `node --test tests/browser_harness_test.js` and `node tests/browser_fixture_process_test.js` from `WsprryPi-UI`. Scenario assertions and production carrier adapters remain independent of the shared plumbing; the dependency-free USB lane must not acquire an undeclared browser package.

Compatible network test binaries share object compilation through `scripts/network_build_rules.mk` and `scripts/build_identity.py`. Reuse depends on compiler-selected inputs, flags, toolchain, dependency contents and output identity; missing or unstable identity refuses reuse. Required test executables still run on unchanged builds. Different architectures, profiles, source revisions and cryptographic contexts do not share artifacts without equivalent input identity. Build budgets respect the parent jobserver and available resources.

Software checks do not qualify physical USB, installation, services, GPIO, transmitter timing or RF. Native inventory delays and historical tracer failures without a demonstrated cause remain diagnostic limitations; a later successful run alone does not establish a repair. Keep manual/component entry points and recovery assets unless their present duties and equivalent replacements are established.

The production TLS pair uses a logical UTC epoch advanced by one monotonic clock; it does not repeatedly correlate independently sampled host wall/steady clocks. Its future scheduled-job case selects a full UTC second at least 60 seconds ahead. The actual Pico dry-run engine retains its UTC-second launch window and 1-ms uncertainty policy. `wtp_fixture_clock_test` checks exact projection, a valid start, and narrow-window, late-start, and inconsistent-clock refusals without sleeping. Host results do not qualify UTC accuracy or physical launch timing.

The USB pair lane uses the named Ubuntu 24.04 runner's installed CMake, Python, Node, C++ compiler, Make, and OpenSSL development tools. It checks executable availability, records versions and OpenSSL flags, then performs the actual CMake configure, compiled endpoint/client builds, and production pair checks. Missing or unusable tools fail the lane; setup does not silently install alternative CMake or Node packages. These image tools are inventoried inputs, not a claim of byte-reproducible system libraries.

## SQLite, local control, and recovery

The [storage and process contract](sqlite-storage-contract.md) describes authoritative settings, one-time migration, private owners, separated services, and platform boundaries. The [local control guide](../../src/application_control/README.md) documents advanced settings and diagnostics. The [recovery guide](../../src/application_recovery/README.md) documents the shared CLI/Maintenance engine, encrypted archives, explicit confirmation, computer moves, and destination-review holds. `make recovery-integration-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` runs the bounded actual-process role, relocation, round-trip, listener-override, and crash matrix. Browser checks use the separate `recovery-maintenance-browser-test` and `recovery-maintenance-controller-browser-test` targets with the existing local browser harness.

## Work and cleanup

The [shared Pi/Pico transmission history and Grafana plan](transmission-history-grafana-plan.md) proposes reception charts, retained transmission observations, and conservative spot association. It records implementation slices, acceptance criteria, source limitations, and unresolved decisions; it does not describe implemented behavior or authorize deployment.

The [release punchlist](../../release_tools/Release%20Punchlist.md) is the selected work authority. Beta 1 preparation retains its exact candidate identities, artifacts and unfinished acceptance gates; a later source commit does not replace that candidate or establish release readiness.

Follow the [repository and work-harness cleanup instructions](repository-cleanup-prompt.md) for an explicit cleanup request. Inventory exact deletion, rewrite, retention and hold decisions first. Preserve current dependencies, active work and recovery assets; retire obsolete prompts and evidence without creating an archive.

## Selected coordinated logging work

The [UDP syslog plan](udp-syslog-plan.md) links the reviewed cross-repository design and coordinated Pi/backend/UI deliverables.

The [UDP logging contract](udp-logging.md) describes the worker package, export-safe journal projection, independent installation status and finite tests. The linked Pico qualification record identifies successful production pairs, physical acceptance and restoration; the combined backlog tracks coordinated publication.
