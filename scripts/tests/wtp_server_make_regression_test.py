#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the parent WTP library build graph with concurrent consumers."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/Makefile").read_text()
targets = ("wtp-protocol-test", "wtp-pi-authority-test", "wtp-pi-listener-test",
           "wtp-pi-routing-probe")
builders = ("wtp-server-host-build", "wtp-client-host-build")
rules = []
for target in (*targets, *builders):
    marker = "\n" + target + ":"
    if marker not in source:
        continue
    start = source.index(marker) + 1
    rules.append(source[start:source.index("\n\n", start)])

# Make performs the real recursive graph. Only compiler/archive work is faked;
# an exclusive writer detects the concurrent archive replacement seen in CI.
helper = r"""#!/usr/bin/env python3
from pathlib import Path
import sys
import time
root = Path(__file__).resolve().parent
def paths(component):
    return (root / f'WTP-{component.title()}/build/libwtp_{component}.a',
            root / ('archive-writer-' + component))
def stable(component):
    library, lock = paths(component)
    assert not lock.exists(), 'consumer raced an archive writer'
    assert library.read_text() == 'complete', 'consumer read an incomplete archive'
if sys.argv[1] == 'archive':
    component = sys.argv[2]
    library, lock = paths(component)
    lock.mkdir()
    try:
        with (root / ('archive-builds-' + component)).open('a') as log:
            log.write('build\n')
        time.sleep(0.2)
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_text('complete')
    finally:
        lock.rmdir()
elif sys.argv[1] == 'test':
    stable(sys.argv[2])
else:
    for component in ('client', 'server'):
        if f'WTP-{component.title()}/build/libwtp_{component}.a' in sys.argv:
            stable(component)
    output = root / sys.argv[sys.argv.index('-o') + 1]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('#!/bin/sh\nexit 0\n')
    output.chmod(0o755)
"""
with tempfile.TemporaryDirectory(prefix="wtp-server-make-") as temporary:
    root = Path(temporary)
    (root / "helper.py").write_text(helper)
    for component in ("client", "server"):
        folder = root / f"WTP-{component.title()}"
        folder.mkdir()
        (folder / "Makefile").write_text(f""".PHONY: all test
all: build/libwtp_{component}.a
test: build/libwtp_{component}.a
\t@python3 ../helper.py test {component}
build/libwtp_{component}.a:
\t@python3 ../helper.py archive {component}
""")
    (root / "Makefile").write_text(
        ".DEFAULT_GOAL := all\nQ := @\nCXX := " + sys.executable +
        " " + str(root / "helper.py") + " compile\n.PHONY: all " +
        " ".join((*targets, *builders)) + "\nall: " +
        " ".join(targets) + "\n\n" + "\n\n".join(rules) + "\n")
    env = os.environ.copy()
    for name in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL"):
        env.pop(name, None)
    # Isolate each archive with the other already cached, as well as testing
    # both cold. Otherwise one library's build can hide a race in the other.
    scenarios = ((targets, ()),
                 (targets[1:], ("client",)),
                 ((targets[0], *targets[2:]), ("server",)))
    for goals, prebuilt in scenarios:
        for jobs in (2, 8):
            for component in ("client", "server"):
                library = root / f"WTP-{component.title()}/build/libwtp_{component}.a"
                library.unlink(missing_ok=True)
                (root / ("archive-builds-" + component)).unlink(missing_ok=True)
                if component in prebuilt:
                    library.parent.mkdir(parents=True, exist_ok=True)
                    library.write_text("complete")
            for repeat in range(2):
                result = subprocess.run(["make", f"-j{jobs}", *goals], cwd=root,
                                        env=env, capture_output=True, text=True, timeout=20)
                assert result.returncode == 0, (result.stdout, result.stderr)
                for component in ("client", "server"):
                    log = root / ("archive-builds-" + component)
                    builds = log.read_text().splitlines() if log.exists() else []
                    assert builds == ([] if component in prebuilt else ["build"])

print("WTP Make graph: one build per archive before concurrent consumers: PASS")
