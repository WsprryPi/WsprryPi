# SPDX-License-Identifier: MIT
"""Package only synthetic runtime files into an owned temporary root."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('recovery_runtime_install', ROOT / 'scripts/recovery_runtime_install.py')
subject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)


class RecoveryRuntimeInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='recovery-runtime-install-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / 'source';self.source.mkdir()
        for name in (*subject.MODULES, 'THIRD_PARTY_NOTICES.txt'):
            (self.source / name).write_text('# synthetic ' + name + '\n')
        self.binary = self.root / 'binary'
        self.binary.write_text('#!/bin/sh\n[ "$1" = "--export-recovery-age-helper" ] || exit 2\nprintf synthetic-helper\n')
        self.binary.chmod(0o755)
        self.destination = self.root / 'installed'

    def test_fixed_package_export_modes_and_no_rewrite_when_unchanged(self):
        subject.install(self.binary, self.source, self.destination)
        helper = self.destination / 'recovery-age'
        self.assertEqual(helper.read_bytes(), b'synthetic-helper')
        self.assertEqual(helper.stat().st_mode & 0o777, 0o755)
        members = list((self.destination / 'recovery').iterdir())
        before = {path.name: path.stat().st_mtime_ns for path in members}
        self.assertEqual(set(before), set(subject.MODULES) | {'THIRD_PARTY_NOTICES.txt'})
        subject.install(self.binary, self.source, self.destination)
        self.assertEqual(before, {path.name: path.stat().st_mtime_ns for path in members})
        self.assertTrue(all(path.stat().st_mode & 0o777 == 0o644 for path in members))

    def test_export_failure_preserves_installed_package(self):
        subject.install(self.binary, self.source, self.destination)
        self.binary.write_text('#!/bin/sh\nexit 2\n')
        with self.assertRaises(ValueError):
            subject.install(self.binary, self.source, self.destination)
        self.assertEqual((self.destination / 'recovery-age').read_bytes(), b'synthetic-helper')

    def test_linked_source_or_installed_member_refuses(self):
        source = self.source / 'jobs.py';source.unlink();source.symlink_to(self.source / 'worker.py')
        with self.assertRaises(OSError):
            subject.install(self.binary, self.source, self.destination)
        source.unlink();source.write_text('# synthetic jobs\n')
        subject.install(self.binary, self.source, self.destination)
        member = self.destination / 'recovery/jobs.py';member.unlink();member.symlink_to(self.root / 'outside')
        with self.assertRaises(ValueError):
            subject.install(self.binary, self.source, self.destination)
        self.assertFalse((self.root / 'outside').exists())


if __name__ == '__main__':
    unittest.main()
