#!/usr/bin/env python3
"""Check the exact upstream PIO UAPI and license import without a network."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2] / "src/vendor/rp1_pio"
provenance = json.loads((root / "PROVENANCE.json").read_text())
assert provenance["repository"] == "https://github.com/raspberrypi/linux"
assert provenance["revision"] == "4103a989a46196239b830bd07693277e42b8f992"
assert provenance["upstream"] == "include/uapi/misc/rp1_pio_if.h"
assert set(provenance["files"]) == {"rp1_pio_if.h", "GPL-2.0", "Linux-syscall-note"}
for name, digest in provenance["files"].items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name
print("Pinned RP1 PIO public UAPI and license import verified")
