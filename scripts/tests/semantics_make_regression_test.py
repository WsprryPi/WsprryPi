#!/usr/bin/env python3
"""Regression coverage for full and portable semantics Make dispatch."""

from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
AGENT_INSTRUCTIONS = ROOT / "AGENTS.md"


def run(*arguments: str, expect: int) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        ("make", *arguments), cwd=SRC, capture_output=True, text=True
    )
    if result.returncode != expect:
        raise AssertionError(
            f"make {' '.join(arguments)} returned {result.returncode}, expected {expect}; "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    return result


def combined(result: subprocess.CompletedProcess[str]) -> str:
    return result.stdout + result.stderr


def main() -> int:
    agent_instructions = AGENT_INSTRUCTIONS.read_text(encoding="utf-8")
    assert (
        "WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO="
        in agent_instructions
    )
    assert "make semantics-test-portable SUDO=" in agent_instructions
    assert "explicit subset, not an automatic fallback" in agent_instructions
    assert "not evidence that the full suite passed" in agent_instructions

    help_result = run("help", expect=0)
    assert (
        "semantics-test          Run the full Linux semantics suite "
        "(requires gpiod.hpp)" in help_result.stdout
    )
    assert (
        "semantics-test-portable Run the simulated-only portable semantics subset"
        in help_result.stdout
    )

    unsupported = run(
        "semantics-test",
        "HOST_OS=Darwin",
        "GPIOD_CPP_HEADER_AVAILABLE=1",
        expect=2,
    )
    unsupported_output = combined(unsupported)
    assert "Full semantics requires Linux" in unsupported_output
    assert "make semantics-test-portable" in unsupported_output
    assert "semantics-test-full" not in unsupported_output

    missing_header = run(
        "semantics-test",
        "HOST_OS=Linux",
        "GPIOD_CPP_HEADER_AVAILABLE=0",
        expect=2,
    )
    missing_header_output = combined(missing_header)
    assert "Full semantics requires gpiod.hpp" in missing_header_output
    assert "make semantics-test-portable" in missing_header_output
    assert "semantics-test-full" not in missing_header_output

    full_dispatch = run(
        "semantics-test",
        "BACKENDS=simulated",
        "ANCILLARY_GPIO=0",
        "HOST_OS=Linux",
        "GPIOD_CPP_HEADER_AVAILABLE=1",
        "MAKE=/bin/echo",
        expect=0,
    )
    assert "--no-print-directory semantics-test-full" in full_dispatch.stdout
    assert (
        "BACKENDS=rpi-gpio,rp1-gpclk,si5351,simulated ANCILLARY_GPIO=1"
        in full_dispatch.stdout
    )

    portable_dispatch = run(
        "semantics-test-portable",
        "BACKENDS=rpi-gpio",
        "ANCILLARY_GPIO=1",
        "MAKE=/bin/echo",
        expect=0,
    )
    assert "BACKENDS=simulated ANCILLARY_GPIO=0" in portable_dispatch.stdout
    assert "semantics-test-portable-profile" in portable_dispatch.stdout

    print("semantics Make dispatch regression tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
