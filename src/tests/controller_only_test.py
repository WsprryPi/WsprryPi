#!/usr/bin/env python3
"""Hardware-free production controller services; only machine identity is injected."""
import argparse
import errno
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def free_port():
    # macOS ephemeral ports commonly exceed the application's allowed range.
    for port in range(39000, 40000):
        with socket.socket() as listener:
            try:
                listener.bind(("127.0.0.1", port))
                return port
            except OSError as error:
                if error.errno != errno.EADDRINUSE:
                    raise
    raise AssertionError("No free test listener port")


def request(port, method, path, body=None, headers=None, with_revision=False, with_headers=False):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        result = response.status, response.read()
        if with_headers:
            return (*result, dict(response.getheaders()))
        return (*result, response.getheader("ETag")) if with_revision else result
    finally:
        connection.close()


def websocket_command(connection, command):
    payload = json.dumps({"command": command, "client_request_id": "controller-test"}).encode()
    mask = b"abcd"
    assert len(payload) < 126
    connection.sendall(bytes([0x81, 0x80 | len(payload)]) + mask +
                       bytes(value ^ mask[index % 4] for index, value in enumerate(payload)))

    def read_exact(size):
        result = b""
        while len(result) < size:
            part = connection.recv(size - len(result))
            assert part, "WebSocket unexpectedly closed"
            result += part
        return result

    header = read_exact(2)
    assert header[0] == 0x81 and not header[1] & 0x80
    size = header[1] & 0x7f
    if size == 126:
        size = struct.unpack("!H", read_exact(2))[0]
    assert size <= 4096
    reply = json.loads(read_exact(size))
    assert reply["client_request_id"] == "controller-test"
    return reply


def fingerprints(directory):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.iterdir() if p.is_file() and p.name != "service.log"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--production-main", action="store_true")
    arguments = parser.parse_args()
    fixture = str(Path(arguments.fixture).resolve())
    rejection_exit = 1 if arguments.production_main else 2
    def rejected(command):
        return subprocess.run(command, timeout=5, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode
    with tempfile.TemporaryDirectory(prefix="wsprrypi-controller-") as temporary:
        directory = Path(temporary)
        ini = directory / "controller.ini"
        # All local station, startup-transmit, endpoint, and GPIO intent must be
        # irrelevant to this role, without being rewritten or defaulted on disk.
        original = ("[Operation]\nTransmit = True\nTransmit Backend = nonexistent\n"
                    "Enable on Boot = Always\nUse LED = True\nUse Shutdown = True\n"
                    "[WSPR]\nCall Sign =\nGrid Square = invalid\n"
                    "[WTP Server]\nEnabled = True\nPort = 31417\n"
                    "[Security]\nPrivileged Network Safety = enforced\n")
        ini.write_text(original)
        ini.chmod(0o600)
        base = [fixture, "--controller-only", "-i", str(ini)]
        # Every actual input read must reject unsafe file kinds/permissions;
        # no preflight check may reopen a different file through the parser.
        unsafe = directory / "unsafe.ini"
        unsafe.symlink_to(ini)
        assert rejected([fixture, "--controller-only", "-i", str(unsafe)]) == rejection_exit
        unsafe.unlink();os.link(ini, unsafe)
        assert rejected([fixture, "--controller-only", "-i", str(unsafe)]) == rejection_exit
        unsafe.unlink();os.mkfifo(unsafe, 0o600)
        assert rejected([fixture, "--controller-only", "-i", str(unsafe)]) == rejection_exit
        unsafe.unlink();unsafe.write_bytes(b"x" * 1048577);unsafe.chmod(0o600)
        assert rejected([fixture, "--controller-only", "-i", str(unsafe)]) == rejection_exit
        unsafe.unlink();ini.chmod(0o666)
        assert rejected(base) == rejection_exit
        ini.chmod(0o600)
        assert ini.read_text() == original
        for options in (["--backend", "simulated"], ["--test-tone", "20m"],
                        ["--controller-only"], ["CALLSIGN", "GRID", "10", "20m"],
                        ["--web-port", "1234junk"], ["--web-port", "65536"],
                        ["--list-i2c-adapters"],
                        ["--web-port", "1234", "--socket-port", "1234"]):
            assert rejected(base + options) == rejection_exit, options
        assert rejected([fixture, "--controller-only"]) == rejection_exit
        assert ini.read_text() == original

        # Invalid durable catalogs and journals inhibit admission before a listener
        # opens. They must survive the failed startup unchanged.
        for suffix in (".wtp-devices.json", ".wtp-assignments.json", ".wtp-fleet-contexts.json"):
            corrupt = Path(str(ini) + suffix)
            corrupt.write_text('{"corrupt":true}')
            corrupt.chmod(0o600)
            before = corrupt.read_bytes()
            assert subprocess.run(base + ["--no-web"], timeout=5).returncode == 1, suffix
            assert corrupt.read_bytes() == before
            corrupt.unlink()

        management = Path(str(ini) + ".management")
        management.mkdir(mode=0o700)
        corrupt = management / ("a" * 32 + ".json")
        corrupt.write_text('{"corrupt":true}')
        corrupt.chmod(0o600)
        before = corrupt.read_bytes()
        assert subprocess.run(base + ["--no-web"], timeout=5).returncode == 1
        assert corrupt.read_bytes() == before
        corrupt.unlink()
        management.rmdir()

        port = free_port()
        # Reserve the first choice while choosing a distinct second listener.
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", port))
            socket_port = free_port()
        with (directory / "service.log").open("w") as log:
            process = subprocess.Popen(base + ["--web-port", str(port), "--socket-port", str(socket_port)],
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 8
                while True:
                    assert process.poll() is None, "Controller exited during startup: " + (directory / "service.log").read_text()
                    try:
                        status, body = request(port, "GET", "/status")
                        if status == 200 and json.loads(body)["controller_ready"]:
                            break
                    except (OSError, http.client.HTTPException):
                        pass
                    assert time.monotonic() < deadline, "Controller readiness deadline"
                    time.sleep(0.03)
                state = json.loads(body)
                assert state["role"] == "controller-only"
                assert state["local_output"] is None and not state["local_transmission_available"]
                status, body = request(port, "GET", "/version")
                assert status == 200 and "controller host" in json.loads(body)["wspr_version"]
                devices_path = "/api/control/v1/host/devices"
                # Two saved target profiles stay controller-owned catalog state;
                # saving or reading them must never select a local output, connect
                # a WTP execution session, or rewrite the transmitter INI.
                for index in (1, 2):
                    status, body, revision = request(port, "GET", devices_path, with_revision=True)
                    assert status == 200 and revision
                    settings = {"Transport": "network_plain", "Hostname": "127.0.0.1",
                                "TCP Port": 40000 + index, "Device ID": str(index) * 32}
                    command = {"operation": "add", "name": f"Saved target {index}",
                               "method": "manual", "discovery_id": "", "settings": settings,
                               "consent_plain": True}
                    headers = {"If-Match": revision, "Content-Type": "application/json",
                               "Origin": f"http://127.0.0.1:{port}", "X-WsprryPico-Request": "1"}
                    assert request(port, "POST", devices_path, json.dumps(command), headers)[0] == 200
                status, body = request(port, "GET", devices_path)
                assert len(json.loads(body)["profiles"]) == 2
                assert ini.read_text() == original
                before = fingerprints(directory)
                for path in ("/api/control/v1/host/devices", "/api/control/v1/host/discovery",
                             "/api/control/v1/host/fleet", "/api/control/v1/host/fleet/operations"):
                    assert request(port, "GET", path)[0] == 200, path
                for method, path in (("GET", "/config"), ("PATCH", "/config"),
                                     ("POST", "/config/repair"), ("GET", "/config/si5351-addresses"),
                                     ("POST", "/control/stop"), ("POST", "/api/wtp/recover"),
                                     ("POST", "/api/control/v1/host/devices/use"),
                                     ("POST", "/api/control/v1/host/wtp-endpoint/enable"),
                                     ("POST", "/api/control/v1/jobs"),
                                     ("POST", "/api/rp1-gpclk-route"),
                                     ):
                    status, body = request(port, method, path, "{}", {"Content-Type": "application/json"})
                    assert status == 409 and json.loads(body)["error"]["code"] == "controller_only", (method, path, status)
                for path in ("/api/control/v1/member/configuration", "/api/control/v1/member/management", "/api/control/v1/network"):
                    status, body = request(port, "GET", path)
                    assert status == 404 and json.loads(body)["schema"] == "target-error", (path, status, body)
                # New management mutations require a process-local browser
                # session in addition to the inherited exact Origin/intent guard.
                session_path = "/api/control/v1/host/management-session"
                session_body = json.dumps({"contract": "Wsprry-Control/1", "schema": "controller-browser-session"})
                target_path = "/api/control/v1/targets/" + "1" * 32 + "/pico/management"
                intent = {"Origin": f"http://127.0.0.1:{port}", "Content-Type": "application/json", "X-WsprryPico-Request": "1"}
                assert request(port, "POST", session_path, session_body, {**intent, "Origin": "http://attacker.invalid"})[0] == 403
                assert request(port, "POST", target_path, '{}', intent)[0] == 403
                code, payload, session_headers = request(port, "POST", session_path, session_body, intent, with_headers=True)
                assert code == 200 and json.loads(payload)["schema"] == "controller-browser-session"
                cookie = session_headers["Set-Cookie"]
                assert "HttpOnly" in cookie and "SameSite=Strict" in cookie and "Path=/" in cookie
                token = session_headers["X-WsprryPi-CSRF"]
                assert len(token) == 64
                authorized = {**intent, "Cookie": cookie.split(";", 1)[0], "X-WsprryPi-CSRF": token}
                assert request(port, "POST", target_path, '{}', {**authorized, "X-WsprryPi-CSRF": "0" * 64})[0] == 403
                # A valid CSRF session grants no target identity or enrollment.
                assert request(port, "POST", target_path, '{}', authorized)[0] == 400
                assert request(port, "GET", "/api/control/v1/targets")[0] == 200
                code, payload = request(port, "GET", "/api/control/v1/targets")
                assert code == 200 and json.loads(payload)["targets"] == []
                snapshot = lambda: {str(path.relative_to(directory)): path.read_bytes() if path.is_file() else None
                    for path in directory.rglob("*") if path.name != "service.log"}
                before_remote = snapshot()
                remote = "/api/control/v1/targets/" + "a" * 32 + "/"
                for resource in ("context", "settings", "status", "operations/" + "7" * 32):
                    code, payload = request(port, "GET", remote + resource)
                    assert code in (404, 409) and json.loads(payload)["schema"] == "target-error", payload
                for method, resource in (("PUT", "settings"), ("POST", "stop")):
                    code, payload = request(port, method, remote + resource, "{}", authorized)
                    assert code == 404 and json.loads(payload)["schema"] == "target-error", payload
                for action in ("offer", "pair", "replace", "remove", "rotate/prepare"):
                    code, payload = request(port, "POST", "/api/pair/v1/" + action, "{}", authorized)
                    assert code == 409 and json.loads(payload)["error"]["code"] == "controller_only", (code, payload)
                assert snapshot() == before_remote, "Deferred remote Pi management cannot create host state or history"
                # Existing Host/Origin and proxy authorization runs before role
                # refusal, and still protects the permitted management routes.
                assert request(port, "POST", "/api/control/v1/host/fleet", "{}",
                               {"Origin": "https://attacker.invalid"})[0] == 403
                assert request(port, "GET", "/api/control/v1/host/fleet", headers={
                    "X-WsprryPi-Client-Address": "not-an-address"})[0] == 403
                assert request(port, "POST", "/api/control/v1/host/fleet", "x" * 32769,
                               {"Content-Type": "application/json"})[0] == 413
                with socket.create_connection(("127.0.0.1", socket_port), timeout=2) as connection:
                    connection.sendall((f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{socket_port}\r\n"
                                        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                                        "Sec-WebSocket-Version: 13\r\n"
                                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n").encode())
                    handshake = b""
                    while not handshake.endswith(b"\r\n\r\n"):
                        handshake += connection.recv(1)
                    assert handshake.startswith(b"HTTP/1.1 101")
                    status = websocket_command(connection, "get_tx_state")
                    assert status["transmit_backend"] is None and status["local_output"] is None
                    for command in ("stop", "tone_start", "bounded_tone", "tone_end", "shutdown", "reboot"):
                        assert websocket_command(connection, command)["error"] == "controller_only"
                assert fingerprints(directory) == before
                assert ini.read_text() == original
            finally:
                process.terminate()
                try:
                    process.wait(timeout=6)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise AssertionError("Controller shutdown deadline exceeded")
            assert process.returncode == 0, (directory / "service.log").read_text()
    print("PASS: controller startup, durable-state inhibition, management reads, HTTP/WebSocket refusals, authorization, no local configuration writes, and shutdown")


if __name__ == "__main__":
    main()
