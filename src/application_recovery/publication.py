# SPDX-License-Identifier: MIT
"""Stopped-backend generation publication. No service or SQL operations here.

The backend prepares/validates the complete candidate and durably holds output
before this owner runs. A directory lock excludes every normal backend writer.
"""
import ctypes
import errno
import hashlib
import os
import re
from pathlib import Path
import shutil
import sys

from . import archive
from .durable import canonical, directory_lock, publish_record, read_record, sync_directory


def decimal(value):
    return type(value) is str and len(value) <= 20 and value.isascii() and value.isdecimal() and not value.startswith('0') and 0 < int(value) < 2**64


def selector(value, identity, role, generation, job_id):
    archive.closed(value, ('schema', 'identity', 'role', 'generation', 'initialization', 'restore_job'))
    archive.require(value['schema'] == 'application-generation/1' and value['identity'] == identity and value['role'] == role
                    and value['generation'] == generation and decimal(generation) and value['restore_job'] == job_id)
    initial = value['initialization']
    archive.closed(initial, ('schema', 'identity', 'role', 'origin', 'source_namespace', 'generation', 'source_revision', 'images_digest'))
    archive.require(initial['schema'] == 'state-initialization/1' and initial['identity'] == identity and initial['role'] == role
                    and initial['generation'] == '1' and type(initial['source_revision']) is str and archive.HASH.fullmatch(initial['source_revision'])
                    and type(initial['images_digest']) is str and archive.HASH.fullmatch(initial['images_digest']))
    archive.require((initial['origin'] == 'fresh' and initial['source_namespace'] == 'fresh') or
                    (initial['origin'] == 'migration' and type(initial['source_namespace']) is str
                     and initial['source_namespace'].startswith('/') and '\0' not in initial['source_namespace']))


def hold(value, identity, role, generation, job_id):
    archive.closed(value, ('schema', 'identity', 'role', 'generation', 'job_id', 'local_output', 'remote_authority', 'revision', 'stage'))
    archive.require(value['schema'] == 'application-restore-hold/1' and value['identity'] == identity and value['role'] == role
                    and value['generation'] == generation and value['job_id'] == job_id and decimal(value['revision'])
                    and value['local_output'] is True and value['remote_authority'] is True and value['stage'] in ('maintenance', 'review'))


def move_no_replace(source, destination):
    library = ctypes.CDLL(None, use_errno=True)
    if sys.platform == 'darwin':
        operation = library.renamex_np
        operation.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        result = operation(os.fsencode(source), os.fsencode(destination), 4)  # RENAME_EXCL
    elif sys.platform == 'linux':
        operation = library.renameat2
        operation.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        result = operation(-100, os.fsencode(source), -100, os.fsencode(destination), 1)
    else:
        raise archive.RecoveryError('Generation publication is unsupported on this platform')
    if result:
        value = ctypes.get_errno()
        raise OSError(value, 'Generation publication refused')


def file_limit(name):
    archive.require(type(name) is str and name != 'authority.json')
    return archive.maximum_file(name if name.startswith('credentials/') else 'application/' + name)


def validate_files(files):
    archive.require(type(files) is list and 0 < len(files) <= archive.MAX_FILES)
    names, total = [], 0
    for item in files:
        archive.closed(item, ('name', 'size', 'sha256'))
        maximum = file_limit(item['name'])
        archive.require(type(item['size']) is int and 0 <= item['size'] <= maximum
                        and type(item['sha256']) is str and archive.HASH.fullmatch(item['sha256']))
        names.append(item['name'])
        total += item['size']
    archive.require(names == sorted(set(names)) and 'state.sqlite3' in names and total <= archive.MAX_PLAINTEXT)


def verify_files(directory, files):
    directory = archive.private_directory(directory)
    validate_files(files)
    expected = {item['name']: item for item in files}
    observed = set()
    directories = {Path(name).parent for name in expected}
    directories |= {parent for path in tuple(directories) for parent in path.parents}
    for path in directory.rglob('*'):
        relative = path.relative_to(directory)
        info = path.lstat()
        if path.is_symlink():
            raise archive.RecoveryError('Unsafe generation member')
        if path.is_dir():
            archive.require(relative in directories)
            archive.private_directory(path)
            continue
        name = relative.as_posix()
        archive.require(name in expected)
        with archive.private_input(path, file_limit(name)) as (stream, info):
            archive.require(info.st_size == expected[name]['size'] and archive.file_digest(stream) == expected[name]['sha256'])
        observed.add(name)
    archive.require(observed == set(expected))


def sync_tree(directory):
    for path in directory.rglob('*'):
        if path.is_file():
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
    for path in sorted((path for path in directory.rglob('*') if path.is_dir()), key=lambda item: len(item.parts), reverse=True):
        sync_directory(path)
    sync_directory(directory)


def discard_partial_copy(directory, files):
    # Only this journal's fixed staging name may be removed. A partial copy can
    # contain a prefix of admitted files, never unknown names or linked data.
    archive.private_directory(directory)
    expected = {item['name']: item for item in files}
    parents = {parent for name in expected for parent in Path(name).parents}
    for path in directory.rglob('*'):
        relative = path.relative_to(directory)
        archive.require(not path.is_symlink())
        if path.is_dir():
            archive.require(relative in parents)
            archive.private_directory(path)
        else:
            archive.require(relative.as_posix() in expected)
            with archive.private_input(path, file_limit(relative.as_posix())) as (_, info):
                archive.require(info.st_size <= expected[relative.as_posix()]['size'])
    shutil.rmtree(directory)
    sync_directory(directory.parent)


def history_file_limit(name):
    if name == 'state.sqlite3-wal':
        return 65 * 1024 * 1024
    if name == 'state.sqlite3-shm':
        return archive.MAX_MANIFEST
    return file_limit(name)


def history_inventory(directory):
    files, count = [], 0
    for path in directory.rglob('*'):
        count += 1
        archive.require(not path.is_symlink() and len(files) <= archive.MAX_FILES + 2 and count <= 4 * archive.MAX_FILES)
        relative = path.relative_to(directory).as_posix()
        if path.is_dir():
            archive.private_directory(path)
            archive.require(relative in ('credentials', 'evidence', 'configuration.management', 'configuration.pi-recurrence-progress') or
                            re.fullmatch(r'(credentials|evidence)/[0-9a-f]{32}', relative))
            continue
        with archive.private_input(path, history_file_limit(relative)) as (stream, info):
            files.append({'name': relative, 'size': info.st_size, 'sha256': archive.file_digest(stream)})
    archive.require(0 < len(files) <= archive.MAX_FILES + 2 and any(item['name'] == 'state.sqlite3' for item in files))
    archive.require(sum(item['size'] for item in files) <= archive.MAX_PLAINTEXT + 66 * 1024 * 1024)
    return sorted(files, key=lambda item: item['name'])


class GenerationPublisher:
    def __init__(self, application_root, job_directory, checkpoint=lambda _: None):
        self.root = archive.private_directory(application_root)
        self.job = archive.private_directory(job_directory)
        archive.require(archive.ID.fullmatch(self.job.name))
        self.checkpoint = checkpoint
        self.journal = self.job / 'publication.json'

    def owner_path(self, generation):
        archive.require(decimal(generation))
        return self.root / ('generation-' + generation + '.owner.json')

    def generation_owner(self, prepared):
        directory = archive.private_directory(self.root / ('generation-' + prepared['generation']))
        info = directory.stat()
        expected = {'schema': 'recovery-generation-owner/1', 'identity': prepared['identity'], 'role': prepared['role'],
                    'generation': prepared['generation'], 'job_id': prepared['job_id'],
                    'initialization_sha256': hashlib.sha256(canonical(prepared['selection']['initialization'])).hexdigest(),
                    'device': info.st_dev, 'inode': info.st_ino}
        path = self.owner_path(prepared['generation'])
        current, _ = read_record(path)
        if current is None:
            publish_record(path, expected, None)
        else:
            archive.require(current == expected)

    def history_targets(self, prepared):
        return sorted((path.name[11:] for path in self.root.iterdir()
                       if path.name.startswith('generation-') and decimal(path.name[11:])
                       and 1 < int(path.name[11:]) < int(prepared['previous_generation'])
                       and path.name[11:] not in prepared['history_keep']), key=int)

    def history_required(self, prepared):
        return (self.root / 'history-pruning.json').exists() or bool(self.history_targets(prepared))

    def prune_history(self, prepared):
        # Caller holds the application-root lock with every backend stopped.
        # Keep the original migration generation, selected generation, and its
        # immediate predecessor. A private inode-bound marker proves ownership.
        pending_path = self.root / 'history-pruning.json'
        initial = hashlib.sha256(canonical(prepared['selection']['initialization'])).hexdigest()
        while True:
            pending, _ = read_record(pending_path)
            if pending is None:
                targets = self.history_targets(prepared)
                if not targets:
                    return
                generation = targets[0]
                directory = self.root / ('generation-' + generation)
                with directory_lock(directory):
                    owner, _ = read_record(self.owner_path(generation))
                    archive.closed(owner, ('schema', 'identity', 'role', 'generation', 'job_id', 'initialization_sha256', 'device', 'inode'))
                    info = directory.stat()
                    archive.require(owner['schema'] == 'recovery-generation-owner/1' and owner['identity'] == prepared['identity']
                                    and owner['role'] == prepared['role'] and owner['generation'] == generation
                                    and type(owner['job_id']) is str and archive.ID.fullmatch(owner['job_id'])
                                    and owner['initialization_sha256'] == initial and type(owner['device']) is int and type(owner['inode']) is int
                                    and owner['device'] == info.st_dev and owner['inode'] == info.st_ino)
                    pending = {'schema': 'recovery-history-pruning/1', 'owner': owner, 'files': history_inventory(directory)}
                    publish_record(pending_path, pending, None)
                self.checkpoint('history-pruning-admitted')
            archive.closed(pending, ('schema', 'owner', 'files'))
            owner = pending['owner']
            archive.closed(owner, ('schema', 'identity', 'role', 'generation', 'job_id', 'initialization_sha256', 'device', 'inode'))
            generation = owner['generation']
            archive.require(pending['schema'] == 'recovery-history-pruning/1' and decimal(generation)
                            and 1 < int(generation) < int(prepared['previous_generation']) and generation not in prepared['history_keep']
                            and owner['schema'] == 'recovery-generation-owner/1' and owner['identity'] == prepared['identity']
                            and owner['role'] == prepared['role'] and owner['initialization_sha256'] == initial
                            and type(owner['job_id']) is str and archive.ID.fullmatch(owner['job_id'])
                            and type(owner['device']) is int and owner['device'] >= 0 and type(owner['inode']) is int and owner['inode'] > 0)
            archive.require(type(pending['files']) is list and 0 < len(pending['files']) <= archive.MAX_FILES + 2)
            expected = {}
            for item in pending['files']:
                archive.closed(item, ('name', 'size', 'sha256'))
                archive.require(type(item['size']) is int and 0 <= item['size'] <= history_file_limit(item['name'])
                                and type(item['sha256']) is str and archive.HASH.fullmatch(item['sha256']) and item['name'] not in expected)
                expected[item['name']] = item
            archive.require('state.sqlite3' in expected and list(expected) == sorted(expected)
                            and sum(item['size'] for item in expected.values()) <= archive.MAX_PLAINTEXT + 66 * 1024 * 1024)
            owner_path = self.owner_path(generation)
            retained, _ = read_record(owner_path)
            archive.require(retained in (None, owner))
            directory = self.root / ('generation-' + generation)
            if directory.exists() or directory.is_symlink():
                archive.require(retained == owner)
                with directory_lock(directory):
                    info = directory.stat()
                    archive.require(info.st_dev == owner['device'] and info.st_ino == owner['inode'])
                    # Deletion can have stopped after any member. Every remaining
                    # file still has to match the durable pre-deletion inventory.
                    count = 0
                    for path in directory.rglob('*'):
                        count += 1
                        archive.require(count <= 4 * archive.MAX_FILES and not path.is_symlink())
                        if path.is_dir():
                            archive.private_directory(path)
                            relative = path.relative_to(directory).as_posix()
                            archive.require(relative in ('credentials', 'evidence', 'configuration.management', 'configuration.pi-recurrence-progress') or re.fullmatch(r'(credentials|evidence)/[0-9a-f]{32}', relative))
                            continue
                        name = path.relative_to(directory).as_posix()
                        archive.require(name in expected)
                        with archive.private_input(path, history_file_limit(name)) as (stream, info):
                            archive.require(info.st_size == expected[name]['size'] and archive.file_digest(stream) == expected[name]['sha256'])
                    shutil.rmtree(directory)
                    sync_directory(self.root)
                self.checkpoint('history-directory-removed')
            if retained is not None:
                archive.require(read_record(owner_path)[0] == owner)
                owner_path.unlink()
                sync_directory(self.root)
            pending_path.unlink()
            sync_directory(self.root)
            self.checkpoint('history-pruned')

    def finish_history(self, prepared):
        with directory_lock(self.root):
            selected, _ = read_record(self.root / 'authority.json')
            held, _ = read_record(self.root / 'restore-hold.json')
            archive.require(selected == prepared['selection'] and held == prepared['hold'])
            self.prune_history(prepared)

    def phase(self, record, phase):
        _, observation = read_record(self.journal)
        record = dict(record, phase=phase)
        publish_record(self.journal, record, observation)
        self.checkpoint(phase)
        return record

    def admit(self, prepared):
        # This proof is written by the actual backend validator, not the web
        # process or archive. Its fixed-root location is the OS trust boundary.
        archive.closed(prepared, ('schema', 'job_id', 'identity', 'role', 'archive_sha256', 'previous_generation',
                                  'generation', 'selection', 'hold', 'files', 'history_keep', 'destination_digest', 'backend_boot'))
        archive.require(prepared['schema'] == 'recovery-generation-prepared/1' and prepared['job_id'] == self.job.name
                        and prepared['role'] in ('pi', 'controller') and type(prepared['identity']) is str and archive.ID.fullmatch(prepared['identity'])
                        and type(prepared['backend_boot']) is str and archive.ID.fullmatch(prepared['backend_boot']) and type(prepared['archive_sha256']) is str and archive.HASH.fullmatch(prepared['archive_sha256'])
                        and type(prepared['destination_digest']) is str and archive.HASH.fullmatch(prepared['destination_digest']) and decimal(prepared['previous_generation'])
                        and decimal(prepared['generation']) and int(prepared['generation']) == int(prepared['previous_generation']) + 1)
        selector(prepared['selection'], prepared['identity'], prepared['role'], prepared['generation'], self.job.name)
        hold(prepared['hold'], prepared['identity'], prepared['role'], prepared['generation'], self.job.name)
        archive.require(prepared['hold']['stage'] == 'review')
        archive.require(type(prepared['history_keep']) is list and len(prepared['history_keep']) <= 64 * 3
                        and all(decimal(item) for item in prepared['history_keep'])
                        and prepared['history_keep'] == sorted(set(prepared['history_keep'])))
        validate_files(prepared['files'])
        return prepared

    def publish(self, prepared):
        prepared = self.admit(prepared)
        with directory_lock(self.root):
            existing, _ = read_record(self.journal)
            if existing is not None:
                archive.require(existing['prepared'] == prepared)
                outcome = self._recover(existing)
                if outcome['state'] == 'restored_generation_held':
                    self.prune_history(prepared)
                    return outcome
                # Resume this same admitted data publication from its complete
                # private candidate. This never replays any external command.
                record = existing
            else:
                previous, _ = read_record(self.root / 'authority.json')
                previous_hold, _ = read_record(self.root / 'restore-hold.json')
                selector(previous, prepared['identity'], prepared['role'], prepared['previous_generation'], self.job.name)
                hold(previous_hold, prepared['identity'], prepared['role'], prepared['previous_generation'], self.job.name)
                archive.require(previous_hold['stage'] == 'maintenance')
                record = self.phase({'schema': 'recovery-publication/1', 'prepared': prepared,
                                     'previous_selection': previous, 'previous_hold': previous_hold}, 'admitted')
            candidate = self.job / 'candidate'
            verify_files(candidate, prepared['files'])
            sync_tree(candidate)
            record = self.phase(record, 'candidate-verified')
            destination = self.root / ('generation-' + prepared['generation'])
            try:
                move_no_replace(candidate, destination)
                sync_directory(self.job)
            except OSError as error:
                if error.errno != errno.EXDEV:
                    raise
                # Fixed-root cross-filesystem staging remains streamed and
                # private; no backend can observe it before selector publication.
                archive.require(shutil.disk_usage(self.root).free >= sum(item['size'] + 4096 for item in prepared['files']))
                staging = self.root / ('generation-' + prepared['generation'] + '.restore-' + self.job.name)
                if staging.exists():
                    discard_partial_copy(staging, prepared['files'])
                staging.mkdir(mode=0o700)
                for item in prepared['files']:
                    source, target = candidate / item['name'], staging / item['name']
                    target.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
                    for parent in target.parents:
                        if parent == staging:
                            break
                        parent.chmod(0o700)
                    with archive.private_input(source, file_limit(item['name'])) as (input_stream, _):
                        descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
                        with os.fdopen(descriptor, 'wb') as output:
                            archive.require(archive.copy_exact(input_stream, output, item['size']) == item['sha256'])
                            output.flush()
                            os.fsync(output.fileno())
                verify_files(staging, prepared['files'])
                sync_tree(staging)
                move_no_replace(staging, destination)
            sync_directory(self.root)
            record = self.phase(record, 'generation-ready')
            outcome = self._recover(record)
            self.prune_history(prepared)
            return outcome

    def recover(self):
        with directory_lock(self.root):
            record, _ = read_record(self.journal)
            if record is None:
                return None
            outcome = self._recover(record)
            if outcome['state'] == 'restored_generation_held':
                self.prune_history(record['prepared'])
            return outcome

    def _recover(self, record):
        archive.closed(record, ('schema', 'prepared', 'previous_selection', 'previous_hold', 'phase'))
        archive.require(record['schema'] == 'recovery-publication/1' and record['phase'] in
                        ('admitted', 'candidate-verified', 'generation-ready', 'publishing', 'published'))
        prepared = self.admit(record['prepared'])
        previous = record['previous_selection']
        previous_hold = record['previous_hold']
        selector(previous, prepared['identity'], prepared['role'], prepared['previous_generation'], self.job.name)
        hold(previous_hold, prepared['identity'], prepared['role'], prepared['previous_generation'], self.job.name)
        archive.require(previous_hold['stage'] == 'maintenance')
        current, current_observation = read_record(self.root / 'authority.json')
        current_hold, hold_observation = read_record(self.root / 'restore-hold.json')
        archive.require(current in (previous, prepared['selection']) and current_hold in (previous_hold, prepared['hold']))
        destination = self.root / ('generation-' + prepared['generation'])
        if not destination.exists():
            # An interrupted pre-publication copy never changes selected files.
            archive.require(current == previous and current_hold == previous_hold)
            return {'state': 'prior_generation_held', 'generation': prepared['previous_generation']}
        verify_files(destination, prepared['files'])
        self.generation_owner(prepared)
        if record['phase'] == 'published' and current == prepared['selection'] and current_hold == prepared['hold']:
            return {'state': 'restored_generation_held', 'generation': prepared['generation']}
        record = self.phase(record, 'publishing')
        if current_hold != prepared['hold']:
            publish_record(self.root / 'restore-hold.json', prepared['hold'], hold_observation)
        self.checkpoint('hold-published')
        if current != prepared['selection']:
            publish_record(self.root / 'authority.json', prepared['selection'], current_observation)
        self.checkpoint('selection-published')
        self.phase(record, 'published')
        return {'state': 'restored_generation_held', 'generation': prepared['generation']}


def recover_hold_preparation(application_root, job_directory):
    """Finish one admitted hold while no backend owns the application root."""
    root, job = archive.private_directory(application_root), archive.private_directory(job_directory)
    with directory_lock(root):
        intent, _ = read_record(root / 'restore-preparation.json')
        if intent is None:
            return None
        archive.closed(intent, ('schema', 'job_id', 'identity', 'role', 'generation', 'previous_selection',
                               'previous_hold', 'selection', 'hold', 'backend_boot'))
        archive.require(intent['schema'] == 'recovery-hold-prepared/1' and intent['job_id'] == job.name
                        and archive.ID.fullmatch(job.name) and type(intent['identity']) is str and archive.ID.fullmatch(intent['identity'])
                        and intent['role'] in ('pi', 'controller') and decimal(intent['generation'])
                        and type(intent['backend_boot']) is str and archive.ID.fullmatch(intent['backend_boot']))
        selector(intent['selection'], intent['identity'], intent['role'], intent['generation'], job.name)
        hold(intent['hold'], intent['identity'], intent['role'], intent['generation'], job.name)
        archive.require(intent['hold']['stage'] == 'maintenance')
        selected, selected_observation = read_record(root / 'authority.json')
        held, held_observation = read_record(root / 'restore-hold.json')
        archive.require(selected in (intent['previous_selection'], intent['selection'])
                        and held in (intent['previous_hold'], intent['hold']))
        proof, proof_observation = read_record(job / 'hold-prepared.json')
        if proof is None:
            publish_record(job / 'hold-prepared.json', intent, proof_observation)
        else:
            archive.require(proof == intent)
        if held != intent['hold']:
            publish_record(root / 'restore-hold.json', intent['hold'], held_observation)
        if selected != intent['selection']:
            publish_record(root / 'authority.json', intent['selection'], selected_observation)
        (root / 'restore-preparation.json').unlink()
        sync_directory(root)
        return {'state': 'maintenance_held', 'generation': intent['generation']}
