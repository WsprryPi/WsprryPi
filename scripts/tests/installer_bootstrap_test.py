#!/usr/bin/env python3
"""Exercise streamed installer bootstrap without packages, services, or hardware."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts/install.sh"
AGE_HELPER = ROOT / "scripts/support_bundle_age_dependency.sh"

# Inject fixture boundaries immediately before the real stdin entry point.
# Keep context detection, orchestration, cloning, command reporting, helper
# loading, metadata validation, and the application dispatcher unchanged.
FIXTURE = r'''
record() { printf '%s\n' "$1" >>"$EVENTS"; }
for step in process_args enforce_sudo setup_log validate_depends validate_sys_accs \
    validate_env_vars check_bash check_sh_ver check_bitness check_release \
    check_arch check_internet prepare_rp1_gpclk_dkms_installation start_script \
    preflight_build_resources print_model_name print_system print_version \
    display_compilation_resource_notes detect_apache_web_mode set_time \
    validate_apache_web_availability capture_install_sem_ver remove_legacy_services \
    remove_legacy_files_and_dirs manage_exe manage_support_bundle_runtime \
    manage_pico_usb_runtime manage_route_application manage_config manage_i2c \
    manage_service activate_rp1_gpclk_runtime_administration manage_web \
    manage_apache validate_wsprrypi_runtime manage_sound \
    cleanup_files_in_directories restore_daemon_state flag_need_reboot \
    finish_script egress; do
    eval "$step() { :; }"
done
logI() { :; }
logE() { printf '[ERROR] %s\n' "$1" >&2; }
warn() { printf '[WARN] %s\n' "$1" >&2; }
sleep() { :; }
prepare_rp1_gpclk_runtime_update() { record neutral-runtime; }
handle_apt_packages() {
    record packages
    [[ "$FAIL_AT" != packages ]]
}
validate_precompiled_runtime() { :; }
validate_rp1_gpclk_build_dependencies() { record rp1-dependencies; }
apply_rp1_gpclk_dkms_installation() { record provider; }
prepare_install_executable() { record application; }
rollback_executable() { record rollback; }

runuser() {
    [[ "$1" == -u && "$2" == "$SUDO_USER" && "$3" == -- &&
       "$4" == git && "$5" == clone && "$6" == -b &&
       "$7" == "$REPO_BRANCH" && "$8" == "$GIT_CLONE" &&
       "$9" == "$LOCAL_REPO_DIR" ]] || return 90
    record clone
    [[ "$FAIL_AT" != clone ]] || return 18
    [[ ! -e "$LOCAL_REPO_DIR" ]] || return 91
    mkdir -p "$LOCAL_REPO_DIR/scripts"
    cp "$AGE_HELPER" "$LOCAL_REPO_DIR/scripts/support_bundle_age_dependency.sh"
    # Use the helper's existing filesystem-fixture seam, including its actual
    # owner/mode/type checks, instead of depending on host /usr/bin/age.
    cat >>"$LOCAL_REPO_DIR/scripts/support_bundle_age_dependency.sh" <<'HELPER'
support_bundle_validate_age_dependency() {
    record age-validation
    support_bundle_validate_age_dependency_at_root "$AGE_ROOT" "$FIXTURE_UID"
}
HELPER
}

prepare_precompiled_executable() {
    [[ "$BINARY_SOURCE" != build ]] || return 0
    [[ "$DRY_RUN" != true ]] || return 0
    # Precompiled preparation already clones before resolving its packages.
    git_clone "$@"
}
if [[ "$EXISTING_CHECKOUT" == true ]]; then
    handle_execution_context() { IS_REPO=true; }
    get_proj_params() { LOCAL_REPO_DIR="$USER_HOME/WsprryPi"; }
fi
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
'''


class InstallerBootstrapTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-bootstrap-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.checkout = self.root / "WsprryPi"
        self.events = self.root / "events"
        self.age_root = self.root / "age-root"
        (self.age_root / "usr/bin").mkdir(parents=True)
        for name in ("age", "age-keygen"):
            tool = self.age_root / "usr/bin" / name
            tool.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            tool.chmod(0o755)

    def run_installer(self, **values: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update(
            USER_HOME=str(self.root), SUDO_USER="fixture-user", REQUIRE_SUDO="false",
            AGE_HELPER=str(AGE_HELPER), AGE_ROOT=str(self.age_root),
            FIXTURE_UID=str(os.getuid()), EVENTS=str(self.events), FAIL_AT="",
            EXISTING_CHECKOUT="false", BINARY_SOURCE="build", ACTION="install",
            DRY_RUN="false", REPO_BRANCH="main", NO_WEB="true",
        )
        environment.update(values)
        source = INSTALLER.read_text(encoding="utf-8")
        entry_point = '\nif [[ -z "${BASH_SOURCE[0]:-}" || "${BASH_SOURCE[0]}" == "$0" ]]; then'
        self.assertEqual(source.count(entry_point), 1)
        return subprocess.run(
            ["bash"], input='getent() { printf "fixture:x:1:1::%s:/bin/sh\\n" "$USER_HOME"; }\n'
            + source.replace(entry_point, "\n" + FIXTURE + entry_point),
            cwd=self.root, env=environment, text=True, capture_output=True,
            timeout=30, check=False,
        )

    def recorded_events(self) -> list[str]:
        return self.events.read_text(encoding="utf-8").splitlines()

    def test_fresh_streamed_build_clones_before_loading_age_helper(self) -> None:
        for branch in ("main", "devel"):
            with self.subTest(branch=branch):
                home = self.root / branch
                home.mkdir()
                self.events.unlink(missing_ok=True)
                self.assertFalse((home / "WsprryPi").exists())
                result = self.run_installer(USER_HOME=str(home), REPO_BRANCH=branch)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.recorded_events(), [
                    "neutral-runtime", "packages", "clone", "age-validation",
                    "rp1-dependencies", "provider", "application",
                ])
                self.assertIn("Complete: Validate support-bundle encryption tools.", result.stdout)

    def test_clone_failure_stops_before_validation_and_provider_changes(self) -> None:
        result = self.run_installer(FAIL_AT="clone")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), ["neutral-runtime", "packages", "clone"])
        self.assertFalse(self.checkout.exists())

    def test_package_failure_does_not_clone_or_load_helper(self) -> None:
        result = self.run_installer(FAIL_AT="packages")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), ["neutral-runtime", "packages"])
        self.assertFalse(self.checkout.exists())

    def test_unsafe_age_stops_before_provider_and_application_changes(self) -> None:
        (self.age_root / "usr/bin/age").chmod(0o777)
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), [
            "neutral-runtime", "packages", "clone", "age-validation",
        ])
        self.assertIn("Failed: Validate support-bundle encryption tools.", result.stdout)

    def test_fresh_dry_run_does_not_clone_or_load_helper(self) -> None:
        result = self.run_installer(DRY_RUN="true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("clone", self.recorded_events())
        self.assertNotIn("age-validation", self.recorded_events())
        self.assertFalse(self.checkout.exists())
        self.assertIn("Complete: (dry) Validate support-bundle encryption tools.", result.stdout)

    def test_precompiled_preparation_does_not_clone_twice(self) -> None:
        result = self.run_installer(BINARY_SOURCE="local")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.recorded_events(), [
            "clone", "neutral-runtime", "packages", "age-validation",
            "rp1-dependencies", "provider", "application",
        ])

    def test_existing_checkout_is_used_without_cloning(self) -> None:
        scripts = self.checkout / "scripts"
        scripts.mkdir(parents=True)
        (scripts / AGE_HELPER.name).write_text(
            AGE_HELPER.read_text(encoding="utf-8") + '''
support_bundle_validate_age_dependency() {
    record age-validation
    support_bundle_validate_age_dependency_at_root "$AGE_ROOT" "$FIXTURE_UID"
}
''', encoding="utf-8",
        )
        result = self.run_installer(EXISTING_CHECKOUT="true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("clone", self.recorded_events())
        self.assertIn("age-validation", self.recorded_events())
        self.assertIn("application", self.recorded_events())


if __name__ == "__main__":
    unittest.main()
