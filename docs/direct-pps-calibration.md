# Direct PPS RF calibration

The optional Raspberry Pi 5 adapter measures RP1 `clk_sys` against an existing
GPS PPS input. It supplies the RP1 RF parent estimate independently of Chrony's
host-frequency estimate. Chrony remains the UTC authority. Pi 1–4, Si5351,
outbound WTP, symbol timing, alternative parents, and new GPIO routing are
outside this adapter's measurement scope.

## Reference and clock identity

The implemented input is Linux `pps0`, named `pps@11.-1`, on GPIO17. The
adapter requires Raspberry Pi 5, `/dev/pio0`, and a readable clock tree showing
200 MHz `clk_sys` directly below 200 MHz `pll_sys`. Identity and clock ancestry
are rechecked around each observation. The existing PPS driver retains its
pin lease. The adapter never sets GPIO mux, direction, pulls, or output data.

The operator must qualify the receiver, wiring, GNSS/reference lock, and an
absolute edge-error budget no greater than one microsecond. The application
additionally requires a synchronized, single selected local Chrony source,
normal leap status, and a known report age at most 300 seconds. This is a
selection-health gate, compatible with the existing 120-second adapter polling
interval, not independent GNSS-lock authentication. Source-signature changes
retire acquisition and its qualification history. Local source names alone do
not establish that reference qualification.

The upstream [Raspberry Pi PIO library](https://github.com/raspberrypi/utils/tree/master/piolib)
documents RP1 PIO access, and its `pio_rp1.c` identifies nominal `clk_sys` as
200 MHz. The implementation uses the public `misc/rp1_pio_if.h` UAPI, imported
unchanged at Raspberry Pi Linux `4103a989a46196239b830bd07693277e42b8f992`,
with its license and provenance under `src/vendor/rp1_pio`. This gives ordinary
Debian cross-builds the same capture support without inventing a private ABI
or accessing physical memory. Linux PPS timestamps alone measure the host time domain;
see the [Linux PPS API](https://www.kernel.org/doc/html/latest/driver-api/pps.html).

## Capture equation and ownership

The checked-in `kRp1PpsProgram` contains an original, input-only PIO program.
It waits for an explicit trigger, a low PPS level, and a rising edge. A
32-bit down-counter then counts two-instruction loops through both high and
low phases of ten complete periods. The boundary branch counts periods;
the program emits one counter word and stops on its next blocking `PULL`.

For returned iteration count `C`, the measured RP1 cycles are:

```text
cycles = 2*C + 2*10 + 1
P = (cycles / (200000000 * 10) - 1) * 1000000 ppm
```

The constant includes initialization and boundary instructions between the
successful rising-edge `WAIT` sample and final rising-edge `JMP PIN` sample.
The instruction-model test executes the actual shipped words across pulse
phase, duty, and odd/even periods, comparing the result with independent
electrical-edge times. Very short pulses can be missed; the receiver must
supply a proper PPS waveform, not isolated pulses shorter than the capture
sampling loop. Missing/extra periods produce an invalid rate or timeout.

Only one result can enter an initially empty RX FIFO. There are no host
operations, FIFO writes, or stalls between measurement endpoints. A delayed
reader cannot extend the measured period. Reads are nonblocking and the
observation deadline is 12 seconds. Allocation claims one free state machine
and references an origin-zero program; occupied instruction memory is a
reported resource error, never a reason to clear global memory. File close
disables/releases only that client's state machines and program references.

The first diagnostic streaming prototype encountered an unsupported
`SM_GET_FLAGS` firmware operation. Production acquisition does not require
that operation or raw PIO-register access: its single-result design prevents
FIFO backpressure during counting.

## Qualification, selection, and loss

Three distinct ten-period observations with spread at most 0.1 ppm qualify a
result. The most recent accepted measurement supplies the candidate. Both
electrical endpoints and four RP1 cycles of quantization/synchronization are
budgeted: the one-microsecond reference-edge allowance yields a conditional
0.202 ppm bound per ten-second observation. Stable repeated values do not
remove a common reference error or establish absolute RF accuracy.

Full measured parent error `P` is positive for a fast source. With intrinsic
`I=-46.245 ppm` and configured residual `R`, direct calibration supplies:

```text
additional A = (P - I) + R
RF model     = 200000000 * (1 + (I + A) / 1000000)
```

The Chrony frequency estimate is replaced, never added again. Configured
manual fallback is independent; residual is unused during fallback. Inputs,
measured full parent error, and selected additional correction retain finite
range checks. Existing intrinsic composition and backend ownership are
unchanged. GPIO/Si5351 signs remain distinct.

Acquisition errors retire the result immediately and restart with an empty
qualification history. A result expires after 30 monotonic seconds, dated
conservatively from trigger time plus the ten-second measurement span rather
than delayed readback. No expired estimate is retained for holdover. Until
qualified, or after loss/expiry, the existing manual/zero-additional policy
applies. Zero additional correction still retains the intrinsic term.

Selection occurs at the existing request preparation/commit boundary for
scheduled work, Test Tone, and inbound Pi WTP. Candidate status can refresh
while running; committed correction, provenance, and divider/event programs
remain immutable. The direct input worker neither mutates a request nor
controls RF output. Disable/backend changes stop its acquisition. The saved
host-estimate preference is retained, and explicit CLI host/manual choices
override direct selection for that invocation.

## Validation boundaries

The parent target `make rp1-pps-calibration-test SUDO=` exercises the instruction
model, conversion and uncertainty limits, reference-report aging, distinct
qualification, manual/residual selection, source loss, expiry, reacquisition,
and resource cleanup through a typed capture seam. `gpio-frequency-correction-test`
includes it. Portable semantics, configuration round-trip, UI unit/browser
coverage, and both actual Pi/Pico integration fixtures remain separate checks.

Software tests and input-only capture probes are not RF qualification.
Independent conducted comparison must identify the candidate binary/provider,
PPS reference, GPSDO lock/output, receiver, routes, frequencies, durations,
attenuation, uncertainty, repeated observations, and verified output-off and
restoration. No general frequency, power, spectral, timing, installation, or
release-readiness guarantee follows from this adapter.
