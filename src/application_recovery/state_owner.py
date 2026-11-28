# SPDX-License-Identifier: MIT
"""Private POSIX recovery handoff to the backend owner.

This adapter moves bounded logical records over an inherited authenticated Unix
socket. Active-state operations reuse the existing publication/evidence engine
under the backend UID; the maintenance process never traverses backend paths.
"""
from contextlib import contextmanager, ExitStack
import ctypes
import hashlib
import os
from pathlib import Path
import secrets
import select
import shutil
import socket
import stat
import struct
import subprocess
import sys
if sys.platform != 'win32':
    import fcntl
import tempfile
import time

from . import archive, evidence, publication
from .durable import canonical, directory_lock, read_record, publish_record, sync_directory, RecoveryLockBusy

SCHEMA = 'native-recovery-owner/1'
MAX_RELAY_FILES = 2 * archive.MAX_FILES + 64
MAX_RELAY_BYTES = 2 * archive.MAX_ARCHIVE
INPUT_RECORDS = {'job.json', 'capture-admission.json', 'archive-manifest.json',
                 'validation-admission.json', 'confirmation-admission.json'}
OUTPUT_RECORDS = {'capture-result.json', 'validation-result.json', 'preview.json', 'proposed-settings.json',
                  'stop-result.json', 'hold-prepared.json', 'candidate-built.json', 'prepared.json', 'publication.json'}
GROUPS = {'decoded', 'snapshot', 'validated', 'source-records', 'candidate', 'candidate-working'}
APPLICATION_RECORDS = {'authority.json', 'restore-hold.json', 'restore-preparation.json'}
OPERATIONS = {'import', 'export', 'clear-candidate', 'prune-job', 'application-record', 'root-available',
              'recover-hold', 'review-failed-hold', 'copy-evidence', 'admit', 'history-required',
              'finish-history', 'recover-publication', 'publish', 'build-candidate', 'seal-candidate'}


def operation_timeout(operation):
    # Preserve the existing 1,800-second offline command budget, with the
    # normal bounded file-transfer budget on either side of that operation.
    return 2400 if operation in ('build-candidate', 'seal-candidate') else 600


def connection_lost(channel):
    if sys.platform == 'win32':
        return channel.lost()
    readable, _, _ = select.select([channel], [], [], 0)
    # No further parent bytes are valid while an offline command is running.
    # Readability therefore means either disconnect or a protocol violation.
    return bool(readable)


class OwnerBusy(archive.RecoveryError):
    pass


class OwnerUnavailable(archive.RecoveryError):
    pass


def peer_uid(channel):
    archive.require(channel.family == socket.AF_UNIX and channel.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_STREAM)
    if sys.platform == 'linux':
        return struct.unpack('3i', channel.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))[1]
    if sys.platform == 'darwin':
        uid, gid = ctypes.c_uint(), ctypes.c_uint()
        library = ctypes.CDLL(None, use_errno=True)
        operation = library.getpeereid
        operation.argtypes = (ctypes.c_int, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint))
        archive.require(operation(channel.fileno(), ctypes.byref(uid), ctypes.byref(gid)) == 0)
        return uid.value
    raise archive.RecoveryError('Native recovery owner transport is unavailable')


def has_no_supplementary_groups(primary_gid):
    if sys.platform != 'darwin':
        return not os.getgroups()
    # Darwin Python reports Directory Services membership. The unsuffixed libc
    # entry point reports the process credential and includes its primary GID.
    getgroups = ctypes.CDLL(None, use_errno=True).getgroups
    getgroups.argtypes = (ctypes.c_int, ctypes.POINTER(ctypes.c_uint))
    getgroups.restype = ctypes.c_int
    groups = (ctypes.c_uint * 2)()
    count = getgroups(len(groups), groups)
    return count == 0 or (count == 1 and groups[0] == primary_gid)


class Wire:
    def __init__(self, channel, timeout=600):
        self.channel = channel
        self.deadline = time.monotonic() + timeout

    def budget(self):
        remaining = self.deadline - time.monotonic()
        archive.require(remaining > 0)
        self.channel.settimeout(remaining)

    def send(self, data):
        self.budget()
        self.channel.sendall(data)

    def read(self, size):
        archive.require(type(size) is int and 0 <= size <= archive.MAX_MANIFEST)
        result = bytearray()
        while len(result) < size:
            self.budget()
            part = self.channel.recv(min(archive.CHUNK, size - len(result)))
            archive.require(bool(part))
            result.extend(part)
        return bytes(result)

    def frame(self, value):
        data = canonical(value)
        archive.require(0 < len(data) <= archive.MAX_MANIFEST)
        self.send(struct.pack('!I', len(data)))
        self.send(data)

    def receive(self):
        size, = struct.unpack('!I', self.read(4))
        archive.require(0 < size <= archive.MAX_MANIFEST)
        return archive.strict_json(self.read(size))


def member_limit(name):
    archive.require(type(name) is str and len(name) <= 160 and not name.startswith('/') and
                    '\\' not in name and '\0' not in name and all(part not in ('', '.', '..') for part in name.split('/')))
    if name in INPUT_RECORDS | OUTPUT_RECORDS:
        return archive.MAX_MANIFEST
    prefix, separator, relative = name.partition('/')
    archive.require(separator and prefix in GROUPS)
    if prefix in ('decoded', 'snapshot', 'validated'):
        return archive.maximum_file(relative)
    if prefix == 'source-records':
        archive.require(relative == 'index.json' or relative in evidence.STATE_NAMES)
        return archive.MAX_MANIFEST
    return publication.history_file_limit(relative)


def validate_manifest(files):
    archive.require(type(files) is list and len(files) <= MAX_RELAY_FILES)
    names, total = [], 0
    for item in files:
        archive.closed(item, ('name', 'size', 'sha256'))
        maximum = member_limit(item['name'])
        archive.require(type(item['size']) is int and 0 <= item['size'] <= maximum and
                        type(item['sha256']) is str and archive.HASH.fullmatch(item['sha256']))
        names.append(item['name'])
        total += item['size']
    archive.require(names == sorted(set(names)) and total <= MAX_RELAY_BYTES)


def inventory(root, allowed):
    root = archive.private_directory(root)
    result, count = [], 0
    for path in root.rglob('*'):
        count += 1
        archive.require(count <= 4 * MAX_RELAY_FILES and not path.is_symlink())
        name = path.relative_to(root).as_posix()
        if path.is_dir():
            archive.private_directory(path)
            continue
        if not allowed(name):
            continue
        with archive.private_input(path, member_limit(name)) as (stream, before):
            digest = archive.file_digest(stream)
            archive.require(archive.observation(archive.stat_descriptor(stream.fileno())) == archive.observation(before))
        result.append({'name': name, 'size': before.st_size, 'sha256': digest})
    result.sort(key=lambda item: item['name'])
    validate_manifest(result)
    return result


def send_files(wire, root, files):
    validate_manifest(files)
    for item in files:
        with archive.private_input(root / item['name'], member_limit(item['name'])) as (source, before):
            archive.require(before.st_size == item['size'])
            remaining, digest = item['size'], hashlib.sha256()
            while remaining:
                part = source.read(min(archive.CHUNK, remaining))
                archive.require(bool(part))
                remaining -= len(part)
                digest.update(part)
                wire.send(part)
            archive.require(not source.read(1) and digest.hexdigest() == item['sha256'] and
                            archive.observation(archive.stat_descriptor(source.fileno())) == archive.observation(before))


def make_private_directory(path, *, exist_ok=False):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.make_directory(path, exist_ok=exist_ok)
    Path(path).mkdir(mode=0o700, exist_ok=exist_ok)
    return archive.private_directory(path)


def temporary_private_directory(parent, prefix):
    if sys.platform == 'win32':
        return make_private_directory(Path(parent) / (prefix + secrets.token_hex(16)))
    return Path(tempfile.mkdtemp(prefix=prefix, dir=parent))


def receive_files(wire, root, files):
    validate_manifest(files)
    root = archive.private_directory(root)
    for item in files:
        path = root / item['name']
        for parent in reversed(path.parent.relative_to(root).parents):
            if str(parent) != '.':
                make_private_directory(root / parent, exist_ok=True)
                archive.private_directory(root / parent)
        make_private_directory(path.parent, exist_ok=True)
        archive.private_directory(path.parent)
        remaining, digest = item['size'], hashlib.sha256()
        with archive.private_output(path, member_limit(item['name'])) as output:
            while remaining:
                part = wire.read(min(archive.CHUNK, remaining))
                output.write(part)
                remaining -= len(part)
                digest.update(part)
            output.flush()
            os.fsync(output.fileno())
        archive.require(digest.hexdigest() == item['sha256'])
    for path in sorted((path for path in root.rglob('*') if path.is_dir()), key=lambda value: len(value.parts), reverse=True):
        sync_directory(path)
    sync_directory(root)


def remove_private_tree(path):
    path = archive.private_directory(path)
    count = 0
    for child in path.rglob('*'):
        count += 1
        archive.require(count <= 4 * MAX_RELAY_FILES and not child.is_symlink())
        if child.is_dir():
            archive.private_directory(child)
        else:
            with archive.private_input(child, archive.MAX_ARCHIVE):
                pass
    shutil.rmtree(path)
    sync_directory(path.parent)


@contextmanager
def installation_lock(home):
    if sys.platform == 'win32':
        from . import native_files
        with ExitStack() as stack:
            try:
                stack.enter_context(native_files.installation_lock(Path(home) / '.wsprrypi-controller-instance'))
            except native_files.NativeLockBusy as error:
                raise OwnerBusy('Controller installation is busy') from error
            yield
        return
    root = archive.private_directory(Path(home) / '.wsprrypi-controller-instance')
    descriptor = os.open(root / 'instance.lock', os.O_RDWR | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        info = archive.stat_descriptor(descriptor)
        archive.require(stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and info.st_nlink == 1 and
                        stat.S_IMODE(info.st_mode) == 0o600 and info.st_size == 0)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise OwnerBusy('Controller installation is busy') from error
        current = (root / 'instance.lock').lstat()
        archive.require((current.st_dev, current.st_ino, current.st_nlink) == (info.st_dev, info.st_ino, 1))
        yield
    finally:
        os.close(descriptor)


def stage_job(context, allow_create=False, *, allow_missing=False):
    home = archive.private_directory(context['owner']['home'])
    root = home / '.wsprrypi-recovery'
    archive.require(root == Path(context['owner']['root']))
    if not root.exists():
        archive.require(not root.is_symlink())
        if allow_missing:
            return None
        archive.require(allow_create)
        make_private_directory(root)
        sync_directory(home)
    root = archive.private_directory(root)
    expected = {'schema': SCHEMA, 'identity': context['identity'], 'role': context['role'],
                'application_root': context['application_root']}
    if sys.platform == 'win32':
        expected['sid'] = context['owner']['sid']
    else:
        expected.update(uid=os.geteuid(), gid=os.getegid())
    with directory_lock(root):
        anchor, _ = read_record(root / 'owner.json')
        if anchor is None:
            archive.require(allow_create)
            # Only unfinished deterministic anchor publication may be resumed.
            for path in root.iterdir():
                archive.require(path.name.startswith('.owner-writing-') and not path.is_symlink())
                with archive.private_input(path, archive.MAX_MANIFEST):
                    pass
                path.unlink()
            if sys.platform == 'win32':
                publish_record(root / 'owner.json', expected, None)
            else:
                descriptor, temporary = tempfile.mkstemp(prefix='.owner-writing-', dir=root)
                try:
                    with os.fdopen(descriptor, 'wb') as output:
                        output.write(canonical(expected));output.flush();os.fsync(output.fileno())
                    publication.move_no_replace(temporary, root / 'owner.json')
                    sync_directory(root)
                finally:
                    Path(temporary).unlink(missing_ok=True)
        else:
            archive.require(anchor == expected)
        jobs = root / 'jobs'
        if not jobs.exists() and allow_missing:
            archive.require(not jobs.is_symlink())
            return None
        if not jobs.exists():
            make_private_directory(jobs)
            sync_directory(root)
        archive.private_directory(jobs)
    job = jobs / context['job_id']
    if not job.exists():
        archive.require(not job.is_symlink())
        if allow_missing:
            return None
        archive.require(allow_create)
        make_private_directory(job)
        sync_directory(jobs)
    return archive.private_directory(job)


def validate_context(value, maintenance_uid, owner_home):
    archive.closed(value, ('schema', 'nonce', 'owner', 'application_root', 'identity', 'role', 'job_id',
                           'operation', 'arguments', 'inputs', 'backend_command', 'backend_runtime'))
    archive.require(value['schema'] == SCHEMA and type(value['nonce']) is str and archive.ID.fullmatch(value['nonce']))
    owner = value['owner']
    archive.closed(owner, ('uid', 'gid', 'home', 'root'))
    archive.require(type(owner['uid']) is int and 0 < owner['uid'] < 2**32 and owner['uid'] == os.geteuid() and
                    type(owner['gid']) is int and 0 < owner['gid'] < 2**32 and owner['gid'] == os.getegid())
    if owner_home is None:
        import pwd
        owner_home = pwd.getpwuid(os.geteuid()).pw_dir
    elif callable(owner_home):
        owner_home = owner_home(value)
    archive.require(owner['home'] == str(owner_home) and owner['root'] == str(Path(owner_home) / '.wsprrypi-recovery'))
    for name in ('application_root', 'backend_runtime'):
        archive.require(type(value[name]) is str and Path(value[name]).is_absolute() and '\0' not in value[name] and '..' not in Path(value[name]).parts)
    archive.require(type(value['identity']) is str and archive.ID.fullmatch(value['identity']) and value['role'] == 'controller')
    archive.require(type(value['operation']) is str and value['operation'] in OPERATIONS and type(value['arguments']) is dict)
    if value['operation'] in ('root-available', 'application-record'):
        archive.require(value['job_id'] is None)
    else:
        archive.require(type(value['job_id']) is str and archive.ID.fullmatch(value['job_id']))
    command = value['backend_command']
    archive.require(type(command) is list and 0 < len(command) <= 8 and
                    all(type(part) is str and '\0' not in part and len(part) <= 4096 for part in command) and Path(command[0]).is_absolute())
    validate_manifest(value['inputs'])
    archive.require(value['operation'] == 'import' or not value['inputs'])
    # A service cannot accidentally retain root capabilities after dropping UID.
    if sys.platform == 'linux':
        library = ctypes.CDLL(None, use_errno=True)
        archive.require(library.prctl(38, 1, 0, 0, 0) == 0)  # PR_SET_NO_NEW_PRIVS
        status = dict(line.split(':', 1) for line in Path('/proc/self/status').read_text().splitlines() if ':' in line)
        archive.require(all(int(status[name].strip(), 16) == 0 for name in ('CapEff', 'CapPrm', 'CapAmb')))
    if maintenance_uid == 0:
        archive.require(has_no_supplementary_groups(owner['gid']))
    return value


def completed_outputs(job):
    def ready(name):
        record, _ = read_record(job / name)
        archive.require(record is None or type(record) is dict)
        return record is not None and record.get('state') == 'ready'
    capture_ready, validation_ready = ready('capture-result.json'), ready('validation-result.json')
    snapshot_names, validated_names = set(), set()
    if capture_ready:
        captured, _ = read_record(job / 'capture-result.json')
        names = captured['inventory']['files']
        archive.require(type(names) is list and len(names) <= archive.MAX_FILES and all(type(name) is str for name in names) and len(names) == len(set(names)))
        for name in names:
            archive.maximum_file(name)
            snapshot_names.add('snapshot/' + name)
    if validation_ready:
        manifest, _ = read_record(job / 'archive-manifest.json')
        archive.validate_manifest(manifest)
        validated_names = {'validated/' + item['name'] for item in manifest['files']}
    built = (job / 'candidate-built.json').exists()
    def allowed(name):
        if name in OUTPUT_RECORDS:
            return True
        if name.startswith('snapshot/'):
            return name in snapshot_names
        if name.startswith('validated/'):
            return name in validated_names
        if name.startswith('source-records/'):
            return validation_ready
        if built and sys.platform == 'win32' and name == 'candidate/state.sqlite3.owner.lock':
            # The backend's empty native owner witness is physical admission,
            # never portable recovery data. Sealing/publication independently
            # retain the actual SQLite owner lock; relay grants no ownership.
            with archive.private_input(job / name, 0):
                pass
            return False
        if built and name in ('candidate/state.sqlite3-wal', 'candidate/state.sqlite3-shm'):
            # Snapshot verification can leave an empty WAL and reconstruction
            # index. Neither belongs to the closed candidate inventory; a WAL
            # containing data must never be omitted from a transfer.
            wal = job / 'candidate/state.sqlite3-wal'
            if wal.exists():
                with archive.private_input(wal, 0):
                    pass
            with archive.private_input(job / name, 0 if name.endswith('-wal') else archive.MAX_MANIFEST):
                pass
            return False
        return built and name.startswith('candidate/')
    return inventory(job, allowed)


def import_job(wire, context):
    arguments = context['arguments']
    archive.closed(arguments, ('allow_create', 'clear'))
    archive.require(type(arguments['allow_create']) is bool and type(arguments['clear']) is list and
                    arguments['clear'] in ([], ['validation'], ['candidate']))
    archive.require(all(item['name'] in INPUT_RECORDS or item['name'].startswith(('decoded/', 'candidate/evidence/'))
                        for item in context['inputs']))
    job = stage_job(context, arguments['allow_create'])
    with directory_lock(job):
        temporary = temporary_private_directory(job, '.relay-')
        try:
            receive_files(wire, temporary, context['inputs'])
            terminal = wire.receive()
            archive.closed(terminal, ('nonce', 'input_complete'))
            archive.require(terminal == {'nonce': context['nonce'], 'input_complete': True})
            incoming, _ = read_record(temporary / 'job.json')
            archive.require(type(incoming) is dict and incoming.get('job_id') == context['job_id'] and
                            incoming.get('identity') == context['identity'] and incoming.get('role') == context['role'])
            previous, _ = read_record(job / 'job.json')
            archive.require(previous is None or (previous.get('job_id') == context['job_id'] and
                            previous.get('identity') == context['identity'] and previous.get('role') == context['role']))
            if arguments['clear'] == ['validation']:
                archive.require(incoming.get('state') == 'validating' and incoming.get('confirmation_sha256') is None and
                                not (job / 'publication.json').exists() and not (job / 'stop-result.json').exists())
                for name in ('decoded', 'validated', 'source-records'):
                    if (job / name).exists():
                        remove_private_tree(job / name)
                for name in ('validation-result.json', 'preview.json', 'proposed-settings.json'):
                    path = job / name
                    if path.exists():
                        read_record(path);path.unlink()
            if arguments['clear'] == ['candidate']:
                archive.require(incoming.get('state') == 'applying' and not (job / 'publication.json').exists() and not (job / 'prepared.json').exists())
                for name in ('candidate', 'candidate-working'):
                    if (job / name).exists():
                        remove_private_tree(job / name)
                if (job / 'candidate-built.json').exists():
                    read_record(job / 'candidate-built.json');(job / 'candidate-built.json').unlink()
            for item in context['inputs']:
                source, destination = temporary / item['name'], job / item['name']
                for parent in reversed(destination.parent.relative_to(job).parents):
                    if str(parent) != '.':
                        make_private_directory(job / parent, exist_ok=True)
                        archive.private_directory(job / parent)
                make_private_directory(destination.parent, exist_ok=True)
                archive.private_directory(destination.parent)
                if destination.exists():
                    with archive.private_input(destination, member_limit(item['name'])):
                        pass
                os.replace(source, destination)
                sync_directory(destination.parent)
            sync_directory(job)
        finally:
            remove_private_tree(temporary)
    return {'state': 'imported'}


def operate(context, *, cancelled=None):
    operation, arguments = context['operation'], context['arguments']
    if operation == 'application-record':
        archive.closed(arguments, ('name',))
        archive.require(arguments['name'] in APPLICATION_RECORDS)
        value, _ = read_record(Path(context['application_root']) / arguments['name'])
        return value
    if operation == 'root-available':
        archive.closed(arguments, ())
        from .lifecycle import root_available
        try:
            with installation_lock(context['owner']['home']):
                return root_available(context['application_root'])
        except OwnerBusy:
            return False
    if operation in ('build-candidate', 'seal-candidate'):
        archive.closed(arguments, ())
        from .jobs import backend_command
        return backend_command({'backend_command': context['backend_command'], 'backend_runtime': context['backend_runtime']}, operation, context['job_id'], cancelled=cancelled)
    job = stage_job(context)
    if operation == 'export':
        archive.closed(arguments, ())
        return None
    if operation == 'copy-evidence':
        archive.closed(arguments, ('generation',))
        archive.require(publication.decimal(arguments['generation']))
        with installation_lock(context['owner']['home']), directory_lock(context['application_root'], generation=True):
            evidence.copy_groups(Path(context['application_root']) / ('generation-' + arguments['generation']) / 'evidence', job / 'candidate/evidence')
        return {'state': 'evidence_retained'}
    if operation == 'recover-hold':
        archive.closed(arguments, ())
        with installation_lock(context['owner']['home']):
            return publication.recover_hold_preparation(context['application_root'], job)
    if operation == 'review-failed-hold':
        archive.closed(arguments, ())
        with installation_lock(context['owner']['home']), directory_lock(context['application_root'], generation=True):
            path = Path(context['application_root']) / 'restore-hold.json'
            held, observation = read_record(path)
            archive.require(type(held) is dict and held.get('job_id') == context['job_id'] and held.get('stage') == 'maintenance' and
                            held.get('local_output') is True and held.get('remote_authority') is True and publication.decimal(held.get('revision')))
            publish_record(path, dict(held, stage='review', revision=str(int(held['revision']) + 1)), observation)
        return {'state': 'review_held'}
    if operation == 'clear-candidate':
        archive.closed(arguments, ())
        with installation_lock(context['owner']['home']):
            archive.require(not (job / 'publication.json').exists() and not (job / 'prepared.json').exists())
            for name in ('candidate', 'candidate-working'):
                if (job / name).exists():
                    remove_private_tree(job / name)
            if (job / 'candidate-built.json').exists():
                read_record(job / 'candidate-built.json');(job / 'candidate-built.json').unlink()
            sync_directory(job)
        return {'state': 'candidate_cleared'}
    if operation == 'prune-job':
        archive.closed(arguments, ())
        remove_private_tree(job)
        return {'state': 'pruned'}
    publisher = publication.GenerationPublisher(context['application_root'], job)
    if operation in ('admit', 'history-required', 'finish-history', 'publish'):
        archive.closed(arguments, ('prepared',))
        prepared = publisher.admit(arguments['prepared'])
        archive.require(prepared['identity'] == context['identity'] and prepared['role'] == context['role'] and prepared['job_id'] == context['job_id'])
        if operation == 'admit':
            return prepared
        if operation == 'history-required':
            return publisher.history_required(prepared)
        with installation_lock(context['owner']['home']):
            if operation == 'finish-history':
                publisher.finish_history(prepared)
                return {'state': 'history_retained'}
            return publisher.publish(prepared)
    if operation == 'recover-publication':
        archive.closed(arguments, ())
        with installation_lock(context['owner']['home']):
            return publisher.recover()
    raise archive.RecoveryError('Native recovery owner operation is unavailable')


def serve_exchange(wire, channel, validate, principal):
    context = None
    try:
        context = validate(wire.receive())
        wire.deadline = time.monotonic() + operation_timeout(context['operation'])
        wire.frame(dict({'schema': SCHEMA, 'nonce': context['nonce'], 'ready': True}, **principal()))
        if context['operation'] == 'import':
            result = import_job(wire, context)
            wire.frame({'schema': SCHEMA, 'nonce': context['nonce'], 'ok': True, 'value': result, 'files': []})
        elif context['job_id'] is None:
            result = operate(context)
            wire.frame({'schema': SCHEMA, 'nonce': context['nonce'], 'ok': True, 'value': result, 'files': []})
        elif context['operation'] == 'prune-job' and stage_job(context, allow_missing=True) is None:
            archive.closed(context['arguments'], ())
            wire.frame({'schema': SCHEMA, 'nonce': context['nonce'], 'ok': True, 'value': {'state': 'pruned'}, 'files': []})
        else:
            if context['operation'] in ('build-candidate', 'seal-candidate'):
                result = operate(context, cancelled=lambda: connection_lost(channel))
                job = stage_job(context)
                with directory_lock(job):
                    files = completed_outputs(job)
                    wire.frame({'schema': SCHEMA, 'nonce': context['nonce'], 'ok': True, 'value': result, 'files': files})
                    send_files(wire, job, files)
            else:
                job = stage_job(context)
                with directory_lock(job):
                    result = operate(context)
                    files = [] if context['operation'] == 'prune-job' else completed_outputs(job)
                    wire.frame({'schema': SCHEMA, 'nonce': context['nonce'], 'ok': True, 'value': result, 'files': files})
                    send_files(wire, job, files)
        return 0
    except BaseException as error:
        try:
            wire.frame({'schema': SCHEMA, 'nonce': context['nonce'] if context else None, 'ok': False,
                        'error': 'busy' if isinstance(error, (OwnerBusy, BlockingIOError, RecoveryLockBusy)) else 'unavailable'})
        except BaseException:
            pass
        return 1


def serve(descriptor, *, maintenance_uid=0, owner_home=None):
    with socket.socket(fileno=descriptor) as channel:
        def validate(value):
            archive.require(peer_uid(channel) == maintenance_uid)
            return validate_context(value, maintenance_uid, owner_home)
        return serve_exchange(Wire(channel), channel, validate,
                              lambda: {'uid': os.geteuid(), 'gid': os.getegid(), 'pid': os.getpid()})


class StateOwner:
    """Root-side fixed backend-owner child, never an arbitrary command runner."""
    def __init__(self, config, *, process_failure=None):
        if sys.platform == 'win32':
            from .windows_state_owner import WindowsStateOwner
            self.config, self.process_failure, self.owner = config, process_failure, config['backend_owner']
            self.native = WindowsStateOwner(config, self)
            return
        self.config = config
        self.process_failure = process_failure
        self.owner = config['backend_owner']
        archive.closed(self.owner, ('uid', 'gid', 'home', 'root', 'worker'))
        archive.require(type(self.owner['uid']) is int and 0 < self.owner['uid'] < 2**32 and
                        type(self.owner['gid']) is int and 0 < self.owner['gid'] < 2**32 and config['role'] == 'controller')
        for name in ('home', 'root', 'worker'):
            archive.require(type(self.owner[name]) is str and Path(self.owner[name]).is_absolute() and '\0' not in self.owner[name])
        archive.require(self.owner['root'] == str(Path(self.owner['home']) / '.wsprrypi-recovery'))
        worker = Path(self.owner['worker']).resolve(strict=True)
        archive.safe_parent(worker)
        info = worker.stat()
        archive.require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid in (0, os.geteuid()) and not info.st_mode & 0o022)
        self.worker = worker
        if os.geteuid() != 0:
            archive.require(self.owner['uid'] == os.geteuid() and self.owner['gid'] == os.getegid())

    def job(self, job_id):
        archive.require(type(job_id) is str and archive.ID.fullmatch(job_id))
        return archive.private_directory(Path(self.config['root']) / 'jobs' / job_id)

    def receive_outputs(self, wire, job, files):
        validate_manifest(files)
        archive.require(all(item['name'] in OUTPUT_RECORDS or item['name'].startswith(('snapshot/', 'validated/', 'source-records/', 'candidate/')) for item in files))
        temporary = temporary_private_directory(job, '.owner-reply-')
        try:
            receive_files(wire, temporary, files)
            groups = {item['name'].split('/')[0] for item in files if '/' in item['name']}
            for group in sorted(groups):
                destination = job / group
                if destination.exists():
                    remove_private_tree(destination)
                if sys.platform == 'win32':
                    # These are private inactive relay groups, not generation
                    # publication. Windows rename refuses an existing target;
                    # never borrow a database owner's move admission here.
                    os.rename(temporary / group, destination)
                else:
                    publication.move_no_replace(temporary / group, destination)
                sync_directory(job)
            for item in files:
                if '/' in item['name']:
                    continue
                destination = job / item['name']
                if destination.exists():
                    read_record(destination)
                os.replace(temporary / item['name'], destination)
                sync_directory(job)
        finally:
            remove_private_tree(temporary)

    def call(self, operation, job_id=None, arguments=None, inputs=None):
        if sys.platform == 'win32':
            return self.native.call(operation, job_id, arguments, inputs)
        archive.require(operation in OPERATIONS)
        arguments, inputs = arguments or {}, inputs or []
        validate_manifest(inputs)
        nonce = secrets.token_hex(16)
        owner = {key: self.owner[key] for key in ('uid', 'gid', 'home', 'root')}
        context = {'schema': SCHEMA, 'nonce': nonce, 'owner': owner, 'application_root': self.config['application_root'],
                   'identity': self.config['identity'], 'role': self.config['role'], 'job_id': job_id, 'operation': operation,
                   'arguments': arguments, 'inputs': inputs, 'backend_command': self.config['backend_command'],
                   'backend_runtime': self.config['backend_runtime']}
        parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        process = None
        replied = False
        try:
            credentials = {'user': owner['uid'], 'group': owner['gid'], 'extra_groups': []} if os.geteuid() == 0 else {}
            process = subprocess.Popen([sys.executable, '-I', '-B', str(self.worker), '--serve', str(child.fileno())],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, close_fds=True,
                pass_fds=(child.fileno(),), env={'PATH': '/usr/bin:/bin', 'LANG': 'C'}, **credentials)
            child.close()
            wire = Wire(parent, operation_timeout(operation))
            wire.frame(context)
            ready = wire.receive()
            archive.closed(ready, ('schema', 'nonce', 'ready', 'uid', 'gid', 'pid'))
            archive.require(ready == {'schema': SCHEMA, 'nonce': nonce, 'ready': True, 'uid': owner['uid'],
                                     'gid': owner['gid'], 'pid': process.pid})
            if operation == 'import':
                send_files(wire, self.job(job_id), inputs)
                wire.frame({'nonce': nonce, 'input_complete': True})
            response = wire.receive()
            archive.require(type(response) is dict)
            if response.get('ok') is False:
                archive.closed(response, ('schema', 'nonce', 'ok', 'error'))
                archive.require(response['schema'] == SCHEMA and response['nonce'] == nonce and response['error'] in ('busy', 'unavailable'))
                archive.require(process.wait(timeout=5) == 1)
                replied = True
                if response['error'] == 'busy':
                    raise OwnerBusy('Native recovery owner is busy')
                raise OwnerUnavailable('Native recovery owner is unavailable')
            archive.closed(response, ('schema', 'nonce', 'ok', 'value', 'files'))
            archive.require(response['schema'] == SCHEMA and response['nonce'] == nonce and response['ok'] is True)
            if response['files']:
                self.receive_outputs(wire, self.job(job_id), response['files'])
            else:
                archive.require(response['files'] == [])
            archive.require(process.wait(timeout=5) == 0)
            replied = True
            return response['value']
        finally:
            try:
                parent.close();child.close()
                if process is not None and process.poll() is None:
                    # Disconnect first so the helper can cancel and reap its own
                    # backend child before resorting to forced helper teardown.
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            finally:
                if process is not None and not replied and self.process_failure is not None:
                    # The production worker must fail closed on an unknown child
                    # outcome, including a failed cleanup. Its C++ owner reaps
                    # the entire private group, including helper descendants.
                    self.process_failure()

    def synchronize(self, job_id, action=None):
        job = self.job(job_id)
        current, _ = read_record(job / 'job.json')
        archive.require(type(current) is dict and current.get('job_id') == job_id)
        def allowed(name):
            if name == 'job.json':
                return True
            if action in ('capture', 'validate', 'prepare') and name in INPUT_RECORDS:
                return True
            if action == 'validate' and name.startswith('decoded/'):
                return True
            return action == 'seal-candidate' and name.startswith('candidate/evidence/')
        files = inventory(job, allowed)
        clear = ['validation'] if action == 'validate' else []
        return self.call('import', job_id, {'allow_create': current.get('state') in ('created', 'capturing', 'validating'), 'clear': clear}, files)

    def collect(self, job_id):
        return self.call('export', job_id)

    def application_record(self, name):
        return self.call('application-record', arguments={'name': name})

    def root_available(self):
        return self.call('root-available')

    def publication(self, job_id):
        return OwnerPublication(self, job_id)


class OwnerPublication:
    def __init__(self, owner, job_id):
        self.owner, self.job_id = owner, job_id

    def command(self, operation, arguments=None):
        self.owner.synchronize(self.job_id)
        return self.owner.call(operation, self.job_id, arguments)

    def admit(self, prepared):
        return self.command('admit', {'prepared': prepared})

    def history_required(self, prepared):
        return self.command('history-required', {'prepared': prepared})

    def finish_history(self, prepared):
        return self.command('finish-history', {'prepared': prepared})

    def recover(self):
        return self.command('recover-publication')

    def publish(self, prepared):
        return self.command('publish', {'prepared': prepared})
