# Si5351 readiness and qualification limits

The Si5351 backend attempts all-output inhibition before ordinary programming.
It checks SYS_INIT before RAM programming and PLLA/XA readiness afterward.
Each stage permits at most 50 reads with interruptible 1 ms waits. These limits
do not bound a blocking kernel I2C transaction. A non-ready status and a transport
failure remain distinct errors. Either during output fails execution and enters
disable cleanup without re-enabling RF.

Cleanup retains its own error separately from the primary fault, including an
uncertain output state. Repeated cleanup returns the retained job receipt
without further I2C access. A successful disable write or register readback does
not establish measured RF inactivity. See the
[component contract](../src/WSPR-Transmitter/README.md#si5351-comparison-capabilities).

The readiness release gate was accepted on October 15, 2026 after software
repairs and two consecutive inhibited wspr1 I2C checks of 1,000 reads each.
The implementation review baseline was
`50097e652b4af70d510f4a29d4edf1ffa24d3f0a`. This disposition covers that bounded
I2C observation. It does not establish general hardware reliability, timing or
RF qualification. Earlier wspr4 CLK0 SYS_INIT failures remain unexplained;
later successful observations do not establish their cause.

The application retains fixed-PLL HF planning and full inhibited 2 m tone
transitions. Typed maintainer experiments for PLL-only updates, integer
MultiSynth selection and burst programming remain opt-in. They are not saved
settings or operator controls. Duty-cycle fades switch output fully on/off;
an envelope name does not establish spectral improvement.

Current physical support claims remain bounded by board, reference clock,
output, drive, mode, frequency and measurement setup. Neither a software check
nor these inhibited I2C observations qualify another setup, a complete encoded
WSPR transmission, calibrated power/frequency/spectrum or temperature extremes.
The [active release evidence](../release_tools/4.0-Beta-1-Release-Evidence.md)
records the selected candidate and its remaining acceptance gates.
