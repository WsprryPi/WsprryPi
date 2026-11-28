#!/usr/bin/env python3
"""Finite production-controller gate in an explicitly isolated native Linux host.

Run as an ordinary account inside a private mount/network namespace with its OS
home replaced by an empty task-owned directory and /run hidden. No fixture
identity, hardware suppression, installed service, or transmitter peer is used.
"""
import argparse
import errno
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import platform
import pwd
import re
import signal
import socket
import struct
import subprocess
import time


SIOCGIFFLAGS = 0x8913
IFF_UP = 0x1


def require_loopback_only():
    # Linux can expose default tunnel interfaces in a fresh namespace. Require
    # every non-loopback interface to be down, even if it has no address.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        active = set()
        for _, name in socket.if_nameindex():
            interface = struct.pack('16s24x', os.fsencode(name))
            flags = struct.unpack_from('H', fcntl.ioctl(probe, SIOCGIFFLAGS, interface), 16)[0]
            if flags & IFF_UP:
                active.add(name)
    assert active == {'lo'}, f'An isolated loopback-only namespace is required: {sorted(active)}'


def occupy_listener(port, prefer_ipv6):
    if prefer_ipv6:
        try:
            return socket.create_server(('::1', port), family=socket.AF_INET6)
        except OSError as error:
            if error.errno not in (errno.EAFNOSUPPORT, errno.EPROTONOSUPPORT, errno.EADDRNOTAVAIL):
                raise
    return socket.create_server(('127.0.0.1', port))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--strace', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--host-network-namespace', required=True)
    args = parser.parse_args()
    binary, tracer = args.binary.resolve(strict=True), args.strace.resolve(strict=True)
    assert platform.system() == 'Linux' and platform.machine() in ('x86_64', 'aarch64')
    assert os.geteuid() != 0, 'The application gate must run unprivileged'
    credentials = dict(line.split(':', 1) for line in Path('/proc/self/status').read_text().splitlines() if ':' in line)
    assert int(credentials['CapEff'].strip(), 16) == 0 and credentials['NoNewPrivs'].strip() == '1'
    require_loopback_only()
    # PID 1's namespace link is privileged on the native host. The launcher
    # verifies the separation before dropping privileges and supplies that ID.
    assert re.fullmatch(r'net:\[\d+\]', args.host_network_namespace)
    assert os.readlink('/proc/self/ns/net') != args.host_network_namespace
    home = Path(pwd.getpwuid(os.geteuid()).pw_dir)
    assert (home / '.issue475-private-home').read_text() == 'controller qualification\n'
    assert not Path('/run/dbus/system_bus_socket').exists(), 'Host service buses must be hidden'
    assert not any(key.startswith('WSPRRYPI_') for key in os.environ), 'No fixture/suppression environment is permitted'
    root = args.evidence.resolve()
    root.mkdir(mode=0o700, parents=True, exist_ok=False)
    os.umask(0o077)
    os.chdir(root)
    traces = []

    def traced(name, arguments):
        trace = root / (name + '.strace')
        traces.append(trace)
        return [str(tracer), '-f', '--kill-on-exit', '-qq', '-e',
                'trace=%file,%network,%process,sched_setscheduler,sched_setparam,sched_setattr',
                '-o', str(trace), str(binary), *arguments]

    def command(name, arguments, expected):
        result = subprocess.run(traced(name, arguments), capture_output=True, text=True, timeout=30)
        (root / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == expected, (name, result.returncode, result.stdout, result.stderr)
        return result.stdout

    assert 'Build profile: controller' in command('version', ['--version'], 0)
    assert 'No station fields' in command('help', ['--help'], 0)
    assert command('backends', ['--list-backends'], 0) == '\n'
    command('missing-role', [], 2)
    command('local-role', ['--backend', 'simulated'], 2)
    command('local-worker', ['--maintenance-worker', '--role', 'pi'], 2)
    state, runtime = root / 'state', root / 'runtime'
    command('initialize', ['--initialize-state', '--controller-only', '--state-directory', str(state)], 0)
    command('validate', ['--check-state', '--controller-only', '--state-directory', str(state)], 0)
    command('local-backend-refusal', ['--controller-only', '--state-directory', str(state),
                                    '--backend', 'simulated'], 1)

    def request(method, path, payload=None):
        connection = http.client.HTTPConnection('127.0.0.1', 22000, timeout=2)
        try:
            connection.request(method, path, body=payload,
                               headers={'Content-Type': 'application/json'} if payload else {})
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def start(name, arguments):
        output = (root / (name + '.log')).open('w')
        process = subprocess.Popen(traced(name, arguments), stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        return process, output

    def stop(owner):
        process, output = owner
        try:
            # Signal the actual production process; strace is only its observer.
            children = Path(f'/proc/{process.pid}/task/{process.pid}/children')
            for child in children.read_text().split() if children.exists() else []:
                if Path(f'/proc/{child}/exe').resolve() == binary:
                    os.kill(int(child), signal.SIGTERM)
            assert process.wait(timeout=20) == 0, 'Production shutdown failed'
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            output.close()

    identity = None
    cache = root / 'listeners.json'
    web_arguments = ['--web-service', '--runtime-directory', str(runtime), '--listener-cache', str(cache)]
    for cycle in range(2):
        backend = start(f'backend-{cycle}', ['--controller-only', '--state-directory', str(state),
                        '--runtime-directory', str(runtime), '--socket-loopback-only',
                        '--web-port', '22000', '--socket-port', '22001'])
        web = None
        try:
            if cycle == 0:
                for listener, port in (('http', 22000), ('websocket', 22001)):
                    case = f'{listener}-port-conflict'
                    with occupy_listener(port, listener == 'websocket'):
                        command(case, web_arguments, 2)
                        assert 'Browser service failed' in (root / f'{case}.log').read_text()
                        assert any('bind(' in line and 'EADDRINUSE' in line
                                   for line in (root / f'{case}.strace').read_text().splitlines()), case
                        assert not cache.exists(), f'{listener} conflict published a listener cache'
                        assert backend[0].poll() is None, f'{listener} conflict stopped the backend'
            web = start(f'web-{cycle}', web_arguments)
            deadline = time.monotonic() + 30
            while True:
                assert backend[0].poll() is None and web[0].poll() is None, 'Production process exited before readiness'
                try:
                    code, status = request('GET', '/status')
                    if code == 200 and status.get('controller_ready'):
                        break
                except (OSError, ValueError, http.client.HTTPException):
                    pass
                assert time.monotonic() < deadline, 'Controller readiness deadline'
                time.sleep(0.1)
            competing = root / f'competing-state-{cycle}'
            command(f'competing-initialize-{cycle}', ['--initialize-state', '--controller-only',
                    '--state-directory', str(competing)], 2)
            assert not competing.exists(), 'Second state root bypassed installation ownership'
            assert status['role'] == 'controller-only'
            assert status['local_output'] is None and status['local_transmission_available'] is False
            assert re.fullmatch('[0-9a-f]{32}', status['device_id'])
            if identity is not None:
                assert status['device_id'] == identity, 'Restart changed the controller identity'
            identity = status['device_id']
            code, version = request('GET', '/version')
            assert code == 200 and 'controller host' in version['wspr_version']
            assert version['compiled_backends'] == '' and version['ancillary_gpio'] is False
            code, devices = request('GET', '/api/control/v1/host/devices')
            assert code == 200 and devices['profiles'] == [], 'Empty enrollment must be ready'
            for method, path in [('GET', '/config'), ('POST', '/control/stop'),
                                 ('POST', '/api/control/v1/host/wtp-endpoint/enable'),
                                 ('POST', '/api/rp1-gpclk-route')]:
                code, response = request(method, path, '{}')
                assert code == 409 and response['error']['code'] == 'controller_only', (path, code, response)
            (root / f'status-{cycle}.json').write_text(json.dumps(status, indent=2) + '\n')
            (root / f'version-{cycle}.json').write_text(json.dumps(version, indent=2) + '\n')
        finally:
            try:
                if web is not None:
                    stop(web)
            finally:
                stop(backend)
    forbidden = re.compile(r'/dev/(?:mem|gpiomem[^" ]*|vcio|i2c-[^" ]*|gpiochip[^" ]*|pio[0-9]+|rp1[^" ]*)|'
                           r'/proc/device-tree|/sys/firmware/devicetree|/proc/cpuinfo|'
                           r'sched_(?:setscheduler|setparam|setattr)\(')
    for trace in traces:
        lines = trace.read_text().splitlines()
        matches = [line for line in lines if forbidden.search(line)]
        assert not matches, (trace, matches)
        foreign_exec = [line for line in lines if re.search(r'\bexecve(?:at)?\(', line)
                        and f'execve("{binary}",' not in line]
        assert not foreign_exec, (trace, foreign_exec)
    require_loopback_only()
    result = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'platform': platform.platform(), 'uid': os.geteuid(), 'gid': os.getegid(),
              'effective_capabilities': credentials['CapEff'].strip(), 'no_new_privileges': True,
              'device_id': identity,
              'cycles': 2, 'trace_files': [path.name for path in traces],
              'result': 'PASS', 'scope': 'native unprivileged software runtime; no installation or physical qualification'}
    (root / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
