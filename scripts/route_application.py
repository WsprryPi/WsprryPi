#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixed-path RP1 maintenance companion; backend-owned state, no INI access.

The application admits the existing stopped-backend route operation and commits
through its native owner. This helper never starts a service or opens SQLite.
"""
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

STATE = Path('/var/lib/wsprrypi/application-state')
BINARY = Path('/usr/local/bin/wsprrypi')
CONTRACT = 'wsprrypi-route-application-v1'


def trusted(path):
    for parent in reversed(path.parents):
        info = parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid or info.st_mode & 0o022:
            raise ValueError('untrusted parent: ' + str(parent))
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid or info.st_mode & 0o022:
            raise ValueError('untrusted file: ' + str(path))
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(64 * 1024 * 1024 + 1)
        if len(data) > 64 * 1024 * 1024:
            raise ValueError('file too large')
        return data, info
    finally:
        os.close(fd)


def validate_service_arguments(arguments):
    """Accept only the fixed installed launch and its transient confirmation."""
    prefix = '/usr/local/bin/wsprrypi -J --state-directory ' + str(STATE)
    if len(arguments) > 8192 or not arguments.startswith(prefix):
        raise ValueError('unsupported installed command')
    remaining = arguments[len(prefix):]
    if remaining and not remaining.startswith(' '):
        raise ValueError('alternate configuration path')
    seen = set()

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate confirmation field')
            result[key] = value
        return result

    while remaining.strip():
        remaining = remaining.lstrip()
        option, _, tail = remaining.partition(' ')
        if option in seen:
            raise ValueError('duplicate installed option')
        seen.add(option)
        if option == '--no-web':
            remaining = tail
            continue
        if option != '--rp1-development-confirmation-json' or not tail:
            raise ValueError('unsupported installed option')
        confirmation, end = json.JSONDecoder(object_pairs_hook=unique_object).raw_decode(tail.lstrip())
        flags = {'enabled', 'physical_connection_confirmed',
                 'attenuation_and_load_confirmed', 'bounded_operation_confirmed',
                 'non_radiating_topology_confirmed', 'experimental_status_acknowledged'}
        if (not isinstance(confirmation, dict) or
                set(confirmation) != flags | {'route', 'operation_id'} or
                any(confirmation[field] is not True for field in flags) or
                confirmation['route'] not in ('GPIO4', 'GPIO20') or
                not isinstance(confirmation['operation_id'], str) or
                not re.fullmatch(r'[A-Za-z0-9._-]{8,64}', confirmation['operation_id'])):
            raise ValueError('invalid installed RP1 confirmation')
        remaining = tail.lstrip()[end:]
        if remaining and not remaining.startswith(' '):
            raise ValueError('ambiguous installed confirmation argument')


def inspect_service_command():
    output = subprocess.check_output(['/usr/bin/systemctl', 'show', 'wsprrypi.service',
        '--property=ExecStart', '--value'], text=True, timeout=10)
    # systemd's fixed argv[] representation; reject alternate configs, command
    # chains and CLI output overrides instead of guessing which file to edit.
    match = re.fullmatch(r'\{ path=/usr/local/bin/wsprrypi ; argv\[\]=([^;]+) ; [^{}]*\}\s*', output)
    if not match:
        raise ValueError('service is missing, masked, or uses an unsupported command; preserve it and repair installation')
    validate_service_arguments(match[1])


def inspect_service():
    inspect_service_command()
    binary, _ = trusted(BINARY)
    if b'WSPRRYPI_ROUTE_RESTORE_IDLE' not in binary:
        raise ValueError('installed application lacks idle restoration support')



def state(route=None):
    if route is not None and route not in ('gpio4', 'gpio20'):
        raise ValueError('invalid route')
    binary, unused = trusted(BINARY)
    if b'--route-maintenance' not in binary:
        raise ValueError('installed application lacks saved-state route maintenance')
    arguments = [str(BINARY), '--route-maintenance', '--state-directory', str(STATE)]
    if route is not None:
        arguments += ['--configure-route', route]
        result = subprocess.run(arguments, stdin=subprocess.DEVNULL, capture_output=True, timeout=30, check=True)
    else:
        result = subprocess.run([str(BINARY), '--route-observation'], stdin=subprocess.DEVNULL,
                                capture_output=True, timeout=10, check=False)
        if result.returncode == 3:
            # Only explicit backend unavailability selects stopped maintenance.
            # Its exclusive backend admission still refuses a startup race.
            result = subprocess.run(arguments, stdin=subprocess.DEVNULL, capture_output=True, timeout=30, check=True)
        else:
            result.check_returncode()
    if len(result.stdout) > 4096:
        raise ValueError('route state exceeds its limit')
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError('duplicate route state field')
            value[key] = item
        return value
    value = json.loads(result.stdout, object_pairs_hook=unique)
    if (not isinstance(value, dict) or set(value) != {'contract', 'backend', 'route', 'transmit', 'config'} or
            value['contract'] != CONTRACT or value['backend'] not in ('gpio', 'rp1-gpclk', 'si5351') or
            value['route'] not in (None, 'gpio4', 'gpio20') or type(value['transmit']) is not bool or
            not isinstance(value['config'], str) or
            not re.fullmatch(re.escape(str(STATE)) + r'/generation-[1-9][0-9]{0,19}/configuration', value['config'])):
        raise ValueError('invalid backend route state')
    if route is not None and (value['backend'] != 'rp1-gpclk' or value['route'] != route or value['transmit']):
        raise ValueError('route maintenance outcome is unconfirmed')
    return value


def configure(route):
    return state(route)


def main(args):
    if os.geteuid() != 0:
        raise ValueError('root required')
    if args == ['inspect']:
        inspect_service()
        value = state()
    elif args == ['inspect-stopped']:
        if b'WSPRRYPI_ROUTE_RESTORE_IDLE' not in trusted(BINARY)[0]:
            raise ValueError('installed application lacks idle restoration support')
        value = state()
    elif len(args) == 2 and args[0] == 'configure':
        value = configure(args[1])
    else:
        raise ValueError('usage: route_application.py inspect | inspect-stopped | configure gpio4|gpio20')
    print(json.dumps(value))


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
