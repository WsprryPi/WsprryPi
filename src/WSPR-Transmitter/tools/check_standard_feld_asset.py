#!/usr/bin/env python3
"""Verify or emit the immutable Standard Feld production table."""

import argparse
import hashlib
import json
import re
from pathlib import Path

EXPECTED_SHA256 = "025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227"
EXPECTED_ASSET_ID = "wsprry-standard-feld-radiolib-5x5-v1"


def canonical_rows(asset_path: Path) -> list[list[int]]:
    raw = asset_path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"canonical asset checksum mismatch: {digest}")

    asset = json.loads(raw)
    if asset["asset_id"] != EXPECTED_ASSET_ID:
        raise SystemExit("canonical asset identity mismatch")
    if list(asset["glyphs"]) != [f"{value:04X}" for value in range(0x20, 0x60)]:
        raise SystemExit("canonical asset repertoire/order mismatch")

    return [
        [int(row, 2) for row in glyph]
        for glyph in asset["glyphs"].values()
    ]


def header_rows(header_path: Path) -> list[list[int]]:
    text = header_path.read_text(encoding="utf-8")
    block = text.split("kStoredRows{{", 1)[1].split("}};", 1)[0]
    values = [
        [int(value) for value in match.split(",")]
        for match in re.findall(r"\{([0-9, ]+)\}", block)
    ]
    return values


def emit(rows: list[list[int]]) -> None:
    print("inline constexpr std::array<std::array<std::uint8_t, 5>, 64> kStoredRows{{")
    for index, row in enumerate(rows):
        suffix = "," if index + 1 < len(rows) else ""
        print("    {" + ", ".join(map(str, row)) + "}" + suffix)
    print("}};")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset", required=True, type=Path)
    parser.add_argument(
        "--header",
        default=Path(__file__).resolve().parents[1] / "src" / "standard_feld_asset.hpp",
        type=Path,
    )
    parser.add_argument("--emit", action="store_true")
    args = parser.parse_args()

    rows = canonical_rows(args.asset)
    if args.emit:
        emit(rows)
        return

    if header_rows(args.header) != rows:
        raise SystemExit("production table does not match canonical asset")
    print("PASS: immutable Standard Feld production table matches canonical asset")


if __name__ == "__main__":
    main()
