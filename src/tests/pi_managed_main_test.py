#!/usr/bin/env python3
"""Production main/loop recovery with explicit simulation and private fixtures."""
import argparse
import configparser
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import time
from controller_only_test import free_port, request


def target_read(port, path):
    # Startup readback may briefly hold the management configuration gate.
    # A refused read changes nothing; retain the exact target/path and bound
    # the wait without retrying any other error or a mutation.
    deadline = time.monotonic() + 5
    while True:
        status, body = request(port, "GET", path)
        if status != 409:
            return status, body
        error = json.loads(body)
        if error.get("schema") != "target-error" or error.get("error", {}).get("code") != "capacity":
            return status, body
        assert time.monotonic() < deadline, (status, body)
        time.sleep(.025)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--network-safety-off", action="store_true")
    parser.add_argument("--initialization", action="store_true")
    args = parser.parse_args()
    executable = str(Path(args.fixture).resolve())
    with tempfile.TemporaryDirectory(prefix="pi-managed-main-") as temporary:
        root = Path(temporary)
        ini = root / "native.ini"
        ledger = root / "revocation-v1"
        trace = Path(str(ledger) + ".simulated-trace.json")
        for backend in ("gpio", "si5351", "wtp"):
            rejected = subprocess.run([executable, str(ledger), "--backend", "simulated", "--backend", backend],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            assert rejected.returncode == 2, "The fixture must reject every physical or external override before main"
        assert not trace.exists() and not ledger.exists()
        source = configparser.ConfigParser(interpolation=None)
        source.optionxform = str
        source.read(Path(__file__).resolve().parents[2] / "config/wsprrypi.ini")
        source["Security"]["Privileged Network Safety"] = "insecure-disabled" if args.network_safety_off else "enforced"
        # Keep a native physical selection in the saved document; the fixture
        # selects simulation explicitly on its CLI without persisting it.
        source["Operation"]["Transmit Backend"] = "gpio"
        source["Operation"]["Enable on Boot"] = "Never"
        source["WSPR"].update({"Call Sign": "AA0NT", "Grid Square": "EM18", "TX Power": "20"})
        source["WTP"].update({"Transport": "network_plain", "Hostname": "127.0.0.1",
                              "TCP Port": "31417", "Device ID": "b" * 32})
        source["WTP Server"]["Enabled"] = "false"
        source["CW"]["Fade Shape"] = "raised_cosine" if args.initialization else "invalid"
        port = free_port()
        source["Operation"]["Web Port"] = str(port)
        source["Operation"]["Socket Port"] = str(port + 1)
        with ini.open("w") as output:
            source.write(output)
        ini.chmod(0o600)
        if args.initialization:
            stock = Path(str(ini) + ".stock")
            stock.write_bytes(ini.read_bytes())
            stock.chmod(0o600)
        original = ini.read_bytes()
        command = [executable, str(ledger), "--backend", "simulated", "-i", str(ini),
                   "--socket-loopback-only", "--socket-loopback-family", "ipv4"]
        with (root / "service.log").open("w") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while True:
                    assert process.poll() is None, (root / "service.log").read_text()
                    try:
                        status, body = request(port, "GET", "/api/control/v1/host/readiness")
                        if status == 200 and json.loads(body)["state"] == "configuration_recovery":
                            break
                    except OSError:
                        pass
                    assert time.monotonic() < deadline, "Production recovery readiness deadline"
                    time.sleep(0.03)
                assert ini.read_bytes() == original and not ledger.exists()
                assert not trace.exists(), "Invalid startup must never construct or clean a backend"
                status, body = request(port, "GET", "/config")
                assert status == 409 and json.loads(body)["schema"] == "configuration-recovery"
                status, body, headers = request(port, "GET", "/api/control/v1/host/configuration-recovery", with_headers=True)
                assert status == 200
                doc = json.loads(body)
                assert (doc["validation"] == "initialization") == args.initialization
                candidate = copy.deepcopy(doc["draft"])
                candidate["CW"]["Fade Shape"] = "raised_cosine"
                if args.initialization:
                    candidate["WSPR"].update({"Call Sign": "AA0NT", "Grid Square": "EM18", "TX Power": 20})
                assert "WTP" not in candidate and "WTP Server" not in candidate
                # Every other field must be an actual typed saved value;
                # the test may not silently insert defaults for omissions.
                assert not doc["defaulted_fields"], doc["defaulted_fields"]
                unavailable = [(section, key) for section, group in candidate.items()
                    for key, value in group.items() if isinstance(value, dict) and value.get("state") == "unavailable"]
                assert not unavailable, unavailable
                context = {"Origin": f"http://127.0.0.1:{port}", "Content-Type": "application/json",
                           "X-WsprryPico-Request": "1"}
                status, _, session = request(port, "POST", "/api/control/v1/host/management-session",
                    json.dumps({"contract": "Wsprry-Control/1", "schema": "controller-browser-session"}), context, with_headers=True)
                assert status == 200
                context.update({"Cookie": session["Set-Cookie"].split(";")[0],
                    "X-WsprryPi-CSRF": session["X-WsprryPi-CSRF"], "If-Match": headers["ETag"]})
                mutation = json.dumps({"contract": "Wsprry-Control/1", "schema": "configuration-recovery-save",
                    "binding": doc["binding"], "capability_revision": doc["capability_revision"],
                    "expected_revision": doc["revision"], "operation_id": "e" * 32, "configuration": candidate})
                status, reply = request(port, "PUT", "/api/control/v1/host/configuration-recovery", mutation, context)
                assert status == 200, reply
                if args.initialization:
                    record = json.loads((Path(str(ledger) + ".recurrence-" + doc["binding"]["device_id"]) / "authority.json").read_text())
                    settings = json.loads(Path(str(ini) + ".managed-settings.json").read_text())
                    assert record["state"] == "staged" and record["clean_proof"]
                    assert record["staged_revision"] == settings["revision"]
                    assert settings["scheduling"]["enabled"] is False
                deadline = time.monotonic() + 10
                while True:
                    assert process.poll() is None, (root / "service.log").read_text()
                    status, body = request(port, "GET", "/config")
                    if status == 200:
                        break
                    assert status in (409, 503), body
                    assert time.monotonic() < deadline, (root / "service.log").read_text()
                    time.sleep(0.03)
                assert json.loads(body)["Operation"]["Transmit"] is False
                if args.network_safety_off:
                    # Off remains the ordinary policy. Only exact native-host
                    # recovery/session observations have independent protection.
                    actual = configparser.ConfigParser(interpolation=None);actual.optionxform = str;actual.read(ini)
                    assert actual["Security"]["Privileged Network Safety"] == "insecure-disabled"
                    policy_deadline = time.monotonic() + 5
                    while request(port, "GET", "/api/control/v1/targets")[0] != 403:
                        assert time.monotonic() < policy_deadline, "Saved network safety Off did not resume after native readiness"
                        time.sleep(0.03)
                    status, readiness = request(port, "GET", "/api/control/v1/host/readiness")
                    assert status == 200 and json.loads(readiness)["state"] == "ready"
                    before = {path: path.read_bytes() for path in root.glob("native.ini*") if path.is_file()}
                    for path in ("/api/control/v1/host/readiness", "/api/control/v1/host/configuration-recovery"):
                        assert request(port, "GET", path, headers={"Origin": "http://outside.invalid"})[0] == 403
                        assert request(port, "GET", path, headers={"X-WsprryPi-Client-Address": "203.0.113.42"})[0] == 403
                    session_body = json.dumps({"contract": "Wsprry-Control/1", "schema": "controller-browser-session"})
                    for rejected in ({"Origin": "http://outside.invalid"}, {"X-WsprryPi-Client-Address": "203.0.113.42"}):
                        assert request(port, "POST", "/api/control/v1/host/management-session", session_body,
                            {**context, **rejected})[0] == 403
                    unprotected = {key: value for key, value in context.items() if key not in ("Cookie", "X-WsprryPi-CSRF")}
                    assert request(port, "PUT", "/api/control/v1/host/configuration-recovery", mutation, unprotected)[0] == 403
                    # A fresh browser can establish a session under Off, then
                    # reconcile the exact accepted request without another save.
                    status, _, new_session = request(port, "POST", "/api/control/v1/host/management-session",
                        session_body, unprotected, with_headers=True)
                    assert status == 200
                    renewed = {**unprotected, "Cookie": new_session["Set-Cookie"].split(";")[0],
                        "X-WsprryPi-CSRF": new_session["X-WsprryPi-CSRF"]}
                    status, replay = request(port, "PUT", "/api/control/v1/host/configuration-recovery", mutation, renewed)
                    assert status == 200 and replay == reply
                    new_operation = json.loads(mutation);new_operation["operation_id"] = "f" * 32
                    assert request(port, "PUT", "/api/control/v1/host/configuration-recovery", json.dumps(new_operation), renewed)[0] == 503
                    assert request(port, "GET", "/api/control/v1/targets/" + doc["binding"]["device_id"] + "/settings")[0] == 403
                    assert all(path.read_bytes() == content for path, content in before.items()), "Off-mode refusal/replay must not write saved history"
                else:
                    status, replay = request(port, "PUT", "/api/control/v1/host/configuration-recovery", mutation, context)
                    assert status == 200 and replay == reply
                    status, body = request(port, "GET", "/api/control/v1/targets")
                    assert status == 200
                    targets = json.loads(body)["targets"]
                    assert len(targets) == 1 and targets[0]["product"] == "WsprryPi"
                    self_id = targets[0]["device_id"]
                    for resource, schema in (("context", "target-context"), ("settings", "target-settings"), ("status", "target-status")):
                        status, body = target_read(port, f"/api/control/v1/targets/{self_id}/{resource}")
                        assert status == 200 and json.loads(body)["schema"] == schema, body
                        assert json.loads(body)["binding"]["device_id"] == self_id
                    status, context_body = target_read(port, f"/api/control/v1/targets/{self_id}/context")
                    assert status == 200
                    stop_context = json.loads(context_body)
                    assert any(cap["resource"] == "stop" and cap["write"] for cap in stop_context["capabilities"])
                    status, settings_body = target_read(port, f"/api/control/v1/targets/{self_id}/settings")
                    assert status == 200
                    stop_settings = json.loads(settings_body)
                    # Deferred cross-target Pi management must not route to
                    # this host, even with a valid local browser session and
                    # an otherwise complete candidate/revision.
                    remote_id = "a" * 32 if self_id != "a" * 32 else "b" * 32
                    protected_files = lambda: {str(path.relative_to(root)): path.read_bytes()
                        for path in root.rglob("*") if path.is_file() and path.relative_to(root).parts[0].startswith("native.ini")}
                    before_remote = protected_files()
                    remote_base = f"/api/control/v1/targets/{remote_id}/"
                    remote_binding = {**stop_context["binding"], "device_id": remote_id}
                    remote_save = json.dumps({"contract": "Wsprry-Control/1", "schema": "target-save",
                        "binding": remote_binding, "capability_revision": stop_context["capability_revision"],
                        "operation_id": "7" * 32, "expected_revision": stop_settings["revision"],
                        "candidate": stop_settings["saved"]})
                    remote_headers = {**context, "If-Match": '"' + stop_settings["revision"] + '"'}
                    for resource in ("context", "settings", "status", "operations/" + "7" * 32):
                        status, body = request(port, "GET", remote_base + resource)
                        assert status in (404, 409) and json.loads(body)["schema"] == "target-error", body
                    status, body = request(port, "PUT", remote_base + "settings", remote_save, remote_headers)
                    assert status == 404 and json.loads(body)["schema"] == "target-error", body
                    remote_stop = json.dumps({"contract": "Wsprry-Control/1", "schema": "target-stop",
                        "binding": remote_binding, "capability_revision": stop_context["capability_revision"],
                        "operation_id": "8" * 32, "expected_revision": stop_settings["revision"],
                        "observed_output": stop_settings["observed_output"]})
                    status, body = request(port, "POST", remote_base + "stop", remote_stop, remote_headers)
                    assert status == 404 and json.loads(body)["schema"] == "target-error", body
                    for action in ("offer", "pair", "replace", "remove", "rotate/prepare"):
                        assert request(port, "POST", "/api/pair/v1/" + action, "{}", remote_headers)[0] == 404
                    assert protected_files() == before_remote, "Deferred remote Pi requests must not mutate host configuration/history"
                    stop_command = json.dumps({"contract": "Wsprry-Control/1", "schema": "target-stop",
                        "binding": stop_context["binding"], "capability_revision": stop_context["capability_revision"],
                        "operation_id": "9" * 32, "expected_revision": stop_settings["revision"],
                        "observed_output": stop_settings["observed_output"]})
                    stop_headers = {**context, "If-Match": '"' + stop_settings["revision"] + '"'}
                    stop_path = f"/api/control/v1/targets/{self_id}/stop"
                    status, stop_receipt = request(port, "POST", stop_path, stop_command, stop_headers)
                    assert status == 200 and json.loads(stop_receipt)["state"] == "committed", stop_receipt
                    stop_bytes = ini.read_bytes()
                    replay_deadline = time.monotonic() + 5
                    while True:
                        status, retry_stop = request(port, "POST", stop_path, stop_command, stop_headers)
                        if status == 200:
                            break
                        # The owning-parser readback can briefly reserve the
                        # configuration gate. Retry only this exact accepted
                        # operation, never a fresh STOP or a changed payload.
                        error = json.loads(retry_stop)
                        assert status == 409 and error.get("schema") == "target-error" and error.get("error", {}).get("code") == "capacity", (status, retry_stop)
                        assert time.monotonic() < replay_deadline, (status, retry_stop)
                        time.sleep(0.02)
                    assert retry_stop == stop_receipt and ini.read_bytes() == stop_bytes, (status, retry_stop, stop_receipt, ini.read_bytes() == stop_bytes)
                before_stop = json.loads(Path(str(ini) + ".managed-settings.json").read_text())
                status, stopped = request(port, "POST", "/control/stop", json.dumps({"command": "stop"}), context)
                assert status == 200 and json.loads(stopped)["persisted"] is True, stopped
                after_stop = json.loads(Path(str(ini) + ".managed-settings.json").read_text())
                assert after_stop["configuration_generation"] != before_stop["configuration_generation"]
                assert after_stop["configuration"]["Operation"]["Transmit"] is False
                assert after_stop["scheduling"] == before_stop["scheduling"]
                status, readiness = request(port, "GET", "/api/control/v1/host/readiness")
                assert status == 200 and json.loads(readiness)["state"] == "ready", readiness
                status, replay = request(port, "PUT", "/api/control/v1/host/configuration-recovery", mutation, context)
                assert status == 200 and replay == reply, "Existing Stop must preserve the accepted recovery receipt"
                state = json.loads(trace.read_bytes())
                assert all(not event["rf_on"] for event in state["events"])
                process.terminate()
                assert process.wait(timeout=10) == 0, (root / "service.log").read_text()
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
    print("PASS: saved network-safety Off preserved; independently protected recovery/readiness/fresh session/exact replay; foreign Origin/proxy and missing CSRF refused; new recovery writes and other management blocked" if args.network_safety_off else
        "PASS: production main, pre-parse owner, saved recovery ports, no backend from defaults, protected save/replay, automatic native readiness, no RF, and clean process shutdown")


if __name__ == "__main__":
    main()
