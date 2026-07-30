# Standard Feld deterministic fixture contract

## Status

This document defines the Phase 1.1 fixture schema as constrained by the Phase 2 asset. It does not yet provide expanded production glyph fixtures and does not authorize a generator, application implementation, rig change, audio output, hardware access, or RF output.

Schema identifier: `standard-feld-fixture-v1-draft`

The suffix remains `draft` until a canonical machine-readable serialization is implemented and independently reproduced.

## Fixture classes

1. `protocol-mechanics`: font-independent synthetic rasters used to verify scan order, polarity, rational offsets, compression equivalence, cancellation checkpoints, and safe terminal state.
2. `glyph`: one selected immutable glyph and its transmitted columns and positions.
3. `message`: normalized input, glyph sequence, spacing, complete raster, events, and duration.
4. `cancellation`: a message fixture plus each required cancellation checkpoint and terminal state.
5. `impairment-source`: a clean exact-contract fixture and a declarative transformation used by the offline rig.

The selected asset is `wsprry-standard-feld-radiolib-5x5-v1`, and the spacing policy is `standard-feld-fixed-cell-spacing-v1`. Phase 1.2 MUST use those exact identities and checksums. Synthetic all-off, all-on, and alternating-position patterns MUST remain labelled synthetic and MUST NOT be presented as production glyphs.

## Required manifest fields

| Field | Requirement |
| --- | --- |
| `schema_id` | Exact fixture-schema identifier |
| `profile_id` | Exact protocol profile identifier |
| `font_asset_id` | Asset identifier, or `null` only for protocol-mechanics fixtures |
| `font_sha256` | Lowercase SHA-256, or `null` only for protocol-mechanics fixtures |
| `spacing_policy_id` | Versioned policy, or `null` only where spacing is absent |
| `fixture_class` | One of the defined classes |
| `fixture_id` | Stable unique identifier |
| `input_encoding` | Declared encoding |
| `original_input` | Exact input, with escaped control characters |
| `normalized_input` | Exact post-normalization input |
| `normalizations` | Ordered transformations, possibly empty |
| `substitutions` | Ordered substitutions, possibly empty |
| `glyphs` | Ordered glyph IDs and widths |
| `positions_per_second` | Integer `245` |
| `columns` | Ordered transmitted columns |
| `positions` | Ordered physical positions with stream, column, and bottom-up indexes |
| `events` | Ordered absolute-offset RF-intent events |
| `total_positions` | Complete integer position count |
| `duration` | Rational numerator and denominator in seconds |
| `cancellation_checkpoints` | Required position boundaries and RF-disabled result |
| `terminal_rf_state` | Must be `off` |
| `generator_repository` | Repository identity |
| `generator_revision` | Immutable commit, or explicit `UNCOMMITTED` during review |
| `manifest_sha256` | SHA-256 recorded in the required sidecar checksum index |

## Position and event representation

Every position record MUST include:

- zero-based stream-position index;
- zero-based transmitted-column index;
- zero-based bottom-to-top physical-position index within that column;
- logical state `0` or `1`;
- exact start offset represented as position index over 245;
- origin classification such as leader, glyph, character spacing, word spacing, or trailer;
- glyph and source-character attribution when applicable.

Every event record MUST include its first position index, position count, logical RF intent, configured-frequency reference, and exact rational start and duration. Expanded events MUST reproduce the position sequence exactly.

## Canonical serialization

The eventual machine-readable form SHOULD be UTF-8 JSON with:

- no byte-order mark;
- LF line endings;
- object keys sorted lexicographically at every level;
- arrays retained in normative transmission order;
- integers for counts and indexes;
- rational times as `{ "numerator": integer, "denominator": integer }` reduced to lowest terms;
- no floating-point value used as normative time;
- exactly one trailing LF.

SHA-256 covers the complete canonical manifest bytes. The value MUST be stored outside the manifest in a checksum index using the repository-relative path and lowercase digest. This avoids a self-referential manifest. A later generator design MUST implement and independently test canonical serialization before the schema loses its `draft` suffix.

## Duration and cancellation checks

- `total_positions` MUST equal the length of the expanded position list.
- Duration MUST equal `total_positions / 245` seconds after rational reduction.
- Event offsets MUST be monotonic and contiguous unless a later schema explicitly represents RF-off gaps as positions.
- The last event or explicit terminal record MUST establish RF disabled.
- Cancellation checkpoints MUST include every physical-position boundary from zero through completion for small canonical fixtures. Large message fixtures MAY use a compact rule plus boundary samples if an automated exhaustive check is retained.
- Cancellation expected state is RF disabled; measured backend latency is outside this fixture contract.

## Provenance and cross-repository traceability

A rig-generated result MUST record the exact rig commit, the WsprryPi protocol commit or document checksum, fixture checksum, font checksum, application versions, settings, and result classification. While work is uncommitted, the revision MUST be recorded as `UNCOMMITTED`; no future commit identifier may be predicted.

When a rig result informs a product conclusion, the rig capability/evidence commit should be published first and the WsprryPi conclusion should cite that immutable commit and the selected artifact checksums. Large generated evidence remains in the rig boundary unless deliberately selected for durable retention after a rights review.

## Phase 1.2 acceptance

After Phase 2, Phase 1.2 must provide at minimum:

- one fixture for every supported glyph;
- normalization, rejection, and substitution cases;
- leading/trailing spacing and word-space cases;
- an empty-input rejection case;
- a corpus fixture retaining `HELL TEST 0123456789 DE WSPRY WSPRY 73` where supported;
- boundary glyphs that exercise arbitrary half-position placement;
- all-off, all-on, and rapid-alternation worst-case mechanics fixtures;
- exact event expansion, total duration, repeat-equality, cancellation, and safe-terminal-state cases;
- manifests and checksums reproducible without hardware.
