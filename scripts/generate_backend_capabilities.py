#!/usr/bin/env python3
"""Generate the compile-time transmission-backend capability header."""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path


SUPPORTED = ("rpi-gpio", "rp1-gpclk", "si5351", "simulated")
MACROS = {
    "rpi-gpio": "WSPRRYPI_BACKEND_RPI_GPIO",
    "rp1-gpclk": "WSPRRYPI_BACKEND_RP1_GPCLK",
    "si5351": "WSPRRYPI_BACKEND_SI5351",
    "simulated": "WSPRRYPI_BACKEND_SIMULATED",
}


class CapabilityError(ValueError):
    """Raised when the requested backend set is invalid."""


def parse_backends(raw: str) -> tuple[str, ...]:
    if not raw.strip():
        raise CapabilityError("at least one backend must be selected")
    requested = [item.strip() for item in raw.split(",")]
    if any(not item for item in requested):
        raise CapabilityError("backend list contains an empty entry")
    duplicates = sorted({item for item in requested if requested.count(item) > 1})
    if duplicates:
        raise CapabilityError(f"duplicate backend(s): {', '.join(duplicates)}")
    unknown = sorted(set(requested) - set(SUPPORTED))
    if unknown:
        raise CapabilityError(
            f"unknown backend(s): {', '.join(unknown)}; supported: {', '.join(SUPPORTED)}"
        )
    return tuple(item for item in SUPPORTED if item in requested)


def header(backends: tuple[str, ...], ancillary_gpio: bool) -> str:
    lines = [
        "#pragma once",
        "",
    ]
    lines.extend(
        f"#define {MACROS[name]} {1 if name in backends else 0}"
        for name in SUPPORTED
    )
    lines.append(f'#define WSPRRYPI_COMPILED_BACKENDS "{",".join(backends)}"')
    lines.append(
        f"#define WSPRRYPI_ANCILLARY_GPIO {1 if ancillary_gpio else 0}"
    )
    return "\n".join(lines) + "\n"


def replace_if_changed(destination: Path, content: str) -> bool:
    destination.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    if destination.exists() and destination.read_bytes() == encoded:
        return False
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=destination.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as temporary:
            temporary.write(encoded)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, destination)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backends", required=True)
    parser.add_argument("--ancillary-gpio", required=True, choices=("0", "1"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--check", action="store_true",
        help="check generated content without writing (0=current, 3=stale)",
    )
    args = parser.parse_args()
    try:
        content = header(parse_backends(args.backends), args.ancillary_gpio == "1")
        if args.check:
            current = args.output.exists() and args.output.read_bytes() == content.encode()
            print(f"backend capabilities {'current' if current else 'stale'}: {args.output}")
            return 0 if current else 3
        changed = replace_if_changed(args.output, content)
    except (CapabilityError, OSError) as error:
        print(f"backend capability generation failed: {error}", file=sys.stderr)
        return 1
    print(f"backend capabilities {'updated' if changed else 'unchanged'}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
