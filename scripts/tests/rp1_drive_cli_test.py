#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the normal Pi option parser in its simulated-backend fixture."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from rp1_drive_selection import CLI_OPTIONS, cli_choices


def main(binary):
    binary = Path(binary).resolve(strict=True)
    choices = cli_choices((ROOT / CLI_OPTIONS).read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="wsprrypi-drive-cli-") as directory:
        environment = {**os.environ, "HOME": directory, "WSPRRYPI_DISABLE_HARDWARE_ACCESS": "1"}
        values = [str(value) for value in sorted(set(range(17)) | choices)] + ["-2", "4x", ""]
        for value in values:
            result = subprocess.run([binary, "--rp1-drive-cli-fixture", value],
                                    cwd=directory, env=environment, capture_output=True,
                                    text=True, timeout=10)
            if value in {str(drive) for drive in choices}:
                assert result.returncode == 0 and f"RP1 drive parsed: {value}" in result.stdout.splitlines(), (
                    value, result.stdout, result.stderr)
            else:
                assert result.returncode == 1 and "RP1 drive parsed:" not in result.stdout, (
                    value, result.returncode, result.stdout, result.stderr)
    print("Production CLI RP1 drive option: PASS (represented values and invalid inputs)")


if __name__ == "__main__":
    main(sys.argv[1])
