#!/usr/bin/env python3
"""Regression coverage for compile-time backend capability generation."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "generate_backend_capabilities.py"
DEFAULT = "rpi-gpio,rp1-gpclk,si5351,simulated"


def run(*arguments: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    effective = list(arguments)
    if "--ancillary-gpio" not in effective:
        effective[0:0] = ["--ancillary-gpio", "1"]
    result = subprocess.run(
        ["python3", str(GENERATOR), *effective], capture_output=True, text=True
    )
    if result.returncode != expect:
        raise AssertionError(
            f"generator returned {result.returncode}, expected {expect}; "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    return result


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="wsprrypi-backends-") as temporary:
        output = Path(temporary) / "backend_capabilities.hpp"
        run("--backends", DEFAULT, "--output", str(output))
        content = output.read_text(encoding="utf-8")
        assert '#define WSPRRYPI_COMPILED_BACKENDS "' + DEFAULT + '"' in content
        assert "#define WSPRRYPI_ANCILLARY_GPIO 1" in content
        assert all(f"#define {macro} 1" in content for macro in (
            "WSPRRYPI_BACKEND_RPI_GPIO", "WSPRRYPI_BACKEND_RP1_GPCLK",
            "WSPRRYPI_BACKEND_SI5351", "WSPRRYPI_BACKEND_SIMULATED",
        ))
        stat = output.stat()
        unchanged = run("--backends", DEFAULT, "--output", str(output))
        assert "unchanged" in unchanged.stdout
        assert output.stat().st_mtime_ns == stat.st_mtime_ns
        run("--backends", DEFAULT, "--output", str(output), "--check")

        run("--backends", "simulated,si5351", "--output", str(output))
        subset = output.read_text(encoding="utf-8")
        assert '#define WSPRRYPI_COMPILED_BACKENDS "si5351,simulated"' in subset
        assert "#define WSPRRYPI_BACKEND_RPI_GPIO 0" in subset
        assert "#define WSPRRYPI_BACKEND_RP1_GPCLK 0" in subset
        assert "#define WSPRRYPI_BACKEND_SI5351 1" in subset
        assert "#define WSPRRYPI_BACKEND_SIMULATED 1" in subset
        run("--backends", "si5351", "--output", str(output))
        run(
            "--backends", "si5351", "--ancillary-gpio", "0",
            "--output", str(output),
        )
        assert "#define WSPRRYPI_ANCILLARY_GPIO 0" in output.read_text()
        assert run(
            "--backends", "simulated", "--output", str(output), "--check", expect=3
        ).stdout.startswith("backend capabilities stale")

        for invalid, fragment in (
            ("", "at least one backend"),
            ("si5351,", "empty entry"),
            ("si5351,si5351", "duplicate backend"),
            ("si5351,magic", "unknown backend"),
        ):
            failed = run("--backends", invalid, "--output", str(output), expect=1)
            assert fragment in failed.stderr, failed.stderr

        failed = run(
            "--backends", "si5351", "--ancillary-gpio", "2",
            "--output", str(output), expect=2,
        )
        assert "invalid choice" in failed.stderr

    print("backend capability regression tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
