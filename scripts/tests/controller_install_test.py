#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline controller bundle and installer boundary checks."""

from pathlib import Path
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

if sys.platform == 'linux':
    import controller_bundle as bundle
    import controller_linux as installer

sys.path.insert(0, str(ROOT / 'src'))
from application_recovery import lifecycle
import controller_macos


@unittest.skipUnless(sys.platform == 'linux', 'Linux controller bundle profile')
class ControllerInstall(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary = {'architecture': 'amd64', 'role': 'controller', 'platform': 'ubuntu-26.04',
                       'source_commit': 'a' * 40, 'application_version': '4.0.0+fixture',
                       'tracked_source_dirty': False, 'sha256': bundle.digest(b'backend with age'),
                       'runtime_library_packages': ['libc6'], 'elf_libraries': ['libc.so.6']}
        self.files = {'bin/wsprrypi': b'backend with age', 'bin/recovery-age': b'age'}
        self.files.update({name: b'notice' for name in bundle.LICENSE_FILES})
        self.files.update({f'recovery/{name}': b'python' for name in bundle.RECOVERY_FILES})
        for name in bundle.REQUIRED_UI - {'ui-manifest.json'}:
            filename = self.root / 'web' / name
            filename.parent.mkdir(parents=True, exist_ok=True)
            filename.write_text('asset ' + name)
            self.files[f'ui/{name}'] = filename.read_bytes()
        module = bundle.load_manifest_module(ROOT)
        ui = module.build_manifest(self.root / 'web', self.binary['source_commit'], self.binary['application_version'])
        self.files['ui/ui-manifest.json'] = module.serialize_manifest(ui)
        self.manifest = {'schema': bundle.SCHEMA, 'binary': self.binary, 'ui_build': ui['packaged_ui_build_id'],
                         'dependencies': dict.fromkeys(bundle.runtime_packages(self.binary), '1:2.3-4ubuntu1'),
                         'source_snapshot': {}, 'control': 'Wsprry-Control/1', 'storage': 'application-state/1'}

    def archive(self, *, members=None, extra=None, tail=b'', digest_overrides=None):
        path = self.root / 'candidate.tar'
        entries = list((members or self.files).items())
        inventory = {name: hashlib.sha256(data).hexdigest() for name, data in entries}
        inventory.update(digest_overrides or {})
        entries.append((bundle.MANIFEST, bundle.encoded({**self.manifest, 'files': inventory})))
        with tarfile.open(path, 'w', format=tarfile.USTAR_FORMAT) as archive:
            for name, content in entries:
                info = tarfile.TarInfo(name)
                info.size = len(content)
                archive.addfile(info, io.BytesIO(content))
            if extra:
                archive.addfile(extra)
        if tail:
            with path.open('ab') as stream:
                stream.write(tail)
        return path

    def verify(self, archive, sha=None, report=None):
        stage = Path(tempfile.mkdtemp(dir=self.root))
        with mock.patch.object(bundle.controller_binary, 'verify', return_value=self.binary if report is None else report), \
             mock.patch.object(bundle.controller_binary, 'elf_header'):
            return bundle.verify(archive, sha or bundle.digest(archive.read_bytes()), 'amd64', stage)

    def test_round_trip_and_wrong_checksum(self):
        payload = self.archive()
        self.assertEqual(self.verify(payload)['ui_build'], self.manifest['ui_build'])
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.verify(payload, '0' * 64)

    def test_requires_the_selected_host_and_php_baseline(self):
        with mock.patch.object(installer.Path, 'read_text', return_value='ID=ubuntu\nVERSION_ID="26.04"\n'), \
             mock.patch.object(installer, 'run', return_value=b'arm64\n'):
            self.assertEqual(installer.architecture(), 'arm64')
        with mock.patch.object(installer.Path, 'read_text', return_value='ID=ubuntu\nVERSION_ID="24.04"\n'):
            with self.assertRaisesRegex(ValueError, 'requires Ubuntu 26.04'):
                installer.architecture()
        self.manifest['dependencies'].pop('libapache2-mod-php8.5')
        self.manifest['dependencies']['libapache2-mod-php8.3'] = '8.3.6-1'
        with self.assertRaisesRegex(ValueError, 'Invalid bundle dependency inventory'):
            self.verify(self.archive())

    def test_refuses_mixed_versions_and_stale_subset(self):
        with self.assertRaisesRegex(ValueError, 'executable identity'):
            self.verify(self.archive(), report={**self.binary, 'application_version': '4.1.0'})
        self.files['ui/site.js'] = b'changed asset'
        with self.assertRaisesRegex(ValueError, 'UI manifest inventory'):
            self.verify(self.archive())

    def test_recovery_helper_must_match_the_selected_backend(self):
        self.files['bin/recovery-age'] = b'other-helper'
        with self.assertRaisesRegex(ValueError, 'embedded executable image'):
            self.verify(self.archive())

    def test_refuses_unlisted_unsafe_and_pi_only_files(self):
        with self.assertRaisesRegex(ValueError, 'inventory or digest'):
            self.verify(self.archive(digest_overrides={'extra': '0' * 64}))
        for path in ('../outside', 'ui/cache/value.js', 'ui/log_stream.php', 'ui/Site.js'):
            with self.subTest(path=path):
                members = {**self.files, path: b'foreign payload'}
                with self.assertRaisesRegex(ValueError, 'Unsafe|Invalid controller payload'):
                    self.verify(self.archive(members=members))
        self.assertFalse((self.root / 'outside').exists())

    def test_refuses_archive_links_duplicate_entries_and_trailing_data(self):
        for info in (tarfile.TarInfo('ui/site.js'), tarfile.TarInfo('symlink')):
            if info.name == 'symlink':
                info.type = tarfile.SYMTYPE
                info.linkname = '/etc/shadow'
            with self.assertRaisesRegex(ValueError, 'Unsafe|bounds'):
                self.verify(self.archive(extra=info))
        with self.assertRaisesRegex(ValueError, 'Unexpected data'):
            self.verify(self.archive(tail=b'A' * 512))

    def test_rendered_services_enforce_separate_owners_and_bounded_routes(self):
        maintenance, backend, web = installer.units().values()
        self.assertIn('User=root', maintenance)
        self.assertIn('--maintenance-worker --role controller', maintenance)
        self.assertIn('NoNewPrivileges=yes', maintenance)
        self.assertIn('AmbientCapabilities=CAP_SETUID\n', maintenance)
        self.assertIn('User=wsprrypi-controller', backend)
        self.assertIn('User=wsprrypi-web', web)
        for definition in (backend, web):
            self.assertIn('CapabilityBoundingSet=\n', definition)
            self.assertIn('NoNewPrivileges=yes', definition)
            self.assertNotIn('AmbientCapabilities=', definition)
        rendered = installer.apache_config(31417, 31418)
        self.assertIn('http://127.0.0.1:31417/api/recovery', rendered)
        self.assertIn('ws://127.0.0.1:31418/socket', rendered)
        self.assertIn('RequestHeader unset X-WsprryPi-Client-Address', rendered)
        self.assertNotIn('/api/support-bundles', rendered)
        with self.assertRaises(ValueError):
            installer.apache_config(31417, 31417)
        self.assertIn('ENV{ID_USB_INTERFACE_NUM}=="04"', installer.USB_RULE)
        self.assertIn('MODE:="0660"', installer.USB_RULE)

    def test_busy_or_foreign_service_refuses_before_backend_stop(self):
        with mock.patch.object(installer, 'systemctl', return_value=b'LoadState=loaded\nFragmentPath=/run/foreign\nDropInPaths=\n') as systemctl:
            with self.assertRaisesRegex(ValueError, 'Conflicting systemd'):
                installer.loaded_units()
            self.assertTrue(all(call.args[0] == 'show' for call in systemctl.call_args_list))
        with mock.patch.object(installer, 'loaded_units', return_value=installer.UNITS), \
             mock.patch.object(installer, 'state_of', return_value='active'), \
             mock.patch.object(installer, 'run', return_value=b'{"schema":"recovery-job-list/1","active":"job"}'), \
             mock.patch.object(installer, 'systemctl') as systemctl:
            with self.assertRaisesRegex(ValueError, 'active or unknown recovery job'):
                installer.quiesce()
            systemctl.assert_not_called()

    def test_late_maintenance_admission_restarts_only_web(self):
        with mock.patch.object(installer, 'loaded_units', return_value=installer.UNITS), \
             mock.patch.object(installer, 'state_of', return_value='active'), \
             mock.patch.object(installer, 'run', side_effect=[
                 b'{"schema":"recovery-job-list/1","active":null}',
                 b'{"schema":"recovery-job-list/1","active":"job"}',
             ]), mock.patch.object(installer, 'systemctl') as systemctl:
            with self.assertRaisesRegex(ValueError, 'active or unknown recovery job'):
                installer.quiesce()
            self.assertEqual(systemctl.call_args_list, [mock.call('stop', installer.UNITS[2]),
                                                       mock.call('start', installer.UNITS[2])])

    def test_quiescent_installation_is_disarmed_at_boot(self):
        with mock.patch.object(installer, 'loaded_units', return_value=installer.UNITS), \
             mock.patch.object(installer, 'require_idle_maintenance'), \
             mock.patch.object(installer, 'state_of', return_value='inactive'), \
             mock.patch.object(installer, 'systemctl') as systemctl:
            installer.quiesce()
            systemctl.assert_called_once_with('disable', *installer.UNITS)

    def test_clean_host_accepts_absent_systemd_registration(self):
        missing = subprocess.CalledProcessError(1, '/usr/bin/systemctl',
                                                output=b'LoadState=not-found\nFragmentPath=\nDropInPaths=\n')
        with mock.patch.object(installer, 'systemctl', side_effect=missing):
            self.assertEqual(installer.loaded_units(), [])

    def test_stopped_maintenance_registry_retains_active_job(self):
        state = self.root / 'maintenance'
        state.mkdir(mode=0o700)
        registry = state / 'jobs.json'
        registry.write_bytes(bundle.encoded({
            'schema': 'recovery-jobs/1', 'identity': 'a' * 32, 'role': 'controller',
            'active': '1' * 32, 'jobs': ['1' * 32],
        }))
        registry.chmod(0o600)
        with mock.patch.object(installer, 'MAINTENANCE_STATE', state), \
             mock.patch.object(installer, 'directory'), \
             mock.patch.object(installer.grp, 'getgrnam', return_value=mock.Mock(gr_gid=123)), \
             mock.patch.object(Path, 'lstat', return_value=mock.Mock(st_uid=0, st_mode=0o100600)):
            with self.assertRaisesRegex(ValueError, 'Stopped maintenance still owns'):
                installer.require_idle_maintenance([])

    def test_shell_dispatch_and_misplaced_role_refusal(self):
        path = ROOT / 'scripts/install.sh'
        result = subprocess.run(['bash', str(path), '--controller-only', '--help'], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result)
        self.assertIn(b'Install a locally verified controller candidate', result.stdout)
        result = subprocess.run(['bash', str(path), 'install', '--controller-only'], capture_output=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'first installer argument', result.stderr)


class MacServiceContract(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'darwin', 'Requires the macOS system Bash and Python')
    def test_controller_dispatch_under_system_bash(self):
        result = subprocess.run(['/bin/bash', str(ROOT / 'scripts/install.sh'), '--controller-only', '--help'],
                                env={'PATH': '/usr/bin:/bin:/usr/sbin:/sbin'}, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b'{render,install,uninstall}', result.stdout)

    def test_fixed_unprivileged_roles_and_lifecycle_wiring(self):
        backend, web, maintenance, http = controller_macos.definitions()
        self.assertEqual(backend['ProgramArguments'], lifecycle.mac_backend_arguments())
        self.assertEqual([unit['UserName'] for unit in (backend, web, maintenance, http)],
                         ['_wsprrypi', '_wsprrypiweb', 'root', '_wsprrypihttp'])
        self.assertEqual(len({unit['Label'] for unit in (backend, web, maintenance, http)}), 4)
        with mock.patch.object(sys, 'platform', 'darwin'), mock.patch.object(os, 'geteuid', return_value=0), \
             mock.patch.object(lifecycle, 'admit_launchd_plist'), \
             mock.patch.object(lifecycle, 'launchd_child', side_effect=['\n pid = 9001\n', '', '']) as launch:
            service = lifecycle.BackendLifecycle({'lifecycle': {'kind': 'launchd'}, 'application_root': '/app'},
                                                 available=lambda: True)
            service.stop(9001)
            service.start()
            self.assertEqual(launch.call_args_list, [
                mock.call('print', lifecycle.MAC_TARGET), mock.call('bootout', lifecycle.MAC_TARGET),
                mock.call('bootstrap', 'system', str(lifecycle.MAC_PLIST)),
            ])

    def test_wrong_mac_backend_process_refuses_before_stop(self):
        with mock.patch.object(sys, 'platform', 'darwin'), mock.patch.object(os, 'geteuid', return_value=0), \
             mock.patch.object(lifecycle, 'admit_launchd_plist'), \
             mock.patch.object(lifecycle, 'launchd_child', return_value='pid = 9002\n') as launch:
            service = lifecycle.BackendLifecycle({'lifecycle': {'kind': 'launchd'}, 'application_root': '/app'},
                                                 available=lambda: True)
            with self.assertRaises(lifecycle.archive.RecoveryError):
                service.stop(9001)
            launch.assert_called_once_with('print', lifecycle.MAC_TARGET)


if __name__ == '__main__':
    unittest.main()
