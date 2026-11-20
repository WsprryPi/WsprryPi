# SPDX-License-Identifier: MIT
"""Browser-service orchestration with fake commands and temporary fixtures."""
import os
from pathlib import Path
import unittest
from fixture_commands import run_shell
from fixture_filesystem import temporary_root

ROOT = Path(__file__).resolve().parents[2]


class BrowserServiceInstall(unittest.TestCase):
    def setUp(self):
        self.root = temporary_root(self, 'browser-service-install-')

    def shell(self, extra='', **environment):
        source = r'''
source "$INSTALLER"
ACTION=install
SEM_VER=9.9.9
WSPR_EXE=wsprrypi
LOCAL_SYSTEMD_DIR="$SOURCE/systemd"
LOCAL_REPO_DIR="$SOURCE"
BINARY_INSTALL_DIR=/usr/local/bin
logD() { :; }; logI() { :; }; logE() { :; }
repair_systemd_service_mask() { local IFS=' '; printf 'repair %s\n' "$*" >>"$CALLS"; }
install_systemd_service_unit() { local IFS=' '; printf 'unit %s\n' "$*" >>"$CALLS"; }
exec_command() {
    local IFS=' '
    local description="$1"; shift
    printf 'command %s\n' "$*" >>"$CALLS"
    [[ "${FAIL_START:-false}" != true || "$1 $2" != 'systemctl restart' ]]
}
resolve_wsprrypi_maintenance_role() { printf '%s\n' "${ROLE:-pi}"; }
''' + extra
        return run_shell(source, cwd=self.root,
                         env={**os.environ, 'INSTALLER': str(ROOT / 'scripts/install.sh'), 'SOURCE': str(ROOT),
                              'CALLS': str(self.root / 'calls'), **environment}, stage=self._testMethodName)

    def test_browser_start_is_independent_of_backend_restart(self):
        result = self.shell('manage_browser_service\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = (self.root / 'calls').read_text()
        self.assertIn('systemctl enable wsprrypi-web.service', calls)
        self.assertIn('systemctl restart wsprrypi-web.service', calls)
        self.assertNotIn('systemctl restart wsprrypi.service', calls)
        self.assertIn('--web-service', calls)
        self.assertNotIn('--state-directory', calls)
        self.assertLess(calls.index('unit '), calls.index('systemctl daemon-reload'))

    def test_no_web_disables_only_browser_and_retains_backend(self):
        result = self.shell('manage_browser_service\n', NO_WEB='true')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = (self.root / 'calls').read_text()
        self.assertIn('systemctl disable --now wsprrypi-web.service', calls)
        self.assertNotIn('systemctl restart', calls)
        self.assertNotIn('systemctl stop wsprrypi.service', calls)

    def test_failed_browser_start_is_reported(self):
        result = self.shell('if manage_browser_service; then exit 91; fi\n', FAIL_START='true')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_maintenance_starts_independently_with_admitted_role_and_no_web(self):
        result = self.shell('manage_maintenance_service\n', NO_WEB='true', ROLE='controller')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = (self.root / 'calls').read_text()
        self.assertIn('recovery_runtime_install.py', calls)
        self.assertIn('--maintenance-worker --role controller', calls)
        self.assertIn('systemctl restart wsprrypi-maintenance.service', calls)
        self.assertNotIn('systemctl restart wsprrypi.service', calls)
        self.assertNotIn('systemctl disable', calls)

    def test_units_preserve_privilege_and_restore_lifecycle_boundaries(self):
        browser = (ROOT / 'systemd/browser.service').read_text()
        backend = (ROOT / 'systemd/generic.service').read_text()
        for required in ('User=www-data', 'Group=www-data', 'StateDirectory=wsprrypi-web',
                         'NoNewPrivileges=true', 'PrivateDevices=true', 'InaccessiblePaths=-/var/lib/wsprrypi',
                         'Wants=wsprrypi.service', 'After=wsprrypi.service'):
            self.assertIn(required, browser)
        for dependency in ('Requires=', 'PartOf=', 'BindsTo='):
            self.assertNotIn(dependency, browser)
        self.assertIn('User=root', backend)
        self.assertIn('Group=www-data', backend)
        self.assertIn('RuntimeDirectoryMode=0750', backend)
        self.assertIn('RuntimeDirectory=wsprrypi', backend)
        maintenance = (ROOT / 'systemd/maintenance.service').read_text()
        self.assertIn('Before=wsprrypi.service', maintenance)
        self.assertIn('User=root', maintenance)
        self.assertIn('StateDirectoryMode=0700', maintenance)
        self.assertNotIn('Requires=wsprrypi', maintenance)

    def test_executable_replacement_refuses_active_or_malformed_maintenance_state(self):
        directory = self.root / 'bin';directory.mkdir()
        executable = directory / 'wsprrypi'
        executable.write_text('#!/bin/sh\nprintf "%s\\n" "$MAINTENANCE_STATE"\n')
        executable.chmod(0o755)
        harness = '''
BINARY_INSTALL_DIR="$PRIVATE_BIN"
systemctl() { [[ "$1" == is-active && "${@: -1}" == wsprrypi-maintenance.service ]]; }
if stop_binary_service; then exit 91; fi
'''
        for value in ('{"schema":"recovery-job-list/1","active":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","jobs":[]}',
                      '{"schema":"recovery-job-list/1","jobs":[]}'):
            result = self.shell(harness, PRIVATE_BIN=str(directory), MAINTENANCE_STATE=value)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((self.root / 'calls').exists())

    def test_executable_replacement_stops_only_known_idle_maintenance_worker(self):
        directory = self.root / 'bin';directory.mkdir()
        executable = directory / 'wsprrypi'
        executable.write_text('#!/bin/sh\nprintf \'%s\\n\' \'{"schema":"recovery-job-list/1","active":null,"jobs":[]}\'\n')
        executable.chmod(0o755)
        result = self.shell('''
BINARY_INSTALL_DIR="$PRIVATE_BIN"
systemctl() { [[ "$1" == is-active && "${@: -1}" == wsprrypi-maintenance.service ]]; }
stop_binary_service
[[ "$WAS_MAINTENANCE_RUNNING" == true ]]
''', PRIVATE_BIN=str(directory))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'calls').read_text().strip(), 'command systemctl stop wsprrypi-maintenance.service')


if __name__ == '__main__':
    unittest.main()
