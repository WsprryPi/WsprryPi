#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inactive source evidence is bounded, portable and independently validated."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive, evidence
from application_recovery.durable import publish_record, read_record

CPP = None

class RecoveryEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='recovery-evidence-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.source, self.records, self.candidate = [self.root / name for name in ('source', 'records', 'candidate')]
        for path in (self.source, self.records, self.candidate):
            path.mkdir(mode=0o700)
        (self.source / 'application').mkdir(mode=0o700)
        (self.source / 'pairing').mkdir(mode=0o700)
        self.source_names = {'application/state.sqlite3': b'synthetic SQL input not copied as active state',
                             'application/authority.json': b'{"synthetic":"initialization provenance"}',
                             'pairing/operations.json': b'{"synthetic":"retained unknown operation"}'}
        for name, data in self.source_names.items():
            path = self.source / name
            path.write_bytes(data);path.chmod(0o600)
        self.manifest = archive.manifest_for_snapshot(self.source,
            {'identity': 'a' * 32, 'role': 'controller', 'application_version': '4.0-test-π', 'platform': 'linux', 'storage': 'application-state/1'},
            list(self.source_names), 1800000000)
        files = []
        for kind in ('configuration', 'catalog', 'initialization', 'operations'):
            name = 'state-' + kind + '.json'
            data = ('{"synthetic_validated_record":"' + kind + '"}').encode()
            (self.records / name).write_bytes(data);(self.records / name).chmod(0o600)
            files.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        publish_record(self.records / 'index.json', {'schema': 'recovery-source-records/1', 'identity': 'a' * 32,
                       'role': 'controller', 'files': files}, None)
        self.sentinel = self.candidate / 'state.sqlite3'
        self.sentinel.write_bytes(b'destination active state must not change');self.sentinel.chmod(0o600)

    def retain(self, archive_hash='b' * 64):
        return evidence.retain_source(self.candidate, self.source, self.records, self.manifest, archive_hash)

    def test_windows_controller_provenance_is_retained_and_revalidated(self):
        self.manifest['source']['platform'] = 'windows'
        self.retain()
        group, index = evidence.inspect(self.candidate / 'evidence')[0]
        self.assertEqual(index['source']['platform'], 'windows')
        self.cpp(self.candidate)
        for platform, role in (('unknown', 'controller'), ('windows', 'pi')):
            changed = dict(index, source=dict(index['source'], platform=platform, role=role))
            changed['content_digest'] = evidence.content_digest(changed)
            with self.assertRaises(archive.RecoveryError):
                evidence.validate_index(changed, changed['content_digest'][:32])

    def cpp(self, path, valid=True):
        result = subprocess.run([CPP, str(path)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)
        return result

    def test_roundtrip_dedup_no_activation_and_independent_cpp_validation(self):
        before = self.sentinel.read_bytes()
        result = self.retain()
        groups = evidence.inspect(self.candidate / 'evidence')
        self.assertEqual(len(groups), 1)
        group, index = groups[0]
        self.assertEqual(index['source']['identity'], 'a' * 32)
        self.assertEqual(self.cpp(self.candidate).stdout.strip(), str(len(index['files']) + 1))
        observation = read_record(group / 'index.json')[1]
        self.assertEqual(self.retain('c' * 64)['group_id'], result['group_id'])
        self.assertEqual(read_record(group / 'index.json')[1], observation)
        self.assertEqual(self.sentinel.read_bytes(), before)
        names = ['application/evidence/' + result['group_id'] + '/' + item.name for item in group.iterdir()]
        for name in names:
            self.assertGreater(archive.maximum_file(name), 0)
            self.assertLessEqual(len(name), 100)
        other = self.root / 'other';other.mkdir(mode=0o700)
        evidence.copy_groups(self.candidate / 'evidence', other / 'evidence')
        self.assertEqual(evidence.inspect(other / 'evidence')[0][1], index)
        self.cpp(other)

    def test_copy_refuses_collision_and_removes_only_its_own_failed_output(self):
        source = self.records / 'state-configuration.json'
        data = source.read_bytes()
        item = {'name': source.name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        existing = self.candidate / 'existing.json'
        existing.write_bytes(b'preserve existing evidence');existing.chmod(0o600)
        with self.assertRaises(FileExistsError):
            evidence.copy_file(source, existing, item)
        self.assertEqual(existing.read_bytes(), b'preserve existing evidence')
        failed = self.candidate / 'failed.json'
        with self.assertRaises(archive.RecoveryError):
            evidence.copy_file(source, failed, dict(item, sha256='0' * 64))
        self.assertFalse(failed.exists())
        self.assertEqual(source.read_bytes(), data)

    def test_unknown_extra_members_corruption_and_symlinks_refuse(self):
        self.retain()
        group, index = evidence.inspect(self.candidate / 'evidence')[0]
        extra = group / 'run-command.json';extra.write_bytes(b'{}');extra.chmod(0o600)
        with self.assertRaises(archive.RecoveryError):
            evidence.inspect(self.candidate / 'evidence')
        self.cpp(self.candidate, False)
        extra.unlink()
        member = group / index['files'][0]['name'];original = member.read_bytes()
        member.write_bytes(original + b'changed')
        with self.assertRaises(archive.RecoveryError):
            evidence.inspect(self.candidate / 'evidence')
        self.cpp(self.candidate, False)
        member.unlink();member.symlink_to(self.records / index['files'][0]['name'])
        with self.assertRaises((archive.RecoveryError, OSError)):
            evidence.inspect(self.candidate / 'evidence')
        self.cpp(self.candidate, False)

    def test_closed_index_and_archive_paths(self):
        self.retain()
        group, index = evidence.inspect(self.candidate / 'evidence')[0]
        old, observation = read_record(group / 'index.json')
        publish_record(group / 'index.json', dict(old, unknown=True), observation)
        with self.assertRaises(archive.RecoveryError):
            evidence.inspect(self.candidate / 'evidence')
        self.cpp(self.candidate, False)
        for name in ('application/evidence/../../state.sqlite3', 'application/evidence/' + 'a' * 32 + '/run-command.json',
                     'application/evidence/' + 'a' * 31 + '/state-operations.json'):
            with self.assertRaises(archive.RecoveryError):
                archive.maximum_file(name)
        self.assertEqual(archive.maximum_file('application/evidence/' + 'a' * 32 + '/target-' + 'b' * 32 + '.json'), 16 * 1024 * 1024)

if __name__ == '__main__':
    CPP = str(Path(sys.argv[1]).resolve())
    unittest.main(argv=[sys.argv[0]])
