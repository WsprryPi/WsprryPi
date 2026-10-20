#!/usr/bin/env python3

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "scripts" / "render-apache-privileged-network-policy.py"


def run_renderer(ini_text: str, existing_policy: str | None = None) -> str:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        ini = root / "wsprrypi.ini"
        output = root / "policy.conf"
        ini.write_text(ini_text, encoding="utf-8")
        if existing_policy is not None:
            output.write_text(existing_policy, encoding="utf-8")
        subprocess.run([
            "python3", str(RENDERER), "--ini", str(ini),
            "--output", str(output),
        ], check=True)
        return output.read_text(encoding="utf-8")


enforced = run_renderer(
    "[Security]\nPrivileged Network Safety = enforced\n")
assert "Current-network authorization is enforced" in enforced
assert "RequestHeader unset X-WsprryPi-Client-Address" in enforced
assert (
    'RequestHeader set X-WsprryPi-Client-Address '
    '"expr=%{CONN_REMOTE_ADDR}"'
) in enforced
assert "Require ip" not in enforced
assert "^/wsprrypi/(?:config(?:/|$)|control(?:/|$)|version$|api(?:/|$)|socket(?:/|$))" in enforced

invalid = run_renderer(
    "[Security]\nPrivileged Network Safety = true\n")
assert invalid == enforced

missing = run_renderer("[Common]\nCall Sign = TEST\n")
assert missing == enforced

disabled = run_renderer(
    "[Security]\nPrivileged Network Safety = insecure-disabled\n")
assert "NETWORK SAFETY OFF" in disabled
assert "RequestHeader unset X-WsprryPi-Client-Address" in disabled
assert "RequestHeader set X-WsprryPi-Client-Address" in disabled

# Upgrade from the former startup-snapshot policy must atomically replace its
# stale Require-ip CIDRs instead of retaining or merging them.
upgraded = run_renderer(
    "[Security]\nPrivileged Network Safety = enforced\n",
    "# Managed by WsprryPi.\nRequire ip 192.0.2.0/24\n",
)
assert upgraded == enforced
assert "192.0.2.0/24" not in upgraded

print("apache privileged-network policy renderer tests: PASS")
