#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual process deaths across private multi-file generation publication."""
import hashlib
import errno
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive
from application_recovery.durable import canonical, directory_lock, publish_record, read_record
from application_recovery.publication import GenerationPublisher
from application_recovery import publication


class PublicationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='recovery-publication-')
        self.addCleanup(self.temporary.cleanup)
        self.parent = Path(self.temporary.name).resolve()
        self.root, self.job = self.parent / 'application', self.parent / ('b' * 32)
        for path in (self.root, self.job, self.root / 'generation-1', self.job / 'candidate'):
            path.mkdir(mode=0o700)
        self.identity = 'a' * 32
        initial = {'schema': 'state-initialization/1', 'identity': self.identity, 'role': 'controller', 'generation': '1',
                   'origin': 'fresh', 'source_namespace': 'fresh', 'source_revision': 'c' * 64, 'images_digest': 'd' * 64}
        self.previous = {'schema': 'application-generation/1', 'identity': self.identity, 'role': 'controller', 'generation': '1',
                         'initialization': initial, 'restore_job': self.job.name}
        self.previous_hold = {'schema': 'application-restore-hold/1', 'identity': self.identity, 'role': 'controller',
                              'generation': '1', 'job_id': self.job.name, 'local_output': True, 'remote_authority': True,
                              'revision': '1', 'stage': 'maintenance'}
        publish_record(self.root / 'authority.json', self.previous, None)
        publish_record(self.root / 'restore-hold.json', self.previous_hold, None)
        self.old = b'old complete generation and retained recovery evidence'
        self.new = b'new complete generation with retained pending evidence'
        for path, data in [(self.root / 'generation-1/state.sqlite3', self.old), (self.job / 'candidate/state.sqlite3', self.new),
                           (self.job / 'candidate/configuration.udp-logs.json', b'{"synthetic":"new policy"}')]:
            path.write_bytes(data)
            path.chmod(0o600)
        files = [{'name': path.name, 'size': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                 for path in sorted((self.job / 'candidate').iterdir())]
        self.prepared = {'schema': 'recovery-generation-prepared/1', 'job_id': self.job.name, 'identity': self.identity,
                         'role': 'controller', 'archive_sha256': 'e' * 64, 'previous_generation': '1', 'generation': '2',
                         'selection': dict(self.previous, generation='2'), 'hold': dict(self.previous_hold, generation='2', revision='2', stage='review'),
                         'files': files, 'history_keep': [], 'destination_digest': 'f' * 64, 'backend_boot': 'c' * 32}
        publish_record(self.job / 'prepared.json', self.prepared, None)

    def assert_held_generation(self, expected):
        selection, _ = read_record(self.root / 'authority.json')
        hold, _ = read_record(self.root / 'restore-hold.json')
        self.assertEqual(selection['generation'], expected)
        self.assertEqual(hold['generation'], expected)
        self.assertIs(hold['local_output'], True)
        self.assertIs(hold['remote_authority'], True)
        chosen = self.root / ('generation-' + expected)
        self.assertEqual((chosen / 'state.sqlite3').read_bytes(), self.old if expected == '1' else self.new)
        self.assertEqual((self.root / 'generation-1/state.sqlite3').read_bytes(), self.old)
        if expected == '2':
            self.assertEqual((chosen / 'configuration.udp-logs.json').read_bytes(), b'{"synthetic":"new policy"}')

    def next_restore(self, generation):
        job = self.parent / f'{generation:032x}'
        job.mkdir(mode=0o700)
        candidate = job / 'candidate'
        candidate.mkdir(mode=0o700)
        data = ('synthetic complete generation ' + str(generation)).encode()
        (candidate / 'state.sqlite3').write_bytes(data)
        (candidate / 'state.sqlite3').chmod(0o600)
        previous, stamp = read_record(self.root / 'authority.json')
        previous = dict(previous, restore_job=job.name)
        publish_record(self.root / 'authority.json', previous, stamp)
        held, stamp = read_record(self.root / 'restore-hold.json')
        held = dict(held, job_id=job.name, stage='maintenance', revision=str(int(held['revision']) + 1))
        publish_record(self.root / 'restore-hold.json', held, stamp)
        prepared = dict(self.prepared, job_id=job.name, previous_generation=str(generation - 1), generation=str(generation),
                        selection=dict(previous, generation=str(generation)),
                        hold=dict(held, generation=str(generation), stage='review', revision=str(int(held['revision']) + 1)),
                        files=[{'name': 'state.sqlite3', 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}])
        publish_record(job / 'prepared.json', prepared, None)
        return job, prepared

    def history_ready(self):
        GenerationPublisher(self.root, self.job).publish(self.prepared)
        job, prepared = self.next_restore(3)
        GenerationPublisher(self.root, job).publish(prepared)
        return self.next_restore(4)

    def test_repeated_restore_retains_original_selected_and_one_predecessor(self):
        job, prepared = self.history_ready()
        GenerationPublisher(self.root, job).publish(prepared)
        self.assertEqual({path.name for path in self.root.glob('generation-*') if path.is_dir()},
                         {'generation-1', 'generation-3', 'generation-4'})
        self.assertEqual((self.root / 'generation-1/state.sqlite3').read_bytes(), self.old)
        self.assertFalse((self.root / 'generation-2.owner.json').exists())
        self.assertFalse((self.root / 'history-pruning.json').exists())
        self.assertEqual(read_record(self.root / 'authority.json')[0]['generation'], '4')

    def test_history_process_death_and_partial_deletion_resume(self):
        for phase in ('history-pruning-admitted', 'history-directory-removed', 'partial-delete'):
            with self.subTest(phase=phase):
                # Each case needs its own independently published history.
                if phase != 'history-pruning-admitted':
                    self.tearDown()
                    self.setUp()
                job, prepared = self.history_ready()
                code = """import os,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from application_recovery import publication
from application_recovery.durable import read_record
root,job=map(Path,sys.argv[2:4]); phase=sys.argv[4]
if phase=='partial-delete':
 def interrupted(path):
  next(item for item in path.rglob('*') if item.is_file()).unlink();os._exit(73)
 publication.shutil.rmtree=interrupted
owner=publication.GenerationPublisher(root,job,lambda value:os._exit(73) if value==phase else None)
owner.publish(read_record(job/'prepared.json')[0])
"""
                result = subprocess.run([sys.executable, '-c', code, str(Path(__file__).resolve().parents[2] / 'src'),
                                         str(self.root), str(job), phase], timeout=20)
                self.assertEqual(result.returncode, 73)
                self.assertTrue((self.root / 'history-pruning.json').is_file())
                self.assertEqual(GenerationPublisher(self.root, job).recover(), {'state': 'restored_generation_held', 'generation': '4'})
                self.assertFalse((self.root / 'generation-2').exists())
                self.assertFalse((self.root / 'history-pruning.json').exists())
                self.assertTrue((self.root / 'generation-1/state.sqlite3').is_file())
                self.assertTrue((self.root / 'generation-3/state.sqlite3').is_file())
                self.assertTrue((self.root / 'generation-4/state.sqlite3').is_file())

    def test_current_catalog_credential_dependency_keeps_older_generation(self):
        job, prepared = self.history_ready()
        prepared['history_keep'] = ['2']
        publish_record(job / 'prepared.json', prepared, read_record(job / 'prepared.json')[1])
        GenerationPublisher(self.root, job).publish(prepared)
        self.assertTrue((self.root / 'generation-2/state.sqlite3').is_file())
        self.assertTrue((self.root / 'generation-2.owner.json').is_file())
        self.assertFalse((self.root / 'history-pruning.json').exists())

    def test_replaced_directory_is_not_adopted_as_owned_history(self):
        job, prepared = self.history_ready()
        original = self.root / 'generation-2'
        original.rename(self.root / 'retained-user-data')
        original.mkdir(mode=0o700)
        path = original / 'state.sqlite3'
        path.write_bytes(self.new)
        path.chmod(0o600)
        with self.assertRaises(archive.RecoveryError):
            GenerationPublisher(self.root, job).publish(prepared)
        self.assertEqual(path.read_bytes(), self.new)
        self.assertTrue((self.root / 'retained-user-data/state.sqlite3').is_file())

    def test_unknown_history_content_refuses_without_deleting_operator_data(self):
        job, prepared = self.history_ready()
        unexpected = self.root / 'generation-2/operator-note.txt'
        unexpected.write_text('preserve unknown data')
        unexpected.chmod(0o600)
        with self.assertRaises(archive.RecoveryError):
            GenerationPublisher(self.root, job).publish(prepared)
        self.assertEqual(unexpected.read_text(), 'preserve unknown data')
        self.assertTrue((self.root / 'generation-2/state.sqlite3').is_file())
        self.assertTrue((self.root / 'generation-3/state.sqlite3').is_file())
        self.assertTrue((self.root / 'generation-4/state.sqlite3').is_file())

    def test_complete_publication_and_duplicate_have_one_held_outcome(self):
        publisher = GenerationPublisher(self.root, self.job)
        expected = {'state': 'restored_generation_held', 'generation': '2'}
        self.assertEqual(publisher.publish(self.prepared), expected)
        before = read_record(self.job / 'publication.json')[1]
        self.assertEqual(publisher.publish(self.prepared), expected)
        self.assertEqual(read_record(self.job / 'publication.json')[1], before)
        self.assert_held_generation('2')
        self.assertFalse((self.job / 'candidate').exists())

    def test_stale_destination_refuses_before_generation_move(self):
        changed = dict(self.previous, restore_job='d' * 32)
        publish_record(self.root / 'authority.json', changed, read_record(self.root / 'authority.json')[1])
        with self.assertRaises(archive.RecoveryError):
            GenerationPublisher(self.root, self.job).publish(self.prepared)
        self.assertTrue((self.job / 'candidate/state.sqlite3').exists())
        self.assertFalse((self.root / 'generation-2').exists())

    def test_interrupted_cross_filesystem_copy_can_resume_same_publication(self):
        original_move, original_copy = publication.move_no_replace, archive.copy_exact
        def cross_device(source, destination):
            if source == self.job / 'candidate':
                raise OSError(errno.EXDEV, 'synthetic separate filesystem')
            return original_move(source, destination)
        def interrupted_copy(source, destination, size):
            destination.write(source.read(min(size, 7)))
            raise OSError(errno.ENOSPC, 'synthetic interrupted copy')
        publisher = GenerationPublisher(self.root, self.job)
        with patch.object(publication, 'move_no_replace', side_effect=cross_device):
            with patch.object(archive, 'copy_exact', side_effect=interrupted_copy), self.assertRaises(OSError):
                publisher.publish(self.prepared)
            self.assert_held_generation('1')
            self.assertEqual(publisher.publish(self.prepared)['state'], 'restored_generation_held')
        self.assert_held_generation('2')

    def test_backend_directory_lock_excludes_publication(self):
        # flock is cross-process; a separate process holds the backend's exact
        # root lock while this worker attempts publication.
        program = 'import fcntl,os,sys;f=os.open(sys.argv[1],os.O_RDONLY);fcntl.flock(f,fcntl.LOCK_EX);print("ready",flush=True);sys.stdin.read()'
        child = subprocess.Popen([sys.executable, '-c', program, str(self.root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), 'ready')
            with self.assertRaises(archive.RecoveryError):
                GenerationPublisher(self.root, self.job).publish(self.prepared)
            self.assertFalse((self.root / 'generation-2').exists())
        finally:
            child.communicate('', timeout=5)
        self.assertEqual(child.returncode, 0)

    def test_corrupt_candidate_and_unknown_preparation_do_not_publish(self):
        for field, value in [('schema', 'recovery-generation-prepared/2'), ('generation', '../old'), ('identity', False)]:
            with self.subTest(field=field), self.assertRaises(archive.RecoveryError):
                GenerationPublisher(self.root, self.job).publish(dict(self.prepared, **{field: value}))
        (self.job / 'candidate/state.sqlite3').write_bytes(b'tampered')
        with self.assertRaises(archive.RecoveryError):
            GenerationPublisher(self.root, self.job).publish(self.prepared)
        self.assert_held_generation('1')

    def test_unsafe_generation_members_refuse_before_selection(self):
        (self.job / 'candidate/unexpected').write_text('not a supported record')
        with self.assertRaises(archive.RecoveryError):
            GenerationPublisher(self.root, self.job).publish(self.prepared)
        self.assert_held_generation('1')


class CrashPublicationTest(unittest.TestCase):
    def test_every_durable_boundary_recovers_one_complete_held_generation(self):
        for point in ('admitted', 'candidate-verified', 'generation-ready', 'publishing', 'hold-published', 'selection-published', 'published'):
            fixture = PublicationTest('test_complete_publication_and_duplicate_have_one_held_outcome')
            fixture.setUp()
            try:
                with self.subTest(point=point):
                    child = subprocess.run([sys.executable, '-B', __file__, '--crash', str(fixture.root), str(fixture.job), point],
                                           capture_output=True, text=True, timeout=10)
                    self.assertEqual(child.returncode, 73, child.stderr)
                    outcome = GenerationPublisher(fixture.root, fixture.job).recover()
                    expected = '1' if point in ('admitted', 'candidate-verified') else '2'
                    self.assertEqual(outcome['generation'], expected)
                    fixture.assert_held_generation(expected)
            finally:
                fixture.doCleanups()


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--crash':
        root, job, point = Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
        GenerationPublisher(root, job, lambda current: os._exit(73) if current == point else None).publish(read_record(job / 'prepared.json')[0])
        raise SystemExit(74)
    unittest.main()
