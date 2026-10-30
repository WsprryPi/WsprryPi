#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Current application INI and finite HTTP-load checks; simulated-only, loopback."""
import argparse
import configparser
import fcntl
import hashlib
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

from production_browser_load import normal_browser_schedule


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    binary = parser.parse_args().binary.resolve(strict=True)
    assert sys.platform == 'linux', 'Linux private-network regression required'
    backends = subprocess.check_output([str(binary), '--list-backends'], text=True, timeout=5).strip()
    assert set(backends.split(',')) == {'simulated', 'wtp'}, 'Build with BACKENDS=simulated'
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
        settings.read_dict({
            'Operation': {'Mode': 'WSPR', 'Transmit': 'false', 'Use LED': 'false', 'Use Amp': 'false',
                          'Use Shutdown': 'false', 'Enable on Boot': 'Never', 'Transmit Backend': 'wtp',
                          'Web Port': str(web_port), 'Socket Port': str(control_port)},
            'WTP': {'Transport': 'network', 'Hostname': 'pico-loopback.local',
                    'TLS Server Identity': 'pico-loopback.local', 'Device ID': 'a' * 32,
                    'TCP Port': '18443', 'Endpoint': '', 'Allow Frequency Adjustment': 'true',
                    'Start Uncertainty ns': '500000000'},
            'WTP Server': {'Enabled': 'false'},
            'Calibration': {'PPM': '0'},
            'GPIO': {'Use System Clock Frequency Estimate': 'false', 'Frequency Residual PPM': '0', 'Manual PPM': '0'},
        })
        certificate, key = root / 'certificate.pem', root / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-config', '/dev/null', '-subj', '/CN=pico-loopback.local', '-keyout', str(key), '-out', str(certificate)],
                       check=True, timeout=30, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        settings['WTP'].update({'TLS CA File': str(certificate), 'TLS Client Certificate': str(certificate),
                                'TLS Client Key': str(key)})
        with ini.open('w') as output:
            settings.write(output)
        def get(path, port=web_port):
            with opener.open(f'http://127.0.0.1:{port}{path}', timeout=3) as response:
                assert response.status == 200, path
                data = response.read(1024 * 1024 + 1)
                assert len(data) <= 1024 * 1024, 'Bounded response required'
                return data
        with (root / 'process.log').open('w') as log:
            child = subprocess.Popen([str(binary), '--backend', 'simulated', '-i', str(ini),
                                      '--socket-loopback-only', '--socket-loopback-family', 'ipv4'],
                                     cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
            try:
                end = time.monotonic() + 20
                while True:
                    assert child.poll() is None, (root / 'process.log').read_text()[-4000:]
                    try:
                        config = json.loads(get('/config'))
                        break
                    except OSError:
                        assert time.monotonic() < end, 'Loopback startup deadline: '+(root / 'process.log').read_text()[-4000:]
                        time.sleep(.05)
                wtp, operation = config['WTP'], config['Operation']
                assert wtp['Device ID'] == 'a' * 32 and wtp['Transport'] == 'network'
                assert wtp['Hostname'] == wtp['TLS Server Identity'] == 'pico-loopback.local'
                assert wtp['TCP Port'] == 18443 and wtp['Endpoint'] == ''
                assert wtp['Start Uncertainty ns'] == 500000000 and wtp['Allow Frequency Adjustment'] is True
                assert operation['Transmit Backend'] == 'wtp', 'Simulation must not replace saved selection'
                assert operation['Transmit'] is False and operation['Enable on Boot'] == 'Never'
                assert all(operation[name] is False for name in ('Use LED', 'Use Amp', 'Use Shutdown'))
                assert operation['Web Port'] == web_port and operation['Socket Port'] == control_port
                before = hashlib.sha256(ini.read_bytes()).digest()
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
                assert hashlib.sha256(ini.read_bytes()).digest() == before, 'Read-only load changed saved INI'
                assert json.loads(get('/config')) == config, 'Read-only load changed exposed settings'
            finally:
                if child.poll() is None:
                    child.terminate()
                    try: child.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        child.kill(); child.wait(timeout=3); raise
                assert child.returncode == 0, (root / 'process.log').read_text()[-4000:]
        assert 'Transmit backend: simulated' in (root / 'process.log').read_text()
        template = ini.read_text()
        invalid = {
            'lowercase-mode': template.replace('Mode = WSPR', 'mode = WSPR', 1),
            'lowercase-device-key': template.replace('Device ID = ', 'device id = ', 1),
            'default-cannot-supply-mode': '[DEFAULT]\nMode = WSPR\n\n' + template.replace('Mode = WSPR\n', '', 1),
        }
        for name, text in invalid.items():
            assert text != template
            bad = root / (name + '.ini'); bad.write_text(text)
            record = root / (name + '.log')
            with record.open('w') as output:
                child = subprocess.Popen([str(binary), '--backend', 'simulated', '-i', str(bad),
                                          '--socket-loopback-only', '--socket-loopback-family', 'ipv4'],
                                         cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                         env=dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
                try:
                    end = time.monotonic() + 15
                    while 'Configuration load failed:' not in record.read_text():
                        assert child.poll() is None, record.read_text()[-4000:]
                        assert time.monotonic() < end, record.read_text()[-4000:]
                        time.sleep(.05)
                    while True:
                        assert child.poll() is None, record.read_text()[-4000:]
                        try:
                            safe = json.loads(get('/config', 31415))
                            break
                        except OSError:
                            assert time.monotonic() < end, record.read_text()[-4000:]
                            time.sleep(.05)
                    assert safe['Operation']['Transmit'] is False, 'Invalid INI enabled output'
                finally:
                    if child.poll() is None:
                        child.terminate()
                        try: child.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            child.kill(); child.wait(timeout=3); raise
                    assert child.returncode == 0, record.read_text()[-4000:]
            print('PASS current candidate rejects ' + name + ' without enabling output')

    print('PASS current candidate INI round-trip and both finite browser-load profiles; loopback, simulated-only')


if __name__ == '__main__':
    main()
