#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Closed recovery container, real encryption, and hostile input refusals."""
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive


class RecoveryArchiveTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='recovery-archive-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.snapshot = self.root / 'snapshot'
        self.snapshot.mkdir(mode=0o700)
        (self.snapshot / 'application').mkdir(mode=0o700)
        self.contents = {'application/state.sqlite3': b'synthetic private SQL snapshot\0' * 10000,
                         'application/authority.json': b'{"synthetic":"private initialization"}'}
        for name, value in self.contents.items():
            target = self.snapshot / name
            target.write_bytes(value)
            target.chmod(0o600)
        self.manifest = archive.manifest_for_snapshot(self.snapshot, {'identity': 'a' * 32, 'role': 'pi',
            'application_version': '4.0.0-test', 'storage': 'application-state/1', 'platform': 'linux'}, list(self.contents), 1800000000)
        self.helper = Path(os.environ['WSPRRYPI_RECOVERY_AGE_HELPER']).resolve()
        self.phrase = b'synthetic long archive test passphrase'

    def decoded(self):
        target = self.root / 'decoded'
        target.mkdir(mode=0o700)
        return target

    def test_actual_encryption_round_trip_and_no_source_change(self):
        output = self.root / 'backup.age'
        archive.create_archive(self.helper, self.snapshot, output, self.manifest, self.phrase)
        self.assertNotIn(self.contents['application/authority.json'], output.read_bytes())
        destination = self.decoded()
        self.assertEqual(archive.validate_archive(self.helper, output, destination, self.phrase), self.manifest)
        for name, content in self.contents.items():
            self.assertEqual((destination / name).read_bytes(), content)
            self.assertEqual((self.snapshot / name).read_bytes(), content)
            self.assertEqual((destination / name).stat().st_mode & 0o777, 0o600)

    def test_wrong_passphrase_tamper_and_truncation_never_validate(self):
        output = self.root / 'backup.age'
        archive.create_archive(self.helper, self.snapshot, output, self.manifest, self.phrase)
        original = output.read_bytes()
        for index, (phrase, data) in enumerate([(b'wrong passphrase', original), (self.phrase, original[:-1]),
                                               (self.phrase, original[:-1] + bytes([original[-1] ^ 1]))]):
            with self.subTest(index=index):
                output.write_bytes(data)
                destination = self.root / f'decoded-{index}'
                destination.mkdir(mode=0o700)
                with self.assertRaises(archive.RecoveryError):
                    archive.validate_archive(self.helper, output, destination, phrase)
                self.assertEqual((self.snapshot / 'application/state.sqlite3').read_bytes(), self.contents['application/state.sqlite3'])

    def test_changed_snapshot_removes_partial_archive(self):
        (self.snapshot / 'application/authority.json').write_bytes(b'changed after manifest capture')
        output = self.root / 'backup.age'
        with self.assertRaises(archive.RecoveryError):
            archive.create_archive(self.helper, self.snapshot, output, self.manifest, self.phrase)
        self.assertFalse(output.exists())

    def test_replaced_upload_refuses_original_receipt_before_decryption(self):
        output = self.root / 'backup.age'
        archive.create_archive(self.helper, self.snapshot, output, self.manifest, self.phrase)
        stamp = list(archive.observation(output.stat()))
        replacement = self.root / 'replacement.age'
        archive.create_archive(self.helper, self.snapshot, replacement, self.manifest, self.phrase)
        os.replace(replacement, output)
        destination = self.decoded()
        with self.assertRaises(archive.RecoveryError):
            archive.validate_archive(self.helper, output, destination, self.phrase, stamp)
        self.assertEqual(list(destination.iterdir()), [])

    def test_unsafe_files_and_collision_refuse_without_overwrite(self):
        output = self.root / 'existing.age'
        output.write_bytes(b'keep existing artifact')
        output.chmod(0o600)
        with self.assertRaises(FileExistsError):
            archive.create_archive(self.helper, self.snapshot, output, self.manifest, self.phrase)
        self.assertEqual(output.read_bytes(), b'keep existing artifact')
        original = self.snapshot / 'application/authority.json'
        link = self.snapshot / 'application/saved'
        original.rename(link)
        original.symlink_to(link)
        with self.assertRaises(OSError):
            archive.create_archive(self.helper, self.snapshot, self.root / 'refused.age', self.manifest, self.phrase)
        self.assertFalse((self.root / 'refused.age').exists())

    def test_manifest_types_duplicates_unknown_names_and_resource_bounds(self):
        for change in [lambda v: v.update(extra=True), lambda v: v.update(schema='wsprrypi-recovery/2'),
                       lambda v: v['source'].update(identity='b' * 31), lambda v: v['files'][0].update(size=True),
                       lambda v: v['source'].update(platform='unknown'),
                       lambda v: v['source'].update(platform='windows', role='pi'),
                       lambda v: v['files'].append(v['files'][0]), lambda v: v['files'][0].update(size=archive.MAX_ARCHIVE),
                       lambda v: v['files'][0].update(name='../../etc/passwd'),
                       lambda v: v['files'][0].update(name='application/source-configuration')]:
            value = json.loads(json.dumps(self.manifest))
            change(value)
            with self.subTest(value=value), self.assertRaises(archive.RecoveryError):
                archive.validate_manifest(value)
        with self.assertRaises(archive.RecoveryError):
            archive.strict_json(b'{"schema":1,"schema":2}')
        with self.assertRaises(archive.RecoveryError):
            archive.strict_json(b'[' * 30 + b'0' + b']' * 30)

    def test_controller_source_platforms_use_the_same_archive(self):
        for platform in ('linux', 'darwin', 'windows'):
            with self.subTest(platform=platform):
                manifest = json.loads(json.dumps(self.manifest))
                manifest['source'].update(platform=platform, role='controller')
                archive.validate_manifest(manifest)
                output = io.BytesIO()
                archive.write_container(output, self.snapshot, manifest)
                destination = self.root / ('decoded-' + platform)
                destination.mkdir(mode=0o700)
                self.assertEqual(archive.read_container(io.BytesIO(output.getvalue()), destination), manifest)

    def test_strict_container_rejects_trailing_bytes_header_metadata_and_digest(self):
        output = io.BytesIO()
        archive.write_container(output, self.snapshot, self.manifest)
        original = output.getvalue()
        bad_header = bytearray(original)
        bad_header[0] = ord('x')
        bad_payload = bytearray(original)
        # Mutating the final member's payload retains a valid tar header.
        offset = original.index(self.contents['application/state.sqlite3'][:100])
        bad_payload[offset] ^= 1
        for index, data in enumerate([original + b'\0', original[:-512], bad_header, bad_payload]):
            with self.subTest(index=index):
                destination = self.root / f'refusal-{index}'
                destination.mkdir(mode=0o700)
                with self.assertRaises(archive.RecoveryError):
                    archive.read_container(io.BytesIO(data), destination)


if __name__ == '__main__':
    unittest.main()
