# Issue 446 conducted RF validation

The September 13, 2026 matched comparison passed the focused legacy GPIO TONE
frequency-offset check. With the same requested carrier, the installed fix was
**2.28586 Hz above an isolated old-spacing control**, versus the expected
**2.197265625 Hz**. The 0.08859 Hz difference is inside the predeclared
0.3 Hz tolerance; fit residual RMS was 0.10202 Hz, below the 0.15 Hz limit.

This validates the frequency-mapping change on the identified chain. It is not
an absolute GPIO clock calibration, other-band/processor qualification, or a
new start/stop timing or spectral-purity campaign.

## Exact setup

- Source: `ef9a76a5be4d8223b89bcd64df1e1a376d48a0a5`, clean `devel` on wspr4.
- Installed binary SHA-256:
  `b21fa9200b0671232c88b28595ef591f96825dd8a60f1e22e6a6d516faccc674`.
- Raspberry Pi 4 Model B Rev 1.1, BCM2711; selected GPCLK parent **PLLD**,
  nominal **750,000,000 Hz**, confirmed by committed correction status.
- Legacy GPIO backend, **GPIO4**, GPIO power level **0**. Provider disabled,
  manual and residual correction frozen at **0 PPM**. Ancillary GPIO and band
  selectors disabled in an external temporary configuration.
- Operator-confirmed conducted paths: GPIO4 and GPSDO each through **60 dB
  attenuation** to the receiver. The path was kept unchanged; no new filter
  qualification or filter identity is claimed.
- Receiver: wspr5 SDRplay **RSP1B 2404058C60**, 250,000 samples/s,
  200,000 Hz bandwidth, 20 dB gain, channel 0, AGC and bias tee off. Receiver
  tuning was 25 kHz below each requested tone. No receiver calibration profile
  was applied; simultaneous reference subtraction was used instead.
- Reference: Leo Bodnar **LBE-1421 0673ED0FA107**, firmware 1.9, output 1,
  LOW level, satellite/PLL locked and antenna OK. Output 2 and PPS remained
  disabled. The matched capture used **14,107,100 Hz** throughout.

The isolated control starts from the same source and copied release objects.
Only `wspr_transmit.cpp` and build-version metadata were recompiled. The exact
[control patch](control.patch) restores `12000 / 8192` Hz table spacing in the
legacy plan adapter; there are no clock-model, calibration, receiver, or
power changes. It is an **old-behavior control**, not a historical release.
Its SHA-256 is
`bae72e716d9d16bb5fbf1acb932e93c73fa3e70284802924164c8052a608f0f2`.
It was never installed. The installed executable hash remained unchanged.

## Authorized runs and results

The initial authorization covered two 10-second tones, at 7,040,100 Hz and
14,097,100 Hz. Their stable reference-relative offsets were approximately
+71.24 Hz and +144.99 Hz with 0 PPM correction. Those raw offsets include the
uncalibrated transmitter clock and are **not** evidence of a remaining fixed
2.197 Hz defect. A cross-band comparison could not isolate clock drift, so
these exploratory captures are retained as inconclusive for the fix.

The first capture ended before the final approximately three seconds of its
tone. It contains sufficient steady carrier for frequency analysis; its stop
is supported by the product transaction and GPIO input readback. No extra RF
was run to perfect that timing. All three captures have complete requested
sample counts, zero overflows, zero timeouts, zero clipped samples, and
verified receiver cleanup.

The operator then separately approved four 5-second tones at 14,097,100 Hz:
**old, new, new, old**. The limits in [matched-plan.json](matched-plan.json)
were written before that run. The matched capture spans all four tones and
post-tone quiet. The total GPIO RF budget used was **40 seconds**: 20 seconds
exploratory and 20 seconds matched.

| Matched burst | Build | Mean offset relative to reference, Hz |
| --- | --- | ---: |
| 1 | Old-spacing control | +144.57764 |
| 2 | Installed fix | +146.87750 |
| 3 | Installed fix | +146.84380 |
| 4 | Old-spacing control | +144.57718 |

These offsets are intentionally not reported as calibrated carrier errors.
The comparison fits the simultaneous-reference-subtracted observations to
`baseline + linear time drift + fixed-build indicator × effect`. Thirteen
one-second steady windows give an effect of **+2.28586 Hz**, standard error
0.06472 Hz, and residual RMS 0.10202 Hz. The old controls bracket the fixed
build in time and agree to approximately 0.0005 Hz in their burst means.
That supports attribution of the observed step to the spacing change rather
than cross-band clock drift. See [assessment.json](matched/assessment.json).

## Analysis and adversarial review

[analyze.py](analyze.py) uses one-second Hann windows, an interpolated FFT
peak, and simultaneous GPSDO peaks. [assess.py](assess.py) binds each burst
to its recorded transaction and uses only whole one-second windows at least
0.5 seconds inside the recorded bounds, with strong tone/reference contrast.
The initial gap-based grouping could not separate all bursts because a
one-second FFT window overlapped the short quiet gaps. That offline alignment
attempt failed; transaction-based alignment replaced it without changing RF
data or acceptance tolerances. No transmission was repeated for this repair.

The exploratory captures also stepped the reference by 10 kHz to inspect the
receiver sample-rate scale. The passing result does not depend on an absolute
receiver frequency model: a 10 PPM sample-rate error changes a 2.2 Hz matched
difference by only 0.000022 Hz. It does not justify reusing an old calibration
profile or inferring a transmitter correction from this measurement.

Adversarial review checked control-source isolation, identical RF/receiver
conditions, effect sign, clock-drift confounding, transaction/window binding,
unchanged tolerances, retained unsuccessful analysis, capture integrity,
bounded RF allocation, restoration, and the limited acceptance claim. The
initial two-band inference and gap-based alignment were rejected; the final
matched assessment has no unresolved finding within this focused scope.

Offline reproduction after generating `frequency-windows.json` from retained
IQ, with NumPy available:

```sh
python analyze.py /path/to/matched 14097100
python assess.py /path/to/matched /path/to/matched/result.json
```

The archived `runners/` files document the actual finite sessions. They are
not general qualification tools and are not authorization to transmit again.

## Evidence and restoration

The `7040100/`, `14097100/`, and `matched/` directories retain capture metadata,
frequency windows, GPSDO state history, and complete bounded-tone responses.
Raw IQ remains on wspr5 at
`/home/pi/issue446-rf/{7040100,14097100,matched}/capture.cf32`; each metadata
file records its full SHA-256 and size. Installed/private INI contents and
raw IQ are intentionally excluded from Git.

Every tone has a successful product-owned deadline stop, scheduler restore,
normal process exit, and GPIO4 input readback. The original wspr4 service was
restored after each session, and the installed INI was byte-for-byte unchanged
(`cdab1a816495325ebe4dfe8d3f453c2ce89f430f7e9d84befda4fc9ff12ff5cb`).
Transmit remains false and Enable on Boot remains Never. Its pre-existing
Si5351 startup inhibition remains; this GPIO validation does not resolve it.
GPSDO output 1, output 2, and PPS are off, and the initial frequency/level were
restored. wspr5's normal WsprryPi service was left running.

## Documentation Impact

Updated operator documentation in the separate `Wsprry_Pi_Docs` repository:
Maintenance Test Tone, CLI Service/GPIO/Test Controls, and Transmission Timing
and Calibration, committed as
[`c4cfc4ac9908adcc40a53702bb38803700b8a92e`](https://github.com/WsprryPi/Wsprry_Pi_Docs/commit/c4cfc4ac9908adcc40a53702bb38803700b8a92e).
The strict Sphinx HTML build passed. Impeccable review covered
all three rendered pages at 1440×1000 and 390×844 with no horizontal overflow;
the mechanical detector found no issues. Two new heading-anchor warnings were
fixed before the successful strict build. Existing screenshots remain accurate
because no application UI controls or layout changed.

The source regression coverage recorded in
[issue-446-validation.md](../../issue-446-validation.md) remains applicable;
no production code changed in this RF/documentation follow-up.
