# Step 3 — Freeze the font evidence

## Status

Complete for documentary font evidence. A historical Feld-Hell research reference is frozen **by external source and SHA-256**, not copied into this repository. Current fldigi and RadioLib tables are frozen by immutable source revisions. No production font was selected, no transmitter or renderer was implemented, and no signal, hardware, or RF test was performed.

## Technical summary

The best available historical Feld-Hell font evidence is Frank Dörenberg N4SPP's binary transcription of the actual Tbs 24a character drum, supported by photographs of the drum and an independently described 41-character repertoire. It records 41 tracks—`A–Z`, `0–9`, `+ - ? /`, and the Hell pause symbol—as seven columns of 14 physical positions, scanned bottom-to-top. The downloaded transcription hashed to:

```text
SHA-256  1dc94ee5ba9aa62acecfe0607a1109c48e286595c34931095fc3b3a8bdae93d5
```

This source changes an earlier simplification without changing the established timing. Twenty-six of the 40 printable glyphs contain at least one feature that crosses a fixed adjacent-row-pair boundary. The original drum therefore uses 245 physical positions/s and two-position minimum mark/space runs that may begin at either phase. A fixed-pair 7x7 font remains timing-compatible and readable, but it is not a bit-exact representation of the historical drum. [HELL-EVID-0023] [HELL-EVID-0024]

fldigi commit `0a1a30d0c5762d90f570fd51b1d7aecf44ce7ce5` contains 15 selectable GPL-3.0-or-later font tables, each covering printable ASCII. Its transmitter adds blank columns, reverses stored row order into bottom-to-top transmission, and ends a glyph after its last occupied column. After reconstructing that behavior, the closest historical-style table is `real`, but it is not identical: among the 40 shared printable historical characters, 37 produce seven-column cells, and only 27 match the drum transcription exactly. The differing or non-seven-column set is `- / 1 3 6 7 9 ? I K P Q U`. The ordinary fldigi `7x7` table matches only five of 37 comparable historical glyphs exactly. [HELL-EVID-0025]

RadioLib commit `0795caa41c6350a2f862137cfc22528c2aaad2bc` uses a deliberately simplified 5x5 image inside a 7x7 cell, with 64 stored entries covering space through underscore and lowercase mapped onto uppercase. Its MIT-licensed table is a reproducible embedded implementation reference, not a historical font. [HELL-EVID-0026]

The 105-baud FM/MSK/PSK family requires a separate six-row logical font; Hell-80 requires its historical 9x7/63-element font; GL-Hell adds a hidden start column and modifies the repertoire. These are not transformations that a standard Feld glyph table can satisfy bit-for-bit. [HELL-EVID-0027] [HELL-EVID-0028]

## Research question and decision use

Which historical and modern glyph tables are sufficiently identified, reproducible, licensed, and geometrically understood to support later offline interoperability experiments without confusing visual similarity with bit identity?

The answer supports Step 5 fixture design. It does not choose what Wsprry Pi should implement.

## Scope and method

The search prioritized original equipment evidence, manufacturer manuals, equipment-derived reconstructions, immutable source, and official software documentation. Candidate tables were compared at four distinct layers:

1. source/stored glyph geometry;
2. logical image geometry;
3. transmitted raster after spacing, orientation, and duplication rules;
4. receiver/display geometry.

Temporary local analysis parsed the historical text transcription and fldigi C++ tables. For fldigi, the comparison reproduced `get_font_data()` and `tx_char()` behavior: reverse the 14 stored rows for transmission, retain occupied columns through the last set bit, add one blank column before and after, then compare a seven-column transmitted cell with the historical bottom-to-top 7x14 matrix. Only identity and complete row/column flips were considered; no artistic redrawing or best-fit shifting was allowed. Temporary analysis files were not committed.

`OBS`, `CALC`, `INFER`, `CONFLICT`, `UNKNOWN`, `NOT-ASSESSED`, and `N/A` retain their Step 1.2 meanings.

## Geometry and font terms

| Layer | Historical Feld-Hell | Typical simplified software font |
| --- | --- | --- |
| Source/stored | 41 physical drum tracks, each 7 columns x 14 positions | Bitmap table, often 5 image columns x 7 logical rows or 14 stored rows |
| Logical image | Mostly five image-bearing columns plus two blank spacing columns | Program-specific; may be proportional and may extend printable ASCII |
| Transmitted | 7x14 at 245 positions/s; two-position minimum runs may use either phase | Often 7x14 fixed pairs or direct 7x7 at 122.5 intervals/s |
| Display | Two vertically displaced paper copies | Waterfall pixels, scaling, filtering, and repeated display are receiver choices |

The historical character cell is therefore not accurately summarized as 98 independent bits or as 49 fixed pair-aligned bits. It is a 98-position physical pattern subject to an approximately two-position minimum run rule.

## Candidate inventory

| Candidate | Provenance and immutable identity | Repertoire | Geometry and spacing | License/redistribution | Step 3 disposition |
| --- | --- | --- | --- | --- | --- |
| Tbs 24a actual-drum transcription | Equipment-derived N4SPP transcription; external SHA-256 `1dc94e…3d5` | 41 tracks: A–Z, 0–9, `+ - ? /`, pause; space is a keyboard timing function | Fixed 7x14; first/last columns blank; bottom-to-top; no start pulse | Page copyright; no explicit artifact redistribution license found | **Frozen historical research reference by URL and checksum; not copied** |
| Tbs 24a drum photographs/diagram | Identified machine photographs and binary reconstruction on N4SPP font page | Corroborates physical track construction | Physical 7x14 drum positions | Images individually attributed/copyrighted | Corroboration only; link, do not redistribute |
| fldigi 15-font collection | Immutable commit `0a1a30d`; `src/feld/Feld*.cxx` and `feldfonts.cxx` | 95 printable ASCII entries per table | 14 stored rows; occupied width varies; TX adds leading/trailing blank columns | GPL-3.0-or-later | Frozen modern implementation reference by commit |
| fldigi `real` | `FeldReal-14.cxx` at the same commit | Printable ASCII; lowercase glyphs duplicate uppercase style in this table | Closest inspected fldigi table to drum; fine 14-row features; proportional TX termination | GPL-3.0-or-later | Historical-style modern reference; not “the original” |
| RadioLib simplified Hell font | Immutable commit `0795caa`; `Hellschreiber.cpp` | 64 stored positions; accepts space–underscore and maps `a–z` to uppercase; rejects other bytes | 5x5 image stored in 5 rows, padded to a 7x7 transmitted cell | MIT | Frozen embedded implementation reference by commit |
| MultiPSK Feld-Hell | Current official manual; exact source table unavailable | `UNKNOWN` exact table; manual describes traditional/G3PLX-derived behavior | 7x7/7x14 description, bottom-to-top; exact bitmap `UNKNOWN` | Software/manual rights only; no table redistribution basis | Documented implementation lead, not frozen bitmap |
| G3PLX-derived Feld font lineage | MultiPSK documentation and software history identify lineage | Exact revision and complete table not recovered | Reported Feld-compatible | `UNKNOWN` | Provenance lead only; do not assert bit identity |
| IZ8BLY 105-baud font | Developer documentation; xfhell reports receiving tables from IZ8BLY | Special low-resolution character set; exact authoritative table not recovered | Seven columns x six transmitted rows at 105 baud | `UNKNOWN` for original table | Required profile geometry established; bitmap not frozen |
| Siemens Hell-72 GL transcription | Equipment-derived N4SPP binary transcription; external SHA-256 `3a586d…672` | Feld core plus `. , ' = ( ) :`; no pause; modified `E K Q ?` | 7x14; eight-position hidden start pulse in first column; one printed spacing column | No explicit redistribution license found | Frozen GL research reference by URL/checksum; not copied |
| Siemens Hell-80 | Original manual plus reconstruction | Exact complete bitmap table not recovered in redistributable digital form | Historical 9 rows x 7 columns, 63 elements; magnetic-core font memory | Manual/archive rights; table license `UNKNOWN` | Geometry frozen; bitmap remains unresolved |
| Presse/F-Hell | Historical reconstruction | Standard Hell family repertoire varies by equipment/service | 7x14 family; rate/profile dependent | `UNKNOWN` | Font-family relationship only; exact service table unresolved |
| Duplo-Hell | Developer description | Uses Feld font in documented amateur design | Feld glyph/column structure, two columns sent concurrently | Exact program table/license `UNKNOWN` | Reuse relationship established; bitmap follows selected Feld profile |
| S/MT-Hell | Developer technical specification | Profile-specific; typical 7x5 image | Five columns plus spacing; up-then-across; row encoded by frequency | Specification says do not copy without permission | Geometry reference only; no bitmap frozen |
| C/MT-Hell | Developer technical specification/software history | Often operating-system fonts or fixed program font | Row count/profile dependent, commonly 9 or 16 | Font license follows selected system/program font | No common canonical bitmap exists |

## All fldigi font tables at the inspected revision

All 15 files declare GPL-3.0-or-later and contain 95 printable-ASCII records. The transmitter always operates on 14 row slots, despite `12` or `14` in the menu label, and terminates after the last horizontally occupied bit. Thus several fonts are proportional on air and cannot always be normalized to the historical fixed seven-column cell without changing timing.

| Menu label | Source file | Historical comparison result |
| --- | --- | --- |
| `7x7 14` | `Feld7x7-14.cxx` | 37 comparable seven-column glyphs; 5 exact |
| `7x7n 14` | `Feld7x7n-14.cxx` | 6 comparable; 1 exact |
| `Dx 14` | `FeldDx-14.cxx` | 13 comparable; 4 exact |
| `fat 14` | `FeldFat-14.cxx` | 1 comparable; 0 exact |
| `hell 12` | `FeldHell-12.cxx` | 36 comparable; 3 exact |
| `little 12` | `FeldLittle-12.cxx` | 5 comparable; 3 exact |
| `lo8 14` | `FeldLo8-14.cxx` | 4 comparable; 0 exact |
| `low 14` | `FeldLow-14.cxx` | 37 comparable; 0 exact |
| `modern 14` | `FeldModern-14.cxx` | 38 comparable; 12 exact |
| `modern8 14` | `FeldModern8-14.cxx` | 6 comparable; 2 exact |
| `narr 14` | `FeldNarr-14.cxx` | 38 comparable; 22 exact |
| `real 14` | `FeldReal-14.cxx` | 37 comparable; **27 exact**, closest inspected table |
| `style 14` | `FeldStyl-14.cxx` | 38 comparable; 2 exact |
| `vert 14` | `FeldVert-14.cxx` | 37 comparable; 1 exact |
| `wide 14` | `FeldWide-14.cxx` | 3 comparable; 0 exact |

“Comparable” means the reconstructed transmitted glyph occupied exactly seven columns including fldigi's added blanks. It does not mean the remaining glyphs are unreadable; it means they use a different character duration/width and cannot qualify for F1 against a fixed 7x14 drum cell.

## Historical versus fldigi `real`

| Set | Result |
| --- | --- |
| Shared printable historical repertoire | 40 characters |
| Seven-column transmitted cells in fldigi `real` | 37 |
| Bit-exact 7x14 matches | 27 |
| Exact set | `+ 0 2 4 5 8 A B C D E F G H J L M N O R S T V W X Y Z` |
| Different bitmap or width | `- / 1 3 6 7 9 ? I K P Q U` |
| Historical-only control glyph | Hell pause symbol |
| fldigi-only repertoire | Remaining printable ASCII, including lowercase and broader punctuation |

Representative requested characters:

| Character | fldigi `real` vs actual drum | Interpretation |
| --- | --- | --- |
| `A B E M N O S 0 2 5 8 +` | Exact at transmitted 7x14 font layer | F1 for these individual glyphs only |
| `1 ? / -` | Different bitmap or width | Still visually related; not F1 |

No claim of complete on-air interoperability follows from this static comparison. Pulse shaping, timing, receiver behavior, and error conditions belong to Step 5.

## The fixed-pair assumption fails on the actual drum

Of the 40 printable historical glyphs, 26 contain at least one row-pair boundary with unequal values under fixed pairing. The affected set is:

```text
A B D K M N Q R S V W X Y Z 0 1 2 3 4 5 6 7 8 9 + /
```

This does not imply a 4.082 ms isolated pulse. The observed patterns use runs of at least two positions, but their start may be offset by one position and neighboring features may merge into longer runs. Murray Greenman's technical specification independently explains half-position placement and requires a minimum 8.16 ms element. [HELL-EVID-0023] [HELL-EVID-0024]

## Mode-to-font compatibility

| Step 2 profile | Font requirement | Historical Tbs 24a table | fldigi tables | Classification at font/raster layer |
| --- | --- | --- | --- | --- |
| Standard Feld-Hell | 17.5 columns/s; two-position minimum; bottom-to-top | Native reference | `real` closest but incomplete; `7x7` simplified | Historical self: F1; `real`: F2 overall; simplified 7x7: F2 timing/readability, not bit-exact |
| fldigi Slow, X5, X9 | fldigi-selected table with profile rate | Same bitmap can be rate-scaled, but historical fidelity changes with table | Native implementation behavior | F2 font/raster when same table and geometry; rate compatibility requires exact profile |
| FSK Hell-245 | Implementation-dependent Feld-style table | Potentially usable if its 14-position contract is preserved | Native in fldigi | F2 candidate; implementation-specific |
| FM/MSK or PSK Hell-105 | Special six-row font | Not directly reusable | fldigi uses common selectable 14-row machinery for its labeled 105 profile; cross-program identity unproven | Historical Feld table F4 without defined transformation; authoritative 105 bitmap `UNKNOWN` |
| FM/MSK or PSK Hell-245 | Feld/computer 14-row profile | Potentially readable | Program-specific | F2/F3 candidate; requires Step 5 |
| Historical Hell-80 | 9x7, 63-element font | Incompatible geometry | fldigi common tables do not establish historical identity | F4 relative to Feld font; historical bitmap unresolved |
| fldigi Hell-80 | fldigi selectable common table | Not historical Hell-80 identity | Native implementation | F3 candidate versus historical Hell-80; Step 5 required |
| GL-Hell | 7x14 plus hidden start column and modified repertoire | Core shapes related but missing framing/start data | No inspected native GL profile | F4 as unmodified Feld stream; external GL transcription frozen separately |
| Presse/F-Hell | Service/profile-specific 7x14 | Closely related standard family | Potentially readable | F2/F3 depending exact service font/rate |
| Duplo-Hell | Selected Feld font; paired columns in waveform | Suitable bitmap source | Program-dependent | Font may be F1/F2 while waveform remains distinct |
| S/MT-Hell | Typically 7x5/profile-specific | Requires reduction/transformation | Generic PC/program fonts possible | F3 at best without an exact profile table |
| C/MT-Hell | Row-count/profile-specific; often arbitrary computer font | Not a canonical requirement | System-font rendering reported | No universal comparison; profile must freeze its own raster |

F1–F4 above apply only at the stated font/raster layer. Complete interoperability includes rate, waveform, framing, polarity, and receiver behavior.

## Repertoire and substitution findings

- **Historical Tbs 24a:** 41 encoded drum tracks. The blank/space key consumes time without a drum track. The pause symbol is a transmitted glyph. Period and comma are not present; operating practice used textual substitutions documented by the historical reconstruction. [HELL-EVID-0023]
- **fldigi:** every table has 95 entries for ASCII space through tilde. Carriage return and line feed become space. Characters outside the table range cause `get_font_data()` to terminate; broader application encoding behavior was not traced and remains `NOT-ASSESSED`. The selected font applies to all seven fldigi Hell profiles.
- **RadioLib:** accepts ASCII space through underscore; lowercase `a–z` maps onto uppercase entries; other bytes return failure. Leading and trailing blank columns produce a fixed seven-column cell. [HELL-EVID-0026]
- **105-baud family:** six rows are required to retain 17.5 columns/s at 105 baud; a special font is mandatory. The authoritative complete IZ8BLY table and license remain `UNKNOWN`. [HELL-EVID-0027]

## Licensing and artifact decision

| Source | Rights finding | Repository action |
| --- | --- | --- |
| N4SPP historical and GL transcriptions | Publicly downloadable, but no explicit redistribution license found; page and images carry copyright/attributions | Record URL, provenance, access date, and checksum only; do not copy |
| Greenman technical specification | Explicit “Do not copy without permission” notice | Cite and summarize; do not copy |
| fldigi tables | GPL-3.0-or-later notices in every inspected font source | Reference immutable source; no duplicate vendoring needed for this spike |
| RadioLib table | MIT license at immutable revision | Reference immutable source; no duplicate vendoring needed |
| MultiPSK/IZ8BLY/G3PLX tables | Exact source/license not recovered | Do not copy or freeze a guessed table |

No font artifact directory was created. This is deliberate: immutable upstream revisions already preserve the licensed modern tables, while copying the historically valuable transcriptions would exceed the rights evidence recovered in this step.

## Frozen reference manifest

| ID | Role | Immutable locator | SHA-256 or revision | Storage |
| --- | --- | --- | --- | --- |
| `HELL-FONT-HIST-FELD-01` | Historical Tbs 24a reference | `https://www.hellschreiber.com/misc-hell/hell-keyboard-encoding.txt` | `1dc94ee5ba9aa62acecfe0607a1109c48e286595c34931095fc3b3a8bdae93d5` | External only |
| `HELL-FONT-HIST-GL-01` | Historical GL reference | `https://www.hellschreiber.com/misc-hell/hell-72-GL-font-binary.txt` | `3a586deb07ff76a72418c5b5c8005d03cf974caa1282637d18553ced2c076672` | External only |
| `HELL-FONT-FLDIGI-01` | Current multi-font implementation | fldigi source tree | Commit `0a1a30d0c5762d90f570fd51b1d7aecf44ce7ce5` | Immutable upstream, GPL-3.0-or-later |
| `HELL-FONT-RADIOLIB-01` | Embedded simplified implementation | RadioLib source tree | Commit `0795caa41c6350a2f862137cfc22528c2aaad2bc` | Immutable upstream, MIT |

The external historical URLs are checksum-frozen but not guaranteed permanently hosted. A later archival copy requires explicit rights clearance.

## Source disagreements and decisions

| Issue | Resolution |
| --- | --- |
| Historical 7x14 as fixed identical pairs | Rejected. It describes common simplified tables, not all actual-drum glyphs. HELL-DEC-0006 is superseded. |
| Does 245 positions/s mean 245 baud? | No new baud claim is introduced. The drum has 245 physical positions/s, while ordinary transitions have an 8.163 ms/two-position minimum and the historical/manual convention remains 122.5 baud. |
| Is fldigi `real` the original font? | Rejected. It is the closest inspected fldigi table, but 13 shared characters differ or have a different width. |
| Does a different bitmap prevent reading? | Not necessarily. Most Feld-style fonts are expected to be human-readable, but F1 requires exact bitmap, spacing, traversal, and timing. |
| Can one standard font cover every Hell family? | Rejected. 105-baud, Hell-80, GL, S/MT, and C/MT impose different geometry, framing, or profile contracts. |

Accepted decisions are recorded as HELL-DEC-0019 through HELL-DEC-0022. Font selection for production remains deferred.

## Limitations and confidence

- **High:** Tbs 24a repertoire, 7x14 bottom-to-top geometry, checksum of the retrieved transcription, phase-offset features, fldigi table inventory/behavior at the inspected commit, RadioLib table/behavior and licenses.
- **Moderate:** the transcription's bit accuracy, because it is equipment-derived and photographically supported but was not independently digitized from a second drum in this step; overall F2 expectation for modern Feld-style fonts.
- **Low/unknown:** exact G3PLX lineage table, authoritative IZ8BLY 105-baud bitmap, complete Hell-80 bitmap, exact MultiPSK bitmap, and cross-program behavior without Step 5 captures.

No rendering, decoder, audio, spectral, GPIO, hardware, or over-the-air validation was performed. Static glyph matching establishes only the font/raster layer.

## Step 5 fixture requirements

Step 5 should retrieve each external fixture, verify its checksum before use, and record the exact program revision. At minimum it should render and transmit offline:

1. `HELL-FONT-HIST-FELD-01` at the corrected 245-position/two-position-minimum contract;
2. fldigi `real` and `7x7` at commit `0a1a30d`;
3. RadioLib's simplified table at commit `0795caa`;
4. an authoritative 105-baud table if recovered;
5. historical and fldigi Hell-80 fixtures only after the historical bitmap is recovered.

The comparison text must cover the full shared repertoire and include `A B E M N O S 0 1 2 5 8 ? / + -`. Tests must distinguish bitmap equality, human readability, rate/framing interoperability, and spectral consequences.

## Further questions

- Can a second independently digitized Tbs 24a drum confirm the external transcription bit-for-bit?
- Can redistribution permission be obtained so the historical transcription can be archived with the spike?
- Which exact G3PLX revision underlies MultiPSK and older amateur programs?
- Can the authoritative IZ8BLY 105-baud and Hell-80 tables be recovered with clear licenses?
- Which font/profile combinations are actually used on air? That belongs to Step 4, not this report.

## Recommended next step

Proceed to **Step 4 — Measure real adoption**. Count supported, scheduled, and observed use separately; require time bounds, denominators, mode-identification rules, and bias notes. Do not infer prevalence from the number of available fonts or programs.
