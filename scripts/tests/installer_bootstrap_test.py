#!/usr/bin/env python3
"""Exercise streamed installer bootstrap without packages, services, or hardware."""

from __future__ import annotations

import os
import shutil
import subprocess
import unittest
from fixture_commands import run_shell as run_fixture_shell
from fixture_filesystem import temporary_root
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts/install.sh"
AGE_HELPER = ROOT / "scripts/support_bundle_age_dependency.sh"

# Inject fixture boundaries immediately before the real stdin entry point.
# Keep context detection, orchestration, cloning, command reporting, helper
# loading, metadata validation, and the application dispatcher unchanged.
FIXTURE = r'''
eval "$(declare -f print_version | sed '1s/print_version/installer_print_version/')"
record() { printf '%s\n' "$1" >>"$EVENTS"; }
for step in process_args enforce_sudo setup_log validate_depends validate_sys_accs \
    validate_env_vars check_bash check_sh_ver check_bitness check_release \
    check_arch check_internet set_time \
    validate_apache_web_availability capture_install_sem_ver remove_legacy_services \
    remove_legacy_files_and_dirs manage_exe manage_support_bundle_runtime \
    manage_pico_usb_runtime manage_route_application manage_config manage_i2c \
    manage_service manage_udp_logging activate_rp1_gpclk_runtime_administration manage_web \
    manage_apache validate_wsprrypi_runtime manage_sound \
    cleanup_files_in_directories restore_daemon_state flag_need_reboot \
    begin_application_ownership complete_application_ownership remove_application_ownership \
    finish_script egress; do
    eval "$step() { :; }"
done
logD() { :; }
logI() { printf '[INFO ] %s\n' "$1"; }
logE() { printf '[ERROR] %s\n' "$1" >&2; }
logW() { printf '[WARN] %s\n' "$1" >&2; }
warn() { printf '[WARN] %s\n' "$1" >&2; }
sleep() { :; }
start_script() { record confirmation; [[ "$FAIL_AT" != confirmation ]]; }
preflight_build_resources() { record resources; [[ "$FAIL_AT" != resources ]]; }
apply_build_resource_plan() { record build-resources; [[ "$FAIL_AT" != build-resources ]]; }
print_model_name() { record model-info; }
print_system() { record system-info; }
print_version() { record version-info; installer_print_version "$@"; }
display_compilation_resource_notes() { record parallelism-info; }
detect_apache_web_mode() { record apache-mode; }
rp1_gpclk_dkms_installation_selected() { [[ "$RP1_SELECTED" == true ]]; }
prepare_rp1_gpclk_runtime_update() { record neutral-runtime; }
ensure_picotool() {
    exec_command "Install and validate USB-capable picotool" fixture_picotool
}
fixture_picotool() {
    [[ -d "$LOCAL_REPO_DIR/scripts" ]] || return 94
    record picotool
    [[ "$FAIL_AT" != picotool ]]
}
resolve_libgpiod_runtime_pkg() { printf '%s\n' libgpiod-runtime-fixture; }
apt-get() {
    local IFS=' '
    case "$*" in
        update) record apt-update; [[ "$FAIL_AT" != apt-update ]];;
        'install -f -y') record apt-repair; [[ "$FAIL_AT" != apt-repair ]];;
        'install -y libprecompiled-fixture')
            record precompiled-packages
            [[ "$FAIL_AT" != precompiled-packages ]];;
        'install -y '*)
            [[ " $* " == *' git '* ]] || return 92
            record packages
            [[ "$FAIL_AT" != packages ]] || return 1
            cp "$GIT_STUB" "$FIXTURE_BIN/git"
            chmod 0755 "$FIXTURE_BIN/git"
            ;;
        *) return 93;;
    esac
}
curl() {
    if [[ "${*: -1}" == */scripts/precompiled_binary.py ]]; then
        cat "$BINARY_HELPER"
    else
        cp "$RP1_HELPER" "$RP1_GPCLK_DKMS_HELPER"
    fi
}
validate_precompiled_runtime() { :; }
validate_rp1_gpclk_build_dependencies() { record rp1-dependencies; }
apply_rp1_gpclk_dkms_installation() { record provider; }
prepare_install_executable() { printf "%s\n" "$BINARY_SOURCE" >"$RESOLVED_BINARY"; record application; }
rollback_executable() { record rollback; }

runuser() {
    [[ "$1" == -u && "$2" == "$SUDO_USER" && "$3" == -- &&
       "$4" == git && "$5" == clone && "$6" == -b &&
       "$7" == "$REPO_BRANCH" && "$8" == "$GIT_CLONE" &&
       "$9" == "$LOCAL_REPO_DIR" ]] || return 90
    record clone
    [[ "$FAIL_AT" != clone ]] || return 18
    command -v git >/dev/null || return 94
    [[ ! -e "$LOCAL_REPO_DIR" ]] || return 91
    mkdir -p "$LOCAL_REPO_DIR/scripts" "$LOCAL_REPO_DIR/config"
    cp "$SOURCE_ROOT/config/rp1-provider-requirements.json" "$LOCAL_REPO_DIR/config/"
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
    git_clone "$@" || return 1
    BINARY_RUNTIME_PACKAGES=(libprecompiled-fixture)
}
if [[ "$EXISTING_CHECKOUT" == true ]]; then
    handle_execution_context() { IS_REPO=true; }
    get_proj_params() { LOCAL_REPO_DIR="$USER_HOME/WsprryPi"; }
fi

# Uninstall runs real context detection, preflight and dispatch. Only external
# source transport and mutating lifecycle boundaries are replaced.
if [[ "$ACTION" == uninstall ]]; then
    git() {
        [[ "$1" == clone && "$2" == --depth && "$3" == 1 &&
           "$4" == --single-branch && "$5" == --branch &&
           "$6" == "$REPO_BRANCH" && "$7" == -- && "$8" == "$GIT_CLONE" ]] || return 95
        record uninstall-fetch
        printf '%s\n' "${9%/source}" >"$STAGED_SOURCE"
        [[ "$FAIL_AT" != uninstall-fetch ]] || return 18
        mkdir -p "$9/scripts" "$9/config"
        cp "$SOURCE_ROOT"/scripts/*.py "$9/scripts/"
        cp "$SOURCE_ROOT"/scripts/*runtime_install.sh "$9/scripts/"
        cp "$SOURCE_ROOT/config/pico-usb-installer.json" "$SOURCE_ROOT/config/rp1-provider-requirements.json" "$9/config/"
        case "$FAIL_AT" in
            missing) rm "$9/scripts/$BROKEN_INPUT";;
            symlink) rm "$9/scripts/$BROKEN_INPUT"; ln -s "$SOURCE_ROOT/scripts/$BROKEN_INPUT" "$9/scripts/$BROKEN_INPUT";;
            python-syntax) printf 'invalid python !\n' >"$9/scripts/udp_log_control.py";;
            shell-entry) printf ':\n' >"$9/scripts/pico_usb_runtime_install.sh";;
            shell-syntax) printf 'if\n' >"$9/scripts/pico_usb_runtime_install.sh";;
            policy) printf '{invalid\n' >"$9/config/pico-usb-installer.json";;
        esac
    }
    for step in prepare_owned_rp1_gpclk_runtime_removal remove_owned_rp1_gpclk_dkms_provider \
        manage_apache manage_web manage_udp_logging manage_service manage_i2c \
        manage_config manage_route_application manage_pico_usb_runtime \
        manage_support_bundle_runtime manage_exe manage_sound; do
        eval "$step() { record remove:$step; }"
    done
    egress() { local status=$?; cleanup_uninstall_source; exit "$status"; }
fi
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
'''


class InstallerBootstrapTest(unittest.TestCase):
    def setUp(self) -> None:
        self.root = temporary_root(self, "wsprrypi-bootstrap-")
        self.checkout = self.root / "WsprryPi"
        self.events = self.root / "events"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        # Only these tools exist in the fixture PATH. Git is genuinely absent
        # until the mocked APT transaction installs it; host Git cannot mask it.
        for name in ("bash", "python3", "dirname", "basename", "realpath", "uname",
                     "whoami", "tr", "sed", "grep", "awk", "cut", "cat", "cp",
                     "chmod", "install", "mkdir", "mktemp", "rm", "tail", "tee", "sleep", "stat", "ln"):
            tool = shutil.which(name)
            self.assertIsNotNone(tool, name)
            (self.bin / name).symlink_to(tool)
        self.git_stub = self.root / "git-stub"
        self.git_stub.write_text('#!/bin/sh\n[ "$1" = --version ]\n', encoding="utf-8")
        self.rp1_helper = self.root / "rp1-helper.py"
        self.rp1_helper.write_text('''import os, subprocess, sys
from pathlib import Path
try:
    subprocess.run(["git", "--version"], check=True, capture_output=True)
except (OSError, subprocess.CalledProcessError):
    print("RP1-GPIO-CLK-DKMS operation refused: required command is unavailable: git", file=sys.stderr)
    sys.exit(1)
with Path(os.environ["EVENTS"]).open("a") as events:
    events.write("rp1-plan\\n")
if os.environ["FAIL_AT"] == "plan":
    print("RP1-GPIO-CLK-DKMS operation refused: fixture source rejected", file=sys.stderr)
    sys.exit(17)
''', encoding="utf-8")
        self.binary_helper = self.root / "binary-helper.py"
        self.binary_helper.write_text('''import os, sys
from pathlib import Path
with Path(os.environ["EVENTS"]).open("a") as events:
    events.write("binary-resolution\\n")
result=os.environ.get("AUTO_BINARY_RESULT","release")
if result=="failure":
    print("fixture release lookup failed",file=sys.stderr)
    sys.exit(17)
print("release v4.0.0-beta.1 "+"a"*64 if result=="release" else "build")
''',encoding="utf-8")
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
            PATH=str(self.bin), FIXTURE_BIN=str(self.bin), GIT_STUB=str(self.git_stub),
            SOURCE_ROOT=str(ROOT), STAGED_SOURCE=str(self.root / "staged-source"),
            BROKEN_INPUT="udp_log_control.py",
            RP1_HELPER=str(self.rp1_helper), RP1_SELECTED="false",
            BINARY_HELPER=str(self.binary_helper), RESOLVED_BINARY=str(self.root / "resolved-binary"),
        )
        environment.update(values)
        source = INSTALLER.read_text(encoding="utf-8")
        entry_point = '\nif [[ -z "${BASH_SOURCE[0]:-}" || "${BASH_SOURCE[0]}" == "$0" ]]; then'
        self.assertEqual(source.count(entry_point), 1)
        return run_fixture_shell('getent() { printf "fixture:x:1:1::%s:/bin/sh\\n" "$USER_HOME"; }\n'
            + source.replace(entry_point, "\n" + FIXTURE + entry_point), cwd=self.root, env=environment,
                                 stage=self._testMethodName, stdin=True)

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
                    *self.package_events(), "clone", "neutral-runtime", "age-validation",
                    "picotool", "rp1-dependencies", "provider", "application",
                ])
                self.assertIn("Complete: Validate support-bundle encryption tools.", result.stdout)

    def test_default_resolves_branch_release_before_resource_and_package_selection(self) -> None:
        for result in ("release", "build"):
            with self.subTest(result=result):
                home=self.root / result
                home.mkdir()
                self.events.unlink(missing_ok=True)
                (self.bin / "git").unlink(missing_ok=True)
                run=self.run_installer(USER_HOME=str(home),REPO_BRANCH="devel",
                                       BINARY_SOURCE="",AUTO_BINARY_RESULT=result)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertEqual((self.root / "resolved-binary").read_text().strip(),result)
                events=self.recorded_events()
                self.assertLess(events.index("binary-resolution"),events.index("resources"))
                self.assertLess(events.index("binary-resolution"),events.index("apt-update"))
                lines=run.stdout.splitlines()
                self.assertLess(max(i for i,line in enumerate(lines) if "[INFO ]" in line),
                                min(i for i,line in enumerate(lines) if "Running:" in line))

    def test_default_lookup_failure_stops_without_compiling_or_installing_packages(self) -> None:
        result=self.run_installer(BINARY_SOURCE="",AUTO_BINARY_RESULT="failure")
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.recorded_events(),["binary-resolution"])
        self.assertFalse((self.bin / "git").exists())
        self.assertIn("fixture release lookup failed",result.stderr)

    def test_default_dry_run_does_not_resolve_or_download_release(self) -> None:
        result=self.run_installer(BINARY_SOURCE="",DRY_RUN="true")
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertNotIn("binary-resolution",self.recorded_events())
        self.assertFalse(self.checkout.exists())
        self.assertIn("release availability is unresolved",result.stdout)

    def test_all_info_precedes_command_progress(self) -> None:
        for mode in ("build", "local", "release"):
            for dry_run in ("false", "true"):
                for rp1 in ("false", "true"):
                    with self.subTest(mode=mode, dry_run=dry_run, rp1=rp1):
                        home = self.root / f"{mode}-{dry_run}-{rp1}"
                        home.mkdir()
                        self.events.unlink(missing_ok=True)
                        (self.bin / "git").unlink(missing_ok=True)
                        result = self.run_installer(
                            USER_HOME=str(home), REPO_BRANCH="devel", LOG_LEVEL="INFO",
                            BINARY_SOURCE=mode, DRY_RUN=dry_run, RP1_SELECTED=rp1,
                            BINARY_RELEASE_TAG="v4.0.0-beta.1" if mode == "release" else "",
                        )
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        lines = result.stdout.splitlines()
                        info = [i for i, line in enumerate(lines) if "[INFO ]" in line]
                        progress = [i for i, line in enumerate(lines)
                                    if "Running:" in line or "Complete:" in line]
                        self.assertTrue(info, result.stdout)
                        self.assertTrue(progress, result.stdout)
                        self.assertLess(max(info), min(progress), result.stdout)
                        self.assertIn("[INFO ] Managing required packages (this may take a few minutes).",
                                      result.stdout)
                        announcement = "[INFO ] Resolved RP1-GPIO-CLK-DKMS installation plan."
                        self.assertEqual(result.stdout.count(announcement), int(rp1 == "true"))
                        self.assertNotIn("Complete: Resolve RP1-GPIO-CLK-DKMS installation plan.",
                                         result.stdout)

    def test_clone_failure_stops_before_validation_and_provider_changes(self) -> None:
        result = self.run_installer(FAIL_AT="clone")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), [*self.package_events(), "clone"])
        self.assertFalse(self.checkout.exists())

    def test_package_failure_does_not_clone_or_load_helper(self) -> None:
        result = self.run_installer(FAIL_AT="packages")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), self.package_events())
        self.assertFalse(self.checkout.exists())

    def test_unsafe_age_stops_before_provider_and_application_changes(self) -> None:
        (self.age_root / "usr/bin/age").chmod(0o777)
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), [
            *self.package_events(), "clone", "neutral-runtime", "age-validation",
        ])
        self.assertIn("Failed: Validate support-bundle encryption tools.", result.stdout)

    def test_picotool_failure_stops_before_provider_and_application_changes(self) -> None:
        result = self.run_installer(FAIL_AT="picotool")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("picotool", self.recorded_events())
        self.assertNotIn("provider", self.recorded_events())
        self.assertNotIn("application", self.recorded_events())

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
            *self.package_events(), "clone", "neutral-runtime", "precompiled-packages", "age-validation",
            "picotool", "rp1-dependencies", "provider", "application",
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

    def assert_uninstall_staging_removed(self) -> None:
        marker = self.root / "staged-source"
        if marker.exists():
            self.assertFalse(Path(marker.read_text().strip()).exists())

    def test_streamed_uninstall_fetches_validates_and_preserves_existing_checkout(self) -> None:
        self.checkout.mkdir()
        sentinel = self.checkout / "operator-work"
        sentinel.write_text("preserve me")
        result = self.run_installer(ACTION="uninstall", REPO_BRANCH="devel",
                                    REMOVE_RP1_GPCLK_DKMS="false")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = self.recorded_events()
        self.assertEqual(events[:5], ["confirmation", "model-info", "system-info",
                                      "version-info", "uninstall-fetch"])
        self.assertTrue(all(event.startswith("remove:") for event in events[5:]))
        self.assertGreater(len(events[5:]), 10)
        self.assertIn("Complete: Validate uninstall helper source.", result.stdout)
        self.assertLess(result.stdout.index("Complete: Fetch uninstall helper source."),
                        result.stdout.index("Complete: Validate uninstall helper source."))
        self.assertEqual(sentinel.read_text(), "preserve me")
        self.assertEqual(list(self.checkout.iterdir()), [sentinel])
        self.assert_uninstall_staging_removed()

    def test_streamed_uninstall_failures_stop_before_any_teardown(self) -> None:
        cases = [("uninstall-fetch", ""), ("python-syntax", ""),
                 ("shell-syntax", ""), ("policy", ""),
                 ("shell-entry", "")]
        for name in ("udp_log_control.py", "rp1_qualification.py", "install_runtime_reconcile.py",
                     "runtime_reconcile.py", "route_application.py", "rp1_gpclk_dkms_install.py",
                     "pico_usb_deploy.py", "pico_usb_bundle.py",
                     "support_bundle_runtime_install.sh", "pico_usb_runtime_install.sh",
                     "../config/pico-usb-installer.json"):
            cases.extend([("missing", name), ("symlink", name)])
        for failure, name in cases:
            with self.subTest(failure=failure, name=name):
                self.events.unlink(missing_ok=True)
                result = self.run_installer(ACTION="uninstall", FAIL_AT=failure, BROKEN_INPUT=name)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.recorded_events(), ["confirmation", "model-info", "system-info",
                                                          "version-info", "uninstall-fetch"])
                if failure in ("missing", "symlink"):
                    self.assertIn("Missing or unsafe uninstall helper input:", result.stderr)
                self.assert_uninstall_staging_removed()
                self.assertFalse(self.checkout.exists())

    def test_local_uninstall_validates_without_cloning_or_writing_checkout(self) -> None:
        checkout = self.root / "local-checkout"
        shutil.copytree(ROOT / "scripts", checkout / "scripts", ignore=shutil.ignore_patterns("__pycache__"))
        (checkout / "config").mkdir()
        shutil.copyfile(ROOT / "config/pico-usb-installer.json", checkout / "config/pico-usb-installer.json")
        shutil.copyfile(ROOT / "config/rp1-provider-requirements.json", checkout / "config/rp1-provider-requirements.json")
        result = run_fixture_shell(r'''
source "$INSTALLER"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
ACTION=uninstall
IS_REPO=true
LOCAL_REPO_DIR="$CHECKOUT"
git() { exit 97; }
trap cleanup_uninstall_source EXIT
prepare_uninstall_source
[[ "$UNINSTALL_SOURCE_VALIDATED" == true ]]
[[ "$LOCAL_REPO_DIR" == "$CHECKOUT" ]]
[[ -d "$UNINSTALL_SOURCE_DIR/bytecode" ]]
''', cwd=self.root, env={**os.environ, "INSTALLER": str(INSTALLER),
                        "CHECKOUT": str(checkout), "DRY_RUN": "false", "TERM": "dumb"},
            stage=self._testMethodName)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(list(checkout.rglob("*.pyc")))
        self.assertFalse(list(checkout.rglob("__pycache__")))

    def test_actual_runtime_wrappers_skip_missing_source_during_streamed_dry_run(self) -> None:
        result = run_fixture_shell(r'''
source "$INSTALLER"
ACTION=uninstall
IS_REPO=false
LOCAL_REPO_DIR="$CHECKOUT"
logD() { :; }
git() { exit 97; }
python3() { exit 98; }
prepare_uninstall_source
manage_support_bundle_runtime
manage_pico_usb_runtime
[[ -z "$UNINSTALL_SOURCE_DIR" ]]
''', cwd=self.root, env={**os.environ, "INSTALLER": str(INSTALLER),
                        "CHECKOUT": str(self.checkout), "DRY_RUN": "true", "TERM": "dumb"},
            stage=self._testMethodName)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.checkout.exists())

    def test_streamed_uninstall_dry_run_does_not_acquire_source(self) -> None:
        result = self.run_installer(ACTION="uninstall", DRY_RUN="true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("uninstall-fetch", self.recorded_events())
        self.assertFalse(self.checkout.exists())
        self.assertFalse((self.root / "staged-source").exists())

    @staticmethod
    def package_events() -> list[str]:
        return ["confirmation", "resources", "model-info", "system-info", "version-info",
                "parallelism-info", "apache-mode", "build-resources", "apt-update", "apt-repair", "packages"]

    def test_fresh_pi5_installs_git_before_rp1_planning(self) -> None:
        self.assertIsNone(shutil.which("git", path=str(self.bin)))
        result = self.run_installer(REPO_BRANCH="devel", RP1_SELECTED="true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.recorded_events(), [
            *self.package_events(), "clone", "rp1-plan", "neutral-runtime", "age-validation",
            "picotool", "rp1-dependencies", "provider", "application",
        ])

    def test_package_failures_stop_before_rp1_planning(self) -> None:
        for failure in ("apt-update", "apt-repair", "packages"):
            with self.subTest(failure=failure):
                self.events.unlink(missing_ok=True)
                result = self.run_installer(FAIL_AT=failure, RP1_SELECTED="true")
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("rp1-plan", self.recorded_events())
                self.assertNotIn("provider", self.recorded_events())
                self.assertNotIn("application", self.recorded_events())

    def test_rejected_confirmation_or_resources_does_not_install_packages(self) -> None:
        for failure in ("confirmation", "resources", "build-resources"):
            with self.subTest(failure=failure):
                self.events.unlink(missing_ok=True)
                result = self.run_installer(FAIL_AT=failure, RP1_SELECTED="true")
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("apt-update", self.recorded_events())
                self.assertNotIn("rp1-plan", self.recorded_events())
                self.assertFalse((self.bin / "git").exists())

    def test_planning_failure_reports_helper_diagnostic_and_stops_installation(self) -> None:
        result = self.run_installer(FAIL_AT="plan", RP1_SELECTED="true")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), [*self.package_events(), "clone", "rp1-plan"])
        self.assertIn("RP1-GPIO-CLK-DKMS operation refused: fixture source rejected", result.stderr)
        self.assertIn("Failed: Resolve RP1-GPIO-CLK-DKMS installation plan.", result.stderr)
        self.assertNotIn("Complete: Resolve RP1-GPIO-CLK-DKMS installation plan.", result.stdout)
        self.assertNotIn("before package mutation", result.stderr)

    def test_pi5_dry_run_does_not_install_git_or_execute_the_planner(self) -> None:
        result = self.run_installer(DRY_RUN="true", RP1_SELECTED="true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.recorded_events(), [
            "confirmation", "resources", "model-info", "system-info", "version-info",
            "parallelism-info", "apache-mode", "build-resources", "neutral-runtime",
            "rp1-dependencies", "provider", "application",
        ])
        self.assertFalse((self.bin / "git").exists())
        self.assertFalse(self.checkout.exists())

    def test_precompiled_package_failure_stops_before_application_changes(self) -> None:
        result = self.run_installer(BINARY_SOURCE="local", FAIL_AT="precompiled-packages")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.recorded_events(), [
            *self.package_events(), "clone", "neutral-runtime", "precompiled-packages",
        ])

    def test_planning_failure_is_retained_in_the_real_installer_log(self) -> None:
        log_file = self.root / "installer.log"
        result = subprocess.run(
            ["bash", "-c", r'''
source "$INSTALLER"
LOCAL_REPO_DIR="$(dirname "$(dirname "$INSTALLER")")"
init_log() { :; }
setup_log
rp1_gpclk_dkms_installation_selected() { return 0; }
python3() { printf 'RP1-GPIO-CLK-DKMS operation refused: fixture source rejected\n' >&2; return 17; }
if prepare_rp1_gpclk_dkms_installation; then exit 1; fi
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" ]]
'''],
            env={**os.environ, "INSTALLER": str(INSTALLER), "LOG_FILE": str(log_file),
                 "LOG_OUTPUT": "both", "LOG_LEVEL": "INFO", "TERM": "dumb", "COLUMNS": "200",
                 "ACTION": "install", "DRY_RUN": "false"},
            text=True, capture_output=True, timeout=30, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for output in (result.stdout, log_file.read_text(encoding="utf-8")):
            self.assertIn("fixture source rejected", output)


if __name__ == "__main__":
    unittest.main()
