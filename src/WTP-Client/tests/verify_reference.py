#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only verification before optional reference endpoint compilation."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2 or not sys.argv[1]:
    raise SystemExit("Set PICO_SOURCE to the explicit pinned WsprryPico checkout")
source = Path(sys.argv[1]).resolve()
pin = json.loads((root / "PROVENANCE.json").read_text())["revision"]


def git(*args):
    return subprocess.check_output(["git", "-C", str(source), *args])


if git("rev-parse", "HEAD").decode().strip() != pin:
    raise SystemExit("Reference checkout HEAD does not match PROVENANCE.json")
paths = git("ls-tree", "-r", "--name-only", pin, "src/wtp").decode().splitlines()
if not paths:
    raise SystemExit("Missing reference WTP sources")
for path in paths:
    if (source / path).read_bytes() != git("show", f"{pin}:{path}"):
        raise SystemExit(f"Reference source differs from pin: {path}")
print(f"Read-only reference verification passed: {pin}, {len(paths)} source files")
