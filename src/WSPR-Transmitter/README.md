# WSPR Transmitter

`WSPR-Transmitter` is a C++20 component for generating WSPR (Weak Signal
Propagation Reporter) RF transmissions with precise symbol timing.

The current production-proven path is a Raspberry Pi backend that uses Broadcom
mailbox, DMA, PWM, and clock hardware to emit RF. The codebase has been
refactored so transmission control and scheduling live in a generic controller,
while platform-specific hardware work lives behind a backend interface.

## Standard Feld compiler contract

The backend-neutral compiler supports the immutable
`standard-feld-wsprry-v1` profile. This is a production data and execution-plan
contract only; neither hardware backend is qualified for Standard Feld by this
source-level implementation.

The profile uses the MIT-licensed
`wsprry-standard-feld-radiolib-5x5-v1` asset derived from RadioLib commit
`0795caa41c6350a2f862137cfc22528c2aaad2bc`. Its retained license is in
`third_party/RadioLib-Hellschreiber-LICENSE.txt`. The canonical asset SHA-256 is
`025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227`.

The compiler:

- accepts ASCII space through underscore and normalizes lowercase ASCII to
  uppercase;
- rejects an empty message or any other byte before constructing a plan;
- preserves fixed seven-column cells and every accepted space;
- adds one blank leader and trailer cell;
- scans columns left to right and 14 physical positions bottom to top;
- derives every boundary from integer position counts at exactly 245
  positions per second;
- emits one event per physical position so every position remains an explicit
  cancellation and progress boundary;
- finishes in the RF-disabled state.

For `C` normalized characters the plan contains `98 × (C + 2)` positions and
has exact duration `2 × (C + 2) / 5` seconds. Absolute nanosecond boundaries
use integer round-half-up of `n × 1,000,000,000 / 245`; rounded per-position
durations are never accumulated.

To verify the checked-in production table against the canonical research JSON:

```bash
python3 tools/check_standard_feld_asset.py \
  --asset ../../docs/research/standard-feld/assets/font/font.json
```

Use `--emit` to print the deterministic initializer for maintainer review.
The compiler test is available as `make standard-feld-test` where the
standalone Makefile is supported.

This slice does not add parent configuration, scheduling, CLI, persistence,
status, UI, backend enablement, physical timing tests, RF tests, or release
support. Those remain separate integration and qualification work.

- **EVIDENCE:** The production table checker matches all 64 stored glyphs to
  the checksum-pinned canonical asset, and the compiler test covers frozen
  input, raster, timing, duration, progress, cancellation-boundary, and
  terminal-state contracts.
- **DESIGN DECISION:** One event is retained per physical position so every
  frozen position boundary is directly addressable for cancellation and
  progress reporting.
- **INFERENCE:** The existing backend-neutral event vocabulary can represent
  the compiled raster without another hardware primitive.
- **UNRESOLVED QUALIFICATION:** Neither existing hardware backend accepts or
  physically realizes this mode yet. Timing, cancellation latency, safe idle,
  spectral behavior, RF behavior, and operator behavior remain unassessed.

## What The Project Does

The component can:

- Configure a WSPR transmission from RF frequency, power level, PPM
  correction, and optional callsign/grid/power message fields
- Run either continuous-tone output or full WSPR symbol transmissions
- Schedule WSPR transmissions on proper window boundaries
- Stop, reconfigure, and restart safely
- Monitor backend-specific hardware faults and recover through backend-owned
  recovery logic

The public facade is `WsprTransmitter`. Higher-level code uses that class and
does not need to manipulate DMA, PWM, mailbox, or watchdog details directly.

## High-Level Architecture

The architecture is split into three layers:

### 1. Controller / Facade

`WsprTransmitter` owns:

- Public API
- Transmission configuration state
- Scheduler and worker-thread orchestration
- High-level transmission state machine
- Symbol timing decisions
- Callback delivery

The controller decides when a transmission starts and when each symbol should
be emitted. It does not own backend-private hardware sequencing state.

### 2. Generic Backend Interface

`WsprTransmitBackend` defines the hardware-facing contract used by the
controller. The interface is intentionally expressed in generic transmission
operations rather than Raspberry Pi DMA concepts.

Shared backend-neutral data types live in `wspr_transmit_types.hpp`:

- `WsprTransmissionPlan`
  Snapshot of transmission intent and configuration passed from controller to
  backend. It currently includes:
  - RF center frequency in hertz (Hz)
  - Tone spacing in hertz (Hz)
  - Logical output power level
  - Symbol count
- `WsprTransmissionConfigureResult`
  Small result returned by backend configuration. It currently reports the
  applied RF center frequency in hertz (Hz) after backend quantization or
  hardware adjustment.

### 3. Raspberry Pi Backend

`WsprRpiBackend` owns all Raspberry Pi-specific implementation details,
including:

- Mailbox allocation and peripheral mapping
- DMA control-block setup and ring management
- PWM / GPCLK programming
- GPIO drive-strength based output control
- DMA watchdog monitoring
- Watchdog recovery and hardware reset behavior

Those details are intentionally kept out of the generic controller/backend
boundary.

## Backend Model

The backend model is designed so additional hardware implementations can be
added without changing the controller's scheduling and transmission logic.

Current backend responsibilities:

- Prepare hardware resources for a configured transmission
- Apply the transmission plan to hardware
- Report the applied configuration back to the controller
- Enable and disable RF output
- Emit controller-scheduled symbols
- Handle backend-private fault monitoring and recovery

Current controller responsibilities:

- Build the transmission plan
- Determine symbol timing
- Drive lifecycle sequencing
- Maintain high-level state
- Coordinate stop and restart behavior

## Transmission Lifecycle

At a high level, the lifecycle is:

1. `configure(...)`
   The controller stores user configuration, derives WSPR symbol data when
   message mode is selected, and builds the backend-neutral transmission plan.
2. `prepareTransmission()`
   The backend allocates or initializes hardware-specific resources.
3. `configureTransmission(plan) -> result`
   The backend applies hardware configuration and returns the actual RF center
   frequency in hertz (Hz) that will be used.
4. `startAsync()`
   The controller starts either immediate tone transmission or the WSPR
   scheduler, depending on mode.
5. `beginTransmissionOutput(plan)`
   The backend enables RF output.
6. `emitSymbol(plan, sym_num, tsym, symbol_index)`
   The controller calls this once per scheduled symbol. `tsym` is expressed in
   seconds. In continuous-tone mode, `tsym` is `0.0`.
7. `endTransmissionOutput()`
   The backend disables RF output.
8. `cleanupTransmission()`
   Backend resources are released during shutdown or reconfiguration.

This split keeps timing control in the controller while leaving hardware
sequencing private to the backend.

## Repository Layout

```text
src/
  main.cpp                    Demo / test harness
  Makefile                    Standalone build rules
  wspr_transmit.hpp           Controller / facade public API
  wspr_transmit.cpp           Controller implementation
  wspr_transmit_backend.hpp   Generic backend interface
  wspr_transmit_backend_rpi.hpp
  wspr_transmit_backend_rpi.cpp
                              Raspberry Pi backend
  wspr_transmit_types.hpp     Shared state, plan, and result types
```

## Dependencies

Inside WsprryPi, this component uses ordinary sibling component trees that
provide:

- WSPR message encoding
- Broadcom mailbox access and DMA-safe memory allocation

The standalone `Makefile` resolves them at `../WSPR-Reference`, `../Mailbox`,
and `../Signal-Handler` relative to this component root. The compatibility
stubs under `external/` remain part of this component's standalone boundary.

## Building

```bash
cd src
make debug
```

Common targets:

- `debug` builds the debug test binary
- `release` builds an optimized binary
- `test` runs the hardware-free component suite without `sudo`
- `simulated-backend-test` and `simulated-backend-realtime-test` exercise only
  the simulated backend
- `transmission-controller-contract-test` and `si5351-planner-test` exercise
  backend-neutral contracts
- `si5351-transition-test` uses fake I2C, and `startup-quiesce-test` uses fake
  device boundaries
- `clean` removes build artifacts

The `watchdog` and `gdb` targets are live diagnostics, not ordinary tests. They
refuse to proceed unless `WSPR_TRANSMITTER_LIVE_TEST=YES` is explicitly set.
The live qualification targets only build their executables; running those
executables requires a separate, reviewed hardware/RF procedure.

No hardware test, installation, service operation, GPIO, mailbox, DMA, I2C, or
RF activity is part of the ordinary `test` target.

## Debian Contract CI

The component's retained `Debian backend contracts` workflow records its former
standalone CI. In the monorepo, `.github/workflows/debian-non-hardware.yml` is
the active workflow and runs the simulator, transmission-controller,
startup-quiescence, and fake-I2C transition contracts in an isolated Debian
Trixie checkout without hardware or elevated privileges.

Si5351 transition, startup-quiescence, full debug-build, and physical-backend
regressions use the ordinary sibling component trees recorded by WsprryPi.
Neither workflow performs installation,
service management, transmitter device access, live transmission, or RF
qualification.

## Runtime Notes

- RF frequency values are expressed in hertz (Hz)
- Symbol duration values are expressed in seconds
- Power reporting helpers return estimated milliwatts (mW)
- The Raspberry Pi backend is the only production-verified backend at present

## Current Status

- Controller/backend refactor complete
- Generic backend interface in place
- Raspberry Pi RF path re-verified after refactor
- `WsprTransmissionPlan` and `WsprTransmissionConfigureResult` are part of the
  stable shared interface

## License

This component is covered by the WsprryPi repository's root `LICENSE.md` (MIT).
The former component repository remains an untouched historical reference.

## Extraction

To extract this component for independent use, copy `src/WSPR-Transmitter` and
the required sibling components (`WSPR-Reference`, `Mailbox`, and
`Signal-Handler`) while preserving their relative layout, then add repository
metadata and an appropriate license file. Run the hardware-free suite from this
component's `src` directory before considering any separately authorized
physical-backend qualification. WsprryPi-specific integration should remain
outside the extracted component.
