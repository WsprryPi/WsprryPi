# Phase 2 font selection

## Status

**Selected:** `wsprry-standard-feld-radiolib-5x5-v1`.

This is a proposed immutable product asset for the design spike. It is not production implementation. Phase 3 qualified the pinned exact corpus to application-level receive F3 through the documented rig adapter; broader Gate C coverage remains open.

## Selection

The selected asset is the simplified RadioLib Hellschreiber artwork from immutable commit `0795caa41c6350a2f862137cfc22528c2aaad2bc`, canonically represented in [`assets/font/font.json`](assets/font/font.json). The upstream source file SHA-256 is `44e1e4fd22d130d018e8e02745845fe2cf059eb6730a813190f8a2b30486c3cb`; the canonical asset SHA-256 is `025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227`.

RadioLib stores five image rows of seven bits. Its transmitter adds one blank logical row above and below, forming a centered 7-by-7 logical cell. The Wsprry Pi contract deterministically duplicates each logical row into two physical positions, producing a fixed seven-column by 14-position raster at 245 positions/s.

This choice is deliberately a modern simplified font rather than a claim of historical bit identity. Every logical vertical pixel occupies a fixed physical-position pair. That is conformant with the Phase 1.1 protocol, which permits but does not require artwork to exercise arbitrary half-position starts. The asset cannot be described as an actual-drum transcription or inherit historical F1 status.

## Candidate dispositions

| Candidate | Immutable identity | Redistribution | Geometry | Disposition |
| --- | --- | --- | --- | --- |
| RadioLib simplified table | Commit `0795caa`; source SHA-256 recorded | MIT; license text retained | Deterministic centered 5x5 artwork in 7x7, exactly expandable to 7x14 | **Selected** |
| fldigi `real` and other tables | Commit `0a1a30d` | GPL-3.0-or-later | 14-row tables but transmitter widths are often proportional; fixed seven-column normalization would change some contracts | **Reference only**; not copied |
| Tbs 24a equipment-derived transcription | External SHA-256 `1dc94e…3d5` | No explicit artifact redistribution permission found | Historical fixed 7x14 and half-position features | **Historical reference only**; not copied |
| Independent new artwork | No asset exists | Could be first-party MIT if genuinely independent | Would require authorship and design evidence | **Not needed**; avoids unnecessary authorship and compatibility risk |

No composite score was used. RadioLib clears the immutable-identity, deterministic-geometry, and redistribution gates. Its weaker historical fidelity and untested exact-asset interoperability remain explicit qualification risks rather than being hidden by the selection.

## Repertoire

The canonical stored repertoire is exactly ASCII `U+0020` through `U+005F` inclusive:

- space;
- `!"#$%&'()*+,-./`;
- digits `0` through `9`;
- `:;<=>?@`;
- uppercase `A` through `Z`;
- `[\]^_`.

Lowercase ASCII `a` through `z` is accepted as input and normalized one-to-one to uppercase before lookup. Lowercase glyphs are not separately stored.

Input is decoded as UTF-8, then restricted to the stated ASCII repertoire plus lowercase normalization. Invalid UTF-8, controls, tabs, carriage returns, line feeds, backtick, braces, vertical bar, tilde, non-ASCII code points, and all other unsupported input cause the complete message to be rejected before output.

No glyph substitution is permitted. Rejection is preferred to lossy or locale-dependent replacement.

## Canonical geometry

For every stored glyph:

1. Read five seven-bit rows top to bottom.
2. Add an all-zero seven-bit logical row above and below.
3. Traverse transmitted columns left to right.
4. For each column, traverse logical rows bottom to top.
5. Duplicate each logical row value into two consecutive physical positions.

This produces exactly seven columns and 14 binary physical positions per column. The first and last transmitted columns are blank because the source rows' outer bits are zero. The first and last logical rows are also blank by expansion.

Phase 2 validation confirmed that all 64 source records match the canonical JSON and expand to valid fixed 7-by-14 binary rasters. Full transmitted raster fixtures remain Phase 1.2 work.

## License and provenance

RadioLib's repository license at the pinned commit is MIT. The license permits use, copying, modification, publication, distribution, and sublicensing subject to retaining its copyright and permission notice. Wsprry Pi retains that notice in [`assets/font/LICENSE-RadioLib.txt`](assets/font/LICENSE-RadioLib.txt).

This is a project distribution decision based on the identified license, not legal advice. The canonical JSON is a modified representation derived from the RadioLib table and remains accompanied by the RadioLib MIT notice.

Generated exact-asset fixtures may be redistributed with the research record under the same attribution boundary. Third-party application screenshots and captures require separate provenance and evidence-retention review.

## Interoperability risk

The selected glyphs differ materially from the historical transcription and the fldigi `real` table. Distinctive digits, punctuation, narrow strokes, and the compact five-by-five artwork require explicit scoring. The earlier clean F3 result used application-native fonts and does not establish readability for this asset.

Phase 2 itself made no F3 or F4 claim. Phase 3 subsequently tested this checksum as Wsprry-derived audio received by fldigi 4.2.12 and xfhell 3.5.2 with retained settings, renders, repeated clean trials, and a fixed impairment matrix. The exact corpus reached receive F3; exact-contract application-direction coverage, independent scoring, repertoire/substitution coverage, and F4 feasibility remain open. See the [Phase 3 report](phase-3-offline-qualification.md).
