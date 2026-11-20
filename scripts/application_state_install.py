#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Admit saved application state through the stopped backend's explicit commands."""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate listener bootstrap field')
        result[key] = value
    return result


def listener_configuration(data):
    if len(data) > 4096:
        raise ValueError('Listener bootstrap exceeds its limit')
    value = json.loads(data, object_pairs_hook=unique)
    if (not isinstance(value, dict) or
            set(value) != {'schema', 'device_id', 'role', 'web_port', 'socket_port'} or
            value['schema'] != 'listener-bootstrap/1' or value['role'] not in ('pi', 'controller') or
            not isinstance(value['device_id'], str) or not re.fullmatch('[0-9a-f]{32}', value['device_id']) or
            any(type(value[key]) is not int or not 1024 <= value[key] <= 49151
                for key in ('web_port', 'socket_port')) or
            value['web_port'] == value['socket_port']):
        raise ValueError('Invalid listener bootstrap')
    return value


def read_listeners(path, owner=0):
    path = Path(path)
    for parent in reversed(path.parents):
        info = parent.lstat()
        # Fixture roots may live below a system-owned sticky temporary directory.
        sticky = info.st_uid == 0 and bool(info.st_mode & stat.S_ISVTX)
        if not stat.S_ISDIR(info.st_mode) or (info.st_mode & 0o022 and not sticky) or info.st_uid not in (0, owner):
            raise ValueError('Untrusted listener bootstrap parent')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != owner or info.st_mode & 0o022 or info.st_nlink != 1:
            raise ValueError('Untrusted listener bootstrap')
        return listener_configuration(os.read(fd, 4097))
    finally:
        os.close(fd)


def publish_listeners(path, data):
    value = listener_configuration(data)
    path = Path(path)
    if path.exists() or path.is_symlink():
        read_listeners(path, os.geteuid())
    descriptor, temporary = tempfile.mkstemp(prefix='.wsprrypi-listeners-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            os.fchmod(output.fileno(), 0o644)
            output.write((json.dumps(value, sort_keys=True) + '\n').encode())
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        parent = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def admit(binary, state, source, listeners, controller=False, run=subprocess.run):
    """Never create the state directory here or infer fresh state from a missing DB."""
    binary, state, source, listeners = map(Path, (binary, state, source, listeners))
    if any(not path.is_absolute() for path in (binary, state, source, listeners)):
        raise ValueError('Installation paths must be absolute')

    def command(*arguments):
        result = run([str(binary), *map(str, arguments)], check=True, stdin=subprocess.DEVNULL,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
        return result.stdout

    if command('--storage-format').strip() != 'application-state/1':
        raise ValueError('Selected executable does not support application-state/1')
    if not state.parent.exists():
        state.parent.mkdir(mode=0o700)
        descriptor = os.open(state.parent.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    role = ['--controller-only'] if controller else []
    if state.exists() or state.is_symlink():
        # Includes incomplete and damaged established roots. Their own admission
        # machinery must refuse or recover them; never import a new source here.
        command('--check-state', '--state-directory', state, *role)
    elif source.exists() or source.is_symlink():
        command('--migrate-state', '--state-directory', state, '--ini-source', source, *role)
    else:
        command('--initialize-state', '--state-directory', state, *role)
    data = command('--check-state', '--state-directory', state, *role, '--listener-bootstrap')
    publish_listeners(listeners, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--read-listeners', type=Path)
    parser.add_argument('--role-only', action='store_true')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--state-directory', default=Path('/var/lib/wsprrypi/application-state'), type=Path)
    parser.add_argument('--ini-source', default=Path('/usr/local/etc/wsprrypi.ini'), type=Path)
    parser.add_argument('--listeners', default=Path('/usr/local/etc/wsprrypi-listeners.json'), type=Path)
    parser.add_argument('--controller-only', action='store_true')
    args = parser.parse_args()
    if args.read_listeners:
        if args.binary or args.controller_only:
            parser.error('Listener inspection cannot install state')
        value = read_listeners(args.read_listeners, os.geteuid())
        print(value['role'] if args.role_only else str(value['web_port']) + '|' + str(value['socket_port']))
        return
    if args.role_only:
        parser.error('--role-only requires --read-listeners')
    if not args.binary:
        parser.error('--binary is required for state installation')
    if os.geteuid() != 0:
        parser.error('Saved-state installation requires root')
    admit(args.binary, args.state_directory, args.ini_source, args.listeners, args.controller_only)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit('Saved-state operation failed: ' + (error.stderr or 'backend refused admission').strip())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit('Saved-state installation failed: ' + str(error))
