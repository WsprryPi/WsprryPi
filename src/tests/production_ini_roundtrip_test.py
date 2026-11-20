#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One-time migration, SQLite and split HTTP load; simulated-only, loopback."""
import argparse
import configparser
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import sys
import subprocess
import struct
import tempfile
import time
import urllib.request
import urllib.error

from production_browser_load import normal_browser_schedule
from software_web_service import start_software_web_service, stop_software_web_service


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--fixture', type=Path, required=True, help='Actual production main with typed private-storage and loopback-only seams')
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    fixture = args.fixture.resolve(strict=True)
    assert sys.platform == 'linux', 'Linux private-network regression required'
    backends = subprocess.check_output([str(binary), '--list-backends'], text=True, timeout=5).strip()
    assert set(backends.split(',')) == {'simulated'}, 'Build with BACKENDS=simulated'
    version = subprocess.check_output([str(binary), '--version'], text=True, stderr=subprocess.STDOUT, timeout=5)
    assert 'Ancillary GPIO: disabled' in version, 'Build with ANCILLARY_GPIO=0'
    names = {name for _, name in socket.if_nameindex()}
    # Linux can expose inactive tunnel templates even in a fresh namespace.
    templates = {'tunl0', 'gre0', 'gretap0', 'erspan0', 'ip_vti0', 'ip6_vti0', 'sit0', 'ip6tnl0', 'ip6gre0'}
    assert 'lo' in names and names <= templates | {'lo'}, 'Run in a private network namespace'
    # Query the current namespace through the socket, not a sysfs mount inherited
    # from the parent namespace. Constants are Linux SIOCGIFFLAGS/SIOCSIFFLAGS.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as control:
        def interface_flags(name):
            request = bytearray(256)
            encoded = name.encode('ascii'); request[:len(encoded)] = encoded
            fcntl.ioctl(control.fileno(), 0x8913, request)
            return struct.unpack_from('H', request, 16)[0], request
        for name in names - {'lo'}:
            flags, _ = interface_flags(name)
            assert not flags & 1, 'Non-loopback interface is up'
        flags, request = interface_flags('lo')
        if not flags & 1:
            struct.pack_into('H', request, 16, flags | 1)
            fcntl.ioctl(control.fileno(), 0x8914, request)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with tempfile.TemporaryDirectory(prefix='wsprrypi-ini-roundtrip-') as directory:
        root = Path(directory)
        with socket.socket() as web, socket.socket() as control:
            for sock in (web, control):
                for port in range(43000, 43100):
                    try: sock.bind(('127.0.0.1', port)); break
                    except OSError: continue
                else: raise RuntimeError('No free loopback test port below 49152')
            web_port, control_port = web.getsockname()[1], control.getsockname()[1]
        ini = root / 'candidate.ini'
        settings = configparser.ConfigParser(interpolation=None)
        settings.optionxform = str
        settings.read(Path(__file__).resolve().parents[2] / 'config/wsprrypi.ini')
        assert 'WTP' not in settings and 'WTP Server' not in settings
        settings.read_dict({
            'Operation': {'Mode': 'WSPR', 'Transmit': 'false', 'Use LED': 'false', 'Use Amp': 'false',
                          'Use Shutdown': 'false', 'Enable on Boot': 'Never', 'Transmit Backend': 'gpio',
                          'Web Port': str(web_port), 'Socket Port': str(control_port)},
            'WSPR': {'Call Sign': 'AA0NT', 'Grid Square': 'EM18'},
            'Calibration': {'PPM': '0'},
            'GPIO': {'Use System Clock Frequency Estimate': 'false', 'Frequency Residual PPM': '0', 'Manual PPM': '0'},
        })
        with ini.open('w') as output:
            settings.write(output)
        def get(path, port=web_port):
            with opener.open(f'http://127.0.0.1:{port}{path}', timeout=3) as response:
                assert response.status == 200, path
                data = response.read(1024 * 1024 + 1)
                assert len(data) <= 1024 * 1024, 'Bounded response required'
                return data
        template = ini.read_text()
        ini.chmod(0o600)
        state, ledger = root / 'state', root / 'native-ledger'
        environment = dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1')
        admitted = subprocess.run([str(fixture), str(ledger), '--migrate-state', '--state-directory', str(state), '--ini-source', str(ini)],
                                  capture_output=True, text=True, env=environment, timeout=30)
        assert admitted.returncode == 0, admitted.stdout + admitted.stderr
        retired = Path(str(ini) + '.sqlite-migrated')
        assert not ini.exists() and retired.read_text() == template
        database = state / 'generation-1/state.sqlite3'
        assert database.is_file()
        with (root / 'process.log').open('w') as log:
            child = subprocess.Popen([str(fixture), str(ledger), '--backend', 'simulated', '--state-directory', str(state),
                                      '--socket-loopback-only', '--socket-loopback-family', 'ipv4'],
                                     cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
            web_child = start_software_web_service([str(fixture), str(ledger)], ledger, log, environment)
            try:
                end = time.monotonic() + 20
                while True:
                    assert child.poll() is None, (root / 'process.log').read_text()[-4000:]
                    try:
                        config = json.loads(get('/config'))
                        break
                    except http.client.BadStatusLine as error:
                        raise AssertionError(f'Unexpected HTTP listener at web={web_port}, control={control_port}: '+(root / 'process.log').read_text()[-12000:]) from error
                    except OSError:
                        assert time.monotonic() < end, 'Loopback startup deadline: '+(root / 'process.log').read_text()[-4000:]
                        time.sleep(.05)
                assert 'WTP' not in config and 'WTP Server' not in config
                operation = config['Operation']
                assert operation['Transmit Backend'] == 'gpio', 'Simulation must not replace saved selection'
                assert operation['Transmit'] is False and operation['Enable on Boot'] == 'Never'
                assert all(operation[name] is False for name in ('Use LED', 'Use Amp', 'Use Shutdown'))
                assert operation['Web Port'] == web_port and operation['Socket Port'] == control_port
                # Actual host route/guard coverage: published reads never open
                # this configured external endpoint or mutate assignment state.
                fleet_path = '/api/control/v1/host/fleet'
                snapshot = json.loads(get(fleet_path + '/operations'))
                assert snapshot['maximum_outputs'] == 32 and snapshot['assignments'] == []
                assert len(snapshot['controller_incarnation']) == 32
                action_id = '1' * 32
                action = json.loads(get(fleet_path + '/actions?ids=' + action_id))
                assert action['actions'][action_id]['state'] == 'unknown_or_expired'
                for suffix in ('/operations?unknown=1', '/actions', '/actions?unknown=1',
                               '/actions?ids=' + action_id + '&ids=' + action_id,
                               '/actions?ids=' + action_id + '&',
                               '/actions?ids=' + action_id + ',' + action_id):
                    try:
                        get(fleet_path + suffix)
                        raise AssertionError('Malformed Fleet query accepted: ' + suffix)
                    except urllib.error.HTTPError as error:
                        assert 400 <= error.code < 500
                assert json.loads(get(fleet_path + '/operations'))['assignments'] == []
                # Check the database and WAL image after startup, not a retired
                # source file that the running owners must never consult.
                def state_image():
                    return {path.name: hashlib.sha256(path.read_bytes()).digest() for path in database.parent.iterdir()
                            if path.name in ('state.sqlite3', 'state.sqlite3-wal')}
                before = state_image()
                for seconds in (180, 300):
                    now, requests, events = [0.0], [], []
                    class Stop:
                        def is_set(self): return False
                        def wait(self, duration): now[0] += duration
                    def read(path):
                        get(path); requests.append(path); now[0] += .01
                    normal_browser_schedule(0, seconds, Stop(), read,
                                            lambda kind, value: events.append((kind, value)), clock=lambda: now[0])
                    assert len(requests) == 14 and requests.count('/api/wtp') == 8
                    assert len(events) == 16
                assert state_image() == before, 'Read-only load wrote SQLite state'
                assert not ini.exists() and retired.read_text() == template, 'Runtime consulted or recreated retired INI'
                assert json.loads(get('/config')) == config, 'Read-only load changed exposed settings'
            finally:
                stop_software_web_service(web_child)
                if child.poll() is None:
                    child.terminate()
                    try: child.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        child.kill(); child.wait(timeout=3); raise
                assert child.returncode == 0, (root / 'process.log').read_text()[-4000:]
        assert 'Transmit backend: simulated' in (root / 'process.log').read_text()
        invalid = {
            'lowercase-mode': template.replace('Mode = WSPR', 'mode = WSPR', 1),
            'lowercase-station-key': template.replace('Call Sign = ', 'call sign = ', 1),
            'default-cannot-supply-mode': '[DEFAULT]\nMode = WSPR\n\n' + template.replace('Mode = WSPR\n', '', 1),
        }
        for name, text in invalid.items():
            assert text != template
            bad = root / (name + '.ini'); bad.write_text(text)
            rejected_hash = hashlib.sha256(bad.read_bytes()).digest()
            record = root / (name + '.log')
            bad.chmod(0o600)
            result = subprocess.run([str(fixture), str(root / (name + '-ledger')), '--migrate-state',
                                     '--state-directory', str(root / (name + '-state')), '--ini-source', str(bad)],
                                    cwd=root, capture_output=True, text=True, env=environment, timeout=30)
            record.write_text(result.stdout + result.stderr)
            assert result.returncode != 0, 'Invalid source admitted for migration: ' + name
            assert hashlib.sha256(bad.read_bytes()).digest() == rejected_hash, 'Rejected migration source changed'
            assert not (root / (name + '-ledger.simulated-trace.json')).exists(), 'Migration constructed an output backend'
            print('PASS current candidate rejects ' + name + ' without enabling output')

    print('PASS one-time source migration, SQLite-only saved state, split web service, and both finite browser-load profiles; loopback, simulated-only')


if __name__ == '__main__':
    main()
