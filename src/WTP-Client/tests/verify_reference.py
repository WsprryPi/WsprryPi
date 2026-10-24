#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate selected source availability; Git identity is informational."""
from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 2 or not sys.argv[1]:
    raise SystemExit("Set PICO_SOURCE to the selected WsprryPico source directory")
source = Path(sys.argv[1]).resolve()
required = ["endpoint", "job_service", "codec", "json", "frame_parser", "sha256"]
for name in required:
    if not (source / "src/wtp" / (name + ".cpp")).is_file():
        raise SystemExit(f"Missing required Pico source: {name}.cpp in {source}")
print(f"Pico source: {source}", flush=True)
for args in [("rev-parse", "HEAD"), ("status", "--short")]:
    subprocess.run(["git", "-C", str(source), *args], check=False)
