#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Installer failure boundaries confined to temporary files and fake commands."""
import os
from pathlib import Path
import unittest
from fixture_commands import run_shell
from fixture_filesystem import temporary_root

INSTALLER = Path(__file__).resolve().parents[1] / 'install.sh'


class StateBoundary(unittest.TestCase):
    def setUp(self):
        self.root = temporary_root(self, 'state-install-shell-')

    def shell(self, source, **values):
        result = run_shell('source "$INSTALLER"\n' + source, cwd=self.root,
                           env={**os.environ, 'INSTALLER': str(INSTALLER), 'ROOT': str(self.root),
                                **{key: str(value) for key, value in values.items()}},
                           stage=self._testMethodName)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_executable_probe_is_exact_and_does_not_replace_or_start_anything(self):
        candidate = self.root / 'candidate $(not-executed).sh'
        candidate.write_text('#!/bin/sh\n[ "$#" = 1 ] && [ "$1" = --storage-format ] || exit 91\nprintf "%s\\n" "$FORMAT"\n')
        candidate.chmod(0o700)
        for value, expected in (('application-state/1', True), ('application-state/2', False)):
            self.shell(r'''
SUDO_USER=fixture
logE() { :; }
runuser() {
    [[ "$1" == -u && "$2" == fixture && "$3" == -- ]] || return 99
    shift 3
    "$@"
}
systemctl() { return 99; }
if validate_application_state_executable "$CANDIDATE"; then actual=true; else actual=false; fi
[[ "$actual" == "$EXPECTED" ]]
''', CANDIDATE=candidate, FORMAT=value, EXPECTED=str(expected).lower())

    def test_admitted_or_prepared_state_requires_recovery_but_absence_does_not(self):
        self.shell(r'''
STATE_TRANSITION_STARTED=true
if state_transition_needs_recovery "$ROOT/state"; then exit 91; fi
mkdir "$ROOT/state.prepare"
state_transition_needs_recovery "$ROOT/state"
rmdir "$ROOT/state.prepare"
mkdir "$ROOT/state"
state_transition_needs_recovery "$ROOT/state"
STATE_TRANSITION_STARTED=false
if state_transition_needs_recovery "$ROOT/state"; then exit 92; fi
''')

    def test_late_failure_preserves_compatible_binary_backup_and_withholds_restart(self):
        current, previous = self.root / 'binary', self.root / 'previous'
        current.write_bytes(b'new SQLite-capable candidate')
        previous.write_bytes(b'old INI binary')
        self.shell(r'''
BINARY_INSTALLED=true
BINARY_PREVIOUS="$PREVIOUS"
BINARY_DESTINATION="$CURRENT"
STATE_TRANSITION_STARTED=true
WAS_RUNNING=true
logE() { :; }
state_transition_needs_recovery() { [[ "$STATE_TRANSITION_STARTED" == true ]]; }
systemctl() { printf unexpected >"$ROOT/service-called"; return 99; }
exec_command() { printf unexpected >"$ROOT/command-called"; return 99; }
if rollback_executable; then exit 91; fi
[[ "$BINARY_INSTALLED" == state-retained ]]
if restore_daemon_state; then exit 92; fi
cleanup_precompiled_executable
rollback_executable
[[ -f "$PREVIOUS" && -f "$CURRENT" && ! -e "$ROOT/service-called" && ! -e "$ROOT/command-called" ]]
''', CURRENT=current, PREVIOUS=previous)
        self.assertEqual(current.read_bytes(), b'new SQLite-capable candidate')
        self.assertEqual(previous.read_bytes(), b'old INI binary')


if __name__ == '__main__':
    unittest.main()
