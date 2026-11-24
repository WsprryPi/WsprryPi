# SPDX-License-Identifier: MIT
"""Closed, streamed application recovery container; no live-state ownership.

Only the owning backend supplies a coherent private snapshot. Decryption stages
untrusted files privately and never selects or replaces active application state.
"""
from contextlib import contextmanager
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
if sys.platform != 'win32':
    import fcntl
import tarfile
import threading
import time
from .private_acl import require_private_acl

MAX_ARCHIVE = 2 * 1024 * 1024 * 1024
MAX_PLAINTEXT = MAX_ARCHIVE - 2 * 1024 * 1024
MAX_FILES = 1024
MAX_MANIFEST = 1024 * 1024
CHUNK = 65536
ID = re.compile(r'[0-9a-f]{32}')
HASH = re.compile(r'[0-9a-f]{64}')
FIXED_NAMES = {
    'application/state.sqlite3': 64 * 1024 * 1024,
    'application/authority.json': MAX_MANIFEST,
    'application/configuration.recurrence-progress.json': 16384,
    'application/configuration.wtp-assignments.json': MAX_MANIFEST,
    'application/configuration.wtp-fleet-contexts.json': MAX_MANIFEST,
    'application/configuration.udp-logs.json': 65536,
    'application/configuration.usb-logs.json': 65536,
    'pairing/selector.json': MAX_MANIFEST,
    'pairing/authority.json': MAX_MANIFEST,
    'pairing/operations.json': MAX_MANIFEST,
    'pairing/peers.json': MAX_MANIFEST,
    'retirement/authority.json': MAX_MANIFEST,
    'retirement/established.json': MAX_MANIFEST,
}


class RecoveryError(RuntimeError):
    """Fixed operator-facing refusal; never includes private input bytes."""


def require(condition):
    if not condition:
        raise RecoveryError('Recovery archive is invalid, unsupported, or unavailable')


def maximum_file(name):
    require(type(name) is str and len(name) <= 100)
    match = re.fullmatch(r'application/evidence/([0-9a-f]{32})/([^/]+)', name)
    if match:
        if match[2] == 'index.json':
            return MAX_MANIFEST
        from .evidence import maximum
        return maximum(match[2])
    if name in FIXED_NAMES:
        return FIXED_NAMES[name]
    if re.fullmatch(r'application/configuration.management/[0-9a-f]{32}\.json', name):
        return 16 * 1024 * 1024
    if re.fullmatch(r'credentials/[0-9a-f]{32}/(ca\.crt|client\.crt|client\.key)', name):
        return MAX_MANIFEST
    raise RecoveryError('Unsupported recovery archive member')


def closed(value, fields):
    require(type(value) is dict and set(value) == set(fields))


def strict_json(data):
    require(len(data) <= MAX_MANIFEST)
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result)
            result[key] = value
        return result
    try:
        value = json.loads(data, object_pairs_hook=pairs, parse_constant=lambda _: require(False))
        def depth(item, level=0):
            require(level <= 12)
            if type(item) is dict:
                for child in item.values():
                    depth(child, level + 1)
            elif type(item) is list:
                for child in item:
                    depth(child, level + 1)
        depth(value)
        return value
    except (ValueError, RecursionError, UnicodeError) as error:
        raise RecoveryError('Invalid recovery manifest') from error


def validate_manifest(value):
    closed(value, ('schema', 'source', 'created_utc', 'files'))
    require(value['schema'] == 'wsprrypi-recovery/1')
    source = value['source']
    closed(source, ('identity', 'role', 'application_version', 'storage', 'platform'))
    require(type(source['identity']) is str and ID.fullmatch(source['identity']))
    require(source['role'] in ('pi', 'controller') and source['storage'] == 'application-state/1')
    require((source['platform'] in ('linux', 'darwin') or source['platform'] == 'windows' and source['role'] == 'controller'))
    require(type(source['application_version']) is str and 0 < len(source['application_version']) <= 128)
    require(type(value['created_utc']) is int and 0 < value['created_utc'] < 2**63)
    require(type(value['files']) is list and 0 < len(value['files']) <= MAX_FILES)
    names, total = set(), 0
    for item in value['files']:
        closed(item, ('name', 'size', 'sha256'))
        name = item['name']
        maximum = maximum_file(name)
        require(name not in names and type(item['size']) is int and 0 <= item['size'] <= maximum)
        require(type(item['sha256']) is str and HASH.fullmatch(item['sha256']))
        names.add(name)
        total += 512 + ((item['size'] + 511) // 512) * 512
    require({'application/state.sqlite3', 'application/authority.json'} <= names)
    require(total + MAX_MANIFEST + 2048 <= MAX_PLAINTEXT)
    require([item['name'] for item in value['files']] == sorted(names))
    return value


def safe_parent(path):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.safe_parent(path)
    path = Path(path)
    require(path.is_absolute() and '..' not in path.parts)
    for parent in (path.parent, *path.parent.parents):
        info = parent.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid in (0, os.geteuid()))
        require(not info.st_mode & 0o022 or (info.st_uid == 0 and info.st_mode & stat.S_ISVTX))
        if sys.platform == 'darwin':
            descriptor = os.open(parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:
                require_private_acl(descriptor, info.st_uid)
            finally:
                os.close(descriptor)
    return path


def private_directory(path):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.private_directory(path)
    path = safe_parent(path)
    info = path.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == os.geteuid() and stat.S_IMODE(info.st_mode) == 0o700)
    if sys.platform == 'darwin':
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            require_private_acl(descriptor, info.st_uid)
        finally:
            os.close(descriptor)
    return path


@contextmanager
def private_input(path, maximum):
    if sys.platform == 'win32':
        from . import native_files
        with native_files.private_input(path, maximum) as opened:
            yield opened
        return
    path = safe_parent(path)
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = stat_descriptor(descriptor)
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.geteuid()
                and stat.S_IMODE(info.st_mode) == 0o600 and 0 <= info.st_size <= maximum)
        require_private_acl(descriptor, info.st_uid)
        with os.fdopen(descriptor, 'rb', closefd=False) as stream:
            yield stream, info
    finally:
        os.close(descriptor)


def stat_descriptor(descriptor):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.stat_descriptor(descriptor)
    return os.fstat(descriptor)


def path_metadata(path):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.path_info(path)
    return Path(path).lstat()


@contextmanager
def private_output(path, maximum=MAX_ARCHIVE):
    path = safe_parent(path)
    private_directory(path.parent)
    if sys.platform == 'win32':
        from . import native_files
        with native_files.private_output(path, maximum) as opened:
            yield opened[0]
        return
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(descriptor, 'wb', closefd=False) as stream:
            yield stream
    finally:
        os.close(descriptor)


class CiphertextUpload:
    """One private writer; duplicate-chunk reads never share a mutable cursor."""
    def __init__(self, path, size):
        require(type(size) is int and 0 < size <= MAX_ARCHIVE)
        path = safe_parent(path)
        private_directory(path.parent)
        self.size, self.native, self.lock = size, None, threading.Lock()
        if sys.platform == 'win32':
            from . import native_files
            self.native = native_files.NativeFile(path, 2, size)
            self.fd = self.native.fd
        else:
            self.fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)

    def append(self, offset, data):
        with self.lock:
            require(self.fd is not None and type(offset) is int and type(data) is bytes and 0 < len(data) <= CHUNK)
            position = os.lseek(self.fd, 0, os.SEEK_END)
            require(0 <= offset <= position and offset + len(data) <= self.size)
            if offset < position:
                require(offset + len(data) <= position)
                if sys.platform == 'win32':
                    # Windows has no os.pread. This descriptor belongs only to
                    # this writer, and its cursor is serialized by this lock.
                    try:
                        os.lseek(self.fd, offset, os.SEEK_SET)
                        observed = os.read(self.fd, len(data))
                    finally:
                        os.lseek(self.fd, position, os.SEEK_SET)
                else:
                    observed = os.pread(self.fd, len(data), offset)
                require(observed == data)
            else:
                remaining = memoryview(data)
                while remaining:
                    count = os.write(self.fd, remaining)
                    require(count > 0)
                    remaining = remaining[count:]
                position += len(data)
            return position

    def complete(self):
        with self.lock:
            require(self.fd is not None and stat_descriptor(self.fd).st_size == self.size)
            if self.native is not None:
                self.native.sync()
            else:
                os.fsync(self.fd)

    def close(self):
        with self.lock:
            descriptor, self.fd = self.fd, None
            if self.native is not None:
                self.native.close()
                self.native = None
            elif descriptor is not None:
                os.close(descriptor)


def observation(info):
    return info.st_dev, getattr(info, 'native_file_id', info.st_ino), info.st_size, info.st_mtime_ns, info.st_ctime_ns


def file_digest(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(CHUNK), b''):
        digest.update(chunk)
    return digest.hexdigest()


def manifest_for_snapshot(directory, source, names, created_utc=None):
    directory = private_directory(directory)
    require(type(names) is list and len(names) <= MAX_FILES and len(set(names)) == len(names))
    files = []
    for name in sorted(names):
        with private_input(directory / name, maximum_file(name)) as (stream, info):
            digest = file_digest(stream)
            require(observation(stat_descriptor(stream.fileno())) == observation(info))
        files.append({'name': name, 'size': info.st_size, 'sha256': digest})
    return validate_manifest({'schema': 'wsprrypi-recovery/1', 'source': source,
                              'created_utc': int(time.time()) if created_utc is None else created_utc, 'files': files})


class CryptProcess:
    """No shell/preexec callback, secret files, argv secrets, or environment secrets."""
    def __init__(self, helper, action, phrase, input_fd, output_fd, working_directory=None):
        self.native = None
        if sys.platform == 'win32':
            from . import native_files
            self.native = native_files.CryptProcess(helper, action, phrase, input_fd, output_fd, working_directory)
            return
        helper = safe_parent(helper)
        info = helper.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_uid in (0, os.geteuid()) and info.st_nlink == 1
                and not info.st_mode & 0o022 and info.st_size <= 16 * 1024 * 1024)
        require(action in ('encrypt', 'decrypt') and type(phrase) is bytes and 0 < len(phrase) <= 1024)
        read_fd, write_fd = os.pipe()
        null = os.open(os.devnull, os.O_WRONLY)
        try:
            # A real dup2 must clear CLOEXEC even when the first pipe happened
            # to occupy fd 3. Never make a secret descriptor inheritable globally.
            original = read_fd
            read_fd = fcntl.fcntl(original, fcntl.F_DUPFD_CLOEXEC, 10)
            os.close(original)
            frame = struct.pack('!I', len(phrase)) + phrase
            require(os.write(write_fd, frame) == len(frame))
            os.close(write_fd)
            write_fd = -1
            actions = [(os.POSIX_SPAWN_DUP2, input_fd, 0), (os.POSIX_SPAWN_DUP2, output_fd, 1),
                       (os.POSIX_SPAWN_DUP2, null, 2), (os.POSIX_SPAWN_DUP2, read_fd, 3)]
            actions += [(os.POSIX_SPAWN_CLOSE, fd) for fd in set((input_fd, output_fd, null, read_fd)) if fd > 3]
            self.pid = os.posix_spawn(str(helper), [str(helper), action], {'PATH': '/usr/bin:/bin', 'LANG': 'C'}, file_actions=actions)
        finally:
            os.close(read_fd)
            if write_fd >= 0:
                os.close(write_fd)
            os.close(null)
        self.finished = False

    def finish(self):
        if self.native is not None:
            return self.native.finish()
        require(not self.finished)
        _, status = os.waitpid(self.pid, 0)
        self.finished = True
        require(status == 0)

    def cancel(self):
        if self.native is not None:
            return self.native.cancel()
        if self.finished:
            return
        try:
            os.kill(self.pid, 9)
        except ProcessLookupError:
            pass
        os.waitpid(self.pid, 0)
        self.finished = True


def header(name, size):
    item = tarfile.TarInfo(name)
    item.size = size
    item.mode = 0o600
    item.uid = item.gid = item.mtime = 0
    return item.tobuf(format=tarfile.USTAR_FORMAT)


def copy_exact(source, output, size):
    digest = hashlib.sha256()
    while size:
        chunk = source.read(min(CHUNK, size))
        require(chunk)
        output.write(chunk)
        digest.update(chunk)
        size -= len(chunk)
    return digest.hexdigest()


def write_container(output, directory, manifest):
    validate_manifest(manifest)
    encoded = json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()
    require(len(encoded) <= MAX_MANIFEST)
    output.write(header('manifest.json', len(encoded)))
    output.write(encoded)
    output.write(b'\0' * (-len(encoded) % 512))
    for item in manifest['files']:
        with private_input(directory / item['name'], maximum_file(item['name'])) as (source, info):
            require(info.st_size == item['size'])
            output.write(header(item['name'], item['size']))
            require(copy_exact(source, output, item['size']) == item['sha256'])
            require(not source.read(1) and observation(stat_descriptor(source.fileno())) == observation(info))
        output.write(b'\0' * (-item['size'] % 512))
    output.write(b'\0' * 1024)


def exact(input_stream, size):
    output = bytearray()
    while len(output) < size:
        chunk = input_stream.read(size - len(output))
        require(chunk)
        output.extend(chunk)
    return bytes(output)


def member(input_stream, expected_name=None, expected_size=None):
    block = exact(input_stream, 512)
    try:
        item = tarfile.TarInfo.frombuf(block, 'utf-8', 'strict')
    except (tarfile.TarError, ValueError, UnicodeError) as error:
        raise RecoveryError('Invalid recovery member header') from error
    require(item.type == tarfile.REGTYPE and not item.linkname and item.uid == item.gid == item.mtime == 0)
    require(item.mode == 0o600 and not item.pax_headers and block == header(item.name, item.size))
    if expected_name is not None:
        require(item.name == expected_name)
    if expected_size is not None:
        require(item.size == expected_size)
    return item


def read_container(input_stream, destination):
    destination = private_directory(destination)
    first = member(input_stream, 'manifest.json')
    require(0 < first.size <= MAX_MANIFEST)
    manifest = validate_manifest(strict_json(exact(input_stream, first.size)))
    require(exact(input_stream, -first.size % 512) == b'\0' * (-first.size % 512))
    for item in manifest['files']:
        member(input_stream, item['name'], item['size'])
        path = destination / item['name']
        if sys.platform == 'win32':
            from . import native_files
            parent = destination
            for component in path.parent.relative_to(destination).parts:
                parent = native_files.make_directory(parent / component, exist_ok=True)
        else:
            for parent in reversed(path.parent.relative_to(destination).parents):
                if parent != Path('.'):
                    (destination / parent).mkdir(mode=0o700, exist_ok=True)
            path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        private_directory(path.parent)
        with private_output(path, item['size']) as output:
            require(copy_exact(input_stream, output, item['size']) == item['sha256'])
            output.flush()
            os.fsync(output.fileno())
        require(exact(input_stream, -item['size'] % 512) == b'\0' * (-item['size'] % 512))
    require(exact(input_stream, 1024) == b'\0' * 1024 and not input_stream.read(1))
    return manifest


def crypto_pipe():
    if sys.platform == 'win32':
        from . import native_files
        return native_files.pipe()
    return os.pipe()


def create_archive(helper, directory, destination, manifest, phrase):
    directory = private_directory(directory)
    destination = safe_parent(destination)
    private_directory(destination.parent)
    source, sink = crypto_pipe()
    child = None
    created = False
    try:
        with private_output(destination, MAX_ARCHIVE) as output:
            created = True
            descriptor = output.fileno()
            try:
                child = CryptProcess(helper, 'encrypt', phrase, source, descriptor, destination.parent)
                os.close(source)
                source = -1
                with os.fdopen(sink, 'wb') as stream:
                    sink = -1
                    write_container(stream, directory, manifest)
                child.finish()
                require(0 < stat_descriptor(descriptor).st_size <= MAX_ARCHIVE)
                os.fsync(descriptor)
            except BaseException:
                if child:
                    child.cancel()
                raise
    except BaseException:
        # The writer and any owned crypto child are closed before retirement;
        # Windows does not unlink a live private writer as POSIX permits.
        if created:
            destination.unlink(missing_ok=True)
        raise
    finally:
        if source >= 0:
            os.close(source)
        if sink >= 0:
            os.close(sink)


def validate_archive(helper, source, destination, phrase, expected_stamp=None):
    destination = private_directory(destination)
    require(not any(destination.iterdir()))
    read_fd, write_fd = crypto_pipe()
    child = None
    try:
        with private_input(source, MAX_ARCHIVE) as (archive, before):
            require(expected_stamp is None or list(observation(before)) == expected_stamp)
            child = CryptProcess(helper, 'decrypt', phrase, archive.fileno(), write_fd, destination)
            os.close(write_fd)
            write_fd = -1
            with os.fdopen(read_fd, 'rb') as decrypted:
                read_fd = -1
                manifest = read_container(decrypted, destination)
            child.finish()
            require(observation(stat_descriptor(archive.fileno())) == observation(before))
        return manifest
    except BaseException:
        if child:
            child.cancel()
        # The caller owns this uniquely admitted staging directory and cleans
        # its untrusted contents after failure; it is never an active generation.
        raise
    finally:
        if read_fd >= 0:
            os.close(read_fd)
        if write_fd >= 0:
            os.close(write_fd)
