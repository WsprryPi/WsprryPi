#!/usr/bin/env python3
"""Exercise installer version identity and concise progress in temporary files."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts/install.sh"
HARNESS = r'''
set -euo pipefail
source "$INSTALLER"
BINARY_VERSION="${TEST_BINARY_VERSION:-}"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
logD() { printf '[DEBUG] %s\n' "$1" >&2; }
logI() { printf '[INFO ] %s\n' "$1"; }
logE() { printf '[ERROR] %s\n' "$1" >&2; }
logW() { printf '[WARN ] %s\n' "$1" >&2; }
warn() { printf '[WARN ] %s\n' "$1" >&2; }
sleep() { :; }
chown() { :; }
hostname() { printf '127.0.0.1\n'; }
# Preserve real file edits on both supported test hosts; production uses GNU sed.
sed() {
    [[ "${FAIL_SED:-false}" != true ]] || return 28
    if [[ "${1:-}" == -i && "$(uname -s)" == Darwin ]]; then
        local expression="$2"
        # BSD sed requires a separator before the closing command-group brace.
        [[ "$expression" != *' }' ]] || expression="${expression% \}}; }"
        expression="${expression//\\s/[[:space:]]}"
        command sed -i '' "$expression" "${@:3}"
    else
        command sed "$@"
    fi
}
# If the test host has an installed INI, never read or merge its settings.
upgrade_ini() { command cp -- "$2" "$3"; }
'''


class InstallerOutputTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-output-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def run_shell(self, shell: str, **values: str) -> subprocess.CompletedProcess[str]:
        environment = {
            **os.environ,
            "INSTALLER": str(INSTALLER), "FIXTURE_ROOT": str(self.root),
            "USER_HOME": str(self.root), "LOG_FILE": str(self.root / "installer.log"),
            "LOG_LEVEL": "INFO", "REQUIRE_SUDO": "false", "DRY_RUN": "false",
            "ACTION": "install", "NO_WEB": "true", "IS_REPO": "false",
            "SEM_VER": "3.2.0", "BINARY_SOURCE": "release",
            "BINARY_RELEASE_TAG": "v4.0.0-beta.1", "TEST_BINARY_VERSION": "",
        }
        environment.update(values)
        result = subprocess.run(
            ["bash", "-c", HARNESS + shell], cwd=self.root, env=environment,
            text=True, capture_output=True, check=False, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_preflight_distinguishes_selected_pending_and_source_versions(self) -> None:
        cases = (
            ({}, "Wsprry Pi version: pending executable validation."),
            ({"BINARY_SOURCE": "local", "IS_REPO": "true"},
             "Wsprry Pi version: pending local executable validation."),
            ({"BINARY_SOURCE": "build"}, "Wsprry Pi version: pending source checkout."),
            ({"BINARY_SOURCE": "build", "IS_REPO": "true", "SEM_VER": "4.1.0-devel+abc1234"},
             "Wsprry Pi source version: 4.1.0-devel+abc1234"),
            ({"BINARY_SOURCE": "auto", "DRY_RUN": "true"},
             "Wsprry Pi version: unresolved (dry run)."),
        )
        for values, expected in cases:
            with self.subTest(values=values):
                result = self.run_shell('print_version\n', **values)
                self.assertEqual(result.stdout.strip(), f"[INFO ] {expected}")
                self.assertNotIn("3.2.0", result.stdout)

    def test_binary_version_reaches_both_ini_files_and_success_summary(self) -> None:
        for mode in ("release", "local"):
            for trailing_slash in ("", "/"):
                with self.subTest(mode=mode, trailing_slash=trailing_slash):
                    directory = self.root / f"{mode}-{len(trailing_slash)} with spaces"
                    directory.mkdir()
                    source = directory / "source"
                    source.mkdir()
                    target = directory / "destination"
                    target.mkdir()
                    ini = source / "wsprrypi.ini"
                    original = "[WSPR]\nVersion = %SEMANTIC_VERSION%\nCall Sign = F4HFS\n"
                    ini.write_text(original)
                    result = self.run_shell(r'''
print_version
capture_install_sem_ver
LOCAL_CONFIG_DIR="$SOURCE"
manage_config wsprrypi.ini "$DESTINATION"
finish_script 0
''', BINARY_SOURCE=mode, TEST_BINARY_VERSION="4.0.0-beta.1+verified",
                        SOURCE=str(source), DESTINATION=str(target) + trailing_slash)
                    expected = original.replace("%SEMANTIC_VERSION%", "4.0.0-beta.1+verified")
                    for name in ("wsprrypi.ini", "wsprrypi.ini.stock"):
                        self.assertEqual((target / name).read_text(), expected)
                        self.assertEqual((target / name).stat().st_mode & 0o777, 0o644)
                        self.assertEqual(os.stat(str(target) + "//" + name).st_ino,
                                         (target / name).stat().st_ino)
                    self.assertEqual(ini.read_text(), original)
                    self.assertIn("Complete: Set version in INI.", result.stdout)
                    self.assertIn("Complete: Set version in INI backup.", result.stdout)
                    self.assertIn("Wsprry Pi version: 4.0.0-beta.1+verified\n", result.stdout)
                    self.assertNotIn("3.2.0", result.stdout)
                    self.assertNotIn(str(target) + "//", result.stdout + result.stderr)

    def test_source_capture_supplies_the_success_version(self) -> None:
        result = self.run_shell(r'''
LOCAL_REPO_DIR="$FIXTURE_ROOT"
get_sem_ver() { printf '4.2.0-devel+source123\n'; }
capture_install_sem_ver
[[ "$SEM_VER" == 4.2.0-devel+source123 ]]
finish_script 0
''', BINARY_SOURCE="build")
        self.assertIn("Wsprry Pi version: 4.2.0-devel+source123\n", result.stdout)
        self.assertNotIn("3.2.0", result.stdout)

    def test_missing_binary_version_fails_before_success(self) -> None:
        result = self.run_shell(r'''
if capture_install_sem_ver; then
    finish_script 0
    exit 90
fi
''')
        self.assertIn("Precompiled version was not validated.", result.stderr)
        self.assertNotIn("successful", result.stdout)
        self.assertNotIn("Wsprry Pi version:", result.stdout)

    def test_dry_run_failure_and_uninstall_do_not_report_installed_version(self) -> None:
        for action, dry_run, status in (("install", "true", "0"),
                                        ("install", "false", "1"),
                                        ("uninstall", "false", "0")):
            with self.subTest(action=action, dry_run=dry_run, status=status):
                result = self.run_shell(r'''
if finish_script "$FINISH_STATUS"; then
    [[ "$FINISH_STATUS" == 0 ]]
else
    [[ "$FINISH_STATUS" != 0 ]]
fi
''', ACTION=action, DRY_RUN=dry_run, FINISH_STATUS=status,
                    SEM_VER="4.0.0-beta.1+verified")
                self.assertNotIn("Wsprry Pi version:", result.stdout)

    def test_generic_replacement_callers_keep_their_default_label(self) -> None:
        target = self.root / "generic template"
        target.write_text("name=%EXAMPLE%\n")
        result = self.run_shell(r'''
replace_string_in_script "$TARGET" EXAMPLE preserved debug
''', TARGET=str(target))
        self.assertEqual(target.read_text(), "name=preserved\n")
        self.assertIn(f"Complete: Set EXAMPLE in {target}.", result.stdout)
        self.assertIn(str(target), result.stderr)

    def test_short_replacement_failure_retains_path_and_placeholder_diagnostics(self) -> None:
        target = self.root / "INI $(not-executed); file"
        original = "version=%SEMANTIC_VERSION%\n"
        target.write_text(original)
        result = self.run_shell(r'''
if replace_string_in_script "$TARGET" SEMANTIC_VERSION 4.0.0 \
    "Set version in INI" debug; then
    exit 90
fi
''', TARGET=str(target), FAIL_SED="true")
        self.assertEqual(target.read_text(), original)
        self.assertIn("Failed: Set version in INI.", result.stdout)
        self.assertIn(str(target), result.stderr)
        self.assertIn("%SEMANTIC_VERSION%", result.stderr)
        self.assertIn("status 28", result.stdout + result.stderr)
        self.assertNotIn("Complete: Set version in INI.", result.stdout)

    def test_apache_label_changes_without_changing_directive_edits(self) -> None:
        target = self.root / "Apache conf with spaces"
        target.write_text("ErrorLog error.log\n# CustomLog access.log\nServerName localhost\n")
        result = self.run_shell(r'''
modify_comment_lines "$TARGET" .log comment "Update log directives Apache conf" debug
''', TARGET=str(target))
        self.assertEqual(target.read_text(),
                         "# ErrorLog error.log\n# CustomLog access.log\nServerName localhost\n")
        self.assertIn("Complete: Update log directives Apache conf.", result.stdout)
        self.assertIn(str(target), result.stderr)
        result = self.run_shell('modify_comment_lines "$TARGET" .log uncomment debug\n',
                                TARGET=str(target))
        self.assertEqual(target.read_text(),
                         "ErrorLog error.log\nCustomLog access.log\nServerName localhost\n")
        self.assertIn(f"Complete: Update log directives in {target}.", result.stdout)


if __name__ == "__main__":
    unittest.main()
