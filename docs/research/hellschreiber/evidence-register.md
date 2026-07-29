# Hellschreiber Evidence Register

## Purpose

This append-oriented register ties material spike claims to reviewable sources. Step reports should cite the relevant evidence IDs and preserve important limitations or disagreements.

## Evidence-quality hierarchy

1. Original patents, manuals, schematics, and contemporary specifications
2. Measurements or reconstructions tied to identified historical equipment
3. Museum, university, and established technical-collection descriptions
4. Current official software documentation and inspectable source code
5. Independent decoder documentation
6. Amateur-radio club operating material
7. General secondary summaries

## Citation requirements

- Link directly to the supporting page, patent, manual, source file, or specification.
- Record publication or revision dates where available and the date accessed.
- Cite page, section, figure, or source location when it materially improves reviewability.
- Distinguish quoted or directly observed evidence from inference.
- Do not cite search-result pages as evidence.
- Avoid relying on a single secondary source for a normative on-air parameter.

## Confidence definitions

- **High:** Primary evidence plus independent corroboration, with consistent definitions.
- **Moderate:** Multiple consistent secondary or implementation sources, but incomplete primary evidence.
- **Low:** A single source, an inference, an unresolved definition, or material disagreement.

Confidence applies to the specific claim being registered, not to the source as a whole.

## Source-conflict policy

When sources conflict:

1. Preserve the disagreement explicitly.
2. Check whether the sources define terms, layers, or measurement conditions differently.
3. Prefer original specifications for historical behavior and current source code for current software behavior.
4. Do not let modern implementation behavior silently rewrite the historical contract.
5. Mark the conclusion provisional or unresolved when the evidence cannot support a defensible choice.

## Evidence table

Use stable identifiers in the form `HELL-EVID-0001`.

| ID | Step | Claim supported | Source | Source type | Publication or revision date | Access date | Evidence extracted | Confidence | Limitations | Local report reference |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HELL-EVID-0001 | 1.1 | The field machine uses 2.5 char/s, seven columns, bottom-to-top cell scanning, left-to-right columns, 8.16 ms minimum intervals, 122.5 baud, unsynchronized reception, and receiver-side double printing. | [D 758/1 English translation v1.2](https://www.hellschreiber.com/pdf-hell/Feldfernschreiber-manual-translated-V1-2.pdf); [original German scan](https://www.hellschreiber.com/pdf-hell/Hell-manual-1941.pdf), §§9, 17–22, 47–49, 55–58, 90, 94 | Original military manual and identified translation | 1941; translation version 1.2, undated | 2026-07-29 | 150 char/min; 2.5 char/s; five image plus two spacing columns; 17.5 columns/s; 57.2 ms/column; 8.16 ms minimum pulse/pause; 122.5 baud; 900 Hz keyed tone; two-turn receive spindle; uppercase 43-key repertoire. | High | Translation is modern and unofficial; key numerical claims were checked against the original scan and internal arithmetic. | [Reference machine through receiver behavior](steps/1.1-reference-mode.md#reference-machine-and-mode) |
| HELL-EVID-0002 | 1.1 | Rudolf Hell's printing-telegraph patents establish the system lineage; DE541935 is an addition and not the field machine's numerical specification. | [DE541935 public-domain scan](https://commons.wikimedia.org/wiki/File:DE000000541935A_all_pages.pdf); [Hellschreiber patent chronology](https://www.hellschreiber.com/hellschreiber-history.htm) | Original patent scan plus technical chronology | 1929–1931 | 2026-07-29 | Chronology distinguishes core patent DE540849 from related addition DE541935. | Moderate | The original DE540849 text was not used to derive Step 1.1 timing; the 1941 manual controls those values. | [Reference machine and mode](steps/1.1-reference-mode.md#reference-machine-and-mode) |
| HELL-EVID-0003 | 1.1 | Early Hell equipment used a 7x7 image and a helical printer; lack of synchronization makes text wander rather than prevents reading. | [RWTH Aachen technical collection: Hell-Schreiber](https://sammlung.ient.rwth-aachen.de/katalog/fernschreiben-und-fernkopieren/hell-schreiber.html) | University technical collection | Undated collection record | 2026-07-29 | Identifies a 1931 Siemens & Halske apparatus, 7x7 dot image, facsimile principle, helical printing, and asynchronous drift. | Moderate | Describes an early collection apparatus, not every T.typ.58 parameter. | [Historical reconstruction](steps/1.1-reference-mode.md#historical-reconstruction) |
| HELL-EVID-0004 | 1.1 | The standard field machine is identified as Siemens & Halske T.typ.58 / Tbs 24a-32, with production beginning in 1935. | [The Feld-Hell machine](https://www.hellschreiber.com/hellschreiber-the-feld-hell.htm); [font reconstruction](https://www.hellschreiber.com/hellschreiber-fonts.htm) | Identified historical reconstruction | Undated | 2026-07-29 | Provides model/designation and reconstruction context for the surviving machine and drums. | Moderate | Independent enthusiast reconstruction; exact font claims remain deferred pending Step 3 triangulation. | [Reference machine and mode](steps/1.1-reference-mode.md#reference-machine-and-mode) |
| HELL-EVID-0005 | 1.1 | Current fldigi documentation defines Feld-Hell as 122.5 baud and 2.5 char/s, typically 14 raster samples/column transmitted in identical pairs, with no synchronization and selectable pulse shaping. | [fldigi Feld Hell](https://www.w1hkj.org/FldigiHelp/feld_hell_page.html); [Feld Hell configuration](https://www.w1hkj.org/FldigiHelp/feld_hell_configuration_page.html) | Current official software documentation | Documentation for current fldigi family | 2026-07-29 | Bottom-to-top/left-to-right scanning; 14 pixels/column with singles not transmitted; 122.5 baud; 15 fonts; square or raised-cosine edges. | High for fldigi behavior; moderate as historical evidence | Documentation quotes bandwidth/settings using modern operating conventions rather than a historical spectral mask. | [7x7 / 7x14 reconciliation](steps/1.1-reference-mode.md#the-7x7--7x14-and-1225--245-reconciliation) |
| HELL-EVID-0006 | 1.1 | fldigi implements 17.5 columns/s and 14 samples/column (245 samples/s), with paired-row 7x7 fonts and bottom-to-top extraction. | [feld.cxx at commit 0a1a30d](https://sourceforge.net/p/fldigi/fldigi/ci/0a1a30d0c5762d90f570fd51b1d7aecf44ce7ce5/tree/src/feld/feld.cxx); [Feld7x7-14.cxx](https://sourceforge.net/p/fldigi/fldigi/ci/0a1a30d0c5762d90f570fd51b1d7aecf44ce7ce5/tree/src/feld/Feld7x7-14.cxx) | Inspectable current source at immutable revision | Commit 0a1a30d, 2026-07-13 | Constants and loops show 17.5 columns/s, 14 samples/column, paired font rows, column-major rasterization, OOK, and shaping options. | High for this implementation | fldigi supports several modes and fonts; its defaults are not normative historical behavior. | [Modern implementation comparison](steps/1.1-reference-mode.md#modern-implementation-comparison) |
| HELL-EVID-0007 | 1.1 | An independent commercial decoder describes 7x14 lines as paired pixels, effective five image columns, 122.5 baud, no synchronization, and receiver double printing. | [Wavecom Feld-Hell decoder documentation](https://www.wavecom.ch/content/ext/decoderonlinehelp/worddocuments/feldhell.htm) | Independent decoder documentation | Undated | 2026-07-29 | Seven columns x 14 lines; first/last blank; pixel pairs; bottom-to-top then left-to-right; 122.5 Bd; two printed copies. | High as independent corroboration | Decoder documentation is not an original specification. | [Modern implementation comparison](steps/1.1-reference-mode.md#modern-implementation-comparison) |
| HELL-EVID-0008 | 1.1 | MultiPSK uses the same 8.163 ms logical timing, scan order, OOK tone, unsynchronized display, and paired representation, while using ambiguous “245/pseudo-122.5” terminology. | [MultiPSK English manual](https://f6cte.free.fr/MULTIPSK_english_manual.pdf), Feld Hell section | Official software manual | Current manual available 2026-07-29 | States 8.163 ms per pixel, bottom-to-top/left-to-right, 7x7, OOK, raised-cosine shaping, no synchronization, and receive-side duplicated vertical display. | Moderate | Its baud phrasing conflates doubled elementary samples with independent signal intervals. | [7x7 / 7x14 reconciliation](steps/1.1-reference-mode.md#the-7x7--7x14-and-1225--245-reconciliation) |
| HELL-EVID-0009 | 1.1 | RadioLib demonstrates a compatible direct 7x7 implementation at a default 122.5 rate without requiring a 245-sample representation. | [Hell client documentation](https://jgromes.github.io/RadioLib/class_hell_client.html); [source at commit 0795caa](https://github.com/jgromes/RadioLib/blob/0795caa41c6350a2f862137cfc22528c2aaad2bc/src/protocols/Hellschreiber/Hellschreiber.cpp) | Official API documentation and inspectable source at immutable revision | Commit 0795caa, 2026-07-20 | Default Feld-Hell rate 122.5; 7x7 traversal; 49 timed cells; direct carrier or AFSK tone. | High for this implementation | Simplified stored glyphs are an implementation choice and not historical font evidence. | [Modern implementation comparison](steps/1.1-reference-mode.md#modern-implementation-comparison) |
| HELL-EVID-0010 | 1.1 | Logical cells are 8.16 ms while a doubled representation has two 4.08 ms samples; alternating keying has a 61.25 Hz fundamental, and occupied bandwidth depends on shaping and criterion. | [Koos Fockens PA0KDF, *The Bandwidth of a Well Modulated Feld-Hell Signal*](https://feldhell.net/The_Bandwidth_of_a_Well_Modulated_Feld-Hell_Signal.pdf) | Identified measurement and analysis | 2025-01-07 | Separates logical and elementary pixel timing; measures raised-cosine spectra; reports about 200 Hz containment under stated test conditions. | Moderate | A recent amateur measurement, not a historical standard or a Wsprry Pi qualification test. | [Pulse shaping and bandwidth](steps/1.1-reference-mode.md#pulse-shaping-and-bandwidth) |
