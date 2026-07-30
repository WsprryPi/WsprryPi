# Phase 1.2 — Exact-asset protocol fixtures

## Status

`PHASE 1.2 PASS — EXACT-ASSET FIXTURES FROZEN; GATE A SATISFIED`

Gate B remains satisfied. This result freezes an offline protocol contract; it does not establish application interoperability, backend timing, hardware behavior, RF behavior, regulatory suitability, or release readiness.

## Inputs

| Contract | Identity |
| --- | --- |
| Protocol | `standard-feld-wsprry-v1` |
| Fixture schema | `standard-feld-fixture-v1` |
| Fixture set | `standard-feld-exact-asset-v1` |
| Font | `wsprry-standard-feld-radiolib-5x5-v1` |
| Font SHA-256 | `025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227` |
| Spacing | `standard-feld-fixed-cell-spacing-v1` |
| Normalization | `standard-feld-ascii-uppercase-v1` |
| Substitution | `standard-feld-no-substitution-v1` |

## Research tools

- [`generate_fixtures.py`](tools/generate_fixtures.py) reads and checksum-verifies the canonical asset, expands it, applies the frozen input policy, compiles exact positions and events, and writes canonical JSON plus checksums.
- [`validate_fixtures.py`](tools/validate_fixtures.py) independently reconstructs geometry and validates the retained outputs without importing generator code.

Both tools use only the Python standard library. They have no network, application, audio, device, GPIO, or RF integration.

## Fixture inventory

| File | Contents | SHA-256 |
| --- | --- | --- |
| `glyphs.json` | All 64 stored glyphs, stored/logical/transmitted geometry, flattened states, duration | `1d82294c800cf79ac115f4765de6645b5d33ee7f4b5e5de7ced366a7c40fac60` |
| `input-cases.json` | 64 stored-code-point successes, 26 lowercase normalizations, 13 atomic rejection cases | `4cb43931a8b7d252319f0306517e2e7b3ca79435db14fba74fd48d65d793e8c7` |
| `messages.json` | Nine message/repeat cases with every position, compressed events, attribution, rational and nanosecond timing | `6ce3ec82b97af539c3188df1fd8d6f53b96e25c9c3fb97223964ac89f5c8f003` |
| `mechanics.json` | Seven all-off/on, alternation, column-boundary, and character-boundary mechanics patterns | `15da8f166da02a9e75a68544beef4d6be0dbefc3d9cc6ddaab236643afd34f31` |
| `cancellation.json` | 295 exhaustive boundaries for one-character `A`; rule and 3,921-boundary validation count for the corpus | `1402389b2461ce1e4f293236e7c2b51c570e81232fb02a55b42a6260612db261` |
| `safety.json` | Completion, rejection, checksum failure, compilation failure, cancellation, and repeat safe-state cases | `b0029564217f5e0d12765cfdb0bd4ab2fbc75cf9075af05ec8f02a0a567c7127` |
| `manifest.json` | Complete fixture-set identity and input/output checksums | `e38202653fb025e5ee41285804d8840360c6fc8ff6e7ae1ad4095338d07d59ac` |

The checksum index is [`fixtures/generated/v1/SHA256SUMS`](fixtures/generated/v1/SHA256SUMS).

## Timing decision

Rational position index over 245 remains authoritative. Integer nanoseconds use absolute integer round-half-up:

`nanoseconds(n) = round_half_up(n × 1,000,000,000 / 245)`

The implementation uses integer arithmetic. Event duration in nanoseconds is the converted exclusive ending boundary minus the converted starting boundary. No rounded interval is accumulated.

## Validation results

- The asset checksum was verified before generation.
- All 64 expected code points occur exactly once.
- All stored records have five binary rows of seven bits.
- Every retained glyph independently reconstructs to seven columns and 14 bottom-to-top positions.
- Every glyph has 98 positions, blank outer columns, and paired physical positions.
- All 64 stored inputs pass without substitution.
- All 26 lowercase inputs normalize one-to-one to uppercase with a recorded change.
- Empty, invalid UTF-8, controls, backtick, braces, vertical bar, tilde, non-ASCII, and a valid-prefix-plus-invalid case reject atomically and emit no plan.
- Multiple, leading, and trailing input spaces retain fixed all-zero cells in explicit message cases.
- Compressed events expand bit-for-bit to retained positions and their attribution ranges cover every event contiguously.
- Every valid message begins with a blank leader, ends with a blank trailer, and has RF-disabled first and final events.
- `A` compiles to 294 positions and exactly 6/5 seconds; a 294-position repeat interval is accepted.
- `AB` compiles to 392 positions and 8/5 seconds; a 294-position repeat interval is rejected.
- `HELL TEST 0123456789 DE WSPRY WSPRY 73` contains 38 characters, compiles to 3,920 positions including leader/trailer, and lasts exactly 16 seconds.
- Cancellation is available at each physical-position boundary; no backend latency claim is made.
- Checksum and compilation failures expose no partial plan and retain the RF-disabled safe state.
- Canonical regeneration in a fresh temporary directory produced the same eight files byte-for-byte.
- The independent validator passed after regeneration.

## Selected-asset limitation

The RadioLib-derived asset uses paired physical positions for every logical pixel. It therefore cannot provide a production glyph that exercises an arbitrary half-position start. Phase 1.2 retains a clearly synthetic single-position alternation mechanics fixture to exercise the general 14-position protocol representation, while all production glyph fixtures accurately remain paired. This is consistent with the protocol: arbitrary half-position placement is permitted by the profile but is not required of every font.

## Gate disposition

Gate A's required versioned protocol and deterministic raster, event, duration, rejection, repeat, cancellation, and terminal-state fixtures now exist and reproduce independently. Gate A is satisfied.

At Phase 1.2 completion, Gate B remained satisfied and Gate C was open because no prior F3 result transferred to the new asset. Phase 3 later qualified the pinned exact corpus to receive F3 through the documented rig adapter; Gate C is now partially satisfied, with exact-contract application-direction coverage, repertoire/substitution coverage, F4 feasibility, and independent scoring still open. See the [Phase 3 report](phase-3-offline-qualification.md).
