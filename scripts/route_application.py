#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixed-path companion for DKMS route restoration; no hardware operations.

Only the canonical installed service/configuration is managed. INI edits retain
all bytes except the explicit backend, route, and transmit values. No service or
module is started here.
"""
import configparser
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

CONFIG = Path('/usr/local/etc/wsprrypi.ini')
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


def configuration(data, require_runtime_backend=False):
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str
    # IniFile appends another section header when persisting newly added keys.
    # Coalesce sections only in the parsing view; edit() retains the original
    # bytes. Strict parsing still rejects duplicate keys across repeated blocks.
    sections = {None: []}
    section = None
    for line in data.decode('utf-8').splitlines():
        header = re.fullmatch(r'\s*\[([^\]]+)\]\s*', line)
        if header:
            section = header[1]
            sections.setdefault(section, [])
        else:
            sections[section].append(line)
    normalized = []
    for name, lines in sections.items():
        if name is not None:
            normalized.append('[' + name + ']')
        normalized.extend(lines)
    parser.read_string('\n'.join(normalized))
    if parser.defaults():
        raise ValueError('inherited configuration is not supported')
    backend = parser.get('Operation', 'Transmit Backend').lower()
    if backend not in ('gpio', 'rp1-gpclk', 'si5351'):
        raise ValueError('installed configuration selects an unsupported transmit backend')
    if require_runtime_backend and backend != 'rp1-gpclk':
        raise ValueError('route configuration did not select rp1-gpclk')
    if parser.get('Operation', 'Mode').upper() not in ('WSPR', 'QRSS', 'FSKCW', 'DFCW'):
        raise ValueError('managed idle restoration requires a scheduled application mode')
    parser.getboolean('Operation', 'Transmit')
    if parser.get('Operation', 'Enable on Boot').lower() not in ('never', 'follow', 'always'):
        raise ValueError('invalid boot policy')
    if parser.getint('GPIO', 'Transmit Pin') not in (4, 20):
        raise ValueError('invalid saved route')
    return parser


def edit(data, route):
    configuration(data)
    if route not in ('gpio4', 'gpio20'):
        raise ValueError('invalid route')
    replacements = {
        ('Operation', 'Transmit Backend'): 'rp1-gpclk',
        ('GPIO', 'Transmit Pin'): route[4:],
        ('Operation', 'Transmit'): 'False',
    }
    section = None
    seen = set()
    lines = []
    for line in data.decode('utf-8').splitlines(keepends=True):
        header = re.fullmatch(r'\s*\[([^\]]+)\]\s*', line)
        if header:
            section = header[1]
        match = re.fullmatch(r'([^=\r\n]+?\s*=\s*)([^\r\n]*)(\r?\n)?', line)
        if match:
            key = (section, match[1].split('=', 1)[0].strip())
            if key in replacements:
                if key in seen:
                    raise ValueError('duplicate route configuration')
                seen.add(key)
                line = match[1] + replacements[key] + (match[3] or '')
        lines.append(line)
    if seen != set(replacements):
        raise ValueError('explicit route and transmit assignments required')
    result = ''.join(lines).encode('utf-8')
    configuration(result, require_runtime_backend=True)
    return result


def validate_service_arguments(arguments):
    """Accept only the fixed installed launch and its transient confirmation."""
    prefix = '/usr/local/bin/wsprrypi -J -i ' + str(CONFIG)
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


def inspect_service():
    output = subprocess.check_output(['/usr/bin/systemctl', 'show', 'wsprrypi.service',
        '--property=ExecStart', '--value'], text=True, timeout=10)
    # systemd's fixed argv[] representation; reject alternate configs, command
    # chains and CLI output overrides instead of guessing which file to edit.
    match = re.fullmatch(r'\{ path=/usr/local/bin/wsprrypi ; argv\[\]=([^;]+) ; [^{}]*\}\s*', output)
    if not match:
        raise ValueError('service is missing, masked, or uses an unsupported command; preserve it and repair installation')
    validate_service_arguments(match[1])
    binary, _ = trusted(BINARY)
    if b'WSPRRYPI_ROUTE_RESTORE_IDLE' not in binary:
        raise ValueError('installed application lacks idle restoration support')


def configure(route):
    data, info = trusted(CONFIG)
    result = edit(data, route)
    if result == data:
        return
    fd, name = tempfile.mkstemp(prefix='.route-', dir=CONFIG.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            os.fchmod(stream.fileno(), stat.S_IMODE(info.st_mode))
            os.fchown(stream.fileno(), info.st_uid, info.st_gid)
            stream.write(result)
            stream.flush()
            os.fsync(stream.fileno())
        if trusted(CONFIG)[0] != data:
            raise ValueError('configuration changed during route update')
        os.replace(name, CONFIG)
        directory = os.open(CONFIG.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def main(args):
    if os.geteuid() != 0:
        raise ValueError('root required')
    if args == ['inspect']:
        inspect_service()
    elif args == ['inspect-stopped']:
        if b'WSPRRYPI_ROUTE_RESTORE_IDLE' not in trusted(BINARY)[0]:
            raise ValueError('installed application lacks idle restoration support')
    elif len(args) == 2 and args[0] == 'configure':
        configure(args[1])
    else:
        raise ValueError('usage: route_application.py inspect | configure gpio4|gpio20')
    parser = configuration(trusted(CONFIG)[0])
    print(json.dumps({'contract': CONTRACT, 'route': 'gpio'+str(parser.getint('GPIO', 'Transmit Pin')),
                      'transmit': parser.getboolean('Operation', 'Transmit'), 'config': str(CONFIG)}))


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (OSError, ValueError, configparser.Error, subprocess.SubprocessError) as error:
        sys.exit(str(error))
