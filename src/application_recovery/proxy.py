# SPDX-License-Identifier: MIT
"""Reconcile only installer-managed Apache upstream ports after restore.

Production callers supply no paths or commands. The browser keeps its original
origin and maintenance lease while Apache reloads its existing managed routes.
"""
import hashlib
import http.client
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import time
from . import archive
from .durable import publish_record, read_record, sync_directory

START = '    # BEGIN WsprryPi proxy configuration'
END = '    # END WsprryPi proxy configuration'
SITES = Path('/etc/apache2')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_site(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(descriptor)
        archive.require(stat.S_ISREG(before.st_mode) and before.st_uid == os.geteuid() and before.st_nlink == 1
                        and not before.st_mode & 0o022 and 0 <= before.st_size <= archive.MAX_MANIFEST)
        with os.fdopen(os.dup(descriptor), 'rb') as stream:
            data = stream.read(archive.MAX_MANIFEST + 1)
        archive.require(len(data) == before.st_size and archive.observation(before) == archive.observation(os.fstat(descriptor)))
        return data, stat.S_IMODE(before.st_mode)
    finally:
        os.close(descriptor)


def update_ports(data, web, socket):
    text = data.decode('utf-8')
    lines = text.splitlines(keepends=True)
    active, found = False, False
    result = []
    for line in lines:
        if line.rstrip('\r\n') == START:
            archive.require(not active and not found);active, found = True, True
        elif line.rstrip('\r\n') == END:
            archive.require(active);active = False
        elif active:
            line = re.sub(r'\b(http|ws)://127\.0\.0\.1:[0-9]+(?=/)',
                          lambda match: match[1] + '://127.0.0.1:' + str(web if match[1] == 'http' else socket), line)
        result.append(line)
    archive.require(not active)
    return ''.join(result).encode(), found


def replace_site(path, data, expected, mode):
    archive.require(digest(read_site(path)[0]) == expected)
    group = path.lstat().st_gid
    descriptor, temporary = tempfile.mkstemp(prefix='.wsprrypi-proxy-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data);stream.flush();os.fchown(stream.fileno(), -1, group);os.fchmod(stream.fileno(), mode);os.fsync(stream.fileno())
        archive.require(digest(read_site(path)[0]) == expected)
        os.replace(temporary, path);sync_directory(path.parent)
    finally:
        Path(temporary).unlink(missing_ok=True)


def reconcile(job, web, socket, *, sites=SITES, run=subprocess.run):
    # Alternate paths/adapters are typed unit-test inputs, never worker JSON,
    # command-line arguments, environment overrides, or uploaded archive fields.
    archive.require(type(web) is int and type(socket) is int and 1024 <= web <= 49151 and 1024 <= socket <= 49151 and web != socket)
    intent_path = job / 'proxy-publication.json'
    intent, stamp = read_record(intent_path)
    if intent is None:
        entries = []
        enabled = sites / 'sites-enabled'
        if enabled.is_dir():
            visited = set()
            for link in sorted(enabled.glob('*.conf')):
                path = link.resolve(strict=True)
                archive.require(path.parent in ((sites / 'sites-available').resolve(), enabled.resolve()))
                if path in visited:
                    continue
                visited.add(path)
                before, mode = read_site(path)
                after, managed = update_ports(before, web, socket)
                if not managed or before == after:
                    continue
                archive.require(len(entries) < archive.MAX_FILES)
                name = 'proxy-' + str(len(entries))
                # Root-private evidence is outside the replaced generation.
                for suffix, data in (('.before', before), ('.after', after)):
                    target = job / (name + suffix)
                    if target.exists() or target.is_symlink():
                        with archive.private_input(target, archive.MAX_MANIFEST) as (source, _):
                            archive.require(source.read() == data)
                        continue
                    descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
                    with os.fdopen(descriptor, 'wb') as stream:
                        stream.write(data);stream.flush();os.fsync(stream.fileno())
                entries.append({'group': path.parent.name, 'name': path.name, 'evidence': name, 'before': digest(before), 'after': digest(after), 'mode': mode})
        intent = {'schema': 'recovery-proxy-publication/1', 'web_port': web, 'socket_port': socket, 'entries': entries, 'state': 'prepared'}
        stamp = publish_record(intent_path, intent, None)
    archive.closed(intent, ('schema', 'web_port', 'socket_port', 'entries', 'state'))
    archive.require(intent['schema'] == 'recovery-proxy-publication/1' and intent['web_port'] == web and intent['socket_port'] == socket
                    and intent['state'] in ('prepared', 'complete') and type(intent['entries']) is list and len(intent['entries']) <= archive.MAX_FILES)
    if intent['state'] == 'complete':
        return
    for item in intent['entries']:
        archive.closed(item, ('group', 'name', 'evidence', 'before', 'after', 'mode'))
        archive.require(item['group'] in ('sites-available', 'sites-enabled') and type(item['name']) is str and re.fullmatch(r'[A-Za-z0-9_.-]+\.conf', item['name'])
                        and type(item['evidence']) is str and re.fullmatch(r'proxy-[0-9]+', item['evidence'])
                        and archive.HASH.fullmatch(item['before']) and archive.HASH.fullmatch(item['after']) and item['mode'] in (0o600, 0o640, 0o644))
        path = sites / item['group'] / item['name']
        with archive.private_input(job / (item['evidence'] + '.after'), archive.MAX_MANIFEST) as (source, _):
            after = source.read();archive.require(digest(after) == item['after'])
        current, _ = read_site(path)
        if digest(current) == item['after']:
            continue
        replace_site(path, after, item['before'], item['mode'])
    if intent['entries']:
        # A configuration-test or reload failure remains a recoverable restore
        # failure. The old admitted listener and private originals stay intact.
        for command in (['/usr/sbin/apache2ctl', 'configtest'], ['/usr/bin/systemctl', 'reload', 'apache2.service']):
            result = run(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         env={'PATH': '/usr/bin:/bin', 'LANG': 'C'}, timeout=30, check=False)
            archive.require(result.returncode == 0)
    publish_record(intent_path, dict(intent, state='complete'), stamp)


def windows_site(path):
    # The installer provisions this dedicated directory under the maintenance
    # SID, with Apache read access only. Reject namespace aliases on both reads.
    directory = path.parent.lstat()
    before = path.lstat()
    archive.require(stat.S_ISDIR(directory.st_mode) and not directory.st_file_attributes & 0x400
                    and stat.S_ISREG(before.st_mode) and not before.st_file_attributes & 0x400
                    and before.st_nlink == 1 and 0 < before.st_size <= archive.MAX_MANIFEST)
    with path.open('rb') as stream:
        opened = os.fstat(stream.fileno())
        data = stream.read(archive.MAX_MANIFEST + 1)
        after = os.fstat(stream.fileno())
        current = path.lstat()
        # Windows path and handle stat report different meanings of ctime.
        # Match the file identity across them and check each clock separately.
        archive.require(opened.st_ino != 0 and stat.S_ISREG(opened.st_mode) and stat.S_ISREG(current.st_mode)
                        and opened.st_nlink == after.st_nlink == current.st_nlink == 1
                        and not current.st_file_attributes & 0x400 and len(data) == before.st_size
                        and archive.observation(opened)[:-1] == archive.observation(before)[:-1]
                        and archive.observation(after) == archive.observation(opened)
                        and archive.observation(current) == archive.observation(before))
    return data, None


def replace_windows_site(path, data, expected, _):
    import ctypes
    from ctypes import wintypes
    descriptor, temporary = tempfile.mkstemp(prefix='.wsprrypi-proxy-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        archive.require(digest(windows_site(path)[0]) == expected)
        replace = ctypes.WinDLL('kernel32', use_last_error=True).ReplaceFileW
        replace.argtypes = (wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
                            ctypes.c_void_p, ctypes.c_void_p)
        replace.restype = wintypes.BOOL
        if not replace(str(path), temporary, None, 0, None, None):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        Path(temporary).unlink(missing_ok=True)


def reconcile_native(job, web, socket, kind, root, backend):
    # All paths are derived from the compiled native role paths. No recovery
    # document can supply a service label, executable, or configuration path.
    control = root.parent / 'Configuration'
    program = Path(backend).parent.parent
    if kind == 'launchd':
        archive.require(sys.platform == 'darwin' and os.geteuid() == 0)
        site = control / 'proxy.conf'
        read, replace = read_site, replace_site
        commands = ([str(program / 'runtime/bin/httpd'), '-t', '-f', str(control / 'httpd.conf')],
                    ['/bin/launchctl', 'kill', 'SIGUSR1', 'system/org.wsprrypi.controller.http'])
        env = {'PATH': '/usr/bin:/bin', 'LANG': 'C'}
    else:
        archive.require(kind == 'windows_service' and sys.platform == 'win32')
        site = control / 'Proxy/proxy.conf'
        if not site.parent.exists():
            # Native software fixtures without installer-managed hosting.
            return
        read, replace = windows_site, replace_windows_site
        # Apache's WinNT MPM accepts its restart event on control 128.
        # The service DACL grants no start, stop, or configuration right.
        system = Path(os.environ['SystemRoot']) / 'System32'
        commands = ([str(program / 'runtime/apache/bin/httpd.exe'), '-t', '-f', str(control / 'httpd.conf')],
                    [str(system / 'sc.exe'), 'control', 'WsprryPiHttp', '128'])
        env = {'SystemRoot': str(system.parent), 'Path': str(system)}
    publish_native(job, web, socket, site, read, replace, commands, env)


def publish_native(job, web, socket, site, read, replace, commands, env):
    archive.require(type(web) is int and type(socket) is int and 1024 <= web <= 49151 and 1024 <= socket <= 49151 and web != socket)
    path = job / 'native-proxy-publication.json'
    intent, stamp = read_record(path)
    current, mode = read(site)
    if intent is None:
        updated, found = update_ports(current, web, socket)
        archive.require(found)
        intent = {'schema': 'recovery-native-proxy/1', 'before': digest(current), 'after': digest(updated),
                  'web': web, 'socket': socket, 'complete': False}
        stamp = publish_record(path, intent, None)
    archive.closed(intent, ('schema', 'before', 'after', 'web', 'socket', 'complete'))
    archive.require(intent['schema'] == 'recovery-native-proxy/1' and intent['web'] == web and intent['socket'] == socket
                    and archive.HASH.fullmatch(intent['before']) and archive.HASH.fullmatch(intent['after'])
                    and type(intent['complete']) is bool)
    if intent['complete']:
        archive.require(digest(current) == intent['after'])
        return
    if digest(current) != intent['after']:
        archive.require(digest(current) == intent['before'])
        updated, found = update_ports(current, web, socket)
        archive.require(found and digest(updated) == intent['after'])
        replace(site, updated, intent['before'], mode)
    if intent['before'] != intent['after']:
        for command in commands:
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL, env=env, timeout=30)
            archive.require(result.returncode == 0)
        deadline = time.monotonic() + 25
        while True:
            connection = http.client.HTTPConnection('127.0.0.1', 8080, timeout=2)
            try:
                connection.request('GET', '/wsprrypi/version')
                response = connection.getresponse()
                if response.status == 200:
                    status = archive.strict_json(response.read(archive.MAX_MANIFEST + 1))
                    if status.get('host_status', {}).get('controller_ready') is True:
                        break
            except (OSError, http.client.HTTPException):
                pass
            finally:
                connection.close()
            archive.require(time.monotonic() < deadline)
            time.sleep(.25)
    publish_record(path, dict(intent, complete=True), stamp)
