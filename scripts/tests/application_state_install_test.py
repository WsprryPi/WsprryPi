#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline installer admission tests; no installed paths or service operations."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('state_install', Path(__file__).parents[1] / 'application_state_install.py')
subject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)
LISTENERS = dict(schema='listener-bootstrap/1', device_id='a' * 32, role='pi', web_port=32415, socket_port=32416)


class AdmissionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='state-install-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.state, self.source, self.listeners = (self.root / name for name in ('state', 'source.ini', 'listeners.json'))
        self.calls = []

    def run_command(self, argv, **options):
        self.calls.append(argv[1:])
        if argv[1] == '--storage-format':
            text = 'application-state/1\n'
        elif '--listener-bootstrap' in argv:
            text = json.dumps(LISTENERS)
        else:
            text = ''
        return subprocess.CompletedProcess(argv, 0, text, '')

    def admit(self):
        subject.admit(self.root / 'binary', self.state, self.source, self.listeners, run=self.run_command)

    def test_explicit_fresh_and_no_directory_creation_by_installer(self):
        self.admit()
        self.assertEqual(self.calls[1][0], '--initialize-state')
        self.assertFalse(self.state.exists())
        self.assertEqual(subject.read_listeners(self.listeners, os.geteuid()), LISTENERS)
        self.assertEqual(self.listeners.stat().st_mode & 0o777, 0o644)

    def test_upgrade_preserves_source_bytes_and_uses_migration(self):
        self.source.write_bytes(b'custom source bytes\n')
        self.admit()
        self.assertEqual(self.calls[1][0], '--migrate-state')
        self.assertEqual(self.source.read_bytes(), b'custom source bytes\n')

    def test_established_empty_or_symlink_root_never_initializes_or_reimports(self):
        self.source.write_bytes(b'old input must not be consumed')
        for symlink in (False, True):
            if symlink:
                self.state.rmdir()
                self.state.symlink_to(self.root / 'missing')
            else:
                self.state.mkdir()
            self.calls.clear()
            self.admit()
            self.assertEqual(self.calls[1][0], '--check-state')
            self.assertFalse(any(call[0] in ('--initialize-state', '--migrate-state') for call in self.calls))

    def test_failed_check_never_falls_back_or_replaces_last_listener_descriptor(self):
        self.state.mkdir()
        subject.publish_listeners(self.listeners, json.dumps(LISTENERS))
        before = self.listeners.read_bytes()
        def fail(argv, **options):
            if argv[1] == '--check-state':
                raise subprocess.CalledProcessError(2, argv)
            return self.run_command(argv, **options)
        with self.assertRaises(subprocess.CalledProcessError):
            subject.admit(self.root / 'binary', self.state, self.source, self.listeners, run=fail)
        self.assertEqual(self.listeners.read_bytes(), before)

    def test_closed_listener_contract_and_unsafe_publication_refused(self):
        for value in ({**LISTENERS, 'sql': 'ignored'}, {**LISTENERS, 'web_port': True},
                      {**LISTENERS, 'socket_port': 80}, {**LISTENERS, 'role': 'hybrid'},
                      {**LISTENERS, 'socket_port': LISTENERS['web_port']}):
            with self.assertRaises(ValueError):
                subject.publish_listeners(self.listeners, json.dumps(value))
            self.assertFalse(self.listeners.exists())
        self.listeners.symlink_to(self.root / 'absent')
        with self.assertRaises(OSError):
            subject.publish_listeners(self.listeners, json.dumps(LISTENERS))


if __name__ == '__main__':
    unittest.main()
