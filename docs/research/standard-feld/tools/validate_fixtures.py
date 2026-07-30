#!/usr/bin/env python3
"""Independently validate retained Standard Feld fixtures."""

from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path

RATE = 245
NS = 1_000_000_000
ASSET_SHA256 = "025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227"
ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = ROOT / "assets/font/font.json"
FIXTURE_DIR = ROOT / "fixtures/generated/v1"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def boundary_ns(index: int) -> int:
    numerator = index * NS
    return (2 * numerator + RATE) // (2 * RATE)


def reduced(value: int) -> dict[str, int]:
    divisor = math.gcd(value, RATE)
    return {"denominator": RATE // divisor, "numerator": value // divisor}


def load(name: str) -> dict[str, object]:
    return json.loads((FIXTURE_DIR / name).read_text(encoding="utf-8"))


def check_checksums() -> None:
    for line in (FIXTURE_DIR / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        assert sha256(FIXTURE_DIR / name) == digest, name


def reconstruct_glyphs() -> dict[str, str]:
    assert sha256(FONT_PATH) == ASSET_SHA256
    font = json.loads(FONT_PATH.read_text(encoding="utf-8"))
    assert list(font["glyphs"]) == [f"{value:04X}" for value in range(0x20, 0x60)]
    reconstructed: dict[str, str] = {}
    for key, rows in font["glyphs"].items():
        assert len(rows) == 5
        assert all(re.fullmatch(r"[01]{7}", row) for row in rows)
        assert all(row[0] == "0" and row[-1] == "0" for row in rows)
        display = ["0000000", *rows, "0000000"]
        columns = []
        for column in range(7):
            physical = "".join(display[row][column] * 2 for row in range(6, -1, -1))
            assert len(physical) == 14
            columns.append(physical)
        reconstructed[f"U+{key}"] = "".join(columns)
    return reconstructed


def validate_glyphs(reconstructed: dict[str, str]) -> None:
    retained = load("glyphs.json")["glyphs"]
    assert len(retained) == 64
    for glyph in retained:
        codepoint = glyph["codepoint"]
        flat = glyph["flattened_positions"]
        assert reconstructed[codepoint] == flat
        assert len(glyph["columns_left_to_right_positions_bottom_to_top"]) == 7
        assert all(len(column) == 14 for column in glyph["columns_left_to_right_positions_bottom_to_top"])
        assert flat[:14] == "0" * 14 and flat[-14:] == "0" * 14
        assert all(flat[index] == flat[index + 1] for index in range(0, 98, 2))
        assert glyph["duration"] == reduced(98)

    # Asymmetric orientation checks: slash and uppercase A must equal direct reconstruction.
    retained_by_codepoint = {glyph["codepoint"]: glyph for glyph in retained}
    assert retained_by_codepoint["U+002F"]["flattened_positions"] == reconstructed["U+002F"]
    assert retained_by_codepoint["U+0041"]["flattened_positions"] == reconstructed["U+0041"]


def validate_input_cases() -> None:
    cases = load("input-cases.json")
    assert len(cases["stored_repertoire_positive"]) == 64
    assert all(case["accepted"] for case in cases["stored_repertoire_positive"])
    lowercase = cases["lowercase_normalization"]
    assert len(lowercase) == 26
    for index, case in enumerate(lowercase):
        assert case["accepted"]
        assert case["normalized_input"] == chr(ord("A") + index)
        assert len(case["normalizations"]) == 1
        assert case["substitutions"] == []
    assert len(cases["negative"]) >= 13
    for case in cases["negative"]:
        assert not case["accepted"]
        assert not case["emitted_plan"]
        assert case["normalized_input"] is None
        assert case["substitutions"] == []


def expand_events(events: list[dict[str, object]]) -> str:
    result = []
    expected_start = 0
    for index, event in enumerate(events):
        start = event["start_position"]
        end = event["end_position_exclusive"]
        assert event["event_index"] == index
        assert start == expected_start and end > start
        assert event["start"] == reduced(start)
        assert event["duration"] == reduced(end - start)
        assert event["start_nanoseconds"] == boundary_ns(start)
        assert event["end_nanoseconds"] == boundary_ns(end)
        assert event["duration_nanoseconds"] == boundary_ns(end) - boundary_ns(start)
        assert event["carrier_reference"] == ("carrier" if event["rf_state"] == "on" else None)
        attribution_cursor = start
        for attribution in event["attribution_ranges"]:
            assert attribution["start_position"] == attribution_cursor
            assert attribution["end_position_exclusive"] > attribution_cursor
            assert attribution["origin"] in {"leader", "glyph", "word-space", "trailer"}
            attribution_cursor = attribution["end_position_exclusive"]
        assert attribution_cursor == end
        result.append(("1" if event["rf_state"] == "on" else "0") * event["position_count"])
        expected_start = end
    return "".join(result)


def validate_messages(reconstructed: dict[str, str]) -> None:
    messages = {message["fixture_id"]: message for message in load("messages.json")["messages"]}
    assert set(messages) == {
        "single-A-repeat-equal", "repeat-too-short", "lowercase-wspry", "multiple-spaces",
        "leading-trailing-spaces",
        "interoperability-corpus", "complete-repertoire", "boundary-focus", "atomic-rejection",
    }
    for message in messages.values():
        if not message["input"]["accepted"]:
            assert message["positions"] == [] and message["events"] == []
            assert message["terminal_rf_state"] == "off"
            continue
        normalized = message["input"]["normalized_input"]
        expected = reconstructed["U+0020"] + "".join(reconstructed[f"U+{ord(char):04X}"] for char in normalized) + reconstructed["U+0020"]
        records = [position.split("\t") for position in message["positions"]]
        actual = "".join(position[1] for position in records)
        assert actual == expected
        assert expand_events(message["events"]) == expected
        count = len(normalized)
        assert message["total_positions"] == 98 * (count + 2)
        assert message["duration"] == reduced(98 * (count + 2))
        assert message["duration_nanoseconds"] == boundary_ns(message["total_positions"])
        for index, position in enumerate(records):
            assert len(position) == 10
            assert int(position[0]) == index
            assert {"numerator": int(position[7]), "denominator": int(position[8])} == reduced(index)
            assert int(position[9]) == boundary_ns(index)
        assert records[0][4] == "leader"
        assert records[-1][4] == "trailer"
        assert message["events"][0]["rf_state"] == "off"
        assert message["events"][-1]["rf_state"] == "off"
        assert message["terminal_rf_state"] == "off"
        assert message["input"]["substitutions"] == []

    corpus = messages["interoperability-corpus"]
    assert corpus["input"]["normalized_input"] == "HELL TEST 0123456789 DE WSPRY WSPRY 73"
    assert len(corpus["input"]["normalized_input"]) == 38
    assert corpus["total_positions"] == 3920
    assert corpus["duration"] == {"denominator": 1, "numerator": 16}
    assert messages["single-A-repeat-equal"]["repeat_policy"]["accepted"]
    assert not messages["repeat-too-short"]["repeat_policy"]["accepted"]


def validate_mechanics() -> None:
    patterns = {item["fixture_id"]: item for item in load("mechanics.json")["patterns"]}
    assert patterns["all-off"]["states"] == "0" * 28
    assert "1" in patterns["all-on-bracketed"]["states"]
    assert patterns["single-position-alternation-synthetic"]["production_glyph"] is False
    assert patterns["single-position-alternation-synthetic"]["states"] == "01" * 28
    assert patterns["paired-alternation"]["states"] == "0011" * 14
    assert patterns["character-boundary-transition-synthetic"]["states"][98] == "1"
    assert patterns["character-boundary-merge-synthetic"]["states"] == "0" * 196


def validate_cancellation() -> None:
    cancellation = load("cancellation.json")
    small = cancellation["exhaustive_small"]
    assert [item["boundary"] for item in small["checkpoints"]] == list(range(295))
    assert all(item["expected_rf_state"] == "off" for item in small["checkpoints"])
    large = cancellation["large_rule"]
    assert large["all_boundaries_inclusive"] == [0, 3920]
    assert large["automated_exhaustive_count"] == 3921
    assert large["expected_rf_state"] == "off"
    assert sum(1 for _ in range(large["all_boundaries_inclusive"][0], large["all_boundaries_inclusive"][1] + 1)) == 3921
    assert cancellation["latency_claim"] is None


def validate_safety() -> None:
    safety = load("safety.json")
    cases = {case["fixture_id"]: case for case in safety["cases"]}
    assert set(cases) == {
        "valid-completion", "atomic-input-rejection", "asset-checksum-failure", "compilation-failure",
        "cancellation-at-any-position-boundary", "repeat-boundary",
    }
    for case in cases.values():
        assert case["partial_plan_exposed"] is False
        assert case["expected_initial_rf_state"] == "off"
        assert case["expected_terminal_rf_state"] == "off"
    assert cases["asset-checksum-failure"]["plan_committed"] is False
    assert cases["compilation-failure"]["plan_committed"] is False
    assert cases["repeat-boundary"]["next_repeat_begins_with"] == "leader"
    assert safety["backend_latency_claim"] is None


def main() -> None:
    check_checksums()
    reconstructed = reconstruct_glyphs()
    validate_glyphs(reconstructed)
    validate_input_cases()
    validate_messages(reconstructed)
    validate_mechanics()
    validate_cancellation()
    validate_safety()
    print("PASS: independent fixture validation completed")
    print("64 glyphs; 9 message cases; input rejection; event expansion; timing; repeat; cancellation; safe terminal state")


if __name__ == "__main__":
    main()
