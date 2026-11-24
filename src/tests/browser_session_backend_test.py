# SPDX-License-Identifier: MIT
"""Real backend IPC and connection cleanup; transmitter execution suppressed."""
import json
import base64
import http.client
import os
import pwd
from pathlib import Path
import signal
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
from software_backend_admission import backend_admission


class Connection:
    def __init__(self, runtime, channel):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(6)
        self.socket.connect(str(runtime / (channel + ".sock")))
        self.hello = self.receive()
        self.sequence = 0

    def read(self, length):
        result = b""
        while len(result) < length:
            chunk = self.socket.recv(length - len(result))
            assert chunk, "Unexpected connection loss"
            result += chunk
        return result

    def receive(self):
        length = struct.unpack("!I", self.read(4))[0]
        assert 0 < length <= 144 * 1024
        return json.loads(self.read(length))

    def request(self, operation, arguments):
        self.sequence += 1
        request = dict(self.hello, sequence=str(self.sequence), operation=operation, arguments=arguments)
        body = json.dumps(request).encode()
        self.socket.sendall(struct.pack("!I", len(body)) + body)
        response = self.receive()
        assert all(response[key] == self.hello[key] for key in ("protocol", "boot", "connection"))
        assert response["sequence"] == str(self.sequence)
        return response["result"]

    def command(self, command):
        response = self.request("browser.message", {"message": json.dumps(command)})
        assert response["ok"], response
        assert response["value"]["schema"] == "browser-reply/1"
        return json.loads(response["value"]["message"])

    def close(self):
        self.socket.close()


def main(fixture, recovery_helper=None):
    environment = dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS="1")
    with tempfile.TemporaryDirectory(prefix="browser-backend-", dir="/tmp") as temporary:
        root = Path(temporary).resolve()
        private = root / "private"
        private.mkdir(mode=0o700)
        runtime, state, ledger = root / "run", private / "state", private / "identity"
        web_options = {}
        cache = root / "listeners.json"
        if os.environ.get("WSPRRYPI_TEST_SPLIT_UID") == "1":
            assert sys.platform == "linux" and os.geteuid() == 0
            account = pwd.getpwnam("www-data")
            root.chmod(0o755)
            cache_directory = root / "web"
            cache_directory.mkdir(mode=0o700)
            os.chown(cache_directory, account.pw_uid, account.pw_gid)
            cache = cache_directory / "listeners.json"
            web_options = dict(user=account.pw_uid, group=account.pw_gid, extra_groups=[])
        source = private / "source.ini"
        template = (Path(__file__).resolve().parents[2] / "config/wsprrypi.ini").read_text()
        source.write_text(template.replace("Call Sign = NXXX", "Call Sign = AA0NT").replace("Grid Square = ZZ99", "Grid Square = EM18"))
        source.chmod(0o600)
        result = subprocess.run([fixture, str(ledger), "--migrate-state", "--state-directory", str(state),
                                 "--ini-source", str(source)], env=environment, capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stdout + result.stderr
        reservations = []
        for port in range(42000, 42100):
            reservation = socket.socket()
            try:
                reservation.bind(("127.0.0.1", port))
            except OSError:
                reservation.close()
                continue
            reservations.append(reservation)
            if len(reservations) == 2:
                break
        assert len(reservations) == 2, "Two isolated listener ports unavailable"
        ports = [item.getsockname()[1] for item in reservations]
        for item in reservations:
            item.close()
        sessions = []
        with (root / "backend.log").open("w") as output:
            process = subprocess.Popen([fixture, str(ledger), "--state-directory", str(state), "--runtime-directory", str(runtime),
                                        "--backend", "simulated", "--socket-loopback-only", "--web-port", str(ports[0]),
                                        "--socket-port", str(ports[1]),"--socket-loopback-family","ipv4"], env=environment, stdout=output, stderr=subprocess.STDOUT)
            web_output=(root / "web.log").open("w")
            web=subprocess.Popen([fixture,str(ledger),"--web-service","--runtime-directory",str(runtime),
                                  "--listener-cache",str(cache)],env=environment,stdout=web_output,stderr=subprocess.STDOUT,**web_options)
            try:
                deadline = time.monotonic() + 15
                while True:
                    assert process.poll() is None, (root / "backend.log").read_text()
                    assert web.poll() is None, (root / "web.log").read_text()
                    ready = subprocess.run([fixture, str(ledger), "--settings", "read", "--runtime-directory", str(runtime)],
                                           env=environment, capture_output=True, text=True, timeout=10)
                    if ready.returncode == 0:
                        observed = json.loads(ready.stdout)
                        if (observed["editing"] == "ready" and observed["settings"]["Operation.Web Port"]["active"] == ports[0]
                                and observed["settings"]["Operation.Socket Port"]["active"] == ports[1]):
                            break
                    assert time.monotonic() < deadline, (root / "backend.log").read_text()
                    time.sleep(0.05)

                view = json.loads(ready.stdout)
                assert view["settings"]["Operation.Web Port"]["active"] == ports[0]
                assert view["settings"]["Operation.Socket Port"]["active"] == ports[1]

                def saved_configuration():
                    uri = (state / "generation-1/state.sqlite3").as_uri() + "?mode=ro"
                    with sqlite3.connect(uri, uri=True) as database:
                        return database.execute("SELECT payload FROM records WHERE kind='configuration'").fetchone()[0]

                original_saved = saved_configuration()
                if web_options:
                    refusal = subprocess.run([sys.executable, "-c", "import os,sys;os.open(sys.argv[1],os.O_RDONLY)",
                                              str(state / "generation-1/state.sqlite3")],
                                             capture_output=True, text=True, **web_options)
                    assert refusal.returncode != 0 and "PermissionError" in refusal.stderr
                    assert cache.stat().st_uid == web_options["user"]
                    print("PASS actual www-data frontend serves HTTP/WS while kernel permissions deny database access")
                identity_connection=Connection(runtime,"control")
                identity=identity_connection.request("listeners.read",{})["value"]["device_id"]
                backend_boot = identity_connection.hello['boot']
                identity_connection.close()
                capture_busy_check = None
                if recovery_helper:
                    from recovery_capture_backend_checks import exercise
                    capture_busy_check = exercise(Connection, runtime, ledger, state, identity, backend_boot, recovery_helper)
                saved_ports = json.loads(original_saved)["configuration"]["Operation"]
                assert view["settings"]["Operation.Web Port"]["saved"] == saved_ports["Web Port"]
                assert view["settings"]["Operation.Socket Port"]["saved"] == saved_ports["Socket Port"]

                def browser_http(method,path,body=None,headers=None):
                    connection=http.client.HTTPConnection("127.0.0.1",ports[0],timeout=6)
                    try:
                        connection.request(method,path,body,headers or {})
                        response=connection.getresponse()
                        return response.status,response.read()
                    finally:connection.close()

                status,document=browser_http("GET","/config")
                assert status==200 and json.loads(document)["WSPR"]["Call Sign"]=="AA0NT",(status,document)
                status,_=browser_http("PUT","/config","{}",{"Origin":"http://evil.example","Content-Type":"application/json"})
                assert status==403 and saved_configuration()==original_saved

                def http_request(operation, method, path, body=b"", headers=None, target=None):
                    connection = Connection(runtime, "http")
                    try:
                        opened = connection.request("http.open", {"operation": operation, "method": method, "path": path,
                            "target": target or path, "peer": "127.0.0.1", "headers": [["Host", "127.0.0.1"], *(headers or [])],
                            "body_size": str(len(body))})
                        assert opened["ok"], opened
                        if body:
                            refused = connection.request("http.body", {"offset": "1", "bytes": base64.b64encode(body).decode()})
                            assert refused == {"ok": False, "error": "invalid_request"}
                            assert connection.request("http.body", {"offset": "0", "bytes": base64.b64encode(body).decode()})["ok"]
                        response = connection.request("http.dispatch", {})
                        if not response["ok"]:
                            return response, b""
                        metadata = response["value"]
                        assert metadata["schema"] == "browser-http-response/1"
                        content = b""
                        while len(content) < int(metadata["size"]):
                            chunk = connection.request("http.read", {"offset": str(len(content)), "maximum": 65536})
                            assert chunk["ok"], chunk
                            decoded = base64.b64decode(chunk["value"]["bytes"], validate=True)
                            assert decoded and len(decoded) <= 65536
                            content += decoded
                        assert connection.request("http.complete", {"delivered": True})["ok"]
                        return metadata, content
                    finally:
                        connection.close()

                metadata, content = http_request("configuration.read", "GET", "/config")
                assert metadata["status"] == 200, (metadata, content)
                document = json.loads(content)
                assert document["WSPR"]["Call Sign"] == "AA0NT"
                denied, _ = http_request("configuration.write", "PUT", "/config", b"{}",
                                         [["Origin", "http://evil.example"], ["Content-Type", "application/json"]])
                assert denied["status"] == 403 and saved_configuration() == original_saved
                mismatched, _ = http_request("configuration.read", "GET", "/config", target="/api/network-safety")
                assert mismatched == {"ok": False, "error": "invalid_request"}

                upgrade = ("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://127.0.0.1\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n")

                def opened():
                    session = Connection(runtime, "browser")
                    sessions.append(session)
                    admitted = session.request("browser.open", {"upgrade": upgrade, "peer": "127.0.0.1"})
                    assert admitted["ok"], admitted
                    return session

                rejected = Connection(runtime, "browser")
                assert rejected.request("browser.open", {"upgrade": upgrade.replace("http://127.0.0.1", "http://evil.example"),
                                                         "peer": "127.0.0.1"}) == {"ok": False, "error": "invalid_request"}
                rejected.close()
                first, observer = opened(), opened()
                assert not observer.command({"command": "get_tx_state"})["test_tone"]["owned"]
                assert first.request("browser.message", {"message": '{"command":"stop","command":"tone_start"}'}) == {
                    "ok": False, "error": "invalid_request"}
                started = first.command({"command": "tone_start", "frequency_source": "custom_rf", "frequency_hz": 14097100})
                assert started["started"], started
                generation = started["test_tone"]["generation"]
                if capture_busy_check:
                    capture_busy_check()
                    assert observer.command({"command": "get_tx_state"})["test_tone"]["generation"] == generation
                    assert observer.command({"command": "get_tx_state"})["test_tone"]["owned"]
                unrelated = opened()
                unrelated.close()
                assert observer.command({"command": "get_tx_state"})["test_tone"]["generation"] == generation
                assert observer.command({"command": "get_tx_state"})["test_tone"]["owned"]
                first.close()
                deadline = time.monotonic() + 5
                while observer.command({"command": "get_tx_state"})["test_tone"]["owned"]:
                    assert time.monotonic() < deadline, "Owner loss did not clean up its tone"
                    time.sleep(0.02)
                successor = observer.command({"command": "tone_start", "frequency_source": "custom_rf", "frequency_hz": 14097100})
                assert successor["started"] and successor["test_tone"]["generation"] > generation, successor
                first.close()  # Repeated stale cleanup cannot end the successor.
                assert observer.command({"command": "get_tx_state"})["test_tone"]["owned"]
                observer.close()
                final = opened()
                deadline = time.monotonic() + 5
                while final.command({"command": "get_tx_state"})["test_tone"]["owned"]:
                    assert time.monotonic() < deadline
                    time.sleep(0.02)
                events = Connection(runtime, "events")
                feed = events.request("events.read", {"boot": "", "after": "0"})["value"]
                assert feed["resync_required"] and feed["boot"] == final.hello["boot"] and feed["events"] == []
                events.close()
                assert process.poll() is None and not source.exists() and saved_configuration() == original_saved
                # Use the actual web process's WebSocket framing and proxy,
                # rather than only its backend IPC adapter.
                browser_socket=socket.create_connection(("127.0.0.1",ports[1]),timeout=6)
                browser_socket.sendall(upgrade.encode())
                handshake=b""
                while not handshake.endswith(b"\r\n\r\n"):
                    handshake+=browser_socket.recv(1)
                    assert len(handshake)<=4096
                assert handshake.startswith(b"HTTP/1.1 101"),handshake

                def websocket_command(command):
                    payload=json.dumps(command).encode();mask=b"abcd"
                    header=bytes([0x81,0x80|len(payload)]) if len(payload)<126 else bytes([0x81,0x80|126])+struct.pack("!H",len(payload))
                    browser_socket.sendall(header+mask+bytes(byte^mask[index%4] for index,byte in enumerate(payload)))
                    def exact(count):
                        content=b""
                        while len(content)<count:
                            chunk=browser_socket.recv(count-len(content));assert chunk;content+=chunk
                        return content
                    deadline=time.monotonic()+6
                    while time.monotonic()<deadline:
                        header=exact(2);length=header[1]&127
                        if length==126:length=struct.unpack("!H",exact(2))[0]
                        elif length==127:length=struct.unpack("!Q",exact(8))[0]
                        assert length<=65536 and not header[1]&128
                        content=exact(length)
                        if header[0]&15==9:
                            browser_socket.sendall(bytes([0x8a,0x80|length])+mask+bytes(byte^mask[index%4] for index,byte in enumerate(content)))
                            continue
                        assert header[0]&15==1
                        reply=json.loads(content)
                        if reply.get("client_request_id")==command["client_request_id"]:return reply
                    raise AssertionError("No bound WebSocket reply")

                frontend_state=websocket_command({"command":"get_tx_state","client_request_id":"frontend-state"})
                assert frontend_state["test_tone"]["owned"] is False
                frontend_tone=websocket_command({"command":"tone_start","frequency_source":"custom_rf","frequency_hz":14097100,
                                                 "client_request_id":"frontend-tone"})
                assert frontend_tone["started"],frontend_tone
                for session in sessions:
                    session.close()
                web.terminate()
                assert web.wait(timeout=10)==0,(root / "web.log").read_text()
                browser_socket.close()
                assert process.poll() is None and saved_configuration()==original_saved
                deadline=time.monotonic()+5
                monitor=opened()
                while monitor.command({"command":"get_tx_state"})["test_tone"]["owned"]:
                    assert time.monotonic()<deadline
                    time.sleep(0.02)
                monitor.close()
                try:
                    unexpected=socket.create_connection(("127.0.0.1",ports[0]),timeout=1)
                except OSError:pass
                else:unexpected.close();raise AssertionError("Backend still owns the browser HTTP listener")
                cached=json.loads(cache.read_text())
                assert cached["device_id"]==identity and cached["boot"]==final.hello["boot"]
                web=subprocess.Popen([fixture,str(ledger),"--web-service","--runtime-directory",str(runtime),
                                      "--listener-cache",str(cache)],env=environment,stdout=web_output,stderr=subprocess.STDOUT,**web_options)
                deadline=time.monotonic()+10
                while True:
                    assert web.poll() is None,(root / "web.log").read_text()
                    try:
                        if browser_http("GET","/config")[0]==200:break
                    except OSError:pass
                    assert time.monotonic()<deadline
                    time.sleep(0.05)
                assert process.poll() is None and saved_configuration()==original_saved
                # Backend exit drains local handlers before tearing down owners;
                # the separate web process retains its admitted endpoint.
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=10) == 0, (root / "backend.log").read_text()
                assert web.poll() is None
                unavailable, body = browser_http("GET", "/config")
                assert unavailable == 503 and json.loads(body)["error"] == "backend_unavailable", (unavailable, body)
                web.terminate();assert web.wait(timeout=10)==0,(root / "web.log").read_text()
                web = subprocess.Popen([fixture, str(ledger), "--web-service", "--runtime-directory", str(runtime),
                                        "--listener-cache", str(cache)], env=environment, stdout=web_output,
                                       stderr=subprocess.STDOUT, **web_options)
                deadline = time.monotonic() + 10
                while True:
                    assert web.poll() is None, (root / "web.log").read_text()
                    try:
                        if browser_http("GET", "/config")[0] == 503:
                            break
                    except OSError:
                        pass
                    assert time.monotonic() < deadline, "Cached endpoint unavailable without backend"
                    time.sleep(0.05)
                assert saved_configuration() == original_saved
                web.terminate();assert web.wait(timeout=10)==0,(root / "web.log").read_text()
            finally:
                for session in sessions:
                    session.close()
                if web.poll() is None:
                    web.terminate()
                    try:web.wait(timeout=10)
                    except subprocess.TimeoutExpired:web.kill();web.wait(timeout=5)
                web_output.close()
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
    print("PASS: separate backend/web processes, real HTTP/WebSocket routes, origin/path/offset refusal, saved/active ports, owner-only tone cleanup, web restart with unchanged backend/state and event resync; transmitter execution suppressed")


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    with backend_admission(executable):
        main(executable, str(Path(sys.argv[2]).resolve()) if len(sys.argv) == 3 else None)
