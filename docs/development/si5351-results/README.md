# Si5351 conducted comparison report

**2026-10-15:** [Readiness software review and qualification disposition](../si5351-readiness-review.md)
records software repairs, two consecutive inhibited wspr1 I2C passes and the
maintainer's accepted item-6 closure. The historical failures, measurements and
identities below remain unchanged; their cause was not established.

**2026-09-08 follow-up:** a [complete matched 40 m/2 m pair with experiments off](matched-off.md) is now available. Both bands completed; the earlier intermittent SYS_INIT reliability issue remains unresolved. The original campaign findings below are retained.

Final RF qualification is incomplete: repeated SYS_INIT status faults occurred
with both experimental PLL-only tuning and guarded defaults. All five
investigations were evaluated, but the final source is not qualified for
deployment on this bench. The work produced two default-path fixes, an RF timing fix, and
three explicit maintainer experiments. PLL-only 2 m transitions removed the
repeated resolved output interruptions in initial captures, but a final repeated
run failed the SYS_INIT readiness check and the strategy was not promoted. Integer-output-divider
planning did not establish a clear 40 m spectral advantage. Active PLL bursts
failed a live readiness check and were restricted to inhibited programming.
Duty-cycle fading still spreads more energy away from the carrier than hard
keying on this bench, even after correcting its timing overrun.

This is a conducted comparison on one board and RF path, not release or general
RF qualification. The complete measured values and every before/after PDF are in
[the measurement tables](measurements.md). [The review log](review.md) records
findings, repairs and reassessments. [The execution prompt](../si5351-iteration-prompt.md)
contains the initial code review, scope and acceptance workflow.

## Baseline and fixed conditions

Yes, the original carrier-only baselines needed more coverage for a meaningful
“before.” The campaign added fresh controls from devel a523904 using the same
comparison harness: carrier bursts, four-tone transitions, hard keying and
raised-cosine duty fading, at both 40 m and 2 m. The original saved wspr4 carrier
baselines remain intact under `dist/si5351-baseline-20260907`. Their installed
binary differs from devel, so they are contextual references rather than the
sole control for these code changes. The original 2 m formal qualification's
uncalibrated absolute-offset failure is preserved, not reclassified as a pass.

The fixed path is wspr4's 27 MHz TCXO Si5351, I2C1/0x60, CLK0 at 2 mA, into the
user-confirmed combiner. Si5351 and GPSDO each have 20 dB attenuation before the
combiner, followed by 40 dB shared attenuation into wspr5's RSP1B. GPSDO Output 1
is the locked simultaneous reference. No physical wiring, filter or attenuation
was changed. The original wspr2 results are not substituted for wspr4 controls.

| Setting | 40 m | 2 m |
|---|---:|---:|
| Requested carrier | 7,040,100 Hz | 144,490,500 Hz |
| GPSDO Output 1 | 7,050,100 Hz | 144,500,500 Hz |
| SDR center | 7,015,100 Hz | 144,465,500 Hz |
| Si5351 correction | +3.470680 PPM | +3.470680 PPM |

Receiver: RSP1B serial 2404058C60, 250 ksps, 200 kHz bandwidth, 20 dB gain, CF32,
AGC/bias off. GPSDO: LBE-1421 serial 0673ED0FA107, PLL mode, Output 1 low drive,
Output 2/PPS off. Reference lock was sampled throughout captures. PPM was never
retuned to improve an after result; GPSDO-corrected carrier offsets and burst
variation are reported separately.

## Implementation and disposition

| Investigation | Implemented behavior | Disposition |
|---|---|---|
| 1. PLL reset/readiness | Read initialization/reference/PLLA status; bounded cancellable waits while inhibited; immediate disable/failure when active status is non-ready. Compatible PLL-only option keeps MultiSynth/R fixed and omits repeated reset/rekeying. | Readiness checks apply normally. PLL-only remains an opt-in experiment: the live sequence is not a complete encoded WSPR frame or every possible tone ordering. |
| 2. Integer output divider | Select one even integer MultiSynth and R-divider over a complete TONE/WSPR set; tune fractional PLL; reject incompatible or invalid sets. | Opt-in. Fixed-PLL HF planning remains the application default; these measurements do not establish universal jitter or spur improvement. |
| 3. I2C overhead | Burst only inside a single parameter block while RF is off/inhibited; suppress unchanged stable controls with exclusive cache ownership; invalidate cache after errors and reopen. Reset and output-enable writes are never suppressed. Preserve requested drive bits in tone plans. | Bursts/cache elision remain opt-in. A live active-burst failure caused the conservative restriction. Drive-strength preservation is a normal-path correction. |
| 4. Fade envelope | Anchor slice timing to scheduled event deadlines, include I2C overhead, skip expired pulses and finish requested fade-out disabled. | Timing correction applies normally. Duty fading remains chopping, with no analog-envelope claim or physical amplitude-control addition. Existing fade selections remain available. |
| 5. Frequency calculation | Report effective reference × programmed PLL ratio ÷ programmed MultiSynth ratio ÷ R. | Normal-path correction. Register programming and frozen PPM are unchanged by this arithmetic correction. |

The planner already uses bounded rational best approximation to fit fractional
ratios into Si5351 register limits. Both planning strategies retain that method.
The new tests reconstruct the entire chain from packed register bytes; the old
planner fails 52 assertions in that regression check, while the corrected planner
passes. This is evidence of internally consistent reporting, not a claim that
microhertz residuals were resolved by the SDR.

## Interpreting the RF results

The fresh 2 m before trace has a median of three 1 ms samples below -10 dB around
tone changes after 5 ms envelope smoothing. The PLL-only candidates have no
resolved gaps by that metric, with smaller extrapolated phase steps and less
broad transition energy in the initial comparisons. This does not establish
zero physical interruption or calibrated phase-noise performance.

The wide-channel edge plots show full-amplitude chopping in the existing fade.
The original 2 m narrow-threshold on interval was about 531 ms for a nominal
500 ms key; hard keying measured about 502 ms using the same threshold/filter.
After deadline correction, that fade interval is shorter, as expected when its
edges remain within the intended event. The threshold interval is not itself an
exact measurement of the programmed key duration. Slow-I2C software tests verify
that transfer delay does not accumulate over every slice.

Both the earlier and corrected duty fades have lower concentration within
plus/minus 20 Hz than hard keying in the same stage. The corrected timing changes
the chopping sidebands, but does not establish a cleaner signal than no fade.
Linear and raised-cosine deadline/cancellation behavior are tested in software;
these RF captures use raised-cosine fading.

Later 2 m captures show spur variation even in carrier and hard-key controls,
whose programming does not change with the fade correction. Preserve those
observations and avoid assigning every spectral difference to the current code
slice. Absolute offsets also vary between runs with PPM frozen. Short burst
variation is measured; temperature-controlled drift and a full warm-up model
remain unqualified. The 40 m noise floor limits small close-in comparisons.

Stages 1–4 are cumulative experiments. Stage 5 deliberately returns 40 m to the
ordinary fixed-PLL strategy to exercise the corrected reporting path; 2 m retains
the divide-by-6 PLL-only experiment. Both original and repeated final captures
are retained, including the failed 2 m fast-path repetition. A guarded-default
40 m run with all three experiments off also failed during transitions; its
2 m counterpart was therefore not run. Stage 5 versus stage 4 at 40 m is consequently not a single-variable
synthesis comparison. The precise flags, source and executable hashes accompany
every result.

## Rejected attempts and cleanup

The first receiver invocation used numeric booleans and was rejected before
Si5351 transmission. The first step 3 40 m active-burst run completed its carrier
case, then failed during transitions. It is retained separately as rejected;
its partial sequence is not included among successful comparisons. The repaired
candidate retains individual writes whenever RF is active. A later repeated
2 m PLL-only run nevertheless failed at register 0 = 0xC1 (SYS_INIT asserted),
so active bursts alone do not explain all observed readiness failures. The
readiness gate stopped the run and verified cleanup. A subsequent guarded-default
40 m run failed with the same 0xC1 status during ordinary MultiSynth tone updates.
Thus disabling the experimental paths did not resolve the bench failure.
The status gate was not weakened, and a passing retry was not substituted for
these failures. All three experimental strategies remain opt-in.

With RF disabled, register 0 intermittently returned 0xC1: 2 of 100 initial
reads, then 11 of 500 split reads and 11 of 500 repeated-start reads in an
interleaved diagnostic. The high samples occurred at different indices; this
does not establish identical timing or the cause of the indication. Both methods
observed it. The module identity is unknown. Pi throttle flags showed historical
undervoltage/throttling, with no current flags; temperature was 45.2 C. These are
observations, not proof of a power or chip defect. No wiring or hardware change
was made. [Diagnostic records](diagnostics/) preserve the evidence.

The next unfinished qualification step is to characterize and resolve the
intermittent SYS_INIT indication, then repeat guarded-default 40 m and 2 m
and the candidate qualification. This may require module/electrical inspection
beyond the completed fixed-chain comparison; no additional RF was attempted
after the diagnostic. Software tests cannot close this hardware question.

Each successful band record verifies Si5351 register 3 = 255, GPSDO outputs and
PPS off, service restoration, and unchanged installed binary/configuration hashes.
All three retained RF-failure cleanup records verify the same state. GPSDO volatile frequency and
drive are restored. The SSH keys requested by the user remain in place. No reboot
or installed application replacement was performed.

## Validation

Commands and retained logs distinguish software validation from RF evidence:

- Component on wspr4, no hardware I/O: `make -C src/WSPR-Transmitter/src si5351-planner-test si5351-transition-test si5351-startup-quiesce-qualification-test si5351-test SUDO= -j4`. All affected iterations and repaired reassessments passed. `si5351-test` builds the harness; it does not run RF.
- Local planner: `make -C src/WSPR-Transmitter/src si5351-planner-test SUDO=` passed. Local fake-I2C compilation lacks Linux I2C headers, so those tests ran on Linux.
- Parent on macOS, from `src`: `make semantics-test-portable SUDO=` passed, including after final source changes. Its localhost network tests required execution outside sandbox network restrictions. This is the explicit portable subset, not full Linux semantics coverage.
- Full Linux semantics is run from an exact separate Git checkout with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -C src semantics-test SUDO= -j4`; it passed, including runtime semantics, cleanup lifecycle and all 23 coherent UI publication tests. Logs and hashes are in the completion record. The first source-only staging attempt correctly failed metadata generation because it had no Git identity.
- Final isolated test-source verification compared 383 tracked C/C++ files against 369a53d with zero mismatches. Whitespace checks passed. Raw IQ hashes, receiver settings, sample counts, clipping/overflow and reference coverage were checked before analysis. Final representative envelope/spectrum and wide-channel edge renders were visually inspected.

Only `src/WSPR-Transmitter` is modified as a component. No UI changed; Impeccable
and UI visual review are not applicable. Existing source/UI tests in the parent
suite are reported as automated regression coverage, not as a visual UI review.

## Documentation Impact

Updated the component README, execution prompt, maintainer capture/analysis guide,
adversarial review log, and this source-bound report with its measurement tables.
The separate operator documentation repository was inspected read-only. Follow-up
there is needed for the Si5351 readiness behavior, corrected drive retention and
fade timing/limitations in:

- `docs/Command_Line_Operations/transmitter_backends.md`
- `docs/Command_Line_Operations/cw_modes.md`
- `docs/Advanced_Operations/ini_configuration/runtime.md`

No persisted configuration or UI documentation is required for the three
maintainer-only experimental switches. General install and board/reference
selection documentation remains unchanged. Cross-repository operator edits were
not authorized and were not made.

## Final reassessment

The repaired software passes the affected component and both parent test profiles.
Review found no further actionable software defect within this slice after the
recorded repairs. The live status failures remain unresolved and prevent closing
RF qualification. Successful captures, failed attempts and hardware-free evidence
are kept separate; no retries, changed PPM, or relaxed gates conceal that gap.

## Source and evidence identity

The feature branch is `codex/si5351-comparison-iterations`, based on refreshed
devel a523904. Final RF source is 369a53d; subsequent commits contain evidence and
reporting artifacts. `devel` advanced independently during the campaign. This
report does not claim integration with that newer base. The original working
checkout and its unrelated work were preserved.

Raw captures remain in `dist/si5351-iterations-20260908` in the active WsprryPi
workspace and in `/home/pi/si5351-iterations` on wspr5. Compact records and plots
are committed here. [Capture manifest](capture-manifest.json) binds raw paths,
hashes, counts, timestamps and executables. [Completion record](completion.json)
records final tests and qualification status without changing measurement identities.
Commit and push identity is reported with the delivered branch; it is separate
from the captured executable identity.

Primary references: [QRP Labs FDIM 2018 article](https://www.qrp-labs.com/images/news/dayton2018/fdim2018.pdf),
pp. 14, 16–17, 25–26 and 35; [Skyworks AN619](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf).
