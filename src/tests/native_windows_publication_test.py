# SPDX-License-Identifier: MIT
"""Shared publication engine with real native admissions and private fixtures.

The logical SQL bytes are synthetic; SQL interpretation and production relay
qualification belong to their separate application tests.
"""
import errno
import hashlib
import os
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from application_recovery import archive, native_files, publication
from application_recovery.durable import publish_record, read_record


def run(parent):
    root = native_files.make_directory(parent / 'publication')
    initial = {'schema': 'state-initialization/1', 'identity': 'a' * 32, 'role': 'controller', 'origin': 'fresh',
               'source_namespace': 'fresh', 'generation': '1', 'source_revision': 'b' * 64, 'images_digest': 'c' * 64}
    selection = {'schema': 'application-generation/1', 'identity': 'a' * 32, 'role': 'controller',
                 'generation': '1', 'initialization': initial, 'restore_job': '0' * 31 + '2'}
    held = {'schema': 'application-restore-hold/1', 'identity': 'a' * 32, 'role': 'controller', 'generation': '1',
            'job_id': selection['restore_job'], 'local_output': True, 'remote_authority': True, 'revision': '1', 'stage': 'maintenance'}
    publish_record(root / 'authority.json', selection, None)
    publish_record(root / 'restore-hold.json', held, None)

    def prepare(number):
        job = native_files.make_directory(parent / f'{number:032x}')
        candidate = native_files.make_directory(job / 'candidate')
        native_files.initialize_database_snapshot(candidate)
        payload = b'synthetic stopped SQL snapshot ' + str(number).encode()
        with native_files.private_output(candidate / 'state.sqlite3', len(payload)) as (stream, _):
            stream.write(payload)
        previous, stamp = read_record(root / 'authority.json')
        previous = dict(previous, restore_job=job.name)
        publish_record(root / 'authority.json', previous, stamp)
        prior_hold, stamp = read_record(root / 'restore-hold.json')
        prior_hold = dict(prior_hold, job_id=job.name, stage='maintenance', revision=str(number * 2))
        publish_record(root / 'restore-hold.json', prior_hold, stamp)
        prepared = {'schema': 'recovery-generation-prepared/1', 'job_id': job.name, 'identity': 'a' * 32, 'role': 'controller',
                    'archive_sha256': 'd' * 64, 'previous_generation': str(number - 1), 'generation': str(number),
                    'selection': dict(previous, generation=str(number)),
                    'hold': dict(prior_hold, generation=str(number), revision=str(number * 2 + 1), stage='review'),
                    'files': [{'name': 'state.sqlite3', 'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}],
                    'history_keep': [], 'destination_digest': 'e' * 64, 'backend_boot': 'f' * 32}
        return job, prepared

    job, prepared = prepare(2)
    owner = publication.GenerationPublisher(root, job)
    assert owner.publish(prepared) == {'state': 'restored_generation_held', 'generation': '2'}
    assert owner.publish(prepared) == owner.recover()
    receipt, _ = read_record(root / 'generation-2.owner.json')
    metadata = native_files.path_info(root / 'generation-2')
    assert receipt['device'] == metadata.st_dev and receipt['inode'] == int.from_bytes(bytes.fromhex(metadata.native_file_id), 'little')
    assert (root / 'generation-2/state.sqlite3.owner.lock').is_file()

    # Exercise the common streaming branch using explicit fault injection. This
    # does not qualify a second physical volume or claim a native EXDEV event.
    job, prepared = prepare(3)
    original_move = publication.move_no_replace
    def separate_volume(source, destination, lease=None):
        if source == job / 'candidate':
            raise OSError(errno.EXDEV, 'explicit software-only separate-volume fault')
        return original_move(source, destination, lease)
    def no_space(source, destination, size):
        destination.write(source.read(min(size, 7)))
        raise OSError(errno.ENOSPC, 'explicit interrupted staging fault')
    owner = publication.GenerationPublisher(root, job)
    with patch.object(publication, 'move_no_replace', side_effect=separate_volume):
        with patch.object(archive, 'copy_exact', side_effect=no_space):
            try:
                owner.publish(prepared)
            except OSError as error:
                assert error.errno == errno.ENOSPC
            else:
                raise AssertionError('Interrupted staging was accepted')
        assert read_record(root / 'authority.json')[0]['generation'] == '2'
        assert owner.publish(prepared)['generation'] == '3'

    # Resume a durable admitted history deletion after the logical member is
    # already gone; the physical SQL marker remains until data retirement ends.
    job, prepared = prepare(4)
    def stop(phase):
        if phase == 'history-pruning-admitted':
            raise InterruptedError('explicit admitted history interruption')
    try:
        publication.GenerationPublisher(root, job, stop).publish(prepared)
    except InterruptedError:
        pass
    else:
        raise AssertionError('History checkpoint did not run')
    old = root / 'generation-2'
    (old / 'state.sqlite3').unlink()
    assert publication.GenerationPublisher(root, job).recover()['generation'] == '4'
    assert not old.exists() and not (root / 'history-pruning.json').exists()

    # A matching durable receipt permits only the empty-directory final retry;
    # an absent marker with remaining SQL data must never be repaired.
    job, prepared = prepare(5)
    try:
        publication.GenerationPublisher(root, job, stop).publish(prepared)
    except InterruptedError:
        pass
    old = root / 'generation-3'
    marker = old / 'state.sqlite3.owner.lock'
    marker.unlink()
    try:
        publication.GenerationPublisher(root, job).recover()
    except (OSError, archive.RecoveryError):
        pass
    else:
        raise AssertionError('Missing established marker was accepted')
    assert (old / 'state.sqlite3').exists() and not marker.exists()
    (old / 'state.sqlite3').unlink()
    assert publication.GenerationPublisher(root, job).recover()['generation'] == '5'
    assert not old.exists()
    for number, phase in enumerate(('candidate-verified', 'generation-ready', 'hold-published',
                                     'selection-published', 'history-pruning-admitted'), start=6):
        job, prepared = prepare(number)
        publish_record(job / 'prepared.json', prepared, None)
        result = subprocess.run([sys.executable, '-I', '-B', '-X', 'utf8', str(Path(__file__).resolve()),
                                 sys.argv[1], str(root), str(job), phase], cwd=parent,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=25)
        assert result.returncode == 73, (phase, result.returncode, result.stdout, result.stderr)
        # The prior or new selector is still explicitly held after process loss.
        assert read_record(root / 'restore-hold.json')[0]['local_output'] is True
        assert read_record(root / 'restore-hold.json')[0]['remote_authority'] is True
        assert publication.GenerationPublisher(root, job).publish(prepared)['generation'] == str(number)
        assert publication.GenerationPublisher(root, job).recover()['generation'] == str(number)
    print('PASS shared Windows publication, full-ID receipts, persistent review holds, interrupted staged copy, history retirement, missing-marker refusal, and five actual process deaths; synthetic SQL and volume faults only')


if __name__ == '__main__':
    assert sys.platform == 'win32' and len(sys.argv) == 5
    native_files.configure(Path(sys.argv[1]))
    root, job = map(Path, sys.argv[2:4])
    phase = sys.argv[4]
    assert phase in ('candidate-verified', 'generation-ready', 'hold-published', 'selection-published', 'history-pruning-admitted')
    def die(value):
        if value == phase:
            os._exit(73)
    publication.GenerationPublisher(root, job, die).publish(read_record(job / 'prepared.json')[0])
    raise AssertionError('Selected crash checkpoint was not reached')
