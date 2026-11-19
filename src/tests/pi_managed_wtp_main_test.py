#!/usr/bin/env python3
"""Actual native main: dormant legacy values and current Pi management."""
import argparse
import configparser
import json
from pathlib import Path
import subprocess
import tempfile
import time
from controller_only_test import free_port, request


def read_managed(port, path, with_headers=False):
    deadline = time.monotonic() + 5
    while True:
        result = request(port, "GET", path, with_headers=with_headers)
        if result[0] == 200:
            return result
        value = json.loads(result[1])
        assert result[0] in (409, 429) and value.get("schema") == "target-error" and value.get("error", {}).get("code") in ("capacity", "rate_limited"), result[:2]
        assert time.monotonic() < deadline, result[:2]
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()
    executable = str(Path(args.fixture).resolve())
    with tempfile.TemporaryDirectory(prefix="pi-managed-wtp-main-") as temporary:
        root = Path(temporary)
        ini, ledger = root / "native.ini", root / "revocation-v1"
        for backend in ("simulated", "gpio", "si5351"):
            refused = subprocess.run([executable, str(ledger), "--backend", "wtp", "--backend", backend],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            assert refused.returncode == 2
        source = configparser.ConfigParser(interpolation=None);source.optionxform = str
        source.read(Path(__file__).resolve().parents[2] / "config/wsprrypi.ini")
        source["Operation"].update({"Transmit Backend": "gpio", "Transmit": "false", "Enable on Boot": "Never",
            "Use LED": "false", "Use Amp": "false", "Use Shutdown": "false"})
        source["Security"]["Privileged Network Safety"] = "enforced"
        source["WSPR"].update({"Call Sign": "AA0NT", "Grid Square": "EM18", "TX Power": "20"})
        source["WTP"].update({"Transport": "network_plain", "Hostname": "127.0.0.1", "TCP Port": "31417", "Device ID": "b" * 32})
        source["WTP Server"]["Enabled"] = "false"
        port = free_port();source["Operation"]["Web Port"] = str(port);source["Operation"]["Socket Port"] = str(port + 1)
        with ini.open("w") as output:source.write(output)
        ini.chmod(0o600)
        with (root / "service.log").open("w") as log:
            process = subprocess.Popen([executable, str(ledger), "--backend", "simulated", "-i", str(ini),
                "--socket-loopback-only", "--socket-loopback-family", "ipv4"], stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while True:
                    assert process.poll() is None, (root / "service.log").read_text()
                    try:
                        status, body = request(port, "GET", "/api/control/v1/host/readiness")
                        if status == 200 and json.loads(body)["state"] == "ready":break
                    except OSError:pass
                    assert time.monotonic() < deadline, (root / "service.log").read_text()
                    time.sleep(0.03)
                status, body = request(port, "GET", "/api/control/v1/targets");assert status == 200
                self_id = json.loads(body)["targets"][0]["device_id"]
                base = f"/api/control/v1/targets/{self_id}/"
                status, body = read_managed(port, base + "context");assert status == 200;context = json.loads(body)
                status, body, headers = read_managed(port, base + "settings", with_headers=True);assert status == 200;saved = json.loads(body)
                status, body = read_managed(port, base + "status");assert status == 200
                observed = json.loads(body)
                assert observed["destination_id"] == self_id and observed["transmit_effective"] is False and observed["phase"] == "waiting" and observed["waiting_reason"] in ("disabled", "transmit_off"), observed
                status, body = request(port, "GET", "/api/wtp");assert status == 200 and json.loads(body) == {"selected": False, "firmware_maintenance": False}
                auth = {"Origin": f"http://127.0.0.1:{port}", "Content-Type": "application/json", "X-WsprryPico-Request": "1"}
                status, _, session = request(port, "POST", "/api/control/v1/host/management-session",
                    json.dumps({"contract": "Wsprry-Control/1", "schema": "controller-browser-session"}), auth, with_headers=True)
                assert status == 200
                auth.update({"Cookie": session["Set-Cookie"].split(";")[0], "X-WsprryPi-CSRF": session["X-WsprryPi-CSRF"], "If-Match": headers["ETag"]})
                candidate = saved["saved"];candidate["scheduling"]["solar"]["location"]["locator"] = "EM18AA12"
                candidate["scheduling"]["solar"]["morning"].update({"enabled": True, "lower_mdeg": -7001, "upper_mdeg": 8002})
                candidate["configuration"]["CW"]["Message"] = "DORMANT LEGACY EDIT"
                command = json.dumps({"contract": "Wsprry-Control/1", "schema": "target-save", "binding": context["binding"],
                    "capability_revision": context["capability_revision"], "operation_id": "8" * 32, "expected_revision": saved["revision"], "candidate": candidate})
                status, receipt = request(port, "PUT", base + "settings", command, auth)
                assert status == 200 and json.loads(receipt)["state"] == "committed", receipt
                assert request(port, "PUT", base + "settings", command, auth) == (200, receipt)
                status, body = read_managed(port, base + "settings");assert status == 200
                assert json.loads(body)["saved"]["configuration"]["CW"]["Message"] == "DORMANT LEGACY EDIT"
                snapshot = json.loads(body)
                assert snapshot["saved"]["scheduling"] == candidate["scheduling"]
                for cap in context["capabilities"]:
                    if cap["resource"] == "pi-scheduling":assert cap["activation"] == "next_occurrence" and cap["write"]
                before = ini.read_bytes()
                status, body, headers = read_managed(port, base + "settings", with_headers=True)
                auth["If-Match"] = headers["ETag"]
                bad = json.loads(command);bad["expected_revision"] = json.loads(body)["revision"];bad["operation_id"] = "9" * 32
                bad["candidate"]["configuration"]["WTP"] = {}
                assert request(port, "PUT", base + "settings", json.dumps(bad), auth)[0] == 400
                assert ini.read_bytes() == before
                retained = configparser.ConfigParser(interpolation=None);retained.optionxform = str;retained.read(ini)
                assert dict(retained["WTP"]) == dict(source["WTP"]), "Current saves preserve dormant private legacy values"
                status, body = request(port, "POST", "/api/wtp/recover", json.dumps({"operation": "reconcile"}), auth)
                assert status == 409 and json.loads(body)["ok"] is False, body
                assert request(port, "GET", "/api/control/v1/host/readiness")[0] == 200
                assert not Path(str(ledger) + ".simulated-trace.json").exists()
                process.terminate();assert process.wait(timeout=10) == 0, (root / "service.log").read_text()
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:process.wait(timeout=10)
                    except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
    print("PASS: actual native main, dormant legacy value preservation, protected Pi save/replay, closed WTP field refusal, inert retired diagnostics, and no transmission")


if __name__ == "__main__":main()
