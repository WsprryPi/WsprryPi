#!/usr/bin/env python3
"""Generate deterministic Standard Feld research fixtures without device access."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import tempfile
from pathlib import Path

PROFILE_ID = "standard-feld-wsprry-v1"
SCHEMA_ID = "standard-feld-fixture-v1"
ASSET_ID = "wsprry-standard-feld-radiolib-5x5-v1"
ASSET_SHA256 = "025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227"
SPACING_ID = "standard-feld-fixed-cell-spacing-v1"
NORMALIZATION_ID = "standard-feld-ascii-uppercase-v1"
SUBSTITUTION_ID = "standard-feld-no-substitution-v1"
RATE = 245
NS_PER_SECOND = 1_000_000_000

SCRIPT = Path(__file__).resolve()
ROOT = SCRIPT.parents[1]
FONT_PATH = ROOT / "assets/font/font.json"
DEFAULT_OUTPUT = ROOT / "fixtures/generated/v1"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def rational(value: int, denominator: int = RATE) -> dict[str, int]:
    divisor = math.gcd(value, denominator)
    return {"denominator": denominator // divisor, "numerator": value // divisor}


def boundary_ns(index: int) -> int:
    # Exact integer round-half-up of index * 1e9 / 245.
    numerator = index * NS_PER_SECOND
    return (2 * numerator + RATE) // (2 * RATE)


def load_font() -> dict[str, object]:
    if sha256(FONT_PATH) != ASSET_SHA256:
        raise ValueError("canonical font checksum mismatch")
    font = json.loads(FONT_PATH.read_text(encoding="utf-8"))
    if font["asset_id"] != ASSET_ID:
        raise ValueError("unexpected asset ID")
    expected = [f"{code:04X}" for code in range(0x20, 0x60)]
    if list(font["glyphs"]) != expected:
        raise ValueError("font repertoire is not exactly U+0020 through U+005F")
    return font


def expand_glyph(codepoint: int, stored_rows: list[str]) -> dict[str, object]:
    if len(stored_rows) != 5 or any(len(row) != 7 or set(row) - {"0", "1"} for row in stored_rows):
        raise ValueError(f"invalid stored geometry for U+{codepoint:04X}")
    if any(row[0] != "0" or row[-1] != "0" for row in stored_rows):
        raise ValueError(f"nonblank outer column for U+{codepoint:04X}")
    logical_rows = ["0000000", *stored_rows, "0000000"]
    columns: list[str] = []
    for column_index in range(7):
        bottom_to_top = [row[column_index] for row in reversed(logical_rows)]
        physical = "".join(bit * 2 for bit in bottom_to_top)
        if len(physical) != 14:
            raise AssertionError("internal expansion error")
        columns.append(physical)
    flattened = "".join(columns)
    return {
        "codepoint": f"U+{codepoint:04X}",
        "display": chr(codepoint),
        "stored_rows_top_to_bottom": stored_rows,
        "logical_rows_top_to_bottom": logical_rows,
        "columns_left_to_right_positions_bottom_to_top": columns,
        "flattened_positions": flattened,
        "paired_positions": all(flattened[index] == flattened[index + 1] for index in range(0, 98, 2)),
        "total_positions": 98,
        "duration": rational(98),
        "terminal_rf_state": "off",
    }


def normalize(raw: bytes) -> dict[str, object]:
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return rejected(raw, "invalid-utf8")
    if not text:
        return rejected(raw, "empty-input")
    normalized: list[str] = []
    changes: list[dict[str, object]] = []
    for index, character in enumerate(text):
        codepoint = ord(character)
        if 0x61 <= codepoint <= 0x7A:
            replacement = chr(codepoint - 0x20)
            normalized.append(replacement)
            changes.append({"index": index, "from": character, "to": replacement})
        elif 0x20 <= codepoint <= 0x5F:
            normalized.append(character)
        else:
            return rejected(raw, f"unsupported-U+{codepoint:04X}")
    return {
        "accepted": True,
        "emitted_plan": True,
        "normalizations": changes,
        "normalized_input": "".join(normalized),
        "original_utf8_hex": raw.hex(),
        "rejection_reason": None,
        "substitutions": [],
    }


def rejected(raw: bytes, reason: str) -> dict[str, object]:
    return {
        "accepted": False,
        "emitted_plan": False,
        "normalizations": [],
        "normalized_input": None,
        "original_utf8_hex": raw.hex(),
        "rejection_reason": reason,
        "substitutions": [],
    }


def compress_events(positions: list[dict[str, object]]) -> list[dict[str, object]]:
    events: list[dict[str, object]] = []
    start = 0
    while start < len(positions):
        state = positions[start]["state"]
        end = start + 1
        while end < len(positions) and positions[end]["state"] == state:
            end += 1
        attribution: list[dict[str, object]] = []
        segment_start = start
        current = (positions[start]["origin"], positions[start]["character_index"], positions[start]["column_index"])
        for cursor in range(start + 1, end + 1):
            key = None if cursor == end else (
                positions[cursor]["origin"], positions[cursor]["character_index"], positions[cursor]["column_index"]
            )
            if key != current:
                attribution.append({
                    "column_index": current[2],
                    "end_position_exclusive": cursor,
                    "character_index": current[1],
                    "origin": current[0],
                    "start_position": segment_start,
                })
                segment_start = cursor
                current = key
        events.append({
            "attribution_ranges": attribution,
            "carrier_reference": "carrier" if state == 1 else None,
            "duration": rational(end - start),
            "duration_nanoseconds": boundary_ns(end) - boundary_ns(start),
            "end_nanoseconds": boundary_ns(end),
            "end_position_exclusive": end,
            "event_index": len(events),
            "position_count": end - start,
            "rf_state": "on" if state == 1 else "off",
            "start": rational(start),
            "start_nanoseconds": boundary_ns(start),
            "start_position": start,
        })
        start = end
    return events


def build_message(identifier: str, raw: bytes, glyphs: dict[int, dict[str, object]], repeat_positions: int | None = None) -> dict[str, object]:
    result = normalize(raw)
    base = {
        "fixture_class": "message",
        "fixture_id": identifier,
        "input": result,
    }
    if not result["accepted"]:
        return {**base, "events": [], "positions": [], "terminal_rf_state": "off", "total_positions": 0}

    text = str(result["normalized_input"])
    positions: list[dict[str, object]] = []

    def append_cell(origin: str, character_index: int | None, codepoint: int) -> None:
        glyph = glyphs[codepoint]
        columns = glyph["columns_left_to_right_positions_bottom_to_top"]
        for column_index, column in enumerate(columns):
            for position_in_column, bit in enumerate(column):
                index = len(positions)
                positions.append({
                    "character_index": character_index,
                    "column_index": column_index,
                    "glyph": f"U+{codepoint:04X}" if character_index is not None else None,
                    "index": index,
                    "offset": rational(index),
                    "offset_nanoseconds": boundary_ns(index),
                    "origin": origin,
                    "position_in_column": position_in_column,
                    "state": int(bit),
                })

    append_cell("leader", None, 0x20)
    for character_index, character in enumerate(text):
        append_cell("word-space" if character == " " else "glyph", character_index, ord(character))
    append_cell("trailer", None, 0x20)
    events = compress_events(positions)
    expanded = "".join(str(position["state"]) for position in positions)
    expanded_events = "".join(
        ("1" if event["rf_state"] == "on" else "0") * int(event["position_count"]) for event in events
    )
    if expanded != expanded_events:
        raise AssertionError(f"event expansion mismatch for {identifier}")
    total = len(positions)
    position_records = [
        "\t".join(str(value) if value is not None else "" for value in (
            position["index"], position["state"], position["column_index"], position["position_in_column"],
            position["origin"], position["character_index"], position["glyph"],
            position["offset"]["numerator"], position["offset"]["denominator"], position["offset_nanoseconds"],
        ))
        for position in positions
    ]
    repeat = None
    if repeat_positions is not None:
        repeat = {
            "accepted": total <= repeat_positions,
            "interval": rational(repeat_positions),
            "interval_positions": repeat_positions,
        }
    return {
        **base,
        "duration": rational(total),
        "duration_nanoseconds": boundary_ns(total),
        "events": events,
        "glyph_sequence": [f"U+{ord(character):04X}" for character in text],
        "positions": position_records,
        "repeat_policy": repeat,
        "terminal_rf_state": "off",
        "total_positions": total,
    }


def common() -> dict[str, object]:
    return {
        "asset_id": ASSET_ID,
        "asset_sha256": ASSET_SHA256,
        "generator_revision": "the containing WsprryPi Git commit",
        "normalization_policy_id": NORMALIZATION_ID,
        "positions_per_second": RATE,
        "profile_id": PROFILE_ID,
        "schema_id": SCHEMA_ID,
        "spacing_policy_id": SPACING_ID,
        "substitution_policy_id": SUBSTITUTION_ID,
        "time_conversion": "round_half_up(n * 1000000000 / 245) using integer arithmetic",
    }


def generate(output: Path) -> None:
    font = load_font()
    glyphs = {
        int(key, 16): expand_glyph(int(key, 16), value)
        for key, value in font["glyphs"].items()
    }
    metadata = common()
    output.mkdir(parents=True, exist_ok=True)

    lowercase = [normalize(chr(code).encode("ascii")) for code in range(ord("a"), ord("z") + 1)]
    positive_stored = [normalize(chr(code).encode("ascii")) for code in range(0x20, 0x60)]
    negative_raw = [
        ("empty", b""), ("invalid-utf8", b"\xff"), ("tab", b"\t"), ("cr", b"\r"),
        ("lf", b"\n"), ("nul", b"\x00"), ("backtick", b"`"), ("left-brace", b"{"),
        ("vertical-bar", b"|"), ("right-brace", b"}"), ("tilde", b"~"),
        ("non-ascii", "é".encode("utf-8")), ("atomic-prefix", "ABCé".encode("utf-8")),
    ]
    input_cases = {
        **metadata,
        "fixture_class": "input-cases",
        "lowercase_normalization": lowercase,
        "negative": [{"fixture_id": name, **normalize(raw)} for name, raw in negative_raw],
        "stored_repertoire_positive": positive_stored,
    }

    complete_repertoire = "".join(chr(code) for code in range(0x20, 0x60))
    messages = [
        build_message("single-A-repeat-equal", b"A", glyphs, 294),
        build_message("repeat-too-short", b"AB", glyphs, 294),
        build_message("lowercase-wspry", b"wspry", glyphs),
        build_message("multiple-spaces", b"A  B", glyphs),
        build_message("leading-trailing-spaces", b" A ", glyphs),
        build_message("interoperability-corpus", b"HELL TEST 0123456789 DE WSPRY WSPRY 73", glyphs),
        build_message("complete-repertoire", complete_repertoire.encode("ascii"), glyphs),
        build_message("boundary-focus", b"12? /+-.,I", glyphs),
        build_message("atomic-rejection", "ABCé".encode("utf-8"), glyphs),
    ]

    mechanics_patterns = {
        "all-off": "0" * 28,
        "all-on-bracketed": "0" * 14 + "1" * 28 + "0" * 14,
        "single-position-alternation-synthetic": "01" * 28,
        "paired-alternation": "0011" * 14,
        "column-boundary-transition": "0" * 13 + "1" + "1" + "0" * 13,
        "character-boundary-transition-synthetic": "0" * 98 + "1" + "0" * 97,
        "character-boundary-merge-synthetic": "0" * 196,
    }
    mechanics = {
        **metadata,
        "fixture_class": "protocol-mechanics",
        "patterns": [
            {
                "fixture_id": name,
                "production_glyph": False,
                "states": states,
                "total_positions": len(states),
                "duration": rational(len(states)),
            }
            for name, states in mechanics_patterns.items()
        ],
    }

    single = next(message for message in messages if message["fixture_id"] == "single-A-repeat-equal")
    corpus = next(message for message in messages if message["fixture_id"] == "interoperability-corpus")
    corpus_samples = sorted({0, 98, 196, 294, 3920, *(index * 98 for index in range(0, 41, 5))})
    cancellation = {
        **metadata,
        "fixture_class": "cancellation",
        "exhaustive_small": {
            "fixture_id": single["fixture_id"],
            "checkpoints": [
                {"boundary": boundary, "expected_rf_state": "off"}
                for boundary in range(int(single["total_positions"]) + 1)
            ],
        },
        "large_rule": {
            "fixture_id": corpus["fixture_id"],
            "all_boundaries_inclusive": [0, int(corpus["total_positions"])],
            "automated_exhaustive_count": int(corpus["total_positions"]) + 1,
            "expected_rf_state": "off",
            "representative_checkpoints": corpus_samples,
        },
        "latency_claim": None,
    }

    safety = {
        **metadata,
        "fixture_class": "safe-terminal-and-fault",
        "cases": [
            {"fixture_id": "valid-completion", "plan_committed": True, "partial_plan_exposed": False,
             "expected_initial_rf_state": "off", "expected_terminal_rf_state": "off"},
            {"fixture_id": "atomic-input-rejection", "plan_committed": False, "partial_plan_exposed": False,
             "expected_initial_rf_state": "off", "expected_terminal_rf_state": "off"},
            {"fixture_id": "asset-checksum-failure", "plan_committed": False, "partial_plan_exposed": False,
             "expected_initial_rf_state": "off", "expected_terminal_rf_state": "off"},
            {"fixture_id": "compilation-failure", "plan_committed": False, "partial_plan_exposed": False,
             "expected_initial_rf_state": "off", "expected_terminal_rf_state": "off"},
            {"fixture_id": "cancellation-at-any-position-boundary", "plan_committed": True,
             "partial_plan_exposed": False, "expected_initial_rf_state": "off", "expected_terminal_rf_state": "off"},
            {"fixture_id": "repeat-boundary", "plan_committed": True, "partial_plan_exposed": False,
             "expected_initial_rf_state": "off", "expected_terminal_rf_state": "off",
             "next_repeat_begins_with": "leader"},
        ],
        "backend_latency_claim": None,
    }

    files: dict[str, object] = {
        "glyphs.json": {**metadata, "fixture_class": "glyphs", "glyphs": list(glyphs.values())},
        "input-cases.json": input_cases,
        "messages.json": {
            **metadata,
            "fixture_class": "messages",
            "position_field_order": [
                "index", "state", "column_index", "position_in_column", "origin", "character_index",
                "glyph", "offset_numerator", "offset_denominator", "offset_nanoseconds",
            ],
            "position_record_encoding": "tab-separated strings in position_field_order; empty field means null",
            "messages": messages,
        },
        "mechanics.json": mechanics,
        "cancellation.json": cancellation,
        "safety.json": safety,
    }
    for name, value in files.items():
        (output / name).write_bytes(canonical_bytes(value))

    checksums = {name: sha256(output / name) for name in sorted(files)}
    manifest = {
        **metadata,
        "fixture_set_id": "standard-feld-exact-asset-v1",
        "files": checksums,
        "reproducibility": "canonical UTF-8 JSON, sorted object keys, ordered arrays, LF, one trailing LF, no timestamps",
        "source_asset_path": str(FONT_PATH.relative_to(ROOT)),
        "terminal_rf_state": "off",
    }
    (output / "manifest.json").write_bytes(canonical_bytes(manifest))
    all_names = sorted([*files, "manifest.json"])
    checksum_lines = [f"{sha256(output / name)}  {name}" for name in all_names]
    (output / "SHA256SUMS").write_text("\n".join(checksum_lines) + "\n", encoding="utf-8", newline="\n")


def check(output: Path) -> None:
    if not output.is_dir():
        raise SystemExit(f"fixture directory does not exist: {output}")
    with tempfile.TemporaryDirectory(prefix="standard-feld-fixtures-") as temporary:
        regenerated = Path(temporary)
        generate(regenerated)
        expected = sorted(path.name for path in output.iterdir() if path.is_file())
        actual = sorted(path.name for path in regenerated.iterdir() if path.is_file())
        if expected != actual:
            raise SystemExit(f"fixture file list differs: {expected} != {actual}")
        differences = [name for name in expected if (output / name).read_bytes() != (regenerated / name).read_bytes()]
        if differences:
            raise SystemExit(f"fixture bytes differ: {differences}")
    print(f"PASS: {len(expected)} deterministic fixture files match regeneration")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        check(args.output)
        return
    if args.output.exists():
        allowed = {
            "SHA256SUMS", "cancellation.json", "glyphs.json", "input-cases.json", "manifest.json",
            "mechanics.json", "messages.json", "safety.json",
        }
        children = list(args.output.iterdir())
        unexpected = [child for child in children if not child.is_file() or child.name not in allowed]
        if unexpected:
            raise SystemExit(f"refusing to replace output containing unexpected entries: {unexpected}")
        for child in children:
            child.unlink()
    generate(args.output)
    print(f"Generated fixtures in {args.output}")


if __name__ == "__main__":
    main()
