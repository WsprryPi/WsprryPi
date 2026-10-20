#!/usr/bin/env python3
"""Hardware-free regression coverage for systemd service mask recovery."""

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


def snapshot(root: Path) -> dict[str, tuple[str, int, str]]:
    result: dict[str, tuple[str, int, str]] = {}
    for path in sorted((root, *root.rglob("*"))):
        metadata = path.lstat()
        relative = "." if path == root else str(path.relative_to(root))
        if stat.S_ISREG(metadata.st_mode):
            kind = "file"
            identity = hashlib.sha256(path.read_bytes()).hexdigest()
        elif stat.S_ISDIR(metadata.st_mode):
            kind = "directory"
            identity = ""
        elif stat.S_ISLNK(metadata.st_mode):
            kind = "symlink"
            identity = os.readlink(path)
        else:
            kind = "other"
            identity = ""
        result[relative] = (kind, stat.S_IMODE(metadata.st_mode), identity)
    return result


HARNESS = r'''
set -euo pipefail
source "$INSTALLER"

FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
ACTION=install
LOCAL_SYSTEMD_DIR="$TEMPLATE_DIR"
SEM_VER=9.9.9-test

record() { local IFS=' '; printf '%s\n' "$*" >>"$CALLS"; }
logD() { :; }
logI() { :; }
logW() { printf '%s\n' "$1" >>"$MESSAGES"; }
logE() { printf '%s\n' "$1" >>"$MESSAGES"; }
warn() { printf '%s\n' "$1" >>"$MESSAGES"; }
sleep() { :; }
get_sem_ver() { return 91; }
sed() {
    if [[ "${FAIL_STEP:-}" == render && "${1:-}" == -i ]]; then
        return 28
    fi
    if [[ "${1:-}" == -i && "$(uname -s)" == Darwin ]]; then
        command sed -i '' "${@:2}"
    else
        command sed "$@"
    fi
}

systemctl() {
    record systemctl "$@"
    case "$1" in
        is-enabled)
            cat "$STATE"
            return "$(cat "$STATE_STATUS")"
            ;;
        is-active)
            [[ "$(cat "$ACTIVE")" == 1 ]]
            ;;
        unmask)
            [[ "${FAIL_STEP:-}" != unmask ]] || return 17
            if [[ "${KEEP_MASK:-false}" != true ]]; then
                if [[ "${2:-}" == --runtime ]]; then
                    rm -f -- "$RUNTIME"
                else
                    rm -f -- "$PERSISTENT"
                fi
                if [[ -L "$RUNTIME" ]]; then
                    printf 'masked-runtime\n' >"$STATE"
                elif [[ -L "$PERSISTENT" ]]; then
                    printf 'masked\n' >"$STATE"
                else
                    printf 'disabled\n' >"$STATE"
                fi
                printf '1\n' >"$STATE_STATUS"
            fi
            ;;
        stop)
            [[ "${FAIL_STEP:-}" != stop ]] || return 18
            printf '0\n' >"$ACTIVE"
            ;;
        disable)
            [[ "${FAIL_STEP:-}" != disable ]] || return 19
            printf 'disabled\n' >"$STATE"
            printf '1\n' >"$STATE_STATUS"
            ;;
        daemon-reload)
            [[ "${FAIL_STEP:-}" != reload ]] || return 20
            ;;
        enable)
            [[ "${FAIL_STEP:-}" != enable ]] || return 21
            printf 'enabled\n' >"$STATE"
            printf '0\n' >"$STATE_STATUS"
            ;;
        restart)
            [[ "${FAIL_STEP:-}" != restart ]] || return 22
            printf '1\n' >"$ACTIVE"
            ;;
        list-unit-files)
            [[ -e "$PERSISTENT" || -L "$PERSISTENT" ]] &&
                printf 'wsprrypi.service enabled enabled\n'
            ;;
        *) return 23 ;;
    esac
}

cp() {
    record cp "$@"
    [[ "${FAIL_STEP:-}" != stage ]] || return 24
    command cp "$@"
}
chown() {
    record chown "$@"
    [[ "${FAIL_STEP:-}" != ownership ]] || return 25
}
chmod() {
    record chmod "$@"
    [[ "${FAIL_STEP:-}" != mode ]] || return 26
    command chmod "$@"
}
mv() {
    record mv "$@"
    [[ "${FAIL_STEP:-}" != replace ]] || return 27
    local argc=$#
    local source="${@:argc-1:1}"
    local destination="${@:argc:1}"
    command mv -f -- "$source" "$destination"
}

definition=$(declare -f manage_service)
definition=${definition/manage_service /manage_service_under_test }
definition=$(printf '%s\n' "$definition" |
    sed 's|service_path="/etc/systemd/system/${daemon_systemd_name}"|service_path="$PERSISTENT"|;
         s|runtime_service_path="/run/systemd/system/${daemon_systemd_name}"|runtime_service_path="$RUNTIME"|;
         s|"/dev/null"|"$MASK_TARGET"|g')
eval "$definition"

run_install() {
    manage_service_under_test /usr/bin/wsprrypi \
        '/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini' false
}

case "$CASE" in
    install-once) run_install ;;
    reinstall) run_install; run_install ;;
    expect-failure) ! run_install ;;
    dry-run) DRY_RUN=true; run_install ;;
    uninstall) ACTION=uninstall; manage_service_under_test /usr/bin/wsprrypi ignored false ;;
    *) exit 90 ;;
esac
'''


class ServiceInstallRecoveryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-service-")
        self.root = Path(self.temporary.name)
        self.template_dir = self.root / "template"
        self.persistent_dir = self.root / "etc-systemd"
        self.runtime_dir = self.root / "run-systemd"
        self.template_dir.mkdir()
        self.persistent_dir.mkdir()
        self.runtime_dir.mkdir()
        self.template = self.template_dir / "generic.service"
        self.template.write_text(
            "[Unit]\nDescription=%DAEMON_NAME% %SEMANTIC_VERSION%\n"
            "[Service]\nExecStart=%EXEC_START%\nSyslogIdentifier=%SYSLOG_IDENTIFIER%\n",
            encoding="utf-8",
        )
        self.persistent = self.persistent_dir / "wsprrypi.service"
        self.runtime = self.runtime_dir / "wsprrypi.service"
        self.mask_target = self.root / "substitute-dev-null"
        self.mask_target.write_bytes(b"mask target sentinel\n")
        self.mask_target.chmod(0o666)
        self.calls = self.root / "calls"
        self.messages = self.root / "messages"
        self.state = self.root / "state"
        self.state_status = self.root / "state-status"
        self.active = self.root / "active"
        self.calls.write_text("", encoding="utf-8")
        self.messages.write_text("", encoding="utf-8")
        self.set_state("not-found", 4)
        self.active.write_text("0\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def set_state(self, state: str, status: int) -> None:
        self.state.write_text(f"{state}\n", encoding="utf-8")
        self.state_status.write_text(f"{status}\n", encoding="utf-8")

    def mask(self, path: Path) -> None:
        path.symlink_to(self.mask_target)

    def run_case(
        self,
        case: str = "install-once",
        *,
        fail_step: str = "",
        keep_mask: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        environment = {
            **os.environ,
            "INSTALLER": str(INSTALLER),
            "TEMPLATE_DIR": str(self.template_dir),
            "PERSISTENT": str(self.persistent),
            "RUNTIME": str(self.runtime),
            "MASK_TARGET": str(self.mask_target),
            "CALLS": str(self.calls),
            "MESSAGES": str(self.messages),
            "STATE": str(self.state),
            "STATE_STATUS": str(self.state_status),
            "ACTIVE": str(self.active),
            "CASE": case,
            "FAIL_STEP": fail_step,
            "KEEP_MASK": "true" if keep_mask else "false",
        }
        result = subprocess.run(
            ["bash", "-c", HARNESS],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def call_lines(self) -> list[str]:
        return self.calls.read_text(encoding="utf-8").splitlines()

    def assert_regular_unit(self) -> None:
        self.assertTrue(self.persistent.is_file())
        self.assertFalse(self.persistent.is_symlink())
        rendered = self.persistent.read_text(encoding="utf-8")
        self.assertIn("Description=wsprrypi 9.9.9-test", rendered)
        self.assertIn("ExecStart=/usr/local/bin/wsprrypi -J", rendered)
        self.assertEqual(stat.S_IMODE(self.persistent.stat().st_mode), 0o644)

    def assert_mask_target_unchanged(self, before: os.stat_result) -> None:
        after = self.mask_target.stat()
        self.assertEqual(self.mask_target.read_bytes(), b"mask target sentinel\n")
        self.assertEqual(stat.S_IMODE(after.st_mode), stat.S_IMODE(before.st_mode))
        self.assertEqual(after.st_uid, before.st_uid)
        self.assertEqual(after.st_gid, before.st_gid)

    def test_persistent_mask_with_nonzero_is_enabled_status_is_repaired(self) -> None:
        self.mask(self.persistent)
        self.set_state("masked", 1)
        target_before = self.mask_target.stat()
        self.run_case()
        self.assert_regular_unit()
        self.assert_mask_target_unchanged(target_before)
        calls = self.call_lines()
        self.assertLess(calls.index("systemctl unmask wsprrypi.service"), next(
            index for index, line in enumerate(calls) if line.startswith("cp ")
        ))

    def test_runtime_mask_is_repaired(self) -> None:
        self.mask(self.runtime)
        self.set_state("masked-runtime", 1)
        self.run_case()
        self.assertFalse(self.runtime.exists())
        self.assert_regular_unit()
        self.assertIn("systemctl unmask --runtime wsprrypi.service", self.call_lines())

    def test_healthy_enabled_service_stops_and_disables_before_replacement(self) -> None:
        self.persistent.write_text("old unit\n", encoding="utf-8")
        self.set_state("enabled", 0)
        self.active.write_text("1\n", encoding="utf-8")
        self.run_case()
        calls = self.call_lines()
        stop = calls.index("systemctl stop wsprrypi.service")
        disable = calls.index("systemctl disable wsprrypi.service")
        replace = next(index for index, line in enumerate(calls) if line.startswith("mv "))
        reload = calls.index("systemctl daemon-reload")
        enable = calls.index("systemctl enable wsprrypi.service")
        restart = calls.index("systemctl restart wsprrypi.service")
        self.assertLess(stop, disable)
        self.assertLess(disable, replace)
        self.assertLess(replace, reload)
        self.assertLess(reload, enable)
        self.assertLess(enable, restart)
        self.assertNotIn("systemctl unmask wsprrypi.service", calls)

    def test_absent_unit_installs_without_unmask_stop_or_disable(self) -> None:
        self.run_case()
        calls = self.call_lines()
        self.assert_regular_unit()
        self.assertNotIn("systemctl unmask wsprrypi.service", calls)
        self.assertNotIn("systemctl stop wsprrypi.service", calls)
        self.assertNotIn("systemctl disable wsprrypi.service", calls)

    def test_unmasked_disabled_service_is_replaced_without_unmask_or_disable(self) -> None:
        self.persistent.write_text("old unit\n", encoding="utf-8")
        self.set_state("disabled", 1)
        self.run_case()
        calls = self.call_lines()
        self.assert_regular_unit()
        self.assertFalse(any(line.startswith("systemctl unmask") for line in calls))
        self.assertNotIn("systemctl disable wsprrypi.service", calls)

    def test_repeated_installation_is_idempotent(self) -> None:
        self.run_case("reinstall")
        self.assert_regular_unit()
        calls = self.call_lines()
        self.assertEqual(calls.count("systemctl restart wsprrypi.service"), 2)
        self.assertEqual(sum(line.startswith("mv ") for line in calls), 2)
        self.assertFalse(list(self.persistent_dir.glob("*.tmp.*")))

    def test_unmask_failure_stops_before_unit_or_service_mutation(self) -> None:
        self.mask(self.persistent)
        self.set_state("masked", 1)
        self.run_case("expect-failure", fail_step="unmask")
        calls = self.call_lines()
        self.assertTrue(self.persistent.is_symlink())
        self.assertFalse(any(line.startswith(("cp ", "chown ", "chmod ", "mv ")) for line in calls))
        self.assertNotIn("systemctl daemon-reload", calls)
        self.assertIn("installation cannot safely replace the unit", self.messages.read_text())

    def test_surviving_mask_after_successful_unmask_fails_closed(self) -> None:
        self.mask(self.persistent)
        self.set_state("masked", 1)
        self.run_case("expect-failure", keep_mask=True)
        calls = self.call_lines()
        self.assertTrue(self.persistent.is_symlink())
        self.assertFalse(any(line.startswith("cp ") for line in calls))
        self.assertNotIn("systemctl enable wsprrypi.service", calls)
        self.assertIn("did not remove every persistent and runtime mask", self.messages.read_text())

    def test_staging_metadata_and_replacement_failures_stop_later_actions(self) -> None:
        for fail_step in ("stage", "render", "ownership", "mode", "replace"):
            with self.subTest(fail_step=fail_step):
                self.calls.write_text("", encoding="utf-8")
                self.messages.write_text("", encoding="utf-8")
                self.set_state("not-found", 4)
                self.active.write_text("0\n", encoding="utf-8")
                self.persistent.unlink(missing_ok=True)
                self.run_case("expect-failure", fail_step=fail_step)
                calls = self.call_lines()
                self.assertNotIn("systemctl daemon-reload", calls)
                self.assertNotIn("systemctl enable wsprrypi.service", calls)
                self.assertNotIn("systemctl restart wsprrypi.service", calls)
                self.assertFalse(list(self.persistent_dir.glob("*.tmp.*")))

    def test_uninstall_removes_a_mask_symlink_without_touching_its_target(self) -> None:
        self.mask(self.persistent)
        self.set_state("masked", 1)
        target_before = self.mask_target.stat()
        self.run_case("uninstall")
        self.assertFalse(self.persistent.exists())
        self.assertFalse(self.persistent.is_symlink())
        self.assert_mask_target_unchanged(target_before)

    def test_uninstall_stops_disables_removes_and_reloads_an_active_unit(self) -> None:
        self.persistent.write_text("installed unit\n", encoding="utf-8")
        self.set_state("enabled", 0)
        self.active.write_text("1\n", encoding="utf-8")

        self.run_case("uninstall")

        self.assertEqual(self.active.read_text(encoding="utf-8"), "0\n")
        self.assertFalse(self.persistent.exists())
        calls = self.call_lines()
        self.assertIn("systemctl is-active --quiet wsprrypi.service", calls)
        self.assertIn("systemctl stop wsprrypi.service", calls)
        self.assertIn("systemctl disable wsprrypi.service", calls)
        self.assertIn("systemctl daemon-reload", calls)
        self.assertFalse(any(line.startswith("systemctl list-unit-files") for line in calls))

    def test_dry_run_preserves_mask_and_every_fixture_byte(self) -> None:
        self.mask(self.persistent)
        self.set_state("masked", 1)
        before = {
            "template": snapshot(self.template_dir),
            "persistent": snapshot(self.persistent_dir),
            "runtime": snapshot(self.runtime_dir),
            "mask": self.mask_target.read_bytes(),
            "mask_metadata": self.mask_target.stat(),
        }
        self.run_case("dry-run")
        self.assertEqual(snapshot(self.template_dir), before["template"])
        self.assertEqual(snapshot(self.persistent_dir), before["persistent"])
        self.assertEqual(snapshot(self.runtime_dir), before["runtime"])
        self.assertEqual(self.mask_target.read_bytes(), before["mask"])
        self.assertEqual(
            stat.S_IMODE(self.mask_target.stat().st_mode),
            stat.S_IMODE(before["mask_metadata"].st_mode),
        )
        calls = self.call_lines()
        self.assertFalse(any(line.startswith("systemctl unmask") for line in calls))
        self.assertFalse(any(line.startswith(("cp ", "chown ", "chmod ", "mv ")) for line in calls))

    def test_installer_uses_no_follow_atomic_replacement_and_fail_fast_calls(self) -> None:
        source = INSTALLER.read_text(encoding="utf-8")
        self.assertIn('mv -Tf -- "$staged_path" "$service_path"', source)
        self.assertNotIn('cp -f "${source_path}" "${service_path}"', source)
        manage = source[source.index("manage_service() {"):source.index("resolve_wsprrypi_service_ports() {")]
        config = source[source.index("manage_config() {"):source.index("upgrade_ini() {")]
        group = source[source.index("local install_group=("):]
        self.assertLess(manage.index("repair_systemd_service_mask"), manage.index("systemctl is-active"))
        self.assertLess(manage.index("install_systemd_service_unit"), manage.index('exec_command "Reload systemd"'))
        self.assertNotIn("get_sem_ver", manage)
        self.assertNotIn("get_sem_ver", config)
        self.assertLess(
            group.index('"capture_install_sem_ver"'),
            group.index('"manage_config'),
        )


if __name__ == "__main__":
    unittest.main()
