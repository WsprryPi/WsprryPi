# SPDX-License-Identifier: MIT
"""Actual executable entry points, using the private-state production launcher."""
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time
from software_backend_admission import backend_admission


def main():
    fixture = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="sqlite-state-entry-") as temporary:
        root = Path(temporary).resolve()
        environment = dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS="1")

        def run(ledger, *arguments, success=True):
            result = subprocess.run([fixture, str(ledger), *map(str, arguments)], env=environment,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
            assert (result.returncode == 0) == success, (arguments, result.returncode, result.stdout)
            return result.stdout

        def configuration(state):
            uri = (state / "generation-1/state.sqlite3").as_uri() + "?mode=ro"
            with sqlite3.connect(uri, uri=True) as connection:
                return json.loads(connection.execute("SELECT payload FROM records WHERE kind='configuration'").fetchone()[0])

        def settings(ledger):
            deadline = time.monotonic() + 10
            while True:
                result = subprocess.run([fixture, str(ledger), "--settings", "read"], env=environment,
                                        text=True, capture_output=True, timeout=10)
                if result.returncode == 0:
                    view = json.loads(result.stdout)
                    if view["editing"] == "ready":
                        return view
                else:
                    assert result.returncode == 3, result.stdout + result.stderr
                assert time.monotonic() < deadline, result.stdout + result.stderr
                time.sleep(0.05)

        for arguments in (("--initialize-state", "--help"), ("--migrate-state", "--help")):
            text = run(root / "help-ledger", *arguments)
            assert "--state-directory" in text and "Failed to open /proc" not in text
        assert run(root / "format-ledger", "--storage-format").strip() == "application-state/1"
        assert "Failed to open /proc" not in run(root / "format-ledger", "--storage-format", "--backend", "gpio", success=False)
        assert "Failed to open /proc" not in run(root / "format-ledger", "--route-observation", "--backend", "gpio", success=False)
        help_text = run(root / "help-ledger", "--settings", "--help")
        assert "CW.Fade Slice Ms" in help_text and "No offline settings writes" in help_text
        assert "Failed to open /proc" not in help_text
        unavailable = subprocess.run([fixture, str(root / "missing-backend-ledger"), "--settings", "read"], env=environment,
                                     text=True, capture_output=True, timeout=10)
        assert unavailable.returncode == 3 and "Failed to open /proc" not in unavailable.stdout + unavailable.stderr
        for option in ("-i", "--ini-file", "--ini-file=missing", "-imissing"):
            run(root / "retired-input-ledger", option, success=False)
        invalid = root / "invalid"
        for extras in (("--backend", "simulated"), ("--initialize-state",), ("--ini-source", root / "unused")):
            run(root / "invalid-ledger", "--initialize-state", "--state-directory", invalid, *extras, success=False)
            assert not invalid.exists()

        fresh = root / "fresh"
        run(root / "fresh-ledger", "--initialize-state", "--state-directory", fresh, "--controller-only")
        assert configuration(fresh)["role"] == "controller"
        run(root / "fresh-ledger", "--initialize-state", "--state-directory", fresh, "--controller-only")

        source, state, ledger = root / "old.ini", root / "migrated", root / "migrated-ledger"
        original = ("[Operation]\nWeb Port=32101\nSocket Port=32102\n[Meta]\ndebug_logging=true\n"
                    "[Security]\nPrivileged Network Safety=enforced\n[WSPR]\nCall Sign=\n")
        source.write_text(original)
        source.chmod(0o600)
        run(ledger, "--migrate-state", "--state-directory", state, "--ini-source", source, "--controller-only")
        assert not source.exists() and Path(str(source) + ".sqlite-migrated").read_text() == original
        saved = configuration(state)
        assert saved["configuration"] == {"Operation": {"Web Port": 32101, "Socket Port": 32102}}
        assert saved["private"]["debug_logging"] is True
        listeners = json.loads(run(ledger, "--check-state", "--state-directory", state,
                                   "--controller-only", "--listener-bootstrap"))
        assert listeners["schema"] == "listener-bootstrap/1" and listeners["role"] == "controller"
        assert listeners["web_port"] == 32101 and listeners["socket_port"] == 32102
        assert set(listeners) == {"schema", "device_id", "role", "web_port", "socket_port"}
        namespace = run(ledger, "--check-state", "--state-directory", state,
                        "--controller-only", "--companion-namespace").strip()
        assert namespace == str(state / "generation-1/configuration")
        log = root / "backend.log"
        with log.open("w") as output:
            process = subprocess.Popen([fixture, str(ledger), "--controller-only", "--state-directory", str(state),
                                        "--no-web", "--no-debug-logging"], env=environment, stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while "Controller ready" not in log.read_text():
                    assert process.poll() is None, log.read_text()
                    assert time.monotonic() < deadline, log.read_text()
                    time.sleep(0.05)
                assert configuration(state) == saved
                assert not (state / "generation-1/configuration").exists() and not source.exists()
                view = settings(ledger)
                assert view["role"] == "controller" and len(view["settings"]) == 3
                assert view["settings"]["Meta.debug_logging"] == {"saved": True, "active": False, "valid": True, "activation": "startup_override"}
                # A recreated import input is inert after conversion. Only the
                # SQL persistence owner below may change the running settings.
                source.write_text("[Operation]\nWeb Port=32109\nSocket Port=32110\n[Meta]\ndebug_logging=false\n")
                source.chmod(0o600)
                unchanged = settings(ledger)
                assert unchanged["revision"] == view["revision"] and unchanged["settings"] == view["settings"]
                assert configuration(state) == saved
                source.unlink()
                edited = json.loads(run(ledger, "--settings", "write", "--expected-revision", view["revision"],
                                        "--set", "Meta.debug_logging=false", "--set", "Operation.Web Port=32102",
                                        "--set", "Operation.Socket Port=32101"))
                assert edited["revision"] != view["revision"]
                saved = configuration(state)
                assert saved["configuration"]["Operation"] == {"Web Port": 32102, "Socket Port": 32101}
                assert saved["private"]["debug_logging"] is False and saved["private"]["network_safety"] == "enforced"
                assert "revision_conflict" in run(ledger, "--settings", "write", "--expected-revision", view["revision"],
                                                   "--set", "Meta.debug_logging=true", success=False)
                for assignment in ("CW.Fade In Ms=20", "Operation.Web Port=32101", "Meta.Loop TX=true"):
                    run(ledger, "--settings", "write", "--expected-revision", edited["revision"], "--set", assignment, success=False)
                    assert configuration(state) == saved
                no_op = json.loads(run(ledger, "--settings", "write", "--expected-revision", edited["revision"],
                                       "--set", "Meta.debug_logging=false"))
                assert no_op["revision"] == edited["revision"] and configuration(state) == saved
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=10) == 0, log.read_text()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
        # Ready migration does not consult the retired source again.
        run(ledger, "--migrate-state", "--state-directory", state, "--ini-source", source, "--controller-only")
        database = state / "generation-1/state.sqlite3"
        database.rename(database.with_suffix(".retained"))
        run(ledger, "--controller-only", "--state-directory", state, "--no-web", success=False)
        run(ledger, "--initialize-state", "--state-directory", state, "--controller-only", success=False)
        assert not database.exists() and not source.exists()
        native_source, native_state, native_ledger = root / "native.ini", root / "native-state", root / "native-ledger"
        template = (Path(__file__).resolve().parents[2] / "config/wsprrypi.ini").read_text()
        template = template.replace("Call Sign = NXXX", "Call Sign = AA0NT").replace("Grid Square = ZZ99", "Grid Square = EM18")
        template = template.replace("debug_logging = False", "debug_logging = True\nLoop TX = true\nTX Iterations = 27")
        template = template.replace("TX Output = CLK0", "TX Output = CLK2").replace("Fade In Ms = 20", "Fade In Ms = 27")
        native_source.write_text(template)
        native_source.chmod(0o600)
        run(native_ledger, "--migrate-state", "--state-directory", native_state, "--ini-source", native_source)
        native_saved = configuration(native_state)
        assert native_saved["role"] == "pi" and native_saved["configuration"]["CW"]["Fade In Ms"] == 27
        assert native_saved["configuration"]["Si5351"]["TX Output"] == "CLK2"
        assert native_saved["private"]["startup_loop_tx"] is True and native_saved["private"]["startup_tx_iterations"] == 27
        run(native_ledger, "--check-state", "--state-directory", native_state)
        route = json.loads(run(native_ledger, "--route-maintenance", "--state-directory", native_state))
        assert route["backend"] == "gpio" and route["route"] == "gpio4"
        for invalid in ("gpio19", "gpio20 --transmit", "GPIO20"):
            run(native_ledger, "--route-maintenance", "--state-directory", native_state,
                "--configure-route", invalid, success=False)
            assert configuration(native_state) == native_saved
        route = json.loads(run(native_ledger, "--route-maintenance", "--state-directory", native_state,
                               "--configure-route", "gpio20"))
        assert route["backend"] == "rp1-gpclk" and route["route"] == "gpio20" and route["transmit"] is False
        changed = configuration(native_state)
        expected = json.loads(json.dumps(native_saved))
        expected["configuration"]["Operation"]["Transmit Backend"] = "rp1-gpclk"
        expected["configuration"]["Operation"]["Transmit"] = False
        expected["configuration"]["GPIO"]["Transmit Pin"] = 20
        assert changed == expected
        native_saved = changed
        native_log = root / "native-backend.log"
        with native_log.open("w") as output:
            process = subprocess.Popen([fixture, str(native_ledger), "--state-directory", str(native_state),
                                        "--backend", "simulated", "--no-web"], env=environment, stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while "WSPR loop running" not in native_log.read_text():
                    assert process.poll() is None, native_log.read_text()
                    assert time.monotonic() < deadline, native_log.read_text()
                    time.sleep(0.05)
                assert configuration(native_state) == native_saved and not native_source.exists()
                online_route = json.loads(run(native_ledger, "--route-observation"))
                assert online_route["backend"] == "rp1-gpclk" and online_route["route"] == "gpio20"
                assert online_route["transmit"] is False
                run(native_ledger, "--route-maintenance", "--state-directory", native_state,
                    "--configure-route", "gpio4", success=False)
                assert configuration(native_state) == native_saved
                view = settings(native_ledger)
                assert view["role"] == "pi" and len(view["settings"]) == 10
                edited = json.loads(run(native_ledger, "--settings", "write", "--expected-revision", view["revision"],
                                        "--set", "CW.Fade In Ms=29", "--set", "CW.Fade Slice Ms=4",
                                        "--set", "Si5351.TX Output=CLK1", "--set", "Meta.debug_logging=false",
                                        "--set", "Experimental.Allow Unqualified Frequency=true"))
                assert edited["revision"] != view["revision"]
                for name, value in (("CW.Fade In Ms", 29), ("CW.Fade Slice Ms", 4), ("Si5351.TX Output", "CLK1"),
                                    ("Meta.debug_logging", False), ("Experimental.Allow Unqualified Frequency", True)):
                    assert edited["settings"][name]["saved"] == value and edited["settings"][name]["active"] == value
                native_saved = configuration(native_state)
                assert native_saved["private"]["startup_loop_tx"] is True and native_saved["private"]["startup_tx_iterations"] == 27
                assert native_saved["private"]["network_safety"] == "enforced"
                for assignment in ("CW.Fade In Ms=-1", "CW.Fade Slice Ms=0", "Si5351.TX Output=CLK3", "CW.Message=not allowed"):
                    run(native_ledger, "--settings", "write", "--expected-revision", edited["revision"], "--set", assignment, success=False)
                    assert configuration(native_state) == native_saved
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=10) == 0, native_log.read_text()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
        run(native_ledger, "--check-state", "--state-directory", native_state)
        with native_log.open("a") as output:
            process = subprocess.Popen([fixture, str(native_ledger), "--state-directory", str(native_state),
                                        "--backend", "simulated", "--no-web"], env=environment, stdout=output, stderr=subprocess.STDOUT)
            try:
                restarted = settings(native_ledger)
                assert restarted["settings"]["CW.Fade In Ms"]["active"] == 29
                assert restarted["settings"]["CW.Fade Slice Ms"]["active"] == 4
                assert restarted["settings"]["Si5351.TX Output"]["active"] == "CLK1"
                assert restarted["settings"]["Experimental.Allow Unqualified Frequency"]["active"] is True
                assert configuration(native_state) == native_saved and not native_source.exists()
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=10) == 0, native_log.read_text()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
    print("PASS: actual migration/startup, authenticated advanced CLI, atomic role-specific CAS, saved/active values, restart persistence and missing-state refusal")


if __name__ == "__main__":
    with backend_admission(sys.argv[1]):
        main()
