#!/usr/bin/env python3
"""Reproduce the Step 5 source-level Hellschreiber comparisons.

This utility never opens an audio or radio device. It parses immutable upstream
source trees supplied by the caller and writes only derived aggregate results.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import re
import wave
from pathlib import Path


CORPUS_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-?/"
FOCUS = "-/13679?IKPQU"
SAMPLE_RATE = 48000
CENTER_HZ = 1500.0


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_fldigi(path: Path) -> dict[str, list[list[int]]]:
    text = path.read_text(encoding="utf-8")
    result: dict[str, list[list[int]]] = {}
    pattern = re.compile(
        r"\{\s*(?:'((?:\\.|[^'])*)'|(\d+))\s*,\s*\{([^}]*)\}\s*,?\s*\}",
        re.S,
    )
    for quoted, numeric, body in pattern.findall(text):
        if numeric:
            char = chr(int(numeric))
        else:
            char = bytes(quoted, "utf-8").decode("unicode_escape")
        rows = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)", body)]
        if len(rows) != 14:
            continue
        width = max((16 - (row & -row).bit_length() + 1 for row in rows if row), default=0)
        width = max((row.bit_length() for row in rows), default=0)
        # fldigi stores columns in bits 15..0. Retain through the last occupied column.
        occupied = 0
        for row in rows:
            for col in range(16):
                if row & (1 << (15 - col)):
                    occupied = max(occupied, col + 1)
        matrix = []
        for col in range(occupied):
            matrix.append([1 if rows[13 - row] & (1 << (15 - col)) else 0 for row in range(14)])
        result[char] = matrix
    return result


def parse_bdf(path: Path) -> dict[str, list[list[int]]]:
    lines = path.read_text(encoding="latin-1").splitlines()
    result: dict[str, list[list[int]]] = {}
    fbb_line = next(line for line in lines if line.startswith("FONTBOUNDINGBOX "))
    _, fbb_w, fbb_h, fbb_x, fbb_y = fbb_line.split()
    fbb_h_i, fbb_y_i = int(fbb_h), int(fbb_y)
    i = 0
    while i < len(lines):
        if not lines[i].startswith("STARTCHAR "):
            i += 1
            continue
        encoding = None
        dwidth = None
        bbx = None
        bitmap: list[str] = []
        i += 1
        while i < len(lines) and lines[i] != "ENDCHAR":
            if lines[i].startswith("ENCODING "):
                encoding = int(lines[i].split()[1])
            elif lines[i].startswith("DWIDTH "):
                dwidth = int(lines[i].split()[1]) + 1  # xfhell Load_Fonts behavior
            elif lines[i].startswith("BBX "):
                parts = lines[i].split()
                bbx = tuple(map(int, parts[1:5]))
            elif lines[i] == "BITMAP":
                i += 1
                while i < len(lines) and lines[i] != "ENDCHAR":
                    bitmap.append(lines[i].strip())
                    i += 1
                break
            i += 1
        if encoding is not None and dwidth and bbx and 0 <= encoding <= 0x10FFFF:
            width, height, xoff, yoff = bbx
            rows_top = [int(value, 16) for value in bitmap]
            total_bits = max(8, max((len(value) * 4 for value in bitmap), default=8))
            rows_top = [value << max(0, 32 - total_bits) for value in rows_top]
            bmap_idx = fbb_h_i - height - yoff + fbb_y_i
            rows_top = [0] * bmap_idx + rows_top
            rows_top += [0] * (fbb_h_i - len(rows_top))
            # xfhell scans font rows bottom-to-top and columns left-to-right.
            matrix = []
            for col in range(dwidth):
                matrix.append([1 if row & (1 << (31 - col)) else 0 for row in reversed(rows_top)])
            result[chr(encoding)] = matrix
        i += 1
    return result


def parse_radiolib(path: Path) -> dict[str, list[list[int]]]:
    text = path.read_text(encoding="utf-8")
    result: dict[str, list[list[int]]] = {}
    pattern = re.compile(r"\{\s*([^}]+)\}\s*,?\s*//\s*(.+)")
    for body, comment in pattern.findall(text):
        values = [int(bits, 2) for bits in re.findall(r"0b([01]+)", body)]
        if len(values) != 5:
            continue
        label = comment.strip()
        if label == "space":
            char = " "
        elif label == "backslash":
            char = "\\"
        else:
            char = label[0]
        rows_top = [0] + values + [0]
        result[char] = [[1 if rows_top[6 - row] & (1 << (6 - col)) else 0 for row in range(7)] for col in range(7)]
    return result


def parse_historical(path: Path) -> dict[str, list[list[int]]]:
    result: dict[str, list[list[int]]] = {}
    for line in path.read_text(encoding="latin-1").splitlines():
        match = re.match(r"(.+?) = ((?:[01]{14}-){6}[01]{14})", line)
        if match and len(match.group(1)) == 1:
            result[match.group(1)] = [[int(bit) for bit in column] for column in match.group(2).split("-")]
    return result


def pad_height(matrix: list[list[int]], height: int) -> list[list[int]]:
    return [column[:height] + [0] * max(0, height - len(column)) for column in matrix]


def fldigi_tx(matrix: list[list[int]]) -> list[list[int]]:
    height = len(matrix[0]) if matrix else 14
    return [[0] * height] + matrix + [[0] * height]


def trim(matrix: list[list[int]]) -> list[list[int]]:
    value = [column[:] for column in matrix]
    while value and not any(value[0]):
        value.pop(0)
    while value and not any(value[-1]):
        value.pop()
    if not value:
        return []
    bottom = 0
    top = max(len(column) for column in value)
    while bottom < top and not any(bottom < len(column) and column[bottom] for column in value):
        bottom += 1
    while top > bottom and not any(top - 1 < len(column) and column[top - 1] for column in value):
        top -= 1
    return [column[bottom:top] for column in value]


def expand_7_to_14(matrix: list[list[int]]) -> list[list[int]]:
    return [[bit for row in column for bit in (row, row)] for column in matrix]


def distance(a: list[list[int]], b: list[list[int]]) -> tuple[int, int]:
    width = max(len(a), len(b))
    height = max(max((len(c) for c in a), default=0), max((len(c) for c in b), default=0))
    mismatches = 0
    for x in range(width):
        for y in range(height):
            av = a[x][y] if x < len(a) and y < len(a[x]) else 0
            bv = b[x][y] if x < len(b) and y < len(b[x]) else 0
            mismatches += av != bv
    return mismatches, width * height


def write_pbm(path: Path, matrices: list[list[list[int]]], scale: int = 2) -> None:
    height = max((len(col) for matrix in matrices for col in matrix), default=1)
    width = sum(len(matrix) + 1 for matrix in matrices)
    pixels = [[0] * width for _ in range(height)]
    xoff = 0
    for matrix in matrices:
        for x, column in enumerate(matrix):
            for y, bit in enumerate(column):
                if y < height:
                    pixels[height - 1 - y][xoff + x] = bit
        xoff += len(matrix) + 1
    with path.open("w", encoding="ascii") as stream:
        stream.write(f"P1\n{width * scale} {height * scale}\n")
        for row in pixels:
            expanded = [str(bit) for bit in row for _ in range(scale)]
            for _ in range(scale):
                stream.write(" ".join(expanded) + "\n")


def generate_fsk_wav(path: Path, bits: list[int], symbol_rate: float, shift: float) -> dict[str, float | int | str]:
    phase = 0.0
    samples: list[int] = []
    accumulator = 0.0
    for bit in bits:
        accumulator += SAMPLE_RATE / symbol_rate
        count = round(accumulator) - len(samples)
        tone = CENTER_HZ + (-shift / 2.0 if bit else shift / 2.0)
        for _ in range(count):
            samples.append(round(0.8 * 32767 * math.sin(phase)))
            phase = (phase + 2 * math.pi * tone / SAMPLE_RATE) % (2 * math.pi)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SAMPLE_RATE)
        out.writeframes(b"".join(value.to_bytes(2, "little", signed=True) for value in samples))
    return {
        "file": path.name,
        "sha256": sha256(path),
        "sample_rate_hz": SAMPLE_RATE,
        "center_hz": CENTER_HZ,
        "shift_hz": shift,
        "symbol_rate_per_s": symbol_rate,
        "samples": len(samples),
        "duration_s": len(samples) / SAMPLE_RATE,
    }


def synth_fsk(bits: list[int], symbol_rate: float, shift: float, offset: float = 0.0, snr_db: float | None = None) -> list[float]:
    phase = 0.0
    samples: list[float] = []
    accumulator = 0.0
    for bit in bits:
        accumulator += SAMPLE_RATE / symbol_rate
        target = round(accumulator)
        count = max(1, target - len(samples))
        tone = CENTER_HZ + offset + (-shift / 2.0 if bit else shift / 2.0)
        for _ in range(count):
            samples.append(0.8 * math.sin(phase))
            phase = (phase + 2 * math.pi * tone / SAMPLE_RATE) % (2 * math.pi)
    if snr_db is not None:
        rng = random.Random(105)
        signal_rms = math.sqrt(sum(value * value for value in samples) / len(samples))
        noise_rms = signal_rms / (10 ** (snr_db / 20.0))
        samples = [value + rng.gauss(0.0, noise_rms) for value in samples]
    return samples


def tone_energy(values: list[float], frequency: float) -> float:
    omega = 2 * math.pi * frequency / SAMPLE_RATE
    real = sum(value * math.cos(omega * index) for index, value in enumerate(values))
    imag = sum(value * math.sin(omega * index) for index, value in enumerate(values))
    return real * real + imag * imag


def demod_fsk(samples: list[float], rx_rate: float, rx_shift: float, reverse: bool = False) -> list[int]:
    decoded: list[int] = []
    position = 0.0
    while round(position + SAMPLE_RATE / rx_rate) <= len(samples):
        end = round(position + SAMPLE_RATE / rx_rate)
        start = round(position)
        window = samples[start:end]
        low = tone_energy(window, CENTER_HZ - rx_shift / 2.0)
        high = tone_energy(window, CENTER_HZ + rx_shift / 2.0)
        bit = 1 if low > high else 0
        decoded.append(1 - bit if reverse else bit)
        position += SAMPLE_RATE / rx_rate
    return decoded


def expected_at_rx(bits: list[int], tx_rate: float, rx_rate: float, count: int) -> list[int]:
    expected = []
    for index in range(count):
        time_center = (index + 0.5) / rx_rate
        tx_index = min(len(bits) - 1, int(time_center * tx_rate))
        expected.append(bits[tx_index])
    return expected


def flatten(chars: str, font: dict[str, list[list[int]]], height: int) -> list[int]:
    bits: list[int] = []
    for char in chars:
        matrix = font[char]
        for column in matrix:
            bits.extend(column[:height])
    return bits


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fldigi", type=Path, required=True)
    parser.add_argument("--xfhell", type=Path, required=True)
    parser.add_argument("--radiolib", type=Path, required=True)
    parser.add_argument("--historical", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    out = args.output
    (out / "measurements").mkdir(parents=True, exist_ok=True)
    (out / "rasters").mkdir(parents=True, exist_ok=True)
    (out / "audio").mkdir(parents=True, exist_ok=True)

    fldigi_real_path = args.fldigi / "src/feld/FeldReal-14.cxx"
    fldigi_7_path = args.fldigi / "src/feld/Feld7x7-14.cxx"
    xfhell_fm_path = args.xfhell / "xfhell/fonts/12pt/FMFatLoEn.bdf"
    xfhell_real_path = args.xfhell / "xfhell/fonts/14pt/other/FeldRealEn.bdf"
    radiolib_path = args.radiolib / "src/protocols/Hellschreiber/Hellschreiber.cpp"

    fldigi_real = parse_fldigi(fldigi_real_path)
    fldigi_7 = parse_fldigi(fldigi_7_path)
    xfhell_fm = parse_bdf(xfhell_fm_path)
    xfhell_real = parse_bdf(xfhell_real_path)
    radiolib = parse_radiolib(radiolib_path)
    historical = parse_historical(args.historical) if args.historical else {}

    rows = []
    comparisons = [
        ("fldigi-real-vs-xfhell-real", fldigi_real, xfhell_real, False),
        ("fldigi-7x7-vs-radiolib", fldigi_7, radiolib, True),
    ]
    if historical:
        comparisons.append(("fldigi-real-vs-historical-drum", fldigi_real, historical, False))
    for comparison, left, right, expand_right in comparisons:
        for char in CORPUS_CHARS:
            if char not in left or char not in right:
                continue
            a = fldigi_tx(left[char])
            b = expand_7_to_14(right[char]) if expand_right else right[char]
            for layer, aa, bb in [("transmitted-cell", a, b), ("trimmed-glyph", trim(a), trim(b))]:
                mismatches, cells = distance(aa, bb)
                rows.append({
                    "comparison": comparison,
                    "layer": layer,
                    "character": char,
                    "left_width": len(aa),
                    "right_width": len(bb),
                    "mismatched_cells": mismatches,
                    "cells": cells,
                    "exact": mismatches == 0 and len(aa) == len(bb),
                })
    with (out / "measurements/raster-comparison.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    for name, font, height in [
        ("fldigi-real", fldigi_real, 14),
        ("xfhell-real", xfhell_real, 14),
        ("xfhell-fm105", xfhell_fm, 12),
        ("fldigi-7x7", fldigi_7, 14),
        ("radiolib", radiolib, 7),
    ]:
        matrices = [pad_height(font[c], height) for c in FOCUS if c in font]
        if name.startswith("fldigi"):
            matrices = [fldigi_tx(matrix) for matrix in matrices]
        write_pbm(out / f"rasters/{name}-focus.pbm", matrices)

    test = "CQ TEST"
    fldigi_bits = flatten(test, fldigi_7, 14)
    xfhell_bits = flatten(test, xfhell_fm, 12)
    audio = [
        generate_fsk_wav(out / "audio/fldigi-fskh105-source-contract.wav", fldigi_bits, 245.0, 55.0),
        generate_fsk_wav(out / "audio/xfhell-fmhell105-source-contract.wav", xfhell_bits, 210.0, 210.0),
    ]

    impairment_rows = []
    alternating = [index & 1 for index in range(280)]
    contracts = {
        "fldigi-FSKH105": (245.0, 55.0),
        "xfhell-FMHell105": (210.0, 210.0),
    }
    scenarios = [
        ("clean", 0.0, None, False),
        ("reverse-uncompensated", 0.0, None, True),
        ("offset-plus-10Hz", 10.0, None, False),
        ("offset-plus-25Hz", 25.0, None, False),
        ("offset-plus-50Hz", 50.0, None, False),
        ("awgn-0dB", 0.0, 0.0, False),
        ("awgn-minus-6dB", 0.0, -6.0, False),
    ]
    for tx_name, (tx_rate, tx_shift) in contracts.items():
        for rx_name, (rx_rate, rx_shift) in contracts.items():
            for scenario, offset, snr, invert_tx in scenarios:
                tx_bits = [1 - bit for bit in alternating] if invert_tx else alternating
                samples = synth_fsk(tx_bits, tx_rate, tx_shift, offset, snr)
                decoded = demod_fsk(samples, rx_rate, rx_shift)
                expected = expected_at_rx(alternating, tx_rate, rx_rate, len(decoded))
                errors = sum(a != b for a, b in zip(decoded, expected))
                impairment_rows.append({
                    "transmitter_contract": tx_name,
                    "receiver_contract": rx_name,
                    "scenario": scenario,
                    "decisions": len(decoded),
                    "errors": errors,
                    "bit_error_rate": f"{errors / len(decoded):.6f}" if decoded else "",
                    "scope": "source-derived tone decisions; not text or application decode",
                })
    with (out / "measurements/impairment-results.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=impairment_rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(impairment_rows)

    summary = {
        "method": "source-derived raster and continuous-phase FSK fixtures; not application execution",
        "test_text": test,
        "sources": {
            "fldigi_4_2_12_archive_sha256": "028bcb1c100cb790cad36324b8063c13594e160743f9378320ceabcf16dbc44a",
            "xfhell_3_5_2_archive_sha256": "7b16ecdaa425ebc19f7e555fe28473e0228ea344e0203062a4e408346e75b209",
            "radiolib_0795caa_archive_sha256": "6bf3c67958c8fe97d019560e11fb2b99231ff4b86c61aa7e461db640bf396150",
        },
        "track_a_contracts": {
            "fldigi_FSKH105": {"physical_symbol_rate_per_s": 245.0, "column_rate_per_s": 17.5, "rows_per_column": 14, "tone_shift_hz": 55.0, "fixture_font": "Feld7x7-14.cxx"},
            "xfhell_FMHell105": {"physical_symbol_rate_per_s": 210.0, "column_rate_per_s": 17.5, "rows_per_column": 12, "tone_shift_hz": 210.0, "nominal_logical_baud": 105.0, "fixture_font": "FMFatLoEn.bdf"},
        },
        "track_a_exact_contract_match": False,
        "track_a_mismatch": {"symbol_rate_ratio": 245.0 / 210.0, "tone_shift_ratio": 210.0 / 55.0, "row_count_difference": 2},
        "raster_summary": {},
        "audio": audio,
        "tone_decision_tests": {
            "pattern": "280 alternating binary symbols",
            "scenarios": [scenario[0] for scenario in scenarios],
            "result_file": "measurements/impairment-results.csv",
            "scope": "source-derived coherent tone-energy decisions only; no application receiver or human readability claim",
        },
    }
    for comparison, layer in {(row["comparison"], row["layer"]) for row in rows}:
        selected = [row for row in rows if row["comparison"] == comparison and row["layer"] == layer]
        summary["raster_summary"][f"{comparison}:{layer}"] = {
            "characters_compared": len(selected),
            "exact_characters": sum(row["exact"] for row in selected),
            "total_mismatched_cells": sum(row["mismatched_cells"] for row in selected),
            "total_cells": sum(row["cells"] for row in selected),
        }
    (out / "measurements/summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    manifest_files = sorted(path for path in out.rglob("*") if path.is_file() and path.name != "manifest.json")
    manifest = [{"path": str(path.relative_to(out)), "sha256": sha256(path), "bytes": path.stat().st_size} for path in manifest_files]
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
