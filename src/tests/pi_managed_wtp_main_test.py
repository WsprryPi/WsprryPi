#!/usr/bin/env python3
"""Actual main: independent Pi management with an offline software WTP peer."""
import argparse
import configparser
import json
from pathlib import Path
import subprocess
import tempfile
import time
from controller_only_test import free_port, request


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
        source["Operation"].update({"Transmit Backend": "wtp", "Transmit": "false", "Enable on Boot": "Never",
            "Use LED": "false", "Use Amp": "false", "Use Shutdown": "false"})
        source["Security"]["Privileged Network Safety"] = "enforced"
        source["WSPR"].update({"Call Sign": "AA0NT", "Grid Square": "EM18", "TX Power": "20"})
        source["WTP"].update({"Transport": "network_plain", "Hostname": "127.0.0.1", "TCP Port": "31417", "Device ID": "b" * 32})
        source["WTP Server"]["Enabled"] = "false"
        port = free_port();source["Operation"]["Web Port"] = str(port);source["Operation"]["Socket Port"] = str(port + 1)
        with ini.open("w") as output:source.write(output)
        ini.chmod(0o600)
        with (root / "service.log").open("w") as log:
            process = subprocess.Popen([executable, str(ledger), "--backend", "wtp", "-i", str(ini),
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
                status, body = request(port, "GET", base + "context");assert status == 200;context = json.loads(body)
                status, body, headers = request(port, "GET", base + "settings", with_headers=True);assert status == 200;saved = json.loads(body)
                status, body = request(port, "GET", base + "status");assert status == 200
                observed = json.loads(body)
                assert observed["destination_id"] is None and observed["transmit_effective"] is False and observed["phase"] == "unknown"
                status, body = request(port, "GET", "/api/wtp");assert status == 200 and json.loads(body)["ready"] is False
                auth = {"Origin": f"http://127.0.0.1:{port}", "Content-Type": "application/json", "X-WsprryPico-Request": "1"}
                status, _, session = request(port, "POST", "/api/control/v1/host/management-session",
                    json.dumps({"contract": "Wsprry-Control/1", "schema": "controller-browser-session"}), auth, with_headers=True)
                assert status == 200
                auth.update({"Cookie": session["Set-Cookie"].split(";")[0], "X-WsprryPi-CSRF": session["X-WsprryPi-CSRF"], "If-Match": headers["ETag"]})
                candidate = saved["saved"];candidate["scheduling"]["solar"]["location"]["locator"] = "EM18AA12"
                candidate["scheduling"]["solar"]["morning"].update({"enabled": True, "lower_mdeg": -7001, "upper_mdeg": 8002})
                candidate["configuration"]["CW"]["Message"] = "OFFLINE PEER EDIT"
                command = json.dumps({"contract": "Wsprry-Control/1", "schema": "target-save", "binding": context["binding"],
                    "capability_revision": context["capability_revision"], "operation_id": "8" * 32, "expected_revision": saved["revision"], "candidate": candidate})
                status, receipt = request(port, "PUT", base + "settings", command, auth)
                assert status == 200 and json.loads(receipt)["state"] == "committed", receipt
                assert request(port, "PUT", base + "settings", command, auth) == (200, receipt)
                status, body = request(port, "GET", base + "settings");assert status == 200
                assert json.loads(body)["saved"]["configuration"]["CW"]["Message"] == "OFFLINE PEER EDIT"
                snapshot = json.loads(body)
                assert snapshot["saved"]["scheduling"] == candidate["scheduling"]
                for cap in context["capabilities"]:
                    if cap["resource"] == "pi-scheduling":assert cap["activation"] == "stored_only" and cap["write"]
                before = ini.read_bytes()
                status, body, headers = request(port, "GET", base + "settings", with_headers=True)
                auth["If-Match"] = headers["ETag"]
                bad = json.loads(command);bad["expected_revision"] = json.loads(body)["revision"];bad["operation_id"] = "9" * 32
                bad["candidate"]["scheduling"]["solar"]["enabled"] = True
                assert request(port, "PUT", base + "settings", json.dumps(bad), auth)[0] == 409
                assert ini.read_bytes() == before
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
    print("PASS: actual WTP-role main with offline software peer, verified host readiness, unknown destination, protected Pi configuration save/replay, retained explicit reconciliation, and zero transport writes or RF")


if __name__ == "__main__":main()
