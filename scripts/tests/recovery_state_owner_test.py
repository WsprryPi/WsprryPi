#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Unprivileged kernel-peer and streamed-file tests for the private owner adapter."""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from application_recovery import archive
from application_recovery.durable import publish_record, read_record, directory_lock
from application_recovery.state_owner import StateOwner, OwnerBusy, connection_lost, operation_timeout
from application_recovery.jobs import backend_command, BackendUnavailable


@unittest.skipIf(os.geteuid() == 0, 'unprivileged helper tests do not substitute for authorized multi-UID qualification')
class StateOwnerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='recovery-owner-', dir='/tmp')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.home, self.maintenance = self.root / 'backend', self.root / 'maintenance'
        self.home.mkdir(mode=0o700)
        self.maintenance.mkdir(mode=0o700)
        self.application = self.home / 'application-state'
        self.application.mkdir(mode=0o700)
        self.job_id, self.identity = 'a' * 32, 'b' * 32
        self.job = self.maintenance / 'jobs' / self.job_id
        self.job.mkdir(parents=True, mode=0o700)
        (self.maintenance / 'jobs').chmod(0o700)
        publish_record(self.job / 'job.json', {'schema': 'recovery-job/1', 'job_id': self.job_id, 'identity': self.identity,
            'role': 'controller', 'state': 'capturing', 'confirmation_sha256': None}, None)
        self.worker = self.root / 'fixture_worker.py'
        self.worker.write_text('import os,sys\n'
            f'sys.path.insert(0,{str(ROOT / "src")!r})\n'
            'from application_recovery.state_owner import serve\n'
            f'raise SystemExit(serve(int(sys.argv[2]),maintenance_uid=os.geteuid(),owner_home={str(self.home)!r}))\n')
        self.worker.chmod(0o700)
        self.config = {'root': str(self.maintenance), 'application_root': str(self.application),
            'backend_command': ['/usr/bin/false'], 'backend_runtime': str(self.root / 'ipc'),
            'identity': self.identity, 'role': 'controller', 'backend_owner': {
                'uid': os.geteuid(), 'gid': os.getegid(), 'home': str(self.home),
                'root': str(self.home / '.wsprrypi-recovery'), 'worker': str(self.worker)}}
        self.owner = StateOwner(self.config)
        self.backend_job = self.home / '.wsprrypi-recovery/jobs' / self.job_id

    def test_authenticated_import_and_idempotent_metadata(self):
        self.owner.synchronize(self.job_id, 'capture')
        first = (self.backend_job / 'job.json').read_bytes()
        self.owner.synchronize(self.job_id, 'capture')
        self.assertEqual((self.backend_job / 'job.json').read_bytes(), first)
        self.assertEqual(read_record(self.backend_job / 'job.json')[0]['identity'], self.identity)
        self.assertEqual(list(self.application.iterdir()), [])

    def test_pruning_retries_without_recreating_staging(self):
        self.assertEqual(self.owner.call('prune-job', self.job_id), {'state': 'pruned'})
        self.assertFalse((self.home / '.wsprrypi-recovery').exists())
        self.owner.synchronize(self.job_id, 'capture')
        self.assertEqual(self.owner.call('prune-job', self.job_id), {'state': 'pruned'})
        self.assertFalse(self.backend_job.exists())
        self.assertEqual(self.owner.call('prune-job', self.job_id), {'state': 'pruned'})
        self.assertFalse(self.backend_job.exists())
        self.assertEqual(list(self.application.iterdir()), [])

    def test_backend_job_contention_is_definite_busy_without_supervisor_failure(self):
        self.owner.synchronize(self.job_id, 'capture')
        failures = []
        owner = StateOwner(self.config, process_failure=lambda: failures.append('lost'))
        with directory_lock(self.backend_job):
            with self.assertRaises(OwnerBusy):
                owner.collect(self.job_id)
        self.assertEqual(failures, [])
        owner.collect(self.job_id)

    def test_nonempty_candidate_wal_is_refused_before_mirror_publication(self):
        self.owner.synchronize(self.job_id, 'capture')
        candidate = self.backend_job / 'candidate'
        candidate.mkdir(mode=0o700)
        for name, data in (('state.sqlite3', b'snapshot'), ('state.sqlite3-wal', b'uncheckpointed')):
            path = candidate / name
            path.write_bytes(data);path.chmod(0o600)
        publish_record(self.backend_job / 'candidate-built.json', {'state': 'candidate_built'}, None)
        with self.assertRaises(archive.RecoveryError):
            self.owner.collect(self.job_id)
        self.assertFalse((self.job / 'candidate').exists())

    def test_bounded_complete_snapshot_stream_preserves_bytes(self):
        self.owner.synchronize(self.job_id, 'capture')
        directory = self.backend_job / 'snapshot/application'
        directory.mkdir(parents=True, mode=0o700)
        (self.backend_job / 'snapshot').chmod(0o700)
        payload = bytes(range(256)) * 900
        path = directory / 'state.sqlite3'
        path.write_bytes(payload);path.chmod(0o600)
        wal = directory / 'state.sqlite3-wal'
        wal.write_bytes(b'');wal.chmod(0o600)
        shm = directory / 'state.sqlite3-shm'
        shm.write_bytes(b'ephemeral verification index');shm.chmod(0o600)
        publish_record(self.backend_job / 'capture-result.json', {'state': 'ready', 'inventory': {'files': ['application/state.sqlite3']}}, None)
        self.owner.collect(self.job_id)
        self.assertEqual((self.job / 'snapshot/application/state.sqlite3').read_bytes(), payload)
        self.assertEqual((self.job / 'snapshot/application/state.sqlite3').stat().st_mode & 0o777, 0o600)
        self.assertFalse((self.job / 'snapshot/application/state.sqlite3-wal').exists())
        self.assertFalse((self.job / 'snapshot/application/state.sqlite3-shm').exists())

    def test_unfinished_snapshot_is_not_exported(self):
        self.owner.synchronize(self.job_id, 'capture')
        directory = self.backend_job / 'snapshot/application'
        directory.mkdir(parents=True, mode=0o700)
        (self.backend_job / 'snapshot').chmod(0o700)
        path = directory / 'state.sqlite3'
        path.write_bytes(b'partial');path.chmod(0o600)
        self.owner.collect(self.job_id)
        self.assertFalse((self.job / 'snapshot').exists())

    def test_linked_output_is_refused_before_root_mirror_publication(self):
        self.owner.synchronize(self.job_id, 'capture')
        directory = self.backend_job / 'snapshot/application'
        directory.mkdir(parents=True, mode=0o700)
        (self.backend_job / 'snapshot').chmod(0o700)
        target = self.home / 'unrelated'
        target.write_bytes(b'private');target.chmod(0o600)
        os.link(target, directory / 'state.sqlite3')
        publish_record(self.backend_job / 'capture-result.json', {'state': 'ready', 'inventory': {'files': ['application/state.sqlite3']}}, None)
        with self.assertRaises(archive.RecoveryError):
            self.owner.collect(self.job_id)
        self.assertFalse((self.job / 'snapshot').exists())

    def test_production_child_refuses_nonroot_bootstrap_peer(self):
        config = dict(self.config, backend_owner=dict(self.config['backend_owner'],
            worker=str(ROOT / 'src/application_recovery/state_owner_worker.py')))
        owner = StateOwner(config)
        with self.assertRaises((archive.RecoveryError, OSError)):
            owner.synchronize(self.job_id, 'capture')
        self.assertFalse((self.home / '.wsprrypi-recovery').exists())

    def test_bad_stream_digest_never_commits_job_or_application_state(self):
        before = (self.job / 'job.json').read_bytes()
        files = [{'name': 'job.json', 'size': len(before), 'sha256': hashlib.sha256(b'wrong').hexdigest()}]
        with self.assertRaises((archive.RecoveryError, OSError)):
            self.owner.call('import', self.job_id, {'allow_create': True, 'clear': []}, files)
        self.assertFalse((self.backend_job / 'job.json').exists())
        self.assertEqual(list(self.application.iterdir()), [])

    def test_unknown_operation_has_no_filesystem_effect(self):
        with self.assertRaises(archive.RecoveryError):
            self.owner.call('arbitrary-command', self.job_id)
        self.assertFalse((self.home / '.wsprrypi-recovery').exists())

    @unittest.skipUnless(sys.platform == 'darwin', 'Darwin extended ACL coverage')
    def test_extended_file_grant_refuses_even_with_private_mode_bits(self):
        path = self.job / 'job.json'
        subprocess.run(['/bin/chmod', '+a', 'everyone allow read', str(path)], check=True)
        try:
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(archive.RecoveryError):
                with archive.private_input(path, archive.MAX_MANIFEST):
                    pass
        finally:
            subprocess.run(['/bin/chmod', '-N', str(path)], check=True)
        with archive.private_input(path, archive.MAX_MANIFEST):
            pass

    @unittest.skipUnless(sys.platform == 'darwin', 'Darwin extended ACL coverage')
    def test_extended_directory_and_ancestor_grants_refuse_before_import(self):
        for path in (self.home, self.root):
            subprocess.run(['/bin/chmod', '+a', 'everyone allow read,search', str(path)], check=True)
            try:
                self.assertEqual(path.stat().st_mode & 0o777, 0o700)
                with self.assertRaises(archive.RecoveryError):
                    archive.private_directory(self.home)
            finally:
                subprocess.run(['/bin/chmod', '-N', str(path)], check=True)
        self.assertFalse((self.home / '.wsprrypi-recovery').exists())

    def test_abrupt_helper_loss_invokes_supervisor_without_publishing(self):
        self.worker.write_text('import os\nos._exit(9)\n')
        failures = []
        owner = StateOwner(self.config, process_failure=lambda: failures.append('lost'))
        with self.assertRaises((archive.RecoveryError, OSError)):
            owner.synchronize(self.job_id, 'capture')
        self.assertEqual(failures, ['lost'])
        self.assertFalse(self.backend_job.exists())
        self.assertEqual(list(self.application.iterdir()), [])

    def test_offline_build_disconnect_cancels_and_reaps_owned_child(self):
        marker = self.root / 'owned-child.pid'
        script = f'import os,time;from pathlib import Path;Path({str(marker)!r}).write_text(str(os.getpid()));time.sleep(60)'
        config = dict(self.config, backend_command=[sys.executable, '-c', script])
        parent, child = socket.socketpair()
        self.addCleanup(parent.close)
        self.addCleanup(child.close)
        def disconnect():
            deadline = time.monotonic() + 5
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            parent.close()
        closer = threading.Thread(target=disconnect)
        closer.start()
        started = time.monotonic()
        try:
            with self.assertRaises(BackendUnavailable):
                backend_command(config, 'build-candidate', self.job_id, cancelled=lambda: connection_lost(child))
        finally:
            closer.join(timeout=6)
        self.assertFalse(closer.is_alive())
        self.assertLess(time.monotonic() - started, 10)
        self.assertTrue(marker.exists())
        with self.assertRaises(ProcessLookupError):
            os.kill(int(marker.read_text()), 0)
        self.assertGreater(operation_timeout('build-candidate'), 1800)
        self.assertEqual(operation_timeout('seal-candidate'), operation_timeout('build-candidate'))


if __name__ == '__main__':
    unittest.main()
