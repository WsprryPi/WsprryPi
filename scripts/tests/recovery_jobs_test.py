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
import threading
import unittest
from unittest import mock
from unittest.mock import call, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive
from application_recovery.durable import publish_record, read_record
from application_recovery.jobs import BackendUnavailable, RecoveryBusy, RecoveryJobs, backend_command
from application_recovery.state_owner import OwnerBusy, OwnerUnavailable

class RecoveryJobsTest(unittest.TestCase):
    def test_export_refusal_is_backend_unavailable_without_replay(self):
        relay = mock.Mock()
        relay.collect.side_effect = OwnerUnavailable('fixture export refusal')
        self.owner.state_owner = relay
        with self.assertRaises(BackendUnavailable):
            self.owner.collect('a' * 32)
        relay.collect.assert_called_once_with('a' * 32)

    def test_capture_owner_refusal_retains_job_without_archive_or_replay(self):
        job = self.owner.new_job('c' * 32, 'backup')
        self.owner.save(dict(job, state='capturing'))
        self.owner.observe = mock.Mock(return_value={'pid': os.getpid(), 'boot': 'b' * 32})
        self.owner.backend = mock.Mock(side_effect=OwnerUnavailable('fixture owner refusal'))
        self.owner.backup(job['job_id'], b'synthetic fixture phrase')
        saved = self.owner.read(job['job_id'])
        self.assertEqual((saved['state'], saved['error'], saved['archive']), ('failed', 'backend_unavailable', None))
        self.assertIsNone(self.owner.registry['active'])
        self.owner.backend.assert_called_once_with('capture', job['job_id'])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='recovery-jobs-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.config = {'root': str(self.root), 'application_root': str(self.root / 'application'),
                       'backend_runtime': str(self.root / 'backend'), 'backend_command': ['/usr/bin/false'],
                       'backend_owner': None, 'lifecycle': {'kind': 'systemd'}, 'age': '/usr/bin/false', 'identity': 'a' * 32, 'role': 'controller'}
        self.owner = RecoveryJobs(self.config)
        self.sequence = 0

    def upload(self):
        self.sequence += 1
        request = {'action': 'restore.upload.start', 'request_id': f'{self.sequence:032x}', 'size': '8'}
        try:
            return self.owner.command(request)
        except RecoveryBusy:
            if self.owner.thread is None:
                raise
            self.owner.thread.join(3)
            self.assertFalse(self.owner.thread.is_alive())
            if len(self.owner.registry['jobs']) >= 4:
                raise
            return self.owner.command(request)

    def test_slow_retention_refuses_before_admission_without_blocking_reads(self):
        for _ in range(4):
            self.cancel(self.upload())
        started, release = threading.Event(), threading.Event()
        original = self.owner.prune
        def delayed():
            started.set()
            self.assertTrue(release.wait(3))
            original()
        request = {'action': 'restore.upload.start', 'request_id': 'f' * 32, 'size': '8'}
        with mock.patch.object(self.owner, 'prune', side_effect=delayed):
            try:
                with self.assertRaises(RecoveryBusy):
                    self.owner.command(request)
                self.assertTrue(started.wait(1))
                listed = self.owner.command({'action': 'list.summary'})
                self.assertIsNone(listed['active'])
                self.assertEqual(len(listed['jobs']), 4)
                self.assertFalse(any(job['request_id'] == request['request_id'] for job in listed['jobs']))
                with self.assertRaises(RecoveryBusy):
                    self.owner.command(request)
            finally:
                release.set()
                self.owner.thread.join(3)
            admitted = self.owner.command(request)
            self.assertEqual(admitted['request_id'], request['request_id'])
            self.cancel(admitted)

    def test_failed_retention_keeps_intent_and_recovers_before_new_admission(self):
        for _ in range(4):
            self.cancel(self.upload())
        with mock.patch('application_recovery.jobs.private_job_cleanup', side_effect=OSError('fixture cleanup failure')):
            with self.assertRaises(RecoveryBusy):
                self.owner.command({'action': 'restore.upload.start', 'request_id': 'e' * 32, 'size': '8'})
            self.owner.thread.join(3)
        self.assertTrue((self.root / 'pruning.json').exists())
        self.assertIsNone(self.owner.registry['active'])
        with self.assertRaises(OSError):
            self.owner.command({'action': 'restore.upload.start', 'request_id': 'e' * 32, 'size': '8'})
        self.owner = RecoveryJobs(self.config)
        self.assertFalse((self.root / 'pruning.json').exists())
        admitted = self.owner.command({'action': 'restore.upload.start', 'request_id': 'e' * 32, 'size': '8'})
        self.cancel(admitted)

    def test_owner_busy_after_retirement_intent_requires_restart_without_replay(self):
        for _ in range(4):
            self.cancel(self.upload())
        relay = mock.Mock()
        relay.call.side_effect = OwnerBusy('fixture prune owner contention')
        self.owner.state_owner = relay
        request = {'action': 'restore.upload.start', 'request_id': 'e' * 32, 'size': '8'}
        with self.assertRaises(RecoveryBusy):
            self.owner.command(request)
        self.owner.thread.join(3)
        self.assertTrue((self.root / 'pruning.json').exists())
        self.assertEqual(len(self.owner.command({'action': 'list.summary'})['jobs']), 4)
        with self.assertRaises(OSError):
            self.owner.command(request)
        relay.call.assert_called_once_with('prune-job', self.owner.registry['jobs'][0])

    def cancel(self, job):
        self.owner.command({'action': 'cancel', 'job_id': job['job_id']})

    def registry(self):
        return read_record(self.root / 'jobs.json')

    def retry_job(self, *, proof=True):
        job = self.upload()
        self.owner.command({'action': 'restore.upload.chunk', 'job_id': job['job_id'], 'offset': '0',
                            'bytes': base64.b64encode(b'12345678').decode()})
        self.owner.command({'action': 'restore.upload.finish', 'job_id': job['job_id']})
        self.owner.thread.join(3)
        self.assertFalse(self.owner.thread.is_alive())
        self.assertEqual(self.owner.read(job['job_id'])['state'], 'uploaded')
        self.owner.fail(job['job_id'], 'validation_failed')
        job = self.owner.save(dict(self.owner.read(job['job_id']), backend={'pid': os.getpid(), 'boot': 'b' * 32}))
        if proof:
            publish_record(self.root / 'jobs' / job['job_id'] / 'validation-result.json',
                           {'schema': 'recovery-validation-result/1', 'job_id': job['job_id'], 'backend_boot': 'b' * 32,
                            'state': 'failed', 'error': 'busy', 'preview_digest': None}, None)
        return job

    def test_retry_cleanup_does_not_block_commands_and_old_preview_stays_fenced(self):
        job = self.retry_job()
        entered, release = threading.Event(), threading.Event()
        owner = self.owner
        class SlowOwner:
            calls = 0
            def synchronize(self, job_id):
                saved = owner.read(job_id)
                if saved['state'] != 'failed' or saved['error'] != 'interrupted' or saved['backend'] is not None:
                    raise RuntimeError('Old validation was not durably fenced')
                self.calls += 1
                if self.calls < 3:
                    raise OwnerBusy('Definite directory-lock refusal')
                entered.set()
                if not release.wait(3):
                    raise RuntimeError('Cleanup did not run independently of the command')
        owner.state_owner = SlowOwner()
        try:
            reply = owner.command({'action': 'restore.validate', 'job_id': job['job_id'], 'purpose': 'recover', 'passphrase': 'fixture'})
            self.assertEqual(reply['state'], 'validating')
            self.assertTrue(entered.wait(2))
            self.assertEqual(owner.read(job['job_id'])['state'], 'failed')
            status = owner.command({'action': 'status', 'job_id': job['job_id']})
            self.assertEqual((status['state'], status['error'], status['purpose']), ('validating', None, 'recover'))
            self.assertEqual(owner.command({'action': 'list.summary'})['jobs'][0]['state'], 'validating')
            self.assertTrue((self.root / 'jobs' / job['job_id'] / 'validation-result.json').exists())
            with self.assertRaises(archive.RecoveryError):
                owner.command({'action': 'cancel', 'job_id': job['job_id']})
            with self.assertRaises(archive.RecoveryError):
                owner.command({'action': 'restore.confirm', 'job_id': job['job_id'], 'preview_digest': 'c' * 64})
        finally:
            release.set()
            if owner.thread is not None:
                owner.thread.join(3)
        self.assertFalse(owner.thread.is_alive())
        self.assertIsNone(owner.validation_reset)
        self.assertIsNone(owner.registry['active'])
        self.assertEqual(owner.state_owner.calls, 3)
        self.assertEqual(owner.read(job['job_id'])['state'], 'failed')

    def test_retry_without_owner_proof_does_not_queue_cleanup(self):
        job = self.retry_job(proof=False)
        before = self.owner.read(job['job_id'])
        with self.assertRaises(archive.RecoveryError):
            self.owner.command({'action': 'restore.validate', 'job_id': job['job_id'], 'purpose': 'recover', 'passphrase': 'fixture'})
        self.assertEqual(self.owner.read(job['job_id']), before)
        self.assertIsNone(self.owner.validation_reset)
        self.assertIsNone(self.owner.registry['active'])

    def test_crash_after_retry_fence_retains_archive_and_proof(self):
        job = self.retry_job()
        self.owner.reset_validation(job)
        self.owner.registry['active'] = job['job_id']
        self.owner.save_registry()
        recovered = RecoveryJobs(self.config)
        self.assertIsNone(recovered.registry['active'])
        saved = recovered.read(job['job_id'])
        self.assertEqual((saved['state'], saved['error'], saved['backend']), ('failed', 'interrupted', None))
        self.assertEqual(saved['archive'], job['archive'])
        self.assertTrue((self.root / 'jobs' / job['job_id'] / 'validation-result.json').exists())

    def test_retry_cleanup_does_not_replay_unknown_or_outwait_its_busy_budget(self):
        job = self.retry_job()
        self.owner.reset_validation(job)
        self.owner.state_owner = mock.Mock()
        self.owner.state_owner.synchronize.side_effect = archive.RecoveryError('Unknown owner outcome')
        with mock.patch('application_recovery.jobs.time.sleep') as pause, self.assertRaises(archive.RecoveryError):
            self.owner.cleanup_validation(job['job_id'], True)
        self.assertEqual(self.owner.state_owner.synchronize.call_count, 1)
        pause.assert_not_called()
        self.owner.state_owner.synchronize.reset_mock()
        self.owner.state_owner.synchronize.side_effect = OwnerBusy()
        with mock.patch('application_recovery.jobs.time.monotonic', side_effect=[0, 31]), \
                mock.patch('application_recovery.jobs.time.sleep') as pause, self.assertRaises(RecoveryBusy):
            self.owner.cleanup_validation(job['job_id'], True)
        self.assertEqual(self.owner.state_owner.synchronize.call_count, 1)
        pause.assert_not_called()
        self.assertTrue((self.root / 'jobs' / job['job_id'] / 'validation-result.json').exists())

    def preparation_job(self):
        job = self.retry_job()
        return self.owner.save(dict(job, state='applying', error=None, confirmation_sha256='d'*64,
                                    backend={'pid': os.getpid(), 'boot': 'b'*32, 'generation': '1'}))

    def test_preparation_retries_only_definite_busy_on_same_unheld_boot(self):
        job = self.preparation_job()
        current = dict(job['backend'], restore_hold={'maintenance_active': False})
        self.owner.backend = mock.Mock(side_effect=[RecoveryBusy(), RecoveryBusy(), None])
        self.owner.observe = mock.Mock(return_value=current)
        with mock.patch('application_recovery.jobs.time.sleep'):
            self.owner.prepare_backend(job)
        self.assertEqual(self.owner.backend.call_args_list, [mock.call('prepare', job['job_id'])]*3)
        self.assertEqual(self.owner.observe.call_count, 2)

    def test_preparation_unknown_reply_is_never_replayed(self):
        job = self.preparation_job()
        for replies in ([BackendUnavailable()], [RecoveryBusy(), BackendUnavailable()]):
            self.owner.backend = mock.Mock(side_effect=replies)
            self.owner.observe = mock.Mock(return_value=dict(job['backend'], restore_hold={'maintenance_active': False}))
            with mock.patch('application_recovery.jobs.time.sleep'):
                self.owner.prepare_backend(job)
            self.assertEqual(self.owner.backend.call_count, len(replies))
            self.assertEqual(self.owner.observe.call_count, len(replies)-1)

    def test_preparation_busy_does_not_cross_changed_identity_or_hold(self):
        job = self.preparation_job()
        current = dict(job['backend'], restore_hold={'maintenance_active': False})
        for changed in (dict(current, boot='c'*32), dict(current, pid=os.getpid()+1), dict(current, generation='2'),
                        dict(current, restore_hold={'maintenance_active': True})):
            self.owner.backend = mock.Mock(side_effect=RecoveryBusy())
            self.owner.observe = mock.Mock(return_value=changed)
            with self.assertRaises(archive.RecoveryError):
                self.owner.prepare_backend(job)
            self.assertEqual(self.owner.backend.call_count, 1)
        self.owner.backend = mock.Mock(side_effect=RecoveryBusy())
        self.owner.observe = mock.Mock(return_value=current)
        with mock.patch('application_recovery.jobs.time.sleep'), self.assertRaises(RecoveryBusy):
            self.owner.prepare_backend(job)
        self.assertEqual(self.owner.backend.call_count, 8)

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
        descriptor.close()
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
        descriptor.close()
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
                job = self.preparation_job()
                job_id = job['job_id']
                current = dict(job['backend'], restore_hold={'maintenance_active': False})
                with (patch('application_recovery.jobs.backend_command', side_effect=responses) as command,
                      patch('application_recovery.jobs.time.monotonic', side_effect=times),
                      patch('application_recovery.jobs.time.sleep') as sleep,
                      patch.object(self.owner, 'observe', return_value=current) as observe,
                      patch.object(self.owner, 'stop_proof', side_effect=BackendUnavailable) as proof,
                      patch.object(self.owner.lifecycle, 'stop') as stop):
                    self.owner.apply(job_id)
                self.assertEqual(command.call_args_list, [call(self.config, 'prepare', job_id, None)] * 2)
                sleep.assert_called_once()
                observe.assert_called_once_with()
                if reads_proof:
                    proof.assert_called_once_with(job)
                else:
                    proof.assert_not_called()
                stop.assert_not_called()
                self.assertEqual(self.owner.read(job_id)['error'], 'restore_failed')

    def test_confirmed_failure_stays_pinned_while_other_terminal_jobs_prune(self):
        job = self.upload()
        descriptor = self.owner.uploads.pop(job['job_id'])
        descriptor.close()
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
        descriptor.close()
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
