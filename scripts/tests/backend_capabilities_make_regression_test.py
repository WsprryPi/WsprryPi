#!/usr/bin/env python3
"""Integration coverage for production Make backend-capability rules."""

from __future__ import annotations

import os
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "generate_backend_capabilities.py"
RULES = ROOT / "scripts" / "backend_capability_rules.mk"
PRODUCTION_MAKEFILE = ROOT / "src" / "Makefile"
DEFAULT = "rpi-gpio,rp1-gpclk,si5351,simulated"


def run(*arguments: str, cwd: Path, expect: int = 0) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    # Keep the temporary fixture independent from an invoking recursive make's
    # backend profile and jobserver state.
    environment.pop("MAKEFLAGS", None)
    environment.pop("MFLAGS", None)
    environment.pop("MAKELEVEL", None)
    environment.pop("BACKENDS", None)
    environment.pop("ANCILLARY_GPIO", None)
    result = subprocess.run(
        arguments,
        cwd=cwd,
        capture_output=True,
        text=True,
        env=environment,
    )
    if result.returncode != expect:
        raise AssertionError(
            f"{' '.join(arguments)} returned {result.returncode}, expected {expect}; "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    return result


def makefile() -> str:
    return (
        f"DEFAULT_BACKENDS := {DEFAULT}\n"
        "BACKENDS ?= $(DEFAULT_BACKENDS)\n"
        "ANCILLARY_GPIO ?= 1\n"
        "BACKEND_CAPABILITIES_HEADER := build/generated/backend_capabilities.hpp\n"
        f"BACKEND_CAPABILITIES_GENERATOR := {GENERATOR}\n"
        "BUILD_METADATA_INTROSPECTION :=\n"
        "Q := @\n"
        f"include {RULES}\n"
        ".DEFAULT_GOAL := release\n"
        "build/obj/fixture.o: fixture.cpp $(BACKEND_CAPABILITIES_HEADER)\n"
        "\t@mkdir -p $(@D)\n"
        "\t@echo compiled\n"
        "\t@touch $@\n"
        ".PHONY: release\n"
        "release: build/obj/fixture.o\n"
    )


def main() -> int:
    production_makefile = PRODUCTION_MAKEFILE.read_text(encoding="utf-8")
    assert (
        "$(GPIO_BAND_POLICY_TEST_OBJ): $(BACKEND_CAPABILITIES_HEADER)"
    ) in production_makefile

    with tempfile.TemporaryDirectory(prefix="wsprrypi-backend-make-") as temporary:
        fixture = Path(temporary)
        (fixture / "fixture.cpp").write_text("fixture\n", encoding="utf-8")
        (fixture / "Makefile").write_text(makefile(), encoding="utf-8")

        first = run("make", "release", cwd=fixture)
        assert "backend capabilities updated" in first.stdout, first.stdout
        assert "compiled" in first.stdout, first.stdout
        header = fixture / "build/generated/backend_capabilities.hpp"
        obj = fixture / "build/obj/fixture.o"
        first_object_time = obj.stat().st_mtime_ns

        same = run("make", "release", cwd=fixture)
        assert "backend capabilities unchanged" in same.stdout, same.stdout
        assert "compiled" not in same.stdout, same.stdout
        assert obj.stat().st_mtime_ns == first_object_time

        time.sleep(1.1)
        header.touch()
        refreshed = run("make", "release", cwd=fixture)
        assert "backend capabilities unchanged" in refreshed.stdout, refreshed.stdout
        assert "compiled" in refreshed.stdout, refreshed.stdout
        assert obj.stat().st_mtime_ns > first_object_time

        selected = run("make", "release", "BACKENDS=si5351", cwd=fixture)
        assert "backend capabilities updated" in selected.stdout, selected.stdout
        assert '#define WSPRRYPI_COMPILED_BACKENDS "si5351"' in header.read_text()

        strict = run(
            "make", "release", "BACKENDS=si5351", "ANCILLARY_GPIO=0", cwd=fixture
        )
        assert "backend capabilities updated" in strict.stdout, strict.stdout
        assert "#define WSPRRYPI_ANCILLARY_GPIO 0" in header.read_text()

        restored = run("make", "release", cwd=fixture)
        assert "backend capabilities updated" in restored.stdout, restored.stdout
        assert f'#define WSPRRYPI_COMPILED_BACKENDS "{DEFAULT}"' in header.read_text()

    print("backend capability Make integration tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
