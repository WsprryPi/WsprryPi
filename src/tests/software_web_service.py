# SPDX-License-Identifier: MIT
"""Separate browser process for actual-main fixtures; no installed state."""
from pathlib import Path
import subprocess


def start_software_web_service(launcher, ledger, output, environment=None):
    cache = Path(str(ledger) + ".web-listeners.json").resolve()
    return subprocess.Popen([*launcher, "--web-service", "--listener-cache", str(cache)],
                            stdout=output, stderr=subprocess.STDOUT, env=environment)


def stop_software_web_service(process):
    if process is None:
        return
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
