#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Durable maintenance admission/upload/retention without production state."""
import base64
import fcntl
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import call, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive
from application_recovery.durable import publish_record, read_record
from application_recovery.jobs import BackendUnavailable, RecoveryBusy, RecoveryJobs, backend_command

class RecoveryJobsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='recovery-jobs-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.config = {'root': str(self.root), 'application_root': str(self.root / 'application'),
                       'backend_runtime': str(self.root / 'backend'), 'backend_command': ['/usr/bin/false'],
                       'lifecycle': {'kind': 'systemd'}, 'age': '/usr/bin/false', 'identity': 'a' * 32, 'role': 'controller'}
        self.owner = RecoveryJobs(self.config)
        self.sequence = 0

    def upload(self):
        self.sequence += 1
        return self.owner.command({'action': 'restore.upload.start', 'request_id': f'{self.sequence:032x}', 'size': '8'})

    def cancel(self, job):
        self.owner.command({'action': 'cancel', 'job_id': job['job_id']})

    def registry(self):
        return read_record(self.root / 'jobs.json')

    def test_busy_does_not_leave_orphan_and_duplicate_chunk_is_exact(self):
        job = self.upload()
        before = self.registry()[1]
        with self.assertRaises(RecoveryBusy):
            self.upload()
        self.assertEqual(self.registry()[1], before)
        self.assertEqual(len(list((self.root / 'jobs').iterdir())), 1)
        value = {'action': 'restore.upload.chunk', 'job_id': job['job_id'], 'offset': '0',
                 'bytes': base64.b64encode(b'12345678').decode()}
        self.assertEqual(self.owner.command(value), {'offset': '8'})
        self.assertEqual(self.owner.command(value), {'offset': '8'})
        with self.assertRaises(archive.RecoveryError):
            self.owner.command(dict(value, bytes=base64.b64encode(b'92345678').decode()))
        self.cancel(job)

    def test_terminal_job_before_registry_clear_recovers_without_replay(self):
        job = self.upload()
        descriptor = self.owner.uploads.pop(job['job_id'])
        os.close(descriptor)
        self.owner.save(dict(self.owner.read(job['job_id']), state='failed', error='interrupted'))
        recovered = RecoveryJobs(self.config)
        self.assertIsNone(recovered.registry['active'])
        self.assertEqual(recovered.read(job['job_id'])['state'], 'failed')

    def test_unregistered_created_admission_is_preserved_as_interrupted(self):
        job = self.owner.new_job('b' * 32, 'restore')
        value, observation = self.registry()
        publish_record(self.root / 'jobs.json', dict(value, jobs=[], active=None), observation)
        recovered = RecoveryJobs(self.config)
        self.assertEqual(recovered.registry['jobs'], [job['job_id']])
        self.assertIsNone(recovered.registry['active'])
        self.assertEqual(recovered.read(job['job_id'])['error'], 'interrupted')

    def test_retention_pins_download_and_resumes_interrupted_pruning(self):
        jobs = []
        for _ in range(4):
            job = self.upload()
            self.cancel(job)
            jobs.append(job)
        artifact = self.root / 'jobs' / jobs[0]['job_id'] / 'upload.age'
        # A separate descriptor models the broker's pinned download resource.
        with artifact.open('rb') as stream:
            fcntl.flock(stream, fcntl.LOCK_SH | fcntl.LOCK_NB)
            with self.assertRaises(RecoveryBusy):
                self.upload()
            self.assertEqual(len(self.owner.registry['jobs']), 4)
        intent = {'schema': 'recovery-pruning/1', 'job_id': jobs[0]['job_id'],
                  'identity': self.config['identity'], 'role': self.config['role']}
        publish_record(self.root / 'pruning.json', intent, None)
        recovered = RecoveryJobs(self.config)
        self.assertEqual(len(recovered.registry['jobs']), 3)
        self.assertFalse(artifact.parent.exists())
        self.assertFalse((self.root / 'pruning.json').exists())
        # Replay after removal from registry and directory is also harmless.
        publish_record(self.root / 'pruning.json', intent, None)
        self.assertEqual(len(RecoveryJobs(self.config).registry['jobs']), 3)

    def test_polling_does_not_write_and_closed_commands_refuse(self):
        before = self.registry()[1]
        for _ in range(10):
            self.owner.command({'action': 'list'})
        self.assertEqual(self.registry()[1], before)
        for value in [{'action': 'list', 'extra': True}, {'action': 'status', 'job_id': '../state'},
                      {'action': 'restore.upload.start', 'request_id': 'c' * 32, 'size': '08'}]:
            with self.assertRaises(archive.RecoveryError):
                self.owner.command(value)

    def test_confirmed_failure_cannot_be_cancelled(self):
        job = self.upload()
        descriptor = self.owner.uploads.pop(job['job_id'])
        os.close(descriptor)
        self.owner.save(dict(self.owner.read(job['job_id']), state='failed', error='restore_failed', confirmation_sha256='d' * 64))
        before = self.owner.read(job['job_id'])
        with self.assertRaises(archive.RecoveryError):
            self.cancel(job)
        self.assertEqual(self.owner.read(job['job_id']), before)

    def test_restore_retries_only_definite_prepare_busy(self):
        cases = (
            ('busy then accepted', [RecoveryBusy(), {}], [0, 1], True),
            ('busy then lost reply', [RecoveryBusy(), BackendUnavailable()], [0, 1], True),
            ('busy past deadline', [RecoveryBusy(), RecoveryBusy()], [0, 1, 60], False),
        )
        for label, responses, times, reads_proof in cases:
            with self.subTest(label=label):
                job_id = self.upload()['job_id']
                os.close(self.owner.uploads.pop(job_id))
                job = self.owner.save(dict(self.owner.read(job_id), state='applying', confirmation_sha256='d' * 64))
                with (patch('application_recovery.jobs.backend_command', side_effect=responses) as command,
                      patch('application_recovery.jobs.time.monotonic', side_effect=times),
                      patch('application_recovery.jobs.time.sleep') as sleep,
                      patch.object(self.owner, 'stop_proof', side_effect=BackendUnavailable) as proof,
                      patch.object(self.owner.lifecycle, 'stop') as stop):
                    self.owner.apply(job_id)
                self.assertEqual(command.call_args_list, [call(self.config, 'prepare', job_id)] * 2)
                sleep.assert_called_once()
                if reads_proof:
                    proof.assert_called_once_with(job)
                else:
                    proof.assert_not_called()
                stop.assert_not_called()
                self.assertEqual(self.owner.read(job_id)['error'], 'restore_failed')

    def test_confirmed_failure_stays_pinned_while_other_terminal_jobs_prune(self):
        job = self.upload()
        descriptor = self.owner.uploads.pop(job['job_id'])
        os.close(descriptor)
        self.owner.save(dict(self.owner.read(job['job_id']), state='failed', error='restore_failed', confirmation_sha256='d' * 64))
        self.owner.registry['active'] = None
        self.owner.save_registry()
        for _ in range(6):
            other = self.upload()
            self.cancel(other)
        self.assertIn(job['job_id'], self.owner.registry['jobs'])
        self.assertTrue((self.root / 'jobs' / job['job_id'] / 'job.json').is_file())
        self.assertEqual(len(self.owner.registry['jobs']), 4)
        destination = Path(self.config['application_root'])
        destination.mkdir(mode=0o700)
        held = {'schema': 'application-restore-hold/1', 'identity': self.config['identity'], 'role': self.config['role'],
                'generation': '1', 'revision': '2', 'stage': 'review', 'job_id': job['job_id'],
                'local_output': False, 'remote_authority': False}
        publish_record(destination / 'restore-hold.json', held, None)
        other = self.upload()
        self.cancel(other)
        self.assertNotIn(job['job_id'], self.owner.registry['jobs'])

    def test_offline_copy_can_outlive_small_control_query_deadline(self):
        child = self.root / 'slow-offline-owner'
        child.write_text('#!' + sys.executable + '\nimport json,time\ntime.sleep(13)\nprint(json.dumps({"state":"candidate_built"}))\n')
        child.chmod(0o700)
        config = dict(self.config, backend_command=[str(child)])
        self.assertEqual(backend_command(config, 'build-candidate', 'b' * 32), {'state': 'candidate_built'})

    def test_explicit_destination_refusal_does_not_pin_unapplied_job_forever(self):
        job = self.upload()
        descriptor = self.owner.uploads.pop(job['job_id'])
        os.close(descriptor)
        self.owner.save(dict(self.owner.read(job['job_id']), state='failed', error='destination_changed', confirmation_sha256='d' * 64))
        self.owner.registry['active'] = None
        self.owner.save_registry()
        for _ in range(4):
            other = self.upload()
            self.cancel(other)
        self.assertNotIn(job['job_id'], self.owner.registry['jobs'])

    def test_cli_summaries_fit_control_reply_with_four_retained_large_previews(self):
        for _ in range(4):
            job = self.upload()
            self.cancel(job)
            saved = self.owner.read(job['job_id'])
            self.owner.save(dict(saved, preview={'schema': 'recovery-preview/1', 'description': 'x' * 65000}))
        self.assertGreater(len(json.dumps(self.owner.command({'action': 'list'}))), 144 * 1024)
        summary = self.owner.command({'action': 'list.summary'})
        self.assertEqual(len(summary['jobs']), 4)
        self.assertLess(len(json.dumps(summary)), 8192)
        self.assertTrue(all(job['schema'] == 'recovery-job-summary/1' for job in summary['jobs']))
        detail = self.owner.command({'action': 'status', 'job_id': job['job_id']})
        self.assertEqual(len(detail['preview']['description']), 65000)

if __name__ == '__main__':
    unittest.main()
