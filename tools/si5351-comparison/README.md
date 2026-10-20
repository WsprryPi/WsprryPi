# Conducted Si5351 comparison harness

This maintainer harness controls real RF hardware. It is specific to the
user-confirmed wspr4 CLK0 / wspr5 RSP1B / GPSDO Output 1 bench. Each source has
20 dB attenuation before the combiner and 40 dB shared attenuation afterward.
It is not an operator CLI, an unattended scheduler, or general qualification.

The frozen reference model is 27 MHz with +3.470680 ppm. The compared carriers
are 7,040,100 Hz and 144,490,500 Hz; the simultaneous GPSDO reference is 10 kHz
higher. Receiver center is 25 kHz below the transmitter, sample rate 250 ksps,
bandwidth 200 kHz, gain 20 dB, AGC and bias off. Do not adjust PPM between stages.

Run `capture.py` on wspr5 with `--enable-rf`, a unique `--stage`, `--band 40m|2m`,
`--source-revision`, and the isolated wspr4 harness `--binary` path. Optional
`--extra=--pll-only` and `--extra=--integer-ms` select reviewed experiments.
These flags do not change the installed application or its configuration.
The coordinator temporarily stops and restores wspr4's transmitter service,
bounds each transmission with `timeout`, and verifies output-disable and
installed-file identities. GPSDO programming is volatile and independently
bounded, with output shutdown and original frequency/drive restoration.

Each band retains raw CF32 IQ, exact commands, timestamps, executable identity,
receiver metadata, GPSDO lock records, transmitter logs and cleanup results for:

- Three 2-second carrier bursts separated by 2-second idle periods.
- Sixteen contiguous 500 ms tones cycling through four WSPR-spaced frequencies.
- Eight 500 ms keys separated by 500 ms idle, without fading.
- The same keys with 20 ms raised-cosine duty-cycle fades and 2 ms slices.

The transition sequence exercises the backend's WSPR frequency path; it is not
an encoded WSPR message and does not qualify decodability or complete-frame timing.

Run `analyze.py DIRECTORY` and `compare.py BEFORE AFTER` locally with NumPy and
Matplotlib. Analysis checks raw hashes, capture health, receiver settings and
GPSDO coverage. It writes JSON, CSV, PNG and PDF results. Compare all stages
with the same analysis revision. Carrier spectra use steady interiors;
modulated spectra include transition and key edges. The concentration metric
is power within **plus or minus 20 Hz** of the indicated carrier divided by
power within plus or minus 5 kHz; it is not a regulatory occupied-bandwidth test.
Channel isolation precedes 1 kHz decimation, followed by 5 ms power smoothing.
Detected gap counts and extrapolated phase changes are exploratory measurements
with finite resolution, not calibrated timing or phase-noise specifications.
GPSDO-corrected mean offsets, drift, and intentional tone spacing are distinct.

Preserve rejected captures separately. A successful conducted comparison does
not establish other bands, drive levels, outputs, boards, filters, antennas,
receiver configurations, temperature behavior, or release readiness.

`report.py ARTIFACT_ROOT OUTPUT_DIRECTORY` assembles this campaign's explicit
complete-run list, cleanup gates, raw-capture manifest, programming-time logs and
rejected attempts into compact review artifacts. It does not classify partial
or failed runs as successful and does not perform live hardware operations.
