# Hardware-free simulated backend

The simulated backend provides an explicitly selected, non-RF path for Debian
development and CI. It runs the normal request planning, execution-plan
scheduling, cancellation, status, failure, and cleanup lifecycle through
`ITransmissionBackend`, but it does not initialize GPIO, MMIO, mailbox, DMA,
I2C, or transmitter device nodes.

## Build and run

Build the debug executable from `src`:

```sh
make debug
```

Run a short virtual-time QRSS execution as an unprivileged user:

```sh
./build/bin/wsprrypi_debug \
  --backend simulated \
  --no-web \
  --qrss-message E \
  --qrss-frequency 14097100 \
  --qrss-dot-seconds 0.01
```

Simulation is CLI-only and transient. It is never selected as a fallback and is not written to the application configuration. Physical GPIO and Si5351 selections retain their existing requirements and RF safety policy.

## Trace contract

The simulator writes `/tmp/wsprrypi-simulated-trace.json`. The stable JSON document identifies the schema version, backend, plan, ordered events, logical nanosecond timestamps, frequencies, RF state, completion, cancellation, injected failures, and cleanup. Virtual time is the default integration mode, so identical requests produce byte-identical traces without waiting for RF-duration wall-clock time.

The reusable `WSPR-Transmitter` component tests exercise normal completion,
cancellation before and during execution, configure and execute failure, cleanup
failure propagation, repeated execution, deterministic time, startup quiescence,
identity, capabilities, and mode rejection:

```sh
cd WSPR-Transmitter/src
make simulated-backend-test transmission-controller-contract-test
```

The parent regression suite also exercises configuration failure, backend replacement, explicit shutdown, execution, cancellation, repeated cleanup, and destructor-safe cleanup handling:

```sh
cd ../..
make build/bin/cleanup_lifecycle_test
./build/bin/cleanup_lifecycle_test
```

## Selection and injection surfaces

The available controls depend on how the simulator is constructed:

| Surface | Time mode | Fault injection | Trace path |
| --- | --- | --- | --- |
| Application CLI | Explicit `--backend simulated`; virtual time only | Not exposed | `/tmp/wsprrypi-simulated-trace.json` |
| `WsprTransmitter::selectBackend()` | `SimulatedRuntimeConfig::virtual_time` | All simulator fault fields | `SimulatedRuntimeConfig::trace_path` |
| Direct `SimulatedTransmitBackend` construction | `SimulatedBackendConfig::virtual_time` | All simulator fault fields | `SimulatedBackendConfig::trace_path` |

`--backend simulated` is intentionally the only simulator control exposed by the production application CLI. Simulation timing, trace-path overrides, and lifecycle fault injection are transient typed C++ test/developer APIs. They are not parsed from CLI arguments, INI configuration, environment variables, or the UI, and they never apply to physical backends. Additional shell-facing controls require a separately reviewed developer-harness use case.

The CLI selection is transient, is never persisted, and is never selected automatically after hardware initialization fails. In managed mode it remains selected across INI reloads and HTTP configuration writes until the process exits. The managed WTP endpoint uses real-time simulation through its typed C++ construction so remotely armed jobs follow wall-clock scheduling; direct local simulated jobs retain the existing virtual-time behavior. The application calls the public three-argument `WsprTransmitter::selectBackend()` overload internally with default simulator settings.

`WsprTransmitter` owns a small private construction switch rather than a dynamic plugin system. Tests that need the application lifecycle can inject typed settings through the public overload:

```cpp
WsprTransmitter transmitter;
WsprTransmitter::SimulatedRuntimeConfig simulation;
simulation.virtual_time = false;
simulation.trace_path = "/tmp/wsprrypi-real-time-trace.json";

transmitter.selectBackend(
    wsprrypi::BackendKind::SIMULATED,
    WsprTransmitter::Si5351RuntimeConfig{},
    simulation);
```

There is no general backend-factory callback or registry to replace `createBackend()`. Fault-focused backend contract tests normally construct `SimulatedTransmitBackend` directly, while parent lifecycle tests use the typed `selectBackend()` seam.

## Direct backend construction

Direct construction requires an `IExecutionContext`. The context supplies stop observation, interruptible waiting, and progress reporting. `logicalNow()` is part of the reusable context contract, but the current simulator trace takes logical timestamps from each `ExecutionPlan` event rather than calling `logicalNow()`.

This abbreviated pattern shows the canonical lifecycle; see `src/WSPR-Transmitter/src/simulated_transmit_backend_test.cpp` for a complete focused test and `src/WSPR-Transmitter/src/transmission_controller_contract_test.cpp` for controller-owned preparation and cleanup:

```cpp
class TestContext final : public wsprrypi::IExecutionContext {
public:
    bool stop_requested = false;

    bool stopRequested() const noexcept override { return stop_requested; }
    bool waitInterruptibleFor(std::chrono::nanoseconds) override {
        return !stop_requested;
    }
    void reportExecutionProgress(std::size_t) noexcept override {}
    std::chrono::nanoseconds logicalNow() const noexcept override { return {}; }
};

TestContext context;
wsprrypi::SimulatedBackendConfig config;
config.virtual_time = true;
config.trace_path = "/tmp/example-simulation.json";
wsprrypi::SimulatedTransmitBackend backend(context, config);

wsprrypi::ExecutionPlan plan;
plan.id.value = 1;
plan.request_id.value = 400;
plan.backend = wsprrypi::BackendKind::SIMULATED;
plan.mode = wsprrypi::TransmissionMode::QRSS;
plan.reference_frequency_hz = 14097100.0;
plan.events.push_back({
    std::chrono::nanoseconds{0},
    std::chrono::milliseconds{10},
    wsprrypi::RfEventType::RF_ON,
    14097100.0,
    true});
plan.summary.total_duration = std::chrono::milliseconds{10};

const auto configured = backend.configure(plan, {});
if (!configured.ok)
    throw std::runtime_error(configured.error);
const auto execution = backend.execute(plan);
const auto cleanup = backend.cleanup();
```

Application and most integration code should use `TransmissionController`, which compiles a `TransmissionRequest`, assigns a controller-owned plan identity, calls `configure()`, and always attempts cleanup after execution. Direct lifecycle calls are appropriate for focused backend-contract tests, not as a second application architecture.

## Virtual and real-time execution

Both configuration types default `virtual_time` to `true`.

- In virtual time, the simulator walks the existing execution plan without waiting for event durations. Event offsets and completion time remain the plan's logical nanosecond values. Current QRSS and complete-WSPR CI exercise this mode and enforce accelerated wall-clock completion.
- With `virtual_time = false`, the simulator calls `IExecutionContext::waitInterruptibleFor(event.duration)` before reporting and tracing each event. A `false` wait result returns a stopped `ExecutionResult` and records `cancelled` at that event.
- `stopRequested()` and the backend's `stop()` state are checked before every event in both modes. Virtual-time cancellation is therefore observed between events. Real-time contexts can additionally interrupt an in-progress wait.
- The application bridge implements interruptible waits with the transmitter stop condition. A direct test double decides how waiting and cancellation behave.

The checked-in CI and simulator contract tests cover virtual-time execution, injected cancellation, context stop observation, bounded successful real-time waiting, interruptible real-time cancellation, and repeated real-time execution. The real-time test uses deliberately broad elapsed-time bounds and a process timeout; it proves that waits occur and remain interruptible, but it does not measure scheduler precision or jitter. Real-time mode is available to C++ tests through the typed configuration, but it is not CLI-selectable and is not hardware, RF, scheduler, or timing qualification.

## Fault-injection reference

These fields exist with the same names and defaults in `SimulatedBackendConfig` and `WsprTransmitter::SimulatedRuntimeConfig`:

| Field | Type and default | Trigger and result | Trace evidence | CLI |
| --- | --- | --- | --- | --- |
| `virtual_time` | `bool`, `true` | `false` enables interruptible per-event waits | Ordinary lifecycle events | No |
| `trace_path` | `std::string`; empty for direct backend config, `/tmp/wsprrypi-simulated-trace.json` for application runtime config | Nonempty path is replaced whenever the trace is rendered | JSON document at the selected path | No |
| `fail_startup_quiesce` | `bool`, `false` | `quiesceForStartup()` returns `ok == false` | `startup_quiesce_failure`, detail `injected` | No |
| `fail_configure` | `bool`, `false` | `configure()` returns `ok == false` | `configure_failure`, detail `injected` | No |
| `fail_event` | `long`, `-1` | Matching zero-based event index returns `faulted == true` | `execution_failure`, detail `injected` | No |
| `cancel_event` | `long`, `-1` | Matching zero-based event index returns `stopped == true` | `cancelled`, detail `injected` | No |
| `fail_cleanup` | `bool`, `false` | Armed cleanup returns `ok == false` | `cleanup_failure`, detail `injected` | No |

A negative index disables index-based execution and cancellation injection. Startup-quiescence failure is injected before configuration or execution state is armed. It returns `Injected simulated startup quiesce failure.`, and every call appends one deterministic `startup_quiesce_failure` record. Repeated calls return the same failure; explicit cleanup remains safe and is distinct from the unarmed `fail_cleanup` execution fault. The application startup gate retains the error and inhibits later preparation, scheduling, and transmission.

Configure failure occurs after the initial `configure` record. `TransmissionController::prepare()` then attempts cleanup; if cleanup also fails, both error messages are preserved. Execution failure and cancellation likewise remain observable if the subsequent cleanup fails, and cleanup failure forces the combined lifecycle result to remain failed rather than reporting false completion or cancellation success.

External cancellation is distinct from `cancel_event`: `stopRequested()`, `stop()`, or an interrupted real-time wait records `cancelled` without the `injected` detail. The focused simulator test covers `fail_event`, `cancel_event`, context stop observation, and `fail_cleanup`. `src/tests/cleanup_lifecycle_test.cpp` covers application-level configure-plus-cleanup failure, execution cleanup failure, cancellation cleanup failure, backend replacement, repeated cleanup, and destructor-safe reporting.

Tests inject startup-quiescence failure through either typed configuration surface before selecting or constructing the backend:

```cpp
WsprTransmitter::SimulatedRuntimeConfig simulation;
simulation.fail_startup_quiesce = true;
transmitter.selectBackend(
    wsprrypi::BackendKind::SIMULATED,
    WsprTransmitter::Si5351RuntimeConfig{},
    simulation);
```

Direct backend tests use `SimulatedBackendConfig::fail_startup_quiesce` in the same way. This validates software propagation and inhibition only; it does not qualify physical startup quiescence, GPIO, I2C, MMIO, DMA, mailbox, services, scheduling accuracy, or RF behavior.

## Repeated execution and state reset

A simulator instance supports the repeated lifecycle `configure -> execute -> cleanup -> configure -> execute -> cleanup`. Each `configure()` starts a new run by clearing prior trace items and rendered JSON, clearing the stop state, arming cleanup, clearing the cleanup-recorded flag, and capturing the new plan ID, request ID, and mode. The immutable construction configuration—including selected time mode, trace path, and injected faults—remains attached to the backend instance.

Cleanup clears configured and stop state. Its trace record is idempotent: the first cleanup for a run appends exactly one `cleanup` or `cleanup_failure` record, while later cleanup calls do not append duplicates. When `fail_cleanup` is enabled, repeated armed cleanup calls continue to return the deterministic failure even though only one failure record is present.

The same-instance simulator test demonstrates trace reset through a second configure and execute and separately checks repeated-cleanup trace idempotence. Parent cleanup-lifecycle tests cover repeated cleanup outcome preservation. Independent application processes producing byte-identical traces demonstrate cross-process reproducibility; they do not by themselves prove same-instance reset.

`TransmissionController` preserves `TransmissionRequest::id` as `ExecutionPlan::request_id` and assigns plan IDs starting at 1 for each controller instance, incrementing after each successful compile (including plans later rejected by capability checks or backend configuration). Reconstructing a controller restarts that local plan sequence. Direct backend tests provide their own plan and request identities.

## Complete WSPR integration check

The hardware-free CI suite also runs a complete WSPR request through the normal application CLI, WSPR reference preparation, execution-plan compiler, controller preparation, immediate virtual-time dispatch, simulated backend, completion, and cleanup paths. Run the same check from `src` after building the debug executable:

```sh
../scripts/ci/verify-simulated-wspr.sh
```

The script runs this unprivileged request twice with virtual time and compares the JSON traces byte for byte:

```sh
./build/bin/wsprrypi_debug --backend simulated --no-web --no-offset \
  --no-system-clock-frequency-estimate --terminate 1 AA0NT EM18 20 20m
```

It verifies the WSPR mode, distinct request and plan identities, the exact configure-to-cleanup lifecycle order and lifecycle fields, all 162 ordered tone indexes against the pinned WSPR-Reference golden vector, the 14,095,600 Hz dial-to-14,097,100 Hz RF-center conversion, all four absolute tone frequencies, the canonical 682,666,666 ns truncated symbol duration, the exact 110,591,999,892 ns frame duration, RF state, completion, cleanup, and the absence of extra or failure events. Each application run has a 10-second wall-clock limit, comfortably below the logical frame duration, so loss of accelerated virtual time fails the check. Independent processes must produce byte-identical traces, demonstrating cross-process reproducibility. The simulator backend contract test separately repeats configure, execute, and cleanup on the same backend instance and verifies that trace state is reset.

The simulated WSPR run intentionally selects immediate virtual-time dispatch. It does not exercise or qualify the normal wall-clock WSPR window scheduler.

The trace keeps schema version 1 because `request_id` is an additive identity field and existing fields retain their names and semantics. Consumers must continue to tolerate additional JSON fields.

The final JSON trace is `/tmp/wsprrypi-wspr-trace.json`. The second run's file-access trace is `/tmp/wsprrypi-wspr-simulator.strace`; the CI hardware audit checks it together with the other non-hardware traces.

This check validates application and backend software contracts. It does not qualify WSPR timing on physical hardware, GPIO, DMA, mailbox, MMIO, I2C, Si5351 output, RF output or accuracy, installation, services, a Raspberry Pi model, a band, or a transmitter chain.

## Portable semantics validation

`make semantics-test` has one deliberately stable meaning: the full Linux
semantics suite. It compiles the physical and simulated backend integrations,
requires the `gpiod.hpp` C++ development header, and fails before compilation
with a portable-target recommendation when those prerequisites are absent.
The full suite can still be hardware-free at runtime; CI sets
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` and audits device access separately.

On macOS and other development hosts without the Linux GPIO toolchain, run:

```sh
cd src
make semantics-test-portable SUDO=
```

The portable target pins `BACKENDS=simulated ANCILLARY_GPIO=0`; caller-provided
backend values cannot broaden it. It builds the simulated application and runs
the capability, band lookup, clock-model, correction, network-recovery,
UI/source, GPIO band-policy, JavaScript, and UI-publication checks that are
valid under that profile. It does not run the backend-specific runtime
semantics executable or cleanup-lifecycle executable, whose current assertions
require physical backend capabilities. Therefore a portable pass is not a full
semantics pass and is not physical-backend, hardware, timing, installation,
service, or RF qualification.

## Debian CI

The `Debian non-hardware validation` workflow runs in a Debian Trixie container.
It checks out the parent repository with its ordinary component trees, builds the
parent debug executable, runs the semantics and cleanup lifecycle suite,
exercises the RP1 test-double regression, runs the
simulator/controller/startup/Si5351 contract tests, compares deterministic QRSS
and complete WSPR virtual-time traces, and audits file access with `strace`.

For consistent behavior with GNU and uutils `timeout`, the bounded WSPR helper
uses foreground mode while keeping `timeout` as the direct supervisor of
WsprryPi.

The workflow sets `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, passes an empty `SUDO` make variable, and rejects access to transmitter GPIO, MMIO, mailbox, DMA, I2C, or RP1 device paths. It does not run installation, service, test-tone, live qualification, or hardware targets.

Run the same checks locally from the repository root with the commands in `.github/workflows/debian-non-hardware.yml`. The workflow establishes software-contract evidence only; it is not physical-hardware or RF qualification.

Focused physical-backend tests remain separate and must continue to pass. Simulator results demonstrate application and backend-contract behavior only; they do not qualify GPIO timing, I2C hardware, RF output, installation, services, or any physical transmitter.
