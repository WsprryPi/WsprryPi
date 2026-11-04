#!/usr/bin/env python3
"""Exercise the production transport selector without compiling hardware code."""
from pathlib import Path
import os
import subprocess
import tempfile

rules = Path(__file__).resolve().parents[2] / "src/WSPR-Transmitter/src/si5351_transport_rules.mk"
with tempfile.TemporaryDirectory() as directory:
    fixture = Path(directory) / "Makefile"
    fixture.write_text(f"comma := ,\nBACKENDS ?= si5351\ninclude {rules}\nall:\n\t@echo selected=$(SI5351_TRANSPORT_SOURCE) key=$(SI5351_TRANSPORT_KEY) libs=$(SI5351_TRANSPORT_LIBS)\n")
    def run(ok, *args):
        environment = os.environ.copy()
        # Each matrix case selects its own profile. Do not inherit CI's native
        # transport or an invoking make's command-line overrides/jobserver.
        for key in ("SI5351_TRANSPORT", "HOST_OS", "BACKENDS", "ANCILLARY_GPIO",
                    "MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
            environment.pop(key, None)
        result = subprocess.run(["make", "-f", str(fixture), *args], capture_output=True, text=True, env=environment)
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        return result.stdout
    linux = run(True, "HOST_OS=Linux")
    mac = run(True, "HOST_OS=Darwin", "SI5351_TRANSPORT=mcp2221a-hid")
    assert "si5351_transport_linux.cpp" in linux and "transport-linux-i2c" in linux
    assert "si5351_transport_macos.cpp" in mac and "transport-mcp2221a-hid" in mac
    assert "IOKit" in mac and "IOKit" not in linux
    for host, transport in [("Darwin", ""), ("Linux", "mcp2221a-hid"), ("Darwin", "linux-i2c"), ("FreeBSD", "linux-i2c"), ("Linux", "unknown"), ("Linux", "linux-i2c,mcp2221a-hid"), ("Linux", "linux-i2c mcp2221a-hid")]:
        run(False, f"HOST_OS={host}", f"SI5351_TRANSPORT={transport}")
    simulated = run(True, "HOST_OS=Darwin", "BACKENDS=simulated")
    assert "IOKit" not in simulated and "transport-" not in simulated
    for goal in ["help", "semantics-test-portable", "linux-i2c-identity-test", "si5351-planner-test", "simulated-backend-test", "transmission-controller-contract-test"]:
        with fixture.open("a") as out:
            out.write(f"\n{goal}: all\n")
        run(True, "HOST_OS=Darwin", goal)
    run(False, "HOST_OS=Darwin", "help", "all")
print("Si5351 transport Make selection matrix passed")
