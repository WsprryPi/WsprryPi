# Normative Standard Feld protocol core

## Status and conformance language

This document freezes the font-independent protocol core for the proposed Wsprry Pi profile. The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative within this research specification. An implementation is not authorized by this document.

Profile identifier: `standard-feld-wsprry-v1-draft`

The `draft` suffix MUST remain until the asset, spacing policy, and exact fixtures pass the remaining Gate A and Gate B work. A future incompatible change MUST receive a new profile version; it MUST NOT silently reuse this identifier.

## Physical raster and timebase

1. Every transmitted character cell contains exactly seven columns traversed left to right. A selected glyph's image-bearing width may be narrower, but its placement and blank columns MUST produce one complete seven-column transmitted cell.
2. Each transmitted column contains 14 physical positions traversed bottom to top.
3. Column indexes are zero-based from the left. Physical-position indexes are zero-based from the bottom.
4. The normative clock is exactly 245 physical positions per second.
5. Position `n` begins at the rational offset `n / 245` seconds and ends at `(n + 1) / 245` seconds.
6. A column therefore lasts exactly `14 / 245 = 2 / 35` second. The column rate is exactly `35 / 2`, or 17.5 columns/s.
7. The conventional `122.5 baud` designation is retained as a mode label reflecting the ordinary two-position minimum element convention. It is not the normative scheduling clock and MUST NOT replace the 245-position/s raster definition.
8. Implementations and fixtures MUST derive offsets from integer position counts or exact rational arithmetic. They MUST NOT construct a stream by repeatedly adding a rounded `4.0816 ms` interval.

Absolute timestamps may be rendered in integer nanoseconds only by a specified deterministic conversion from `n / 245`. The authoritative value remains the position index and rational offset.

## Raster placement policy

Standard Feld permits features to begin on either physical-position phase while preserving an ordinary minimum run of two positions. A fixed pairing of positions `(0,1)`, `(2,3)`, and so on is not historically equivalent to all actual-drum patterns.

The proposed profile therefore preserves arbitrary half-position placement: a run MAY begin at any physical-position index, but a production glyph MUST NOT introduce an isolated one-position RF-on or RF-off feature inside its image solely as a result of font encoding. Exact treatment of boundary runs joining spacing columns is asset-dependent and remains open.

A paired 7-by-7 representation MAY be evaluated later only as a separately named compatibility representation. It MUST NOT be described as bit-identical to this profile or substituted without a versioned decision and interoperability evidence.

## Modulation and polarity

The profile uses sequential on-off keying at one configured carrier frequency.

| Logical raster state | Required output intent |
| --- | --- |
| `1` | RF enabled at the configured carrier frequency |
| `0` | RF disabled; no alternate mark or space frequency is selected |

The event plan MUST express RF intent explicitly. Logical inversion is not part of this profile. Receiver display polarity is not an RF protocol change and MUST be recorded separately during interoperability testing.

No continuous-phase, frequency-shift, audio-tone, envelope-shape, spectral, or physical switching-performance claim follows from this logical contract. Envelope and transition behavior remain later backend and spectral qualification subjects.

## Initial, idle, completion, cancellation, and fault state

The safe logical terminal state is RF disabled.

- A plan MUST begin from RF disabled.
- Leader and trailer positions, once selected, MUST be explicit raster-zero positions; implicit uncounted keying is forbidden.
- Normal completion MUST end with RF disabled.
- Cancellation, configuration invalidation, shutdown, and fault MUST request RF disabled immediately and MUST NOT wait for a character boundary.
- The protocol-level cancellation boundary is every physical-position boundary. A backend's measured stop latency is a separate qualification result and MUST NOT be inferred from this boundary.
- Idle between scheduled transmissions and repeats is RF disabled.

## Stream construction

The normative stream pipeline is:

1. receive immutable input bytes or text under a declared encoding;
2. validate the input representation;
3. apply the versioned normalization policy;
4. perform repertoire lookup;
5. either reject or apply an explicitly recorded substitution;
6. obtain the immutable glyph raster and width;
7. add the versioned leading/trailing glyph spacing and word-space representation;
8. add explicit stream leader and trailer positions;
9. traverse columns left to right and positions bottom to top;
10. compile logical states into an absolute-offset event plan.

Normalization and substitution are different operations and MUST be reported separately. Neither MAY silently discard, transliterate, uppercase, or replace input unless the later asset contract explicitly authorizes that transformation.

The exact input encoding, repertoire, normalization, substitution, image-bearing glyph widths and placement within the fixed seven-column cell, word spacing, leader length, and trailer length are asset-bound decisions listed below. Until they are frozen, a glyph-bearing message does not have an authoritative raster or complete stream duration.

An empty normalized message MUST be rejected before output unless a later explicit product decision defines a useful no-content operation. It MUST NOT produce an RF-on event.

## Duration and scheduling

Let `N` be the complete number of physical positions after normalization, glyph lookup, spacing, word spaces, leader, and trailer. The authoritative duration is exactly:

`duration = N / 245 seconds`

Duration reporting MUST be derived from the compiled plan, not from character count. A display may round the value but MUST preserve the compiled value for validation.

A scheduled repeat is valid when the complete compiled duration is less than or equal to the repeat interval. Equality remains valid, matching the current non-WSPR policy. A repeat MUST start from a newly safe RF-disabled boundary; no raster state may leak between iterations.

Wall-clock scheduling selects the launch instant. Position offsets inside a committed transmission are monotonic offsets. Clock adjustment MUST NOT reorder or resize positions after the plan is committed.

## Determinism and event compilation

Given identical profile version, asset checksum, spacing-policy version, normalization policy, input, frequency, leader/trailer policy, and fixture-schema version, compilation MUST produce identical ordered logical positions, total position count, and duration.

One event per position and a run-length-compressed event plan are semantically equivalent only when expansion produces identical state for every physical position and identical boundaries. Compression MUST NOT change cancellation checkpoints or terminal-state requirements.

Every compiled event MUST be attributable to a stream element and physical-position range. Future implementation fixtures MUST make that attribution reviewable without device access.

## Versioning and compatibility

The following identities are independent and MUST be recorded together:

- protocol profile identifier and version;
- font asset identifier and SHA-256;
- spacing-policy identifier and version;
- fixture-schema version;
- generator or implementation revision.

Changing a glyph bitmap, repertoire, normalization, substitution, glyph width, spacing, leader/trailer, traversal, timing, polarity, or terminal-state rule changes the contract. Such a change requires a reviewed compatibility decision and renewed affected fixtures and interoperability evidence.

## Asset-dependent requirements blocking Gate A

| Requirement | Why asset-dependent | Phase 2 decision | Required later evidence | Blocks Gate A |
| --- | --- | --- | --- | --- |
| Input encoding and glyph repertoire | A table cannot encode characters it does not contain | Exact accepted input and glyph set | Full repertoire manifest and lookup fixtures | Yes |
| Glyph bitmaps | Shape and half-position placement are font data | One immutable font and checksum | Raster fixture for every glyph | Yes |
| Image-bearing glyph width and placement | Candidate artwork occupies different portions of a cell | Exact placement within one fixed seven-column transmitted cell | Placement manifest and raster cases | Yes |
| Leading/trailing glyph columns | Spacing may be stored in the asset or transmitter-added, but the result must total seven columns | Exact ownership and count | Boundary raster fixtures | Yes |
| Word space | Historical and software implementations differ | Exact blank-column/cell policy | Multiword fixture | Yes |
| Normalization | Lowercase and control handling vary | Versioned normalization rules | Original-to-normalized cases | Yes |
| Unsupported characters | Rejection and substitution have operator consequences | Reject or exact substitution map | Negative and substitution fixtures | Yes |
| Boundary minimum runs | Glyph edges may join spacing zeros | Exact conformance interpretation | Edge-case raster review | Yes |
| Leader and trailer lengths | Affects acquisition and duration | Exact zero-position counts | Stream-boundary fixture | Yes |
| Exact message duration | Character cells are fixed, but normalization, word spaces, leaders, and trailers remain open | Derived from selected contract | Reviewed position/event fixture | Yes |

None of these values is inherited from fldigi, xfhell, RadioLib, or the historical transcription merely because that source is readable or familiar.

## Explicit exclusions and non-claims

This profile does not include or authorize:

- fldigi `FSKH105`, xfhell `FMHell105`, or a blended “Hell 105”;
- the documentary six-row/105-baud/55-Hz profile;
- Hell-80, GL-Hell, or a private Wsprry Pi variant;
- an audio backend or physical audio output;
- code, configuration, CLI, scheduler, persistence, UI, service, or dependency changes;
- timing, jitter, frequency, cancellation-latency, GPIO, Si5351, hardware, spectral, RF, regulatory, on-air, release, or deployment claims.

## Gate status

The font-independent protocol core is frozen for Phase 1.1. Gate A remains open until Phase 2 freezes the asset and spacing policy and Phase 1.2 produces reviewable exact-asset raster, event, duration, and cancellation fixtures.
