#!/usr/bin/env python3
"""Actual no-output controller entry points with a typed private owner namespace.

The OS CSPRNG and production persistent identity provider remain selected. No
interactive home, service, physical device, or transmitter peer is accessed.
"""
import http.client
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import sqlite3


def configuration(state):
    uri = (state / 'generation-1/state.sqlite3').as_uri() + '?mode=ro'
    with sqlite3.connect(uri, uri=True) as connection:
        return json.loads(connection.execute("SELECT payload FROM records WHERE kind='configuration'").fetchone()[0])


def main():
    if sys.platform != 'darwin':
        print('SKIP native persistent controller identity runtime: Linux retains its existing OS identity; macOS runner required')
        return
    fixture = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='wsprry-native-id-', dir='/tmp') as temporary:
        root = Path(temporary).resolve()
        base = [fixture, str(root / 'ledger')]
        state, runtime = root / 'state', root / 'ipc'
        identity = root / '.wsprrypi-controller-identity/authority.json'
        outputs, processes = [], []

        def command(arguments, expected):
            result = subprocess.run(base + arguments, capture_output=True, text=True, timeout=15)
            assert result.returncode == expected, (arguments, result.returncode, result.stdout, result.stderr)
            return result

        def stop(process):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    raise

        def start(name, arguments):
            output = (root / (name + '.log')).open('w')
            outputs.append(output)
            process = subprocess.Popen(base + arguments, stdout=output, stderr=subprocess.STDOUT)
            processes.append(process)
            return process

        def free_ports():
            # The controller listener validator requires 1024..49151. Reserve
            # both temporary test ports together, then let production bind them.
            sockets = []
            try:
                ports = []
                for port in range(22000, 23000):
                    candidate = socket.socket()
                    try:
                        candidate.bind(('127.0.0.1', port))
                    except OSError:
                        candidate.close()
                        continue
                    sockets.append(candidate)
                    ports.append(port)
                    if len(ports) == 2:
                        return ports
                raise AssertionError('No pair of free controller test ports')
            finally:
                for candidate in sockets:
                    candidate.close()

        try:
            initialize = ['--initialize-state', '--controller-only', '--state-directory', str(state)]
            check = ['--check-state', '--controller-only', '--state-directory', str(state)]
            command(initialize, 0)
            original = identity.read_bytes()
            stored = json.loads(original)
            assert set(stored) == {'schema', 'device_id'} and stored['schema'] == 'controller-identity/1'
            assert re.fullmatch('[0-9a-f]{32}', stored['device_id'])
            command(check, 0)
            assert identity.read_bytes() == original
            for cycle in range(2):
                port, websocket = free_ports()
                backend = start(f'backend-{cycle}', ['--controller-only', '--state-directory', str(state),
                    '--runtime-directory', str(runtime), '--web-port', str(port), '--socket-port', str(websocket),
                    '--socket-loopback-only'])
                web = start(f'web-{cycle}', ['--web-service', '--runtime-directory', str(runtime),
                    '--listener-cache', str(root / 'listeners.json')])
                deadline = time.monotonic() + 20
                while True:
                    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=1)
                    try:
                        connection.request('GET', '/status')
                        response = connection.getresponse()
                        status = json.loads(response.read())
                        if response.status == 200 and status.get('controller_ready'):
                            break
                    except (OSError, ValueError, http.client.HTTPException):
                        pass
                    finally:
                        connection.close()
                    assert backend.poll() is None, (root / f'backend-{cycle}.log').read_text()
                    assert web.poll() is None, (root / f'web-{cycle}.log').read_text()
                    assert time.monotonic() < deadline, 'Native identity controller readiness'
                    time.sleep(.05)
                assert status['device_id'] == stored['device_id'] and status['local_output'] is None
                competing = root / f'competing-{cycle}'
                command(['--initialize-state', '--controller-only', '--state-directory', str(competing)], 2)
                assert not competing.exists()
                if cycle == 0:
                    before = configuration(state)
                    view = json.loads(command(['--settings', 'read', '--runtime-directory', str(runtime)], 0).stdout)
                    value = not view['settings']['Meta.debug_logging']['saved']
                    identity.unlink()
                    write = subprocess.run(base + ['--settings', 'write', '--runtime-directory', str(runtime), '--expected-revision', view['revision'],
                        '--set', 'Meta.debug_logging=' + str(value).lower()], capture_output=True, text=True, timeout=10)
                    assert write.returncode in (2, 3), 'Lost identity admitted a local settings mutation'
                    assert backend.wait(timeout=10) != 0, 'Identity loss must stop controller admission'
                    assert not identity.exists(), 'Identity loss must not provision a replacement'
                    assert configuration(state) == before, 'Identity-loss refusal changed saved settings'
                    command(check, 2)
                    command(initialize, 2)
                    assert not identity.exists()
                    # Test-owned recovery only: restore the exact saved record,
                    # then require a new explicit backend process and same ID.
                    identity.write_bytes(original)
                    identity.chmod(0o600)
                    command(check, 0)
                stop(web)
                stop(backend)
            print('PASS production native CSPRNG identity initialization, stable restart, shared-owner exclusion, identity-loss shutdown, no regeneration, and exact-record recovery')
        finally:
            if sys.exc_info()[0]:
                for output in outputs:
                    output.flush()
                for log in root.glob('*.log'):
                    print(log.name, log.read_text(), file=sys.stderr)
            for process in reversed(processes):
                stop(process)
            for output in outputs:
                output.close()


if __name__ == '__main__':
    main()
