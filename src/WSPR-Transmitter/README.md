# WSPR Transmitter

`WSPR-Transmitter` is a C++20 component for generating WSPR (Weak Signal
Propagation Reporter) RF transmissions with precise symbol timing.

Legacy GPIO TONE treats the committed RF frequency as the carrier. Its
single-frequency table has zero spacing, so the continuous symbol-zero emitter
does not subtract the WSPR half-span. A request that would require dithering
across an integer-divider boundary is rejected during configuration instead of
silently moving the carrier. The reported frequency is a synthesis target, not
a measurement; clock error and quantization still require separate validation.
WSPR's four centered tones and QRSS/FSKCW/DFCW reference compensation retain
their existing semantics.

Run `make legacy-tone-frequency-test SUDO=` from this component's `src`
directory for hardware-free table and frequency-contract coverage. See the
[frequency correction and validation note](../../docs/issue-446-validation.md)
for historical test-tone calibration handling and physical-validation limits.

On Raspberry Pi 5, RP1 output exclusively uses the externally provisioned
endpoint `/dev/rp1-gpclk`; it never falls back to the legacy DMA backend.
WsprryPi does not install or attest the provider. Runtime protocol and
capability checks, operation-scoped ownership, bounded one-second DMA chunks,
operational readiness, and cleanup remain required. WsprryPi translates every
supported product mode into the provider's generic finite-event UAPI; product
mode and operator authorization remain application policy and are not sent to
the kernel. GPIO4 and GPIO20 are independent routes. The root-owned, mode-0600
endpoint is the production authority. The provider's package version or build
label is not an application allowlist.

### Parent WTP backend integration

`BackendKind::WTP` identifies the parent's explicitly constructed complete-plan
backend. The shared frequency policy treats it as untested; advertised CAPS do
not establish RF qualification. The production `WsprTransmitter` factory refuses
WTP pending production selection and operator integration. The parent now has a
dedicated [early WTP scheduler](../../docs/wtp-scheduling.md) with
[status and recovery reporting](../../docs/wtp-status-recovery.md).
Parent lifecycle, tests and limitations are documented in [the WTP backend guide](../../docs/wtp-backend.md).
No WTP-Client or USB dependency is introduced into this reusable component.

### Shared chipset clock offsets

The reusable `../Chipset-Offsets` component owns the single intrinsic offset
selector: BCM2835 (Pi1) -2.5 ppm, BCM2836/BCM2837 and BCM2711 0 ppm, and RP1
-46.245 ppm. Both physical GPIO backends and the GPIO clock-model API consume
it; no offset table is owned by `WSPR-Transmitter`.
The live DMA backend distinguishes BCM2835 from the 500 MHz chipsets
and applies the Pi1 baseline to RF divider planning. Its separate PWM/DMA
timing correction is unchanged.

All RP1 WSPR, finite/continuous TONE, QRSS, FSKCW, and DFCW divider plans use
the shared intrinsic baseline **-46.245 ppm**, on both GPIO4 and GPIO20. It is
added once to the caller's selected correction before computing divider words:

`corrected_parent_hz = 200000000 * (1 + (caller_ppm - 46.245) / 1000000)`

Positive PPM means the source runs fast. The physical parent remains nominally
200 MHz; this software correction does not retune the parent or alter the DKMS
set/restore lifecycle. The manual/Chrony selection policy and persisted values
are unchanged; their selected value is now an additional correction to the
RP1 baseline. Backend requests carry only that additional correction; the
physical backend adds the intrinsic value once. Intrinsic compensation and the
composed RF total remain internal, not operator settings or displayed values.
Operator candidate and committed status report only the additional correction
(`correction_ppm`; the existing `effective_gpio_ppm` field likewise excludes
intrinsic compensation), with estimate/residual provenance. Refreshing a candidate does not change a committed
request. Each correction input retains its independent +/-200 ppm bound; their
sum may exceed that bound. A previous manual value representing the entire
RP1 source error must have the new intrinsic value subtracted to avoid counting
it twice (for example, a previous total of -46.245 becomes manual 0).
The same accounting applies to Pi1's -2.5 ppm baseline: a manually entered
total correction of -2.5 becomes an additional correction of 0. No persisted
value is rewritten automatically.

Implicit continuous tone is submitted as one maximum-duration finite event.
WsprryPi retains stop ownership and requests bounded drain when the operator or
application stops it. Long QRSS events are submitted directly; the provider's
fixed one-second DMA chunks bound coherent allocation and cancellation latency
without imposing a 120-second logical-event limit.

The intrinsic baseline is a shared RP1 product default. It is not an
individual-board calibration. Si5351 behavior is unchanged.

The current production-proven path is a Raspberry Pi backend that uses Broadcom
mailbox, DMA, PWM, and clock hardware to emit RF. The codebase has been
refactored so transmission control and scheduling live in a generic controller,
while platform-specific hardware work lives behind a backend interface.

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

## Debian contract CI

The active `.github/workflows/debian-non-hardware.yml` workflow runs the
simulator, transmission-controller,
startup-quiescence, and fake-I2C transition contracts in an isolated Debian
Trixie checkout without hardware or elevated privileges.

Si5351 transition, startup-quiescence, full debug-build, and physical-backend
regressions use the ordinary sibling component trees recorded by WsprryPi.
The workflow does not perform installation,
service management, transmitter device access, live transmission, or RF
qualification.

## Runtime Notes

- RF frequency values are expressed in hertz (Hz)
- Symbol duration values are expressed in seconds
- Power reporting helpers return estimated milliwatts (mW)
- The Raspberry Pi backend is the only production-verified backend at present

## License

This component is covered by the WsprryPi repository's root `LICENSE.md` (MIT).

## Extraction

To extract this component for independent use, copy `src/WSPR-Transmitter` and
the required sibling components (`WSPR-Reference`, `Mailbox`, and
`Signal-Handler`) while preserving their relative layout, then add repository
metadata and an appropriate license file. Run the hardware-free suite from this
component's `src` directory before considering any separately authorized
physical-backend qualification. WsprryPi-specific integration should remain
outside the extracted component.

## Si5351 comparison capabilities

The planner reports output frequency from the calibrated reference multiplied
by the programmed PLL ratio, divided by the programmed MultiSynth and R-divider.
Bounded rational approximation is used for the fractional ratios. The configured
output drive strength is retained when applying tone control registers.

Initially inhibited programming waits for Si5351 initialization/reference/PLLA
readiness with at most 50 interruptible 1 ms waits. A non-ready status while RF is
active immediately inhibits and fails the execution. Duty-cycle fades use the
scheduled event deadlines, including I2C overhead; missed slices are skipped.
These fades still switch RF fully on/off and do not produce an analog amplitude
envelope. Spectral improvement must be measured, not inferred from the shape name.

Maintainer-only typed options and the standalone backend harness support:

- `--pll-only`: compatible integer-MultiSynth plans retune PLLA without repeated
  reset or output inhibition. First and incompatible tones remain guarded.
- `--integer-ms`: choose a common even integer MultiSynth/R-divider over the full
  TONE/WSPR tone set, then tune through the fractional PLL.
- `--burst`: burst contiguous parameter bytes within one PLL/MultiSynth block
  while RF is off/inhibited, and elide unchanged stable clock controls. Active
  programming retains individual writes after a live burst/readiness failure.
  Cache elision requires exclusive register ownership; errors invalidate cache.

These three experiments remain opt-in; the application retains fixed-PLL HF
planning and full inhibited 2 m transitions. No persisted setting or UI control
was added. Same-path conducted evidence for wspr4 CLK0 at 40 m and 2 m is linked
from `docs/development/si5351-results` in the parent repository. It does not
qualify other hardware, full encoded WSPR frames, or temperature extremes.
