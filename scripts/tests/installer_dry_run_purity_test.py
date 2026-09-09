#!/usr/bin/env python3
"""Regression coverage for mutation-free installer dry runs."""

from __future__ import annotations

import hashlib
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts" / "install.sh"


def bash_quote(value: str | Path) -> str:
    return subprocess.run(
        ["bash", "-c", 'printf "%q" "$1"', "bash-quote", str(value)],
        text=True,
        stdout=subprocess.PIPE,
        check=True,
    ).stdout


def snapshot(root: Path) -> dict[str, tuple[str, int, int, str]]:
    result: dict[str, tuple[str, int, int, str]] = {}
    for path in sorted((root, *root.rglob("*"))):
        metadata = path.lstat()
        relative = "." if path == root else str(path.relative_to(root))
        if stat.S_ISREG(metadata.st_mode):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            kind = "file"
        elif stat.S_ISDIR(metadata.st_mode):
            digest = ""
            kind = "directory"
        elif stat.S_ISLNK(metadata.st_mode):
            digest = os.readlink(path)
            kind = "symlink"
        else:
            digest = ""
            kind = "other"
        result[relative] = (kind, stat.S_IMODE(metadata.st_mode), metadata.st_mtime_ns, digest)
    return result


class InstallerDryRunPurityTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-dry-run-")
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_shell(self, source: str, **values: Path | str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update({"INSTALLER": str(INSTALLER)})
        environment.update({name: '' for name in ('FGGLD', 'RESET', 'FGGRN', 'FGRED', 'MOVE_UP', 'CLEAR_LINE')})
        environment.update({name: str(value) for name, value in values.items()})
        result = subprocess.run(
            ["bash", "-c", source],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def test_upgrade_ini_dry_run_creates_nothing(self) -> None:
        paths = self.root / "INI paths; $(not-executed)"
        paths.mkdir()
        old_ini = paths / "old config.ini"
        new_ini = paths / "new config.ini"
        merged_ini = paths / "merged config.ini"
        old_ini.write_text("[WSPR]\nCall Sign = OLD\n", encoding="utf-8")
        new_ini.write_text("[WSPR]\nCall Sign = NEW\n", encoding="utf-8")
        before = snapshot(self.root)

        result = self.run_shell(
            r'''
source "$INSTALLER"
DRY_RUN=true
upgrade_ini "$OLD_INI" "$NEW_INI" "$MERGED_INI" debug
''',
            OLD_INI=old_ini,
            NEW_INI=new_ini,
            MERGED_INI=merged_ini,
        )

        self.assertEqual(snapshot(self.root), before)
        expected_backup = f"Dry run command: cp -f {bash_quote(old_ini)} {bash_quote(f'{old_ini}.pre_migration.bak')}"
        expected_merge = (
            f"Dry run INI merge: old={bash_quote(old_ini)} "
            f"new={bash_quote(new_ini)} output={bash_quote(merged_ini)}"
        )
        self.assertIn(expected_backup, result.stderr)
        self.assertIn(expected_merge, result.stderr)
        self.assertFalse(merged_ini.exists())
        self.assertFalse(Path(f"{old_ini}.pre_migration.bak").exists())

    def test_upgrade_ini_preserves_preexisting_merged_file(self) -> None:
        old_ini = self.root / "old.ini"
        new_ini = self.root / "new.ini"
        merged_ini = self.root / "merged.ini"
        old_ini.write_text("old\n", encoding="utf-8")
        new_ini.write_text("new\n", encoding="utf-8")
        merged_ini.write_text("pre-existing\n", encoding="utf-8")
        merged_ini.chmod(0o640)
        before = snapshot(self.root)

        self.run_shell(
            r'''
source "$INSTALLER"
logD() { :; }
DRY_RUN=true
upgrade_ini "$OLD_INI" "$NEW_INI" "$MERGED_INI" debug
''',
            OLD_INI=old_ini,
            NEW_INI=new_ini,
            MERGED_INI=merged_ini,
        )

        self.assertEqual(snapshot(self.root), before)

    def test_placeholder_replacement_is_display_only(self) -> None:
        target = self.root / "installed $(not-executed); config.ini"
        target.write_text("version=%SEMANTIC_VERSION%\n", encoding="utf-8")
        target.chmod(0o640)
        before = snapshot(self.root)

        result = self.run_shell(
            r'''
source "$INSTALLER"
DRY_RUN=true
replace_string_in_script "$TARGET" SEMANTIC_VERSION 'v9.9.9; $(not-executed)' debug
''',
            TARGET=target,
        )

        self.assertEqual(snapshot(self.root), before)
        replacement = "v9.9.9; $(not-executed)"
        expected = "Dry run command: " + " ".join(
            bash_quote(value)
            for value in (
                "sed",
                "-i",
                f"s|%SEMANTIC_VERSION%|{replacement}|g",
                target,
            )
        )
        self.assertIn(expected, result.stderr)

    def test_exec_command_debug_renders_exact_safe_argv_without_execution(self) -> None:
        sentinel = self.root / "command-invoked"
        before = snapshot(self.root)
        arguments = (
            "plain",
            "two words",
            "$(not-executed)",
            "semi;colon",
            "single'quote",
            "line\nbreak",
            "debug",
        )
        result = self.run_shell(
            r'''
source "$INSTALLER"
probe() { printf invoked >"$SENTINEL"; return 99; }
DRY_RUN=true
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
exec_command "argv display" probe plain "two words" '$(not-executed)' 'semi;colon' \
    "single'quote" $'line\nbreak' debug debug
''',
            SENTINEL=sentinel,
        )
        self.assertEqual(snapshot(self.root), before)
        expected = "Command: " + " ".join(
            bash_quote(value) for value in ("probe", *arguments)
        )
        self.assertIn("Name:    argv display", result.stderr)
        self.assertIn(expected, result.stderr)
        self.assertFalse(sentinel.exists())

    def test_exec_command_info_mode_is_informational_and_suppresses_dry_run(self) -> None:
        sentinel = self.root / "planning-command-invoked"
        result = self.run_shell(
            r'''
source "$INSTALLER"
probe() { printf invoked >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
DRY_RUN=true
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
EXEC_COMMAND_STATUS_MODE=info exec_command "Resolve read-only plan" probe --exact argv debug
[[ ! -e "$SENTINEL" ]]
''',
            SENTINEL=sentinel,
        )
        self.assertIn("[INFO ] Resolve read-only plan.", result.stdout)
        self.assertNotIn("Running:", result.stdout)
        self.assertNotIn("Complete:", result.stdout)
        self.assertIn("Command: probe --exact argv", result.stderr)
        self.assertFalse(sentinel.exists())

    def test_exec_command_info_mode_propagates_live_failure(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
probe() { printf 'child stdout\n'; printf 'child stderr\n' >&2; return 17; }
logI() { printf '[INFO ] %s\n' "$1"; }
logE() { printf '[ERROR] %s\n' "$1"; }
DRY_RUN=false
if EXEC_COMMAND_STATUS_MODE=info exec_command "Resolve read-only plan" probe; then
    exit 1
fi
'''
        )
        self.assertIn("[INFO ] Resolve read-only plan.", result.stdout)
        self.assertIn("[ERROR] Failed: Resolve read-only plan.", result.stdout)
        self.assertIn("Command failed with status 17: probe", result.stderr)
        self.assertNotIn("child stdout", result.stdout)
        self.assertNotIn("child stderr", result.stderr)

    def test_exec_command_info_mode_suppresses_live_success_output(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
probe() { printf 'child stdout\n'; printf 'child stderr\n' >&2; }
logI() { printf '[INFO ] %s\n' "$1"; }
DRY_RUN=false
EXEC_COMMAND_STATUS_MODE=info exec_command "Resolve read-only plan" probe
'''
        )
        self.assertEqual(result.stdout.strip(), "[INFO ] Resolve read-only plan.")
        self.assertNotIn("child stderr", result.stderr)

    def test_exec_command_suppresses_child_output_in_debug_mode(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
probe() { printf 'child stdout\n'; printf 'child stderr\n' >&2; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP='<UP>' CLEAR_LINE='<CLEAR>'
exec_command "Wrapped command" probe debug
'''
        )
        self.assertNotIn("child stdout", result.stdout)
        self.assertNotIn("child stderr", result.stderr)
        self.assertIn("Complete: Wrapped command.", result.stdout)
        self.assertIn("Command: probe", result.stderr)

    def test_exec_command_suppresses_child_output_on_failure(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
probe() { printf 'child stdout\n'; printf 'child stderr\n' >&2; return 19; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
if exec_command "Wrapped command" probe; then
    exit 1
fi
'''
        )
        self.assertNotIn("child stdout", result.stdout)
        self.assertNotIn("child stderr", result.stderr)
        self.assertIn("Failed: Wrapped command.", result.stdout)
        self.assertIn("Command failed with status 19: probe", result.stderr)

    def test_exec_command_has_no_child_output_override(self) -> None:
        source = INSTALLER.read_text(encoding="utf-8")
        self.assertNotIn("EXEC_COMMAND_SHOW_OUTPUT", source)

    def test_support_bundle_validation_uses_standard_dry_run_execution_item(self) -> None:
        sentinel = self.root / "age-validation-invoked"
        result = self.run_shell(
            r'''
source "$INSTALLER"
run_support_bundle_age_dependency_validation() {
    printf invoked >"$SENTINEL"
    return 99
}
DRY_RUN=true
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
validate_support_bundle_age_dependency debug
[[ ! -e "$SENTINEL" ]]
''',
            SENTINEL=sentinel,
        )
        self.assertIn(
            "Complete: (dry) Validate support-bundle encryption tools.",
            result.stdout,
        )
        self.assertNotIn(
            "Dry run: would validate the support-bundle encryption tools.",
            result.stdout,
        )
        self.assertIn(
            "Command: run_support_bundle_age_dependency_validation",
            result.stderr,
        )
        self.assertFalse(sentinel.exists())

    def test_support_bundle_validation_propagates_live_execution_failure(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
run_support_bundle_age_dependency_validation() { return 23; }
logE() { printf '[ERROR] %s\n' "$1" >&2; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
if validate_support_bundle_age_dependency debug; then
    exit 1
fi
'''
        )
        self.assertIn(
            "Failed: Validate support-bundle encryption tools.",
            result.stdout,
        )
        self.assertIn(
            "Required support-bundle encryption tools are unavailable or unsafe.",
            result.stderr,
        )

    def test_placeholder_replacement_allows_planned_new_target(self) -> None:
        target = self.root / "not-installed-yet.ini"
        before = snapshot(self.root)
        self.run_shell(
            r'''
source "$INSTALLER"
logD() { :; }
DRY_RUN=true
replace_string_in_script "$TARGET" SEMANTIC_VERSION v9.9.9 debug
''',
            TARGET=target,
        )
        self.assertEqual(snapshot(self.root), before)

    def test_placeholder_replacement_still_operates_normally(self) -> None:
        target = self.root / "installed.ini"
        target.write_text("version=%SEMANTIC_VERSION%\n", encoding="utf-8")
        self.run_shell(
            r'''
source "$INSTALLER"
logD() { :; }
sed() {
    if [[ "$1" == -i ]]; then
        shift
        command sed -i.bak "$@"
        rm -f "${!#}.bak"
    else
        command sed "$@"
    fi
}
DRY_RUN=false
replace_string_in_script "$TARGET" SEMANTIC_VERSION v9.9.9
''',
            TARGET=target,
        )
        self.assertEqual(target.read_text(encoding="utf-8"), "version=v9.9.9\n")

    def test_log_initialization_creates_no_dry_run_log(self) -> None:
        requested_log = self.root / "installer.log"
        before = snapshot(self.root)
        result = self.run_shell(
            r'''
source "$INSTALLER"
debug_print() { printf '%s\n' "$1"; }
DRY_RUN=true
LOG_FILE="$REQUESTED_LOG"
init_log debug
[[ "$LOG_FILE" == /dev/null ]]
''',
            REQUESTED_LOG=requested_log,
        )
        self.assertEqual(snapshot(self.root), before)
        self.assertIn("Dry run: file logging disabled.", result.stdout)
        self.assertFalse(requested_log.exists())

    def test_representative_install_and_uninstall_fixture_is_unchanged(self) -> None:
        source_dir = self.root / "config"
        destination_dir = self.root / "installed"
        source_dir.mkdir()
        destination_dir.mkdir()
        (source_dir / "wsprrypi.ini").write_text(
            "version=%SEMANTIC_VERSION%\n", encoding="utf-8"
        )
        active = destination_dir / "wsprrypi.ini"
        stock = destination_dir / "wsprrypi.ini.stock"
        active.write_text("active\n", encoding="utf-8")
        stock.write_text("stock\n", encoding="utf-8")
        before = snapshot(self.root)

        self.run_shell(
            r'''
source "$INSTALLER"
logD() { :; }
get_sem_ver() { printf 'test-version\n'; }
DRY_RUN=true
LOCAL_CONFIG_DIR="$SOURCE_DIR"
ACTION=install
manage_config wsprrypi.ini "$DESTINATION_DIR"
ACTION=uninstall
manage_config wsprrypi.ini "$DESTINATION_DIR"
''',
            SOURCE_DIR=source_dir,
            DESTINATION_DIR=destination_dir,
        )

        self.assertEqual(snapshot(self.root), before)

    def test_sound_management_uses_the_canonical_pi5_detector(self) -> None:
        source = INSTALLER.read_text(encoding="utf-8")
        function_body = source.split("\nmanage_sound() {", 1)[1].split(
            "\n# -----------------------------------------------------------------------------", 1
        )[0]
        self.assertIn("if is_pi5; then", function_body)
        self.assertNotIn("is_raspberry_pi_5", function_body)

    def test_pi5_never_disables_legacy_sound_and_uninstall_needs_no_reboot(self) -> None:
        sound_file = self.root / "alsa-blacklist.conf"
        result = self.run_shell(
            r'''
source "$INSTALLER"
definition=$(declare -f manage_sound)
definition=${definition/manage_sound /manage_sound_under_test }
definition=${definition/'file="/etc/modprobe.d/alsa-blacklist.conf"'/'file="$SOUND_FILE"'}
eval "$definition"
is_pi5() { return 0; }
logD() { :; }

ACTION=install
DRY_RUN=false
REBOOT=false
manage_sound_under_test debug
[[ ! -e "$SOUND_FILE" ]]
[[ "$REBOOT" == "false" ]]

printf 'blacklist snd_bcm2835\n' >"$SOUND_FILE"
ACTION=uninstall
DRY_RUN=true
REBOOT=false
manage_sound_under_test debug
[[ -e "$SOUND_FILE" ]]
[[ "$REBOOT" == "false" ]]
flag_need_reboot debug

ACTION=uninstall
DRY_RUN=false
REBOOT=false
manage_sound_under_test debug
[[ ! -e "$SOUND_FILE" ]]
[[ "$REBOOT" == "false" ]]
flag_need_reboot debug
''',
            SOUND_FILE=sound_file,
        )
        self.assertNotIn("re-enabled sound", result.stderr)
        self.assertNotIn("command not found", result.stderr)
        self.assertNotIn("Important Note", result.stdout)
        self.assertNotIn("sound system", result.stdout)

    def test_legacy_pi_sound_cleanup_still_requires_reboot(self) -> None:
        sound_file = self.root / "alsa-blacklist.conf"
        sound_file.write_text("blacklist snd_bcm2835\n", encoding="utf-8")
        self.run_shell(
            r'''
source "$INSTALLER"
definition=$(declare -f manage_sound)
definition=${definition/manage_sound /manage_sound_under_test }
definition=${definition/'file="/etc/modprobe.d/alsa-blacklist.conf"'/'file="$SOUND_FILE"'}
eval "$definition"
is_pi5() { return 1; }
logD() { :; }

ACTION=uninstall
DRY_RUN=false
REBOOT=false
manage_sound_under_test debug
[[ ! -e "$SOUND_FILE" ]]
[[ "$REBOOT" == "true" ]]
''',
            SOUND_FILE=sound_file,
        )

    def test_installer_has_one_reboot_prompt_implementation(self) -> None:
        source = INSTALLER.read_text(encoding="utf-8")
        self.assertEqual(source.count("\nflag_need_reboot() {"), 1)

    def test_compile_dry_run_does_not_create_staging_directory(self) -> None:
        checkout = self.root / "checkout"
        source_dir = checkout / "src"
        source_dir.mkdir(parents=True)
        before = snapshot(self.root)

        self.run_shell(
            r'''
source "$INSTALLER"
logD() { :; }
systemctl() { return 1; }
runuser() { printf invoked >"$SENTINEL"; return 99; }
DRY_RUN=true
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
SUDO_USER=tester
LOCAL_REPO_DIR="$CHECKOUT"
LOCAL_SOURCE_DIR="$SOURCE_DIR"
WSPR_BUILD_TYPE=RELEASE
compile_binary wsprrypi debug
[[ ! -e "$SENTINEL" ]]
''',
            CHECKOUT=checkout,
            SOURCE_DIR=source_dir,
            SENTINEL=self.root / "command-invoked",
        )

        self.assertEqual(snapshot(self.root), before)
        self.assertFalse((checkout / "executables").exists())

    def test_runtime_readiness_rejects_condition_skipped_service(self) -> None:
        result = subprocess.run(
            [
                "bash",
                "-c",
                r'''
source "$INSTALLER"
systemctl() {
    if [[ "$1" == is-active ]]; then return 3; fi
    return 0
}
curl() { return 0; }
sleep() { :; }
warn() { printf '%s\n' "$1" >&2; }
ACTION=install
DRY_RUN=false
NO_WEB=false
validate_wsprrypi_runtime
''',
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "INSTALLER": str(INSTALLER)},
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("did not become active", result.stderr)

    def test_runtime_readiness_requires_proxy_api_when_web_enabled(self) -> None:
        result = subprocess.run(
            [
                "bash",
                "-c",
                r'''
source "$INSTALLER"
systemctl() { [[ "$1" == is-active ]]; }
resolve_wsprrypi_service_ports() {
    WSPRRYPI_WEB_PORT=32415
    WSPRRYPI_SOCKET_PORT=32416
}
curl() {
    [[ "${*: -1}" == "http://127.0.0.1:32415/version" ]] && return 0
    return 22
}
sleep() { :; }
warn() { printf '%s\n' "$1" >&2; }
ACTION=install
DRY_RUN=false
NO_WEB=false
validate_wsprrypi_runtime
''',
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "INSTALLER": str(INSTALLER)},
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("REST endpoint on port 32415 is ready", result.stderr)
        self.assertIn("/wsprrypi/version Apache proxy", result.stderr)

    def test_runtime_readiness_reports_configured_direct_api_failure(self) -> None:
        result = subprocess.run(
            [
                "bash",
                "-c",
                r'''
source "$INSTALLER"
systemctl() { [[ "$1" == is-active ]]; }
resolve_wsprrypi_service_ports() {
    WSPRRYPI_WEB_PORT=32415
    WSPRRYPI_SOCKET_PORT=32416
}
curl() { return 7; }
sleep() { :; }
warn() { printf '%s\n' "$1" >&2; }
ACTION=install
DRY_RUN=false
NO_WEB=false
validate_wsprrypi_runtime
''',
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "INSTALLER": str(INSTALLER)},
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("configured local REST endpoint on port 32415", result.stderr)
        self.assertIn("installed INI and service journal", result.stderr)

    def test_runtime_readiness_waits_for_configured_direct_and_proxy_endpoints(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
resolve_wsprrypi_service_ports() {
    WSPRRYPI_WEB_PORT=32415
    WSPRRYPI_SOCKET_PORT=32416
}
systemctl() { [[ "$1" == is-active ]]; }
curl() {
    printf '%s\n' "${*: -1}" >>"$CALLS"
    count=$(wc -l <"$CALLS")
    ((count >= 3))
}
sleep() { :; }
ACTION=install
DRY_RUN=false
NO_WEB=false
validate_wsprrypi_runtime
''',
            CALLS=self.root / "readiness-calls",
        )
        calls = (self.root / "readiness-calls").read_text(encoding="utf-8").splitlines()
        self.assertEqual(calls[:3], [
            "http://127.0.0.1:32415/version",
            "http://127.0.0.1:32415/version",
            "http://127.0.0.1:32415/version",
        ])
        self.assertEqual(calls[3:], ["http://127.0.0.1/wsprrypi/version"])

    def test_runtime_readiness_no_web_requires_only_active_service(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
systemctl() { [[ "$1" == is-active ]]; }
curl() { printf invoked >"$SENTINEL"; return 99; }
sleep() { :; }
logI() { :; }
ACTION=install
DRY_RUN=false
NO_WEB=true
validate_wsprrypi_runtime
[[ ! -e "$SENTINEL" ]]
''',
            SENTINEL=self.root / "curl-invoked",
        )

    def test_runtime_readiness_dry_run_executes_nothing(self) -> None:
        sentinel = self.root / "readiness-invoked"
        before = snapshot(self.root)
        self.run_shell(
            r'''
source "$INSTALLER"
systemctl() { printf systemctl >"$SENTINEL"; return 99; }
curl() { printf curl >"$SENTINEL"; return 99; }
sleep() { printf sleep >"$SENTINEL"; return 99; }
logD() { :; }
ACTION=install
DRY_RUN=true
validate_wsprrypi_runtime debug
[[ ! -e "$SENTINEL" ]]
''',
            SENTINEL=sentinel,
        )
        self.assertEqual(snapshot(self.root), before)

    def test_service_ports_and_proxy_block_follow_preserved_ini(self) -> None:
        ini = self.root / "installed.ini"
        config = self.root / "site.conf"
        ini.write_text(
            "[Operation]\nWeb Port = 32415\nSocket Port = 32416\n",
            encoding="utf-8",
        )
        config.write_text(
            (ROOT / "config" / "wsprrypi.conf").read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        config.chmod(0o640)

        self.run_shell(
            r'''
source "$INSTALLER"
DRY_RUN=false
install_wsprrypi_proxy_block "$CONFIG" "$INI"
''',
            CONFIG=config,
            INI=ini,
        )

        rendered = config.read_text(encoding="utf-8")
        self.assertIn("http://127.0.0.1:32415/version", rendered)
        self.assertIn("ws://127.0.0.1:32416/socket", rendered)
        self.assertNotIn("127.0.0.1:31415", rendered)
        self.assertNotIn("127.0.0.1:31416", rendered)
        self.assertEqual(rendered.count("# BEGIN WsprryPi proxy configuration"), 1)
        self.assertEqual(rendered.count("# END WsprryPi proxy configuration"), 1)
        self.assertEqual(
            rendered.count("Include /usr/local/etc/wsprrypi-apache-network-policy.conf"),
            1,
        )
        self.assertEqual(stat.S_IMODE(config.stat().st_mode), 0o640)

    def test_service_port_resolution_matches_application_fallbacks(self) -> None:
        ini = self.root / "invalid.ini"
        ini.write_text(
            "[Operation]\nWeb Port = 80\nSocket Port = not-a-port\n",
            encoding="utf-8",
        )
        result = self.run_shell(
            r'''
source "$INSTALLER"
resolve_wsprrypi_service_ports "$INI"
printf '%s|%s\n' "$WSPRRYPI_WEB_PORT" "$WSPRRYPI_SOCKET_PORT"
''',
            INI=ini,
        )
        self.assertEqual(result.stdout.strip(), "31415|31416")

    def test_utc_timezone_dry_run_neither_prompts_nor_reconfigures(self) -> None:
        sentinel = self.root / "timezone-command-invoked"
        before = snapshot(self.root)
        self.run_shell(
            r'''
source "$INSTALLER"
logI() { :; }
date() {
    if [[ "${1:-}" == +%Z ]]; then printf 'UTC\n'; else printf 'now\n'; fi
}
read() { printf read >"$SENTINEL"; return 99; }
dpkg-reconfigure() { printf dpkg-reconfigure >"$SENTINEL"; return 99; }
DRY_RUN=true
TERSE=true
set_time debug
[[ ! -e "$SENTINEL" ]]
''',
            SENTINEL=sentinel,
        )
        self.assertEqual(snapshot(self.root), before)

    def test_finish_script_reports_install_and_uninstall_as_dry_runs(self) -> None:
        for action in ("install", "uninstall"):
            result = self.run_shell(
                r'''
source "$INSTALLER"
DRY_RUN=true
ACTION="$REQUESTED_ACTION"
REPO_TITLE='Wsprry Pi'
finish_script 0 debug
''',
                REQUESTED_ACTION=action,
            )
            self.assertIn("Dry run complete. No changes were applied.", result.stdout)
            self.assertLessEqual(max(map(len, result.stdout.splitlines())), 80)
            self.assertNotIn("Installation successful", result.stdout)
            self.assertNotIn("Uninstallation successful", result.stdout)
            self.assertNotIn("has been uninstalled", result.stdout)
            self.assertNotIn("To configure", result.stdout)

    def test_normal_finish_message_is_unchanged(self) -> None:
        result = self.run_shell(
            r'''
source "$INSTALLER"
hostname() { if [[ "${1:-}" == -I ]]; then printf '127.0.0.1\n'; else command hostname "$@"; fi; }
DRY_RUN=false
ACTION=install
NO_WEB=true
REPO_TITLE='Wsprry Pi'
finish_script 0
'''
        )
        self.assertIn("Installation successful: Wsprry Pi.", result.stdout)
        self.assertNotIn("Dry run successful", result.stdout)

    def test_failed_dry_run_never_claims_completion(self) -> None:
        result = subprocess.run(
            [
                "bash",
                "-c",
                r'''
source "$INSTALLER"
logE() { printf '%s\n' "$1" >&2; }
DRY_RUN=true
ACTION=install
REPO_TITLE='Wsprry Pi'
finish_script 1 debug
''',
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "INSTALLER": str(INSTALLER)},
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("Installation dry run failed", result.stderr)
        self.assertNotIn("successful", result.stdout.lower())
        self.assertNotIn("completed", result.stdout.lower())

    def test_sound_file_contains_only_configuration_not_wrapper_output(self) -> None:
        sound_file = self.root / 'alsa-blacklist.conf'
        result = self.run_shell(r'''
source "$INSTALLER"
definition=$(declare -f manage_sound)
definition=${definition/'file="/etc/modprobe.d/alsa-blacklist.conf"'/'file="$SOUND_FILE"'}
eval "$definition"
is_pi5() { return 1; }
ACTION=install
DRY_RUN=false
REBOOT=false
manage_sound
[[ "$REBOOT" == true ]]
''', SOUND_FILE=sound_file)
        self.assertEqual(sound_file.read_text(), 'blacklist snd_bcm2835\n')
        self.assertIn('Complete: Disable legacy analogue audio.', result.stdout)

    def test_i2c_fallback_logs_the_write_and_preserves_file_content(self) -> None:
        boot_config = self.root / 'config.txt'
        boot_config.write_text('# existing configuration\n')
        result = self.run_shell(r'''
source "$INSTALLER"
definition=$(declare -f manage_i2c)
definition=${definition//\/boot\/firmware\/config.txt/$BOOT_CONFIG}
definition=${definition//\/boot\/config.txt/$BOOT_CONFIG}
eval "$definition"
command() {
    if [[ "${1:-}" == -v && "${2:-}" == raspi-config ]]; then return 1; fi
    builtin command "$@"
}
ACTION=install
DRY_RUN=false
REBOOT=false
manage_i2c
[[ "$REBOOT" == true ]]
REBOOT=false
manage_i2c
[[ "$REBOOT" == false ]]
''', BOOT_CONFIG=boot_config)
        self.assertEqual(boot_config.read_text(), '# existing configuration\n\ndtparam=i2c_arm=on\n')
        self.assertEqual(result.stdout.count('Complete: Enable I2C in boot configuration.'), 1)

    def test_i2c_write_failure_has_no_completion_or_reboot_claim(self) -> None:
        boot_config = self.root / 'config.txt'
        boot_config.write_text('# original\n')
        result = self.run_shell(r'''
source "$INSTALLER"
definition=$(declare -f manage_i2c)
definition=${definition//\/boot\/firmware\/config.txt/$BOOT_CONFIG}
definition=${definition//\/boot\/config.txt/$BOOT_CONFIG}
eval "$definition"
command() {
    if [[ "${1:-}" == -v && "${2:-}" == raspi-config ]]; then return 1; fi
    builtin command "$@"
}
tee() { return 33; }
ACTION=install
DRY_RUN=false
REBOOT=false
if manage_i2c; then exit 1; fi
[[ "$REBOOT" == false ]]
''', BOOT_CONFIG=boot_config)
        self.assertEqual(boot_config.read_text(), '# original\n')
        self.assertIn('Failed: Enable I2C in boot configuration.', result.stdout)
        self.assertNotIn('Complete: Enable I2C', result.stdout)

    def test_direct_mutation_paths_have_dry_run_guards(self) -> None:
        source = INSTALLER.read_text(encoding="utf-8")

        def function(name: str) -> str:
            start = source.index(f"{name}() {{")
            return source[start : source.index("\n}\n", start) + 3]

        sound = function("manage_sound")
        self.assertLess(sound.index('if [[ "$DRY_RUN" == "true" ]]'), sound.index('exec_command "Disable legacy analogue audio"'))
        reboot = function("flag_need_reboot")
        self.assertLess(reboot.index('if [[ "$DRY_RUN" == "true" ]]'), reboot.index("read -rp"))
        ini = function("upgrade_ini")
        self.assertLess(ini.index('if [[ "$DRY_RUN" == "true" ]]'), ini.index("rm -f /tmp/upgrade_ini.err"))


if __name__ == "__main__":
    unittest.main()
