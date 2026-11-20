# SPDX-License-Identifier: MIT
"""Reconcile only installer-managed Apache upstream ports after restore.

Production callers supply no paths or commands. The browser keeps its original
origin and maintenance lease while Apache reloads its existing managed routes.
"""
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
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
            archive.require(not active);active, found = True, True
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
