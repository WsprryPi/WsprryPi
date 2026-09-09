#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline tests of the installed route configuration companion."""
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import route_application as app

SOURCE = Path(__file__).resolve().parents[2]


class Tests(unittest.TestCase):
    def setUp(self):
        self.data = (SOURCE/'config/wsprrypi.ini').read_bytes().replace(
            b'Transmit Backend = gpio', b'Transmit Backend = rp1-gpclk')

    def test_explicit_route_selects_runtime_backend_pin_and_inhibits_transmit(self):
        for backend in (b'gpio', b'si5351'):
            for policy in (b'Never', b'Follow', b'Always'):
                with self.subTest(backend=backend, policy=policy):
                    original = self.data.replace(
                        b'Transmit Backend = rp1-gpclk',
                        b'Transmit Backend = '+backend).replace(
                        b'Enable on Boot = Never', b'Enable on Boot = '+policy).replace(
                        b'Transmit = False', b'Transmit = True')
                    expected = original.replace(
                        b'\nTransmit Backend = '+backend+b'\n',
                        b'\nTransmit Backend = rp1-gpclk\n').replace(
                        b'Transmit Pin = 4', b'Transmit Pin = 20').replace(
                        b'Transmit = True', b'Transmit = False')
                    self.assertEqual(app.edit(original, 'gpio20'), expected)
                    self.assertEqual(app.edit(expected, 'gpio20'), expected)

    def test_neutral_inspection_accepts_gpio_and_si5351_without_mutating_them(self):
        for backend in (b'gpio', b'si5351'):
            with self.subTest(backend=backend):
                original = self.data.replace(
                    b'Transmit Backend = rp1-gpclk',
                    b'Transmit Backend = '+backend)
                parsed = app.configuration(original)
                self.assertEqual(
                    parsed.get('Operation', 'Transmit Backend'), backend.decode())
                self.assertEqual(original, self.data.replace(
                    b'Transmit Backend = rp1-gpclk',
                    b'Transmit Backend = '+backend))
                with self.assertRaises(ValueError):
                    app.configuration(original, require_runtime_backend=True)

    def test_crlf_and_unknown_settings_are_preserved(self):
        data = self.data.replace(b'\n', b'\r\n')+b'\r\n[Custom]\r\nValue = literal%value\r\n'
        self.assertEqual(app.edit(data, 'gpio20'), data.replace(b'Transmit Pin = 4', b'Transmit Pin = 20'))

    def test_invalid_duplicate_and_unsupported_backend_do_not_edit(self):
        for data in (self.data+b'\n[GPIO]\nTransmit Pin = 20\n',
                     self.data.replace(b'Transmit Backend = rp1-gpclk', b'Transmit Backend = simulated'),
                     self.data.replace(b'Transmit Pin = 4', b'Transmit Pin = 19'),
                     self.data.replace(b'Enable on Boot = Never', b'Enable on Boot = unknown')):
            with self.assertRaises(Exception): app.edit(data, 'gpio20')

    def test_appended_sections_preserve_bytes_and_new_keys(self):
        data = self.data + b'\n[Band GPIO]\n8m = \n[GPIO]\nRP1 Future Setting = 2\n'
        self.assertEqual(app.configuration(data).getint('GPIO', 'RP1 Future Setting'), 2)
        self.assertEqual(app.edit(data, 'gpio20'),
                         data.replace(b'Transmit Pin = 4', b'Transmit Pin = 20'))
        with self.assertRaises(Exception):
            app.configuration(data + b'\n[GPIO]\nRP1 Future Setting = 4\n')

    def test_atomic_configuration_and_mode_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'application.ini'
            path.write_bytes(self.data)
            path.chmod(0o640)
            with patch.object(app, 'CONFIG', path), patch.object(app, 'trusted', side_effect=lambda p:(p.read_bytes(), p.stat())), patch.object(os, 'fchown'):
                app.configure('gpio20')
            self.assertEqual(path.read_bytes(), app.edit(self.data, 'gpio20'))
            self.assertEqual(path.stat().st_mode & 0o777, 0o640)
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_failed_replace_keeps_original(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'application.ini'
            path.write_bytes(self.data)
            with patch.object(app, 'CONFIG', path), patch.object(app, 'trusted', side_effect=lambda p:(p.read_bytes(), p.stat())), patch.object(os, 'fchown'), patch.object(os, 'replace', side_effect=OSError('crash')):
                with self.assertRaises(OSError): app.configure('gpio20')
            self.assertEqual(path.read_bytes(), self.data)

    def test_installed_service_layouts_and_old_binary_rejection(self):
        for suffix in ('', ' --no-web'):
            text = '{ path=/usr/local/bin/wsprrypi ; argv[]=/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini'+suffix+' ; ignore_errors=no ; }\n'
            with patch.object(app.subprocess, 'check_output', return_value=text), patch.object(app, 'trusted', return_value=(b'WSPRRYPI_ROUTE_RESTORE_IDLE', None)):
                app.inspect_service()
            with patch.object(app.subprocess, 'check_output', return_value=text), patch.object(app, 'trusted', return_value=(b'old binary', None)):
                with self.assertRaises(ValueError): app.inspect_service()
        with patch.object(app.subprocess, 'check_output', return_value=''):
            with self.assertRaises(ValueError): app.inspect_service()

    def test_bootstrap_installer_uses_selected_checkout_and_honors_dry_run(self):
        for dry_run in ('true', 'false'):
            script = ('source "$INSTALLER"\n'
                'FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=\n'
                'python3() { printf "%s\\n" "$*" >>"$CALLS"; }\n'
                'install() { printf "%s\\n" "$*" >>"$CALLS"; }\n'+
                'ACTION=install\nLOCAL_REPO_DIR=/selected-checkout\nDRY_RUN='+dry_run+
                '\nmanage_route_application\n')
            with tempfile.TemporaryDirectory() as directory:
                calls = Path(directory)/'calls'
                result = subprocess.check_output(['bash', '-c', script], text=True, cwd='/tmp',
                    env={**os.environ, 'INSTALLER': str(SOURCE/'scripts/install.sh'), 'CALLS': str(calls)})
                self.assertIn('Install route application companion.', result)
                self.assertIn('Install runtime reconciliation support.', result)
                if dry_run == 'true':
                    self.assertFalse(calls.exists())
                else:
                    self.assertIn('/selected-checkout/scripts/route_application.py', calls.read_text())
                    self.assertIn('/usr/local/lib/wsprrypi/route_application.py', calls.read_text())


if __name__ == '__main__': unittest.main()
