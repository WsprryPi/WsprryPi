# SPDX-License-Identifier: MIT
"""Separate browser process for actual-main fixtures; no installed state."""
import json
from pathlib import Path
import subprocess
import time


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


def wait_for_pairing_transport(request, port, device_id, headers=None):
    # Native host readiness and the supervised pairing listener have separate
    # lifecycles. Reading the window does not open pairing.
    deadline = time.monotonic() + 10
    while True:
        status, body = request(port, "GET", "/api/pair/v1/window", headers=headers)
        if status == 200:
            window = json.loads(body)
            assert window["schema"] == "window" and window["device_id"] == device_id, body
            assert window["open"] is False, body
            return
        assert status == 503, (status, body)
        assert time.monotonic() < deadline, "Native management listener readiness"
        time.sleep(.03)
