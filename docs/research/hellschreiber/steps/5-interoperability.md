# Step 5 — Perform offline interoperability experiments

## Status

**Source-level checkpoint complete; application-level interoperability remains open.** Reproducible raster, timing, tone-contract, waveform-fixture, and bounded impairment comparisons were completed offline for fldigi 4.2.12, xfhell 3.5.2, RadioLib commit `0795caa`, and the checksum-frozen historical Feld-Hell drum transcription.

No installed application transmitter was captured and no application receiver rendered a counterpart's WAV file. Accordingly, this report establishes exact source-contract matches and mismatches, not full A→B/B→A application interoperability or human readability. No audio device, GPIO, service, radio hardware, RF output, or over-the-air operation was used.

## Technical summary

The most important result is negative: **“FSK Hell-105” and “FM Hell-105” are not sufficient interoperability identifiers.** fldigi 4.2.12's `FSKH105` source sends a 14-position column at 245 physical decisions/s with 55 Hz total tone separation. xfhell 3.5.2's `FMHell` at nominal 105 baud sends a 12-position column at 210 physical decisions/s with 210 Hz total separation. Both preserve 17.5 columns/s, but they are neither F1 raster-identical nor F2 waveform-equivalent. [HELL-EVID-0035] [HELL-EVID-0036] [HELL-EVID-0037]

A source-derived coherent tone-energy test showed that cross-contract binary decisions can sometimes succeed under clean, perfectly centered synthetic conditions. That result is fragile and is not application decoding: the contracts use different row counts, decision rates, tone spacing, fonts, filters, and receiver algorithms. The test therefore prevents an overbroad “incompatible at any layer” claim, but it cannot establish F3 mutual readability. [HELL-EVID-0037]

Standard Feld-Hell has a more stable timing basis across the inspected implementations. fldigi and xfhell both use 14 physical positions per column and 17.5 columns/s for their standard Feld profiles, while RadioLib uses a direct 7×7, 122.5-cell/s simplification. Font and spacing behavior remain materially different. fldigi `real` exactly matches 27 of 40 shared historical transmitted cells; xfhell's `FeldRealEn` and fldigi `real` share 36 of 40 trimmed glyph shapes but no complete transmitted cell because their spacing policies differ. RadioLib's simplified glyphs exactly match only 12 of 40 trimmed fldigi `7x7` glyphs. [HELL-EVID-0038] [HELL-EVID-0039]

The safe outcome for Step 6 is therefore implementation-qualified:

- Wsprry Pi feasibility may use the Step 1.1 Feld timing model as a stable reference.
- It may not assume a generic FSK/FM Hell-105 contract.
- It may not treat a font name or visual similarity as raster identity.
- It may not claim current-software interoperability until actual application A→B and B→A file tests close this step.

## Research questions

1. Do independent implementations sharing a Hell mode label generate the same raster and waveform?
2. Which differences are exact incompatibilities, compatible representations, or merely font/rendering differences?
3. What evidence can Step 6 safely consume without converting source inspection or synthetic loopback into application qualification?

## Scope and safety boundary

The work was performed entirely on files and immutable public source archives. The retained analyzer uses no audio-device, network, GPIO, service, transmitter, or hardware API. WAV files are ordinary 48 kHz mono PCM fixtures written to the documentation directory.

The historical drum transcription was downloaded temporarily, verified against the Step 3 SHA-256, parsed for aggregate comparison, and not copied into the repository. [HELL-EVID-0039]

## Implementations and immutable versions

| Identifier | Source identity | License | Inspected transmit profiles | Receiver execution |
| --- | --- | --- | --- | --- |
| `FLDIGI-4.2.12` | Official `fldigi-4.2.12.tar.gz`; SHA-256 `028bcb1c…dbc44a` | GPL-3.0-or-later | `FSKH105`, `FELDHELL`; `7x7` and `real` fonts | Not executed |
| `XFHELL-3.5.2` | Official `xfhell-3.5.2.tar.bz2`; SHA-256 `7b16ecda…5b209` | GPL-3.0-or-later | `FMHell` at 105; standard FeldHell; packaged BDF fonts | Not executed |
| `RADIOLIB-0795CAA` | GitHub commit archive `0795caa41c6350a2f862137cfc22528c2aaad2bc`; SHA-256 `6bf3c679…6150` | MIT | Direct/AFSK Feld-style 7×7 at default 122.5 | Transmit-only library; not executed |
| `HELL-FONT-HIST-FELD-01` | External drum transcription; SHA-256 `1dc94ee5…3d5` | Redistribution permission not established | Historical 7×14 raster reference | Parsed temporarily; not retained |

All source archives remained under `/tmp`; only derived, licensed fixtures and aggregate measurements were retained. [HELL-EVID-0034]

## Test corpus

The deterministic corpus contains:

- uppercase A–Z;
- digits 0–9;
- `+ - ? /` and spaces;
- the known historical/fldigi-difference set `- / 1 3 6 7 9 ? I K P Q U`;
- repeated narrow, wide, vertical, and horizontal-stroke glyphs;
- `CQ TEST DE N0CALL N0CALL K` as a representative synthetic exchange.

`N0CALL` is explicitly fictional test text; no operator or callsign dataset is present.

## Experimental environment

- Host: Apple arm64, macOS 26.5.2
- Python: standard-library analyzer; no repository-wide dependency
- Fixture audio: PCM signed 16-bit little-endian, mono, 48,000 samples/s, 1,500 Hz nominal center
- Source archives: downloaded from official project locations and checksum-frozen before analysis
- Historical source: temporary checksum verification only

## Fixture and artifact manifest

Artifacts reside under [`../artifacts/step-5/`](../artifacts/step-5/README.md). `manifest.json` records path, size, and SHA-256 for every retained file other than itself.

The two audio fixtures are:

| Fixture | Text | Duration | Physical decisions | Tone separation | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| `fldigi-fskh105-source-contract.wav` | `CQ TEST`; `Feld7x7-14.cxx` | 1.714292 s | 245/s | 55 Hz | `5be94ad3…fdcd` |
| `xfhell-fmhell105-source-contract.wav` | `CQ TEST`; `FMFatLoEn.bdf` | 2.914292 s | 210/s | 210 Hz | `b622e5e2…b6c` |

These are source-derived CPFSK fixtures, not audio captured from the applications.

## Compatibility definitions

| Class | Meaning in this step |
| --- | --- |
| F1 | Same transmitted character raster, including geometry, scan order, and spacing |
| F2 | Different internal form but equivalent normalized transmitted waveform/event sequence |
| F3 | Intended text reliably readable across independent implementations |
| F4 | Human-recognizable fragments without dependable operation |
| Incompatible | Intended text not practically recoverable under correct settings |
| Not assessed | Required application or human-reading evidence was not obtained |

F1 and F2 can be decided from source-derived event evidence. F3, F4, and practical incompatibility require actual receiver/rendering evidence and are not inferred from a tone-decision model.

## Track A — the 105-labelled profiles diverge before decoding

### Source contracts

| Property | fldigi 4.2.12 `FSKH105` | xfhell 3.5.2 `FMHell` at 105 | Result |
| --- | ---: | ---: | --- |
| Column rate | 17.5/s | 17.5/s | Match |
| Physical transmitted positions | 14/column | 12/column | Mismatch |
| Physical decision rate | 245/s | 210/s | Mismatch; ratio 7:6 |
| Nominal/logical label | `FSKH105` / `FM_HELL_105` | `105.0` baud | Naming overlap only |
| Total tone separation | 55 Hz | 210 Hz | Mismatch; ratio 3.818:1 |
| Tone continuity | Continuous phase | Continuous phase | Match at waveform-family level |
| Font | Any selected fldigi 14-row table | Packaged low-resolution paired 12-row BDF | Mismatch |
| Scan | Bottom-to-top, then left-to-right | Bottom-to-top, then left-to-right | Match |

fldigi computes 14 transmitted positions from the common font path at 17.5 columns/s and places tones at center ±27.5 Hz. xfhell halves its nominal dot duration internally, transmits the 12 rows of `FMFatLoEn` at 210 physical positions/s, and places tones at center ±105 Hz. [HELL-EVID-0035] [HELL-EVID-0036]

The original ZL1BPU description defines the 105-baud family as six logical rows at 105 baud and 17.5 columns/s, with a special font. xfhell represents each logical row as a physical pair; fldigi's current implementation instead retains its common 14-position raster machinery. The shared label therefore hides distinct implementation profiles. [HELL-EVID-0027] [HELL-EVID-0035] [HELL-EVID-0036]

### Direction matrix

| Transmitter evidence | Receiver evidence | Exact result | Compatibility disposition |
| --- | --- | --- | --- |
| fldigi source-derived contract | Same source-derived tone settings | Clean alternating-symbol tone test: 0/280 decision errors | Loopback model only; not an application F3 result |
| xfhell source-derived contract | Same source-derived tone settings | Clean alternating-symbol tone test: 0/280 decision errors | Loopback model only; not an application F3 result |
| fldigi source-derived contract | xfhell source-derived tone detector settings | Clean tone test: 0 errors; 0 dB AWGN: 9.17%; −6 dB: 20.00% | Not F1/F2; F3 not assessed |
| xfhell source-derived contract | fldigi source-derived tone settings | Clean tone test: 8.28% errors; 0 dB: 8.28%; −6 dB: 7.67% | Not F1/F2; F3 not assessed |

The cross-contract numbers are diagnostic only. They use an ideal coherent energy comparator and an alternating pattern, not either complete program receiver. The somewhat successful clean decisions occur because finite analysis windows can distinguish off-center tones even when the expected separation differs. They do not repair row-rate drift or font mismatch. [HELL-EVID-0037]

### Controlled impairments

For the same-contract fldigi model, +10 Hz remained error-free, +25 Hz produced 16.43% decision error, and +50 Hz produced 50%. Its 0 dB and −6 dB deterministic AWGN cases produced 0.36% and 8.93%. The same-contract xfhell model remained error-free in the tested +10/+25/+50 Hz and 0/−6 dB cases because its 210 Hz separation is much larger.

Uncompensated tone reversal produced 100% binary inversion in both self-contract cases. A receiver reverse control should correct that mapping, but this analyzer intentionally records the uncompensated failure. These figures characterize only the bounded synthetic detector and must not be presented as receiver sensitivity or channel performance.

### Track A disposition

- fldigi `FSKH105` ↔ xfhell `FMHell105`: **not F1 and not F2**.
- Application A→B: **not assessed**.
- Application B→A: **not assessed**.
- Human readability under correct tuning: **not assessed**.
- Generic “FSK/FM Hell-105 compatible” claim: **rejected** unless both implementation profiles are named and tested.

## Track B — Feld timing converges, fonts and spacing do not

### Historical checksum and fldigi `real`

The historical transcription again hashed to `1dc94ee5ba9aa62acecfe0607a1109c48e286595c34931095fc3b3a8bdae93d5`. The source-derived transmitted-cell comparison reproduced Step 3: 27 of 40 shared printable glyphs are exact between the historical drum and fldigi `real`. The 13 differences remain `- / 1 3 6 7 9 ? I K P Q U`. [HELL-EVID-0039]

This is F1 for those 27 individual cells, not for the complete repertoire. It does not by itself prove application receive behavior.

### fldigi `real` and xfhell `FeldRealEn`

After removing exterior blank rows and columns solely for glyph-shape comparison, 36 of 40 shared glyphs are exact. The four shape differences are `I`, `K`, `0`, and `6`. At the actual transmitted-cell layer, none of the 40 cells is exact because fldigi adds both a leading and trailing blank column while xfhell's BDF loading adds a trailing character-space column.

This pair therefore has strong glyph lineage but not F1 transmitted-cell identity. Since both standard Feld paths use 14 positions/column and 17.5 columns/s, the difference is likely compatible with asynchronous visual reception, but F3 remains untested. [HELL-EVID-0038]

### fldigi `7x7` and RadioLib

After expanding each RadioLib logical row into a 14-position fixed pair, 11 of 40 complete cells and 12 of 40 trimmed glyphs exactly match fldigi `7x7`. The exact trimmed set is `H I M N O T X Y Z 0 - /`. Across the 40 complete cells, 258 of 3,920 positions differ.

Both preserve the conventional 7×7/122.5-cell timing relationship, but the glyphs are visibly different. This is timing compatibility, not F1 raster identity. RadioLib is transmit-only in this experiment and no F3 claim is made. [HELL-EVID-0038]

### Pulse shaping

fldigi's standard Feld path selects square or configured shaped OOK edges; xfhell uses a half-dot cosine rise/fall at transitions; RadioLib's direct client starts and stops the underlying physical layer without a declared Hell-specific shaping envelope. These are not sample-identical waveforms. Their effect on practical cross-readability and occupied bandwidth remains a Step 5 application/measurement gap.

### Track B disposition

| Pair | F1 raster | F2 waveform | F3 readability |
| --- | --- | --- | --- |
| Historical drum ↔ fldigi `real` | Partial: 27/40 exact cells | Not assessed | Not assessed |
| fldigi `real` ↔ xfhell `FeldRealEn` | No complete cells exact; 36/40 trimmed shapes exact | No, spacing/shaping differ | Not assessed; plausible only |
| fldigi `7x7` ↔ RadioLib | Partial: 11/40 exact cells | No, font and keying implementation differ | Not assessed |

## Failures and rejected interpretations

- **Rejected:** Treat `FSKH105`, `FM105`, and `FMHell at 105` as aliases for one waveform.
- **Rejected:** Treat a clean ideal tone decision as application decoding.
- **Rejected:** Treat identical column rate as raster identity.
- **Rejected:** Treat matching trimmed glyph shape as matching transmitted timing.
- **Rejected:** Treat source loopback as independent A→B/B→A interoperability.
- **Not completed:** Installed fldigi playback/capture and xfhell receiver execution. xfhell is a Linux/GTK/ALSA application; no approved, faithful application-level environment with audio-file routing was established on this macOS host.
- **Not completed:** Human-blind readability scoring, dropouts, impulsive noise, leading/trailing truncation, or application clock-error testing. Without actual renderer output these would add precision to the wrong layer.

## Reproducibility

Follow the commands and archive checksums in the [artifact README](../artifacts/step-5/README.md). The final reproduction run regenerated every retained fixture and verified every entry in `manifest.json`. `ffprobe` independently confirmed both WAV files as mono 48 kHz signed 16-bit PCM with the durations recorded above.

## Confidence assessment

| Finding | Evidence state | Confidence |
| --- | --- | --- |
| fldigi and xfhell 105-labelled transmit contracts differ in rate, rows, and shift | `OBS` from source plus `CALC` | High |
| They are not F1 or F2 compatible as inspected | `CALC` from deterministic contracts | High |
| Either can reliably render the other's text | `NOT-ASSESSED` | No conclusion |
| Standard Feld timing is shared at 17.5 columns/s and compatible 122.5/245 representation | `OBS` | High |
| Standard Feld fonts/cells are bit-identical across implementations | Rejected by `CALC` | High |
| Differing Feld fonts remain mutually human-readable | `INFER`, not tested | Low; do not promote |

## Decisions affected

- HELL-DEC-0015 is refined: the FSK/FM Hell-105 alias requires an implementation profile and cannot imply waveform equivalence.
- HELL-DEC-0024 is superseded as a simple test ordering. “FSK Hell-105” must first be split into concrete fldigi and xfhell/IZ8BLY-lineage profiles.
- HELL-DEC-0025 records the implementation-qualified naming rule.
- HELL-DEC-0026 records the rejected generic compatibility claim.
- HELL-DEC-0027 keeps Step 5 open for actual application A→B/B→A tests while allowing Step 6 to use only the bounded source-contract results.

## Unresolved questions

- Can fldigi 4.2.12 be driven with deterministic file input/output without involving a live audio device?
- Can xfhell 3.5.2 be exercised in a contained Linux environment with faithful ALSA file routing and no device access?
- Does fldigi render xfhell's 210-position/s, 210 Hz-shift FMHell105 intelligibly?
- Does xfhell render fldigi's 245-position/s, 55 Hz-shift FSKH105 intelligibly?
- Which current program, if any, implements the original six-row/105-baud/55-Hz profile exactly?
- How do square, raised-cosine, and xfhell half-dot edge shaping affect practical bandwidth under one named measurement criterion?
- At what clock error do the three Feld implementations move from F3 to F4?

## Step 6 inputs

Step 6 may now evaluate separate candidate contracts:

1. standard Feld-Hell timing with an explicit font and spacing policy;
2. fldigi 4.2.12 `FSKH105` implementation profile;
3. xfhell 3.5.2 `FMHell105` implementation profile;
4. the documentary original 105-baud/six-row profile, still requiring a verified current implementation.

Step 6 must not combine items 2–4 into one candidate. Any Wsprry Pi feasibility result must name its intended decoder and target waveform.

## Recommended next step

Close the remaining Step 5 gate with actual offline application playback/capture in both directions for fldigi and xfhell. If that environment cannot be established safely, retain the compatibility class as `NOT-ASSESSED` and proceed to Step 6 only as a conditional feasibility study—not as an interoperability-qualified design.

## Explicit non-claims

This report does not establish receiver sensitivity, occupied bandwidth, hardware timing, GPIO safety, transmitter linearity, RF spectral compliance, on-air performance, regulatory suitability, deployment readiness, or a Wsprry Pi implementation choice.
