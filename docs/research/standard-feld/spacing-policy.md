# Standard Feld spacing and input policy

Policy identifier: `standard-feld-fixed-cell-spacing-v1`

## Character cells

Every normalized character maps to exactly one seven-column by 14-position cell. The canonical font owns the complete seven-column cell after deterministic expansion; no implementation may trim blank columns or transmit proportional widths.

The outer transmitted columns are all zero for every selected glyph. They provide one leading and one trailing blank column around the centered five-column artwork. Adjacent blank columns remain individual logical columns and count independently toward duration.

Event-plan compression may merge consecutive positions with the same RF state, including across cell boundaries, only if expansion preserves every position and its character, column, spacing, leader, or trailer attribution.

## Space and words

ASCII space `U+0020` is one complete all-zero seven-column character cell. Multiple spaces are preserved exactly; they are not collapsed or trimmed. There is no additional automatic inter-word gap beyond the transmitted space cell.

The outer blank columns already contained in every character cell are the complete inter-character spacing. No extra column is inserted between adjacent non-space characters.

## Leader and trailer

Every non-empty message has:

- one all-zero seven-column cell before its first normalized character;
- one all-zero seven-column cell after its last normalized character.

Leader and trailer positions are counted in the compiled duration and attributed separately from message spaces. They produce RF-off intent only. They do not establish measured receiver acquisition or backend stop performance; those remain later qualification questions.

An empty normalized message is rejected and does not produce a leader, trailer, or RF event plan.

## Input and normalization

1. Decode input as UTF-8. Invalid UTF-8 is rejected.
2. Reject an empty input.
3. Map ASCII lowercase `a` through `z` one-to-one to uppercase `A` through `Z`.
4. Preserve stored code points `U+0020` through `U+005F` unchanged.
5. Reject every other code point, including tabs, CR, LF, controls, backtick, braces, vertical bar, tilde, and non-ASCII characters.
6. Perform no locale-dependent case mapping, Unicode normalization, transliteration, replacement, or substitution.
7. Record every lowercase-to-uppercase normalization for diagnostics and fixtures.

Any unsupported code point rejects the complete message before compilation. A valid prefix must not be transmitted.

## Duration

For `C` normalized characters, including every preserved space, the stream contains:

`total_columns = 7 × (C + 2)`

The added two cells are the leader and trailer. Therefore:

`total_positions = 98 × (C + 2)`

`duration = 98 × (C + 2) / 245 seconds = 2 × (C + 2) / 5 seconds`

Each normalized character adds exactly `2/5` second. Leader and trailer together add `4/5` second. These equations apply only after successful normalization and lookup.

The repeat interval remains valid when it equals the complete compiled duration. Each repeat begins and ends in the RF-disabled state and includes its own leader and trailer.

## Versioning

Changing repertoire, normalization, substitution, any glyph data, cell width, blank-column ownership, space width, leader length, or trailer length requires a new asset or spacing-policy version and renewed affected fixtures and interoperability evidence.
