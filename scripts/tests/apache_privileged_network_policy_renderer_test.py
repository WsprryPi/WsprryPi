#!/usr/bin/env python3

import subprocess
import shlex
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
        ], check=True, timeout=30)
        return output.read_text(encoding="utf-8")


enforced = run_renderer(
    "[Security]\nPrivileged Network Safety = enforced\n")
def directives(text):
    return [shlex.split(line) for line in text.splitlines() if line.strip() and not line.lstrip().startswith('#')]

expected = [
    ['<LocationMatch', '^/wsprrypi/(?:config(?:/|$)|control(?:/|$)|version$|api(?:/|$)|socket(?:/|$))>'],
    ['RequestHeader', 'unset', 'X-WsprryPi-Client-Address'],
    ['RequestHeader', 'set', 'X-WsprryPi-Client-Address', 'expr=%{CONN_REMOTE_ADDR}'],
    ['</LocationMatch>'],
]
assert directives(enforced) == expected

invalid = run_renderer(
    "[Security]\nPrivileged Network Safety = true\n")
assert invalid == enforced

missing = run_renderer("[Common]\nCall Sign = TEST\n")
assert missing == enforced

disabled = run_renderer(
    "[Security]\nPrivileged Network Safety = insecure-disabled\n")
assert directives(disabled) == expected

# Upgrade from the former startup-snapshot policy must atomically replace its
# stale Require-ip CIDRs instead of retaining or merging them.
upgraded = run_renderer(
    "[Security]\nPrivileged Network Safety = enforced\n",
    "# Managed by WsprryPi.\nRequire ip 192.0.2.0/24\n",
)
assert upgraded == enforced
assert "192.0.2.0/24" not in upgraded

print("apache privileged-network policy renderer tests: PASS")
