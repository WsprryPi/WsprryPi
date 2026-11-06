# SPDX-License-Identifier: MIT
#!/usr/bin/env python3
"""Offline, unprivileged contract tests for RP1 provider installation."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import io
import json
import lzma
import os
import pathlib
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
from fixture_commands import Command, StrictCommands
from fixture_filesystem import temporary_root


ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ["LOCAL_REPO_DIR"]=str(ROOT)
PROVIDER_ROOT = pathlib.Path(os.environ.get('RP1_DKMS_SOURCE', ROOT.parent/'RP1-GPIO-CLK-DKMS')).resolve()
sys.path[:0] = [str(ROOT/"scripts"), str(PROVIDER_ROOT/"scripts")]
import rp1_gpclk_dkms_install as CLIENT
SCRIPT = PROVIDER_ROOT / 'scripts/provider_lifecycle.py'
SPEC = importlib.util.spec_from_file_location("rp1_installer", SCRIPT)
assert SPEC and SPEC.loader
MOD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inventory_for_uapi(content: bytes = b"uapi\n", version: str = "2.1.0") -> list[dict[str, object]]:
    return [{
        "path": f"usr/src/rp1-gpclk-dkms-{version}/include/uapi/linux/rp1_gpclk.h",
        "type": "file", "mode": "0644", "sha256": digest(content),
    }]


def valid_manifest(version: str = "2.1.0", commit: str = "a" * 40) -> dict[str, object]:
    inventory = inventory_for_uapi(version=version)
    return {
        "schema": MOD.MANIFEST_SCHEMA,
        "schemaVersion": 1,
        "repository": MOD.REPOSITORY,
        "releaseTag": f"v{version}",
        "sourceCommit": commit,
        "productVersion": version,
        "debianVersion": f"{version}-1",
        "package": {
            "name": MOD.PACKAGE_NAME,
            "filename": f"rp1-gpclk-dkms_{version}-1_all.deb",
            "sha256": "b" * 64,
        },
        "dkmsModule": MOD.DKMS_NAME,
        "kernelModule": MOD.MODULE_NAME,
        "uapi": {
            "sha256": inventory[0]["sha256"],
            "path": inventory[0]["path"],
        },
        "administrationProtocol": MOD.ADMIN_PROTOCOL,
        "installationBehavior": {
            "routeNeutral": True,
            "outputDisabled": True,
            "loadsModule": False,
            "appliesOverlay": False,
            "editsBootConfiguration": False,
            "operatesServices": False,
        },
        "packageInventory": inventory,
        "packageInventorySha256": digest(MOD.canonical(inventory)),
        "releaseChannel": "release",
    }


def release(tag: str, *, draft: bool = False, prerelease: bool = False, complete: bool = True) -> dict[str, object]:
    names = []
    if complete:
        names = [MOD.MANIFEST_NAME, MOD.CHECKSUMS_NAME, f"rp1-gpclk-dkms_{tag.removeprefix('v')}-1_all.deb"]
    return {
        "tag_name": tag, "draft": draft, "prerelease": prerelease, "immutable": True,
        "published_at": "2026-08-31T00:00:00Z",
        "assets": [{"name": name, "state": "uploaded", "browser_download_url": f"https://github.com/{MOD.REPOSITORY}/releases/download/{tag}/{name}"} for name in names],
    }


class RunnerFixtures:
    def setUp(self):
        super().setUp()
        # Historical fixtures explicitly select their recorded authority.
        self.addCleanup(MOD.configure_provider_repository, MOD.REPOSITORY)
        MOD.configure_provider_repository(MOD.REPOSITORY)

    def runner(self, responses=None, *, steps=()):
        runner = StrictCommands({key: value for key, value in (responses or {}).items()
                                 if (key.argv if isinstance(key, Command) else key) not in {step.argv for step in steps}},
            steps=steps, responses={(key.argv if isinstance(key, Command) else key): value
                                   for key, value in (responses or {}).items()
                                   if (key.argv if isinstance(key, Command) else key) in {step.argv for step in steps}},
            result=lambda: MOD.CommandResult("", "", 0), error=MOD.ContractError)
        self.addCleanup(runner.assert_complete)
        return runner


class ApplyPolicyTests(RunnerFixtures, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        (self.root / "proc").mkdir()
        (self.root / "proc/modules").write_text("")
        self.state = self.root / "state"
        self.state.mkdir(mode=0o700)
        self.package = self.state / "rp1-gpclk-dkms_2.1.0-1_all.deb"
        self.package.write_bytes(b"fixture")
        self.manifest = valid_manifest()
        self.resolved = {"tag": "v2.1.0", "commit": "a" * 40, "manifest": self.manifest, "packagePath": str(self.package)}
        self.record = self.root / "record.json"

    def test_selected_shell_dry_run_never_invokes_python_or_resolution_tools(self):
        install_script = ROOT / "scripts" / "install.sh"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python3 >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
curl() { printf curl >"$SENTINEL"; return 99; }
mktemp() { printf mktemp >"$SENTINEL"; return 99; }
git() { printf git >"$SENTINEL"; return 99; }
apt-get() { printf apt-get >"$SENTINEL"; return 99; }
dpkg-deb() { printf dpkg-deb >"$SENTINEL"; return 99; }
DRY_RUN=true
ACTION=install
INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_SOURCE="$SOURCE_SELECTOR"
REPO_BRANCH="$WSPRRYPi_BRANCH"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
prepare_rp1_gpclk_dkms_installation debug
apply_rp1_gpclk_dkms_installation debug
prepare_rp1_gpclk_runtime_update debug
activate_rp1_gpclk_runtime_administration debug
[[ ! -e "$SENTINEL" ]]
cleanup_rp1_gpclk_dkms_state
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" && -z "$RP1_GPCLK_DKMS_HELPER" ]]
'''
        for selector, branch, repository in (
            ("release", "main", MOD.REPOSITORY),
            ("devel", "codex/issue-412-test", MOD.REPOSITORY),
            ("commit:" + "a"*40, "devel", MOD.REPOSITORY),
        ):
            sentinel = self.root / f"{selector}.invoked"
            environment = os.environ.copy()
            environment.update({
                "INSTALL_SCRIPT": str(install_script),
                "SENTINEL": str(sentinel),
                "SOURCE_SELECTOR": selector,
                "RP1_GPCLK_DKMS_REPOSITORY": repository,
                "WSPRRYPi_BRANCH": branch,
            })
            result = subprocess.run(
                ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=environment, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(sentinel.exists())
            self.assertEqual(result.stdout.count("Complete: (dry)"), 6)
            self.assertNotIn("[INFO ]", result.stdout)
            self.assertNotIn("Resolve RP1-GPIO-CLK-DKMS installation plan", result.stdout)
            self.assertIn(
                "Complete: (dry) Apply RP1-GPIO-CLK-DKMS installation plan",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Prepare neutral RP1 runtime for application update",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Activate neutral RP1-GPIO-CLK-DKMS administration",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Reload systemd after RP1 runtime activation",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Start WsprryPi after RP1 runtime activation",
                result.stdout,
            )
            self.assertIn("Name:    Resolve RP1-GPIO-CLK-DKMS installation plan", result.stderr)
            self.assertIn("Name:    Apply RP1-GPIO-CLK-DKMS installation plan", result.stderr)
            helper = install_script.parent / "rp1_gpclk_dkms_install.py"
            prepare_command = (
                f"Command: python3 {helper} --provider-repository {repository} prepare --state-dir dry-run:no-state-created "
                f"--install true --source {selector} --wsprry-source {branch} --dry-run --debug"
            )
            apply_command = (
                f"Command: python3 {helper} --provider-repository {repository} apply --state-dir dry-run:no-state-created --debug"
            )
            update_command = (
                f"Command: python3 {helper} --provider-repository {repository} prepare-runtime-update "
                "--state-dir dry-run:no-state-created --debug"
            )
            activation_command = (
                f"Command: python3 {helper} --provider-repository {repository} activate-runtime "
                "--state-dir dry-run:no-state-created --debug"
            )
            self.assertIn(prepare_command, result.stderr)
            self.assertIn(apply_command, result.stderr)
            self.assertIn(update_command, result.stderr)
            self.assertIn(activation_command, result.stderr)

    def test_non_rp1_auto_and_explicit_false_render_no_provider_steps(self):
        install_script = ROOT / "scripts" / "install.sh"
        shell = r'''
source "$INSTALL_SCRIPT"
is_rp1_system() { return "$RP1_STATUS"; }
python3() { printf python3 >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
DRY_RUN=true
ACTION=install
INSTALL_RP1_GPCLK_DKMS="$SELECTION"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
prepare_rp1_gpclk_dkms_installation debug
apply_rp1_gpclk_dkms_installation debug
prepare_rp1_gpclk_runtime_update debug
activate_rp1_gpclk_runtime_administration debug
[[ ! -e "$SENTINEL" ]]
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" && -z "$RP1_GPCLK_DKMS_HELPER" ]]
'''
        for selection, rp1_status in (("auto", "1"), ("false", "0")):
            sentinel = self.root / f"{selection}.invoked"
            result = subprocess.run(
                ["bash", "-c", shell],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env={
                    **os.environ,
                    "INSTALL_SCRIPT": str(install_script),
                    "SENTINEL": str(sentinel),
                    "SELECTION": selection,
                    "RP1_STATUS": rp1_status,
                },
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertNotIn("RP1-GPIO-CLK-DKMS", result.stderr)
            self.assertFalse(sentinel.exists())

    def test_auto_pi5_shell_path_still_reaches_provider_planner(self):
        install_script = ROOT / "scripts" / "install.sh"
        result = subprocess.run(
            [
                "bash",
                "-c",
                r'''
source "$INSTALL_SCRIPT"
is_rp1_system() { return 0; }
python3() { printf invoked >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
ACTION=install
DRY_RUN=true
INSTALL_RP1_GPCLK_DKMS=auto
prepare_rp1_gpclk_dkms_installation
cleanup_rp1_gpclk_dkms_state
[[ ! -e "$SENTINEL" ]]
''',
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={
                **os.environ,
                "INSTALL_SCRIPT": str(install_script),
                "SENTINEL": str(self.root / "auto-pi5-planner"),
            },
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Resolve RP1-GPIO-CLK-DKMS installation plan", result.stdout)
        self.assertFalse((self.root / "auto-pi5-planner").exists())

    def test_shell_rp1_identity_requires_pi5_model_and_bcm2712(self):
        install_script = ROOT / "scripts" / "install.sh"
        compatible = self.root / "compatible"
        scenarios = (
            (b"raspberrypi,5-model-b\0brcm,bcm2712\0", True),
            (b"raspberrypi,5-compute-module\0brcm,bcm2712\0", True),
            (b"raspberrypi,5-model-b\0", False),
            (b"raspberrypi,4-model-b\0brcm,bcm2711\0", False),
            (b"brcm,bcm2712\0", False),
        )
        for payload, expected in scenarios:
            compatible.write_bytes(payload)
            result = subprocess.run(
                [
                    "bash",
                    "-c",
                    'source "$INSTALL_SCRIPT"; is_rp1_system "$COMPATIBLE"',
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env={
                    **os.environ,
                    "INSTALL_SCRIPT": str(install_script),
                    "COMPATIBLE": str(compatible),
                },
                check=False,
            )
            self.assertEqual(result.returncode == 0, expected, payload)

    def test_exec_command_preserves_internal_debug_argv(self):
        install_script = ROOT / "scripts" / "install.sh"
        capture = self.root / "argv.txt"
        shell = r'''
source "$INSTALL_SCRIPT"
probe() { printf '%s\n' "$@" >"$CAPTURE"; }
logD() { :; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
exec_command "argv fidelity" probe before debug after debug
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "CAPTURE": str(capture)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(capture.read_text().splitlines(), ["before", "debug", "after"])

    def test_empty_log_message_fails_without_unbound_argument_abort(self):
        install_script = ROOT / "scripts" / "install.sh"
        result = subprocess.run(
            ["bash", "-c", 'source "$INSTALL_SCRIPT"; logI ""'],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "INSTALL_SCRIPT": str(install_script)},
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("Message is required.", result.stderr)
        self.assertNotIn("unbound variable", result.stderr)

    def test_exec_command_retains_bounded_failure_details(self):
        install_script = ROOT / "scripts/install.sh"
        failure_output = self.root / "apply-output.log"
        shell = r'''
source "$INSTALL_SCRIPT"
failing_helper() {
    local number
    printf '%20000s\n' x
    for number in {1..25}; do
        printf '\033[31mdetail-%02d\033[0m\n' "$number"
    done
    return 7
}
warn() { printf '[WARN] %s\n' "$1"; }
logW() { printf '[LOG] %s\n' "$1"; }
logD() { :; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
EXEC_COMMAND_FAILURE_OUTPUT_FILE="$FAILURE_OUTPUT" \
    exec_command "fixture apply" failing_helper || status=$?
[[ "$status" -eq 7 ]]
'''
        environment = os.environ.copy()
        environment.update({
            "INSTALL_SCRIPT": str(install_script),
            "FAILURE_OUTPUT": str(failure_output),
        })
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "Failure details from fixture apply (last 20 lines, 16 KiB maximum)",
            result.stdout,
        )
        self.assertNotIn("detail-05", result.stdout)
        self.assertIn("detail-06", result.stdout)
        self.assertIn("detail-25", result.stdout)
        self.assertNotIn("\x1b", result.stdout)
        self.assertLessEqual(failure_output.stat().st_size, 16384)
        self.assertTrue(failure_output.read_text().endswith("detail-25\x1b[0m\n"))
        self.assertEqual(stat.S_IMODE(failure_output.stat().st_mode), 0o600)

    def test_shell_propagates_debug_to_all_helper_invocations(self):
        install_script = ROOT / "scripts" / "install.sh"
        capture = self.root / "helper-argv.txt"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() {
    {
        printf 'CALL\n'
        printf 'ARG=%s\n' "$@"
    } >>"$CAPTURE"
}
systemctl() { :; }
logI() { printf '[INFO ] %s\n' "$1"; }
logD() { :; }
DRY_RUN=false
ACTION=install
INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_SOURCE=release
REPO_BRANCH=main
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
prepare_rp1_gpclk_dkms_installation debug
apply_rp1_gpclk_dkms_installation debug
prepare_rp1_gpclk_runtime_update debug
activate_rp1_gpclk_runtime_administration debug
cleanup_rp1_gpclk_dkms_state
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "CAPTURE": str(capture)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("[INFO ]", result.stdout)
        self.assertNotIn("Resolve RP1-GPIO-CLK-DKMS installation plan", result.stdout)
        arguments = capture.read_text().splitlines()
        self.assertEqual(arguments.count("CALL"), 5)
        self.assertEqual(arguments.count("ARG=--debug"), 4)
        self.assertIn("ARG=/usr/local/lib/wsprrypi/runtime_reconcile.py", arguments)

    def test_repository_selection_reaches_every_lifecycle_operation_and_retry(self):
        shell = r'''
set -e
source "$INSTALL_SCRIPT"
python3() {
    { printf '%s\t' "$@"; printf '\n'; } >>"$CAPTURE"
    if [[ "$1" == -I ]]; then command python3 "$@"; return; fi
    local index
    for ((index=1; index<$#; index++)); do
        if [[ "${!index}" == --result-file ]]; then
            index=$((index+1))
            (umask 077; printf '%s\n' '{"schemaVersion":1,"contract":"wsprrypi-rp1-provider-removal-v1","outcome":"absent"}' >"${!index}")
        fi
    done
    local arg
    for arg in "$@"; do
        if [[ "$arg" == remove ]]; then
            printf 'RP1-GPIO-CLK-DKMS absent: no provider removal was required.\n'
        fi
    done
    return 0
}
systemctl() { :; }
logI() { :; }
logD() { :; }
ACTION=install DRY_RUN=false INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_SOURCE=commit:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
REPO_BRANCH=devel
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
for pass in first repeat; do
    prepare_rp1_gpclk_dkms_installation debug
    prepare_rp1_gpclk_runtime_update debug
    apply_rp1_gpclk_dkms_installation debug
    activate_rp1_gpclk_runtime_administration debug
    cleanup_rp1_gpclk_dkms_state
done
ACTION=uninstall
prepare_owned_rp1_gpclk_runtime_removal debug
remove_owned_rp1_gpclk_dkms_provider debug
'''
        for repository in (None, MOD.REPOSITORY):
            with self.subTest(repository=repository):
                capture = self.root / 'repository-argv.txt'
                capture.unlink(missing_ok=True)
                environment = {**os.environ, 'INSTALL_SCRIPT': str(ROOT/'scripts/install.sh'),
                               'CAPTURE': str(capture)}
                if repository is None:
                    environment.pop('RP1_GPCLK_DKMS_REPOSITORY', None)
                else:
                    environment['RP1_GPCLK_DKMS_REPOSITORY'] = repository
                result = subprocess.run(['bash', '-c', shell], text=True, capture_output=True,
                                        env=environment)
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = [line.split() for line in capture.read_text().splitlines()]
                helpers = [args for args in calls if args[0].endswith('rp1_gpclk_dkms_install.py')]
                self.assertEqual(len(helpers), 10)
                for args in helpers:
                    self.assertEqual(args[1:3], ['--provider-repository', MOD.REPOSITORY])
                    self.assertIn('--debug', args)
                    # Parse actual shell arguments with the real helper CLI.
                    parsed = CLIENT.parser().parse_args(args[1:])
                    self.assertEqual(parsed.provider_repository, MOD.REPOSITORY)
                self.assertEqual([args[3] for args in helpers],
                    ['prepare', 'prepare-runtime-update', 'apply', 'activate-runtime']*2 +
                    ['prepare-runtime-removal', 'remove'])
                self.assertEqual(len(calls)-len(helpers), 3, 'startup restoration and structured removal-result validation')
                self.assertEqual(sum(args[:2]==['-I','-'] for args in calls),1)

    def test_invalid_repository_stops_before_helper_staging_or_dependency_resolution(self):
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python >>"$CAPTURE"; return 99; }
curl() { printf curl >>"$CAPTURE"; return 99; }
mktemp() { printf staging >>"$CAPTURE"; return 99; }
uname() { printf kernel >>"$CAPTURE"; return 99; }
warn() { printf '%s\n' "$*"; }
ACTION=install DRY_RUN=false INSTALL_RP1_GPCLK_DKMS=true
[[ "$OPERATION" != *removal && "$OPERATION" != remove_owned* ]] || ACTION=uninstall
"$OPERATION" && exit 98
[[ ! -e "$CAPTURE" ]]
'''
        operations = ('resolve_apt_package_list', 'prepare_rp1_gpclk_dkms_installation',
            'prepare_rp1_gpclk_runtime_update', 'apply_rp1_gpclk_dkms_installation',
            'activate_rp1_gpclk_runtime_administration', 'prepare_owned_rp1_gpclk_runtime_removal',
            'remove_owned_rp1_gpclk_dkms_provider')
        for repository in ('', 'WsprryPi/RP1-DKMS', 'attacker/provider', 'WsprryPi/RP1-GPIO-CLK-DKMS;false'):
            for operation in operations:
                with self.subTest(repository=repository, operation=operation):
                    capture = self.root / 'invalid-repository-effects'
                    result = subprocess.run(['bash', '-c', shell], text=True, capture_output=True,
                        env={**os.environ, 'INSTALL_SCRIPT': str(ROOT/'scripts/install.sh'),
                             'CAPTURE': str(capture), 'OPERATION': operation,
                             'RP1_GPCLK_DKMS_REPOSITORY': repository})
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('RP1_GPCLK_DKMS_REPOSITORY must be', result.stdout)
                    self.assertFalse(capture.exists())

    def test_selected_uninstall_dry_run_reports_check_without_invoking_python(self):
        install_script = ROOT / "scripts" / "install.sh"
        sentinel = self.root / "uninstall-python-invoked"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python3 >"$SENTINEL"; return 99; }
curl() { printf curl >"$SENTINEL"; return 99; }
mktemp() { printf mktemp >"$SENTINEL"; return 99; }
DRY_RUN=true
ACTION=uninstall
REPO_ORG=WsprryPi
REPO_NAME=WsprryPi
REPO_BRANCH=devel
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
remove_owned_rp1_gpclk_dkms_provider debug
[[ ! -e "$SENTINEL" ]]
cleanup_rp1_gpclk_dkms_state
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "SENTINEL": str(sentinel)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(sentinel.exists())
        self.assertEqual(result.stdout.count("Complete: (dry)"), 1)
        self.assertIn("Complete: (dry) Check owned RP1 provider.", result.stdout)
        self.assertLessEqual(max(map(len, result.stdout.splitlines())), 80)
        helper = install_script.parent / "rp1_gpclk_dkms_install.py"
        self.assertIn(
            f"Command: python3 {helper} --provider-repository WsprryPi/RP1-GPIO-CLK-DKMS remove --remove auto --debug",
            result.stderr,
        )

    def test_uninstall_opt_out_never_prepares_or_invokes_helper(self):
        install_script = ROOT / "scripts" / "install.sh"
        sentinel = self.root / "opt-out-helper-activity"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python3 >"$SENTINEL"; return 99; }
curl() { printf curl >"$SENTINEL"; return 99; }
mktemp() { printf mktemp >"$SENTINEL"; return 99; }
logI() { :; }
DRY_RUN=false
ACTION=uninstall
REMOVE_RP1_GPCLK_DKMS=false
remove_owned_rp1_gpclk_dkms_provider debug
[[ ! -e "$SENTINEL" ]]
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" && -z "$RP1_GPCLK_DKMS_HELPER" ]]
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "SENTINEL": str(sentinel)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(sentinel.exists())

    def test_forbidden_runtime_operations_absent_from_installer_paths(self):
        install_source = (ROOT / "scripts/install.sh").read_text()
        helper_source = SCRIPT.read_text()
        self.assertIn('[[ "$ACTION" == "install" ]] || return 0', install_source)
        self.assertIn('*route_args, "--output-inhibit", "1", "--install"', helper_source)
        self.assertNotIn('"--live-output"', helper_source)
        self.assertNotRegex(helper_source, r'\["(?:dtoverlay|modprobe|insmod|rmmod|systemctl|service|reboot|shutdown)"')
        self.assertNotRegex(helper_source, r"config\.txt[^\n]*(?:write|replace|unlink)")

    def test_packages_precede_planning_and_runtime_recovery_precedes_apply(self):
        source = (ROOT / "scripts/install.sh").read_text()
        prepare = source.index('prepare_rp1_gpclk_dkms_installation "$debug" || return 1')
        packages = source.index('handle_apt_packages "$debug" || return 1')
        dependencies = source.index('validate_rp1_gpclk_build_dependencies "$debug" || return 1')
        apply = source.index('apply_rp1_gpclk_dkms_installation "$debug" || return 1')
        update = source.index('prepare_rp1_gpclk_runtime_update "$debug" || return 1')
        package_notice = source.index(
            'logI "Managing required packages (this may take a few minutes)."'
        )
        application = source.index('manage_wsprry_pi "$debug"')
        self.assertLess(source.index('print_version "$debug"'), package_notice)
        self.assertLess(source.index('display_compilation_resource_notes "$debug"'), package_notice)
        self.assertLess(source.index('detect_apache_web_mode "$debug"'), package_notice)
        self.assertLess(package_notice, packages)
        self.assertLess(packages, prepare)
        self.assertLess(prepare, update)
        self.assertLess(update, dependencies)
        self.assertLess(dependencies, apply)
        self.assertLess(update, application)
        group = source[source.index("local install_group=("):]
        companion = group.index('"manage_route_application"')
        service = group.index('"manage_service')
        activation = group.index('"activate_rp1_gpclk_runtime_administration"')
        website = group.index('"manage_web"')
        readiness = group.index('"validate_wsprrypi_runtime"')
        self.assertLess(companion, activation)
        self.assertLess(service, activation)
        self.assertLess(activation, website)
        self.assertLess(website, readiness)

        package_handler_start = source.index("handle_apt_packages() {")
        package_handler_end = source.index("\n}\n", package_handler_start)
        self.assertNotIn(
            "Managing required packages",
            source[package_handler_start:package_handler_end],
        )

    def test_runtime_activation_refreshes_systemd_before_service_validation(self):
        source = (ROOT / "scripts/install.sh").read_text()
        function = source[
            source.index("activate_rp1_gpclk_runtime_administration()"):
            source.index("prepare_owned_rp1_gpclk_runtime_removal()")
        ]
        activation = function.index(
            'exec_command "Activate neutral RP1-GPIO-CLK-DKMS administration"'
        )
        reload = function.index(
            'exec_command "Reload systemd after RP1 runtime activation"'
        )
        start = function.index(
            'exec_command "Start WsprryPi after RP1 runtime activation"'
        )
        self.assertLess(activation, reload)
        self.assertLess(reload, start)
        restoration = function.index('exec_command "Restore configured RP1 route with transmission disabled"')
        self.assertLess(start, restoration)
        self.assertIn('route-restoration-output.log', function)
        self.assertIn('setup cannot confirm transmitter readiness', function)

    def test_route_restoration_failure_stops_installer_activation(self):
        shell = r'''
source "$INSTALL_SCRIPT"
ACTION=install DRY_RUN=false INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_STATE_DIR=/fixture RP1_GPCLK_DKMS_HELPER=/fixture/helper.py
logD() { :; }
warn() { printf '%s\n' "$1"; }
exec_command() {
    printf '%s\n' "$1"
    if [[ "$1" == "Restore configured RP1 route with transmission disabled" ]]; then
        [[ "$EXEC_COMMAND_FAILURE_OUTPUT_FILE" == /fixture/route-restoration-output.log ]] || return 99
        [[ "$2" == python3 && "$3" == /usr/local/lib/wsprrypi/runtime_reconcile.py && "$4" == install ]] || return 98
        return "$ROUTE_RESULT"
    fi
}
activate_rp1_gpclk_runtime_administration || exit 1
printf 'SETUP_CONTINUES\n'
'''
        for status in (0, 12):
            result = subprocess.run(['bash', '-c', shell], text=True, capture_output=True,
                env={**os.environ, 'INSTALL_SCRIPT': str(ROOT/'scripts/install.sh'),
                     'ROUTE_RESULT': str(status)})
            self.assertEqual(result.returncode, 0 if status == 0 else 1, result.stderr)
            self.assertEqual('SETUP_CONTINUES' in result.stdout, status == 0)
            self.assertEqual('cannot confirm transmitter readiness' in result.stdout, status != 0)
            self.assertLess(result.stdout.index('Activate neutral'), result.stdout.index('Restore configured'))

    def test_readiness_keeps_existing_command_results_without_late_info(self):
        source = (ROOT / "scripts/install.sh").read_text()
        manager_start = source.index("manage_wsprry_pi() {")
        manager = source[manager_start:source.index("\n}\n", manager_start)]
        self.assertNotIn('logI ', manager)
        execution_loop = manager.index('for func in "${group_to_execute[@]}"; do')
        loop_end = manager.index("\n    done", execution_loop)
        self.assertLess(loop_end, manager.index('finish_script "$overall_status" "$debug"'))

    def test_helper_invocations_use_exec_command_and_propagate_debug(self):
        source = (ROOT / "scripts/install.sh").read_text()
        self.assertNotRegex(source, r'if ! python3 "\$RP1_GPCLK_DKMS_HELPER"')
        self.assertEqual(source.count('exec_command "Resolve RP1-GPIO-CLK-DKMS installation plan"'), 1)
        self.assertEqual(source.count('exec_command "Apply RP1-GPIO-CLK-DKMS installation plan"'), 1)
        self.assertEqual(source.count('exec_command "Prepare neutral RP1 runtime for application update"'), 1)
        self.assertEqual(source.count('exec_command "Activate neutral RP1-GPIO-CLK-DKMS administration"'), 1)
        self.assertGreaterEqual(source.count('args+=(--debug)'), 4)
        self.assertIn('runtime-update-output.log', source)
        self.assertIn('runtime-removal-output.log', source)
        self.assertIn('activation-output.log', source)

    def test_explicit_true_reaches_planner_without_raspberry_pi_identity(self):
        source = (ROOT / "scripts/install.sh").read_text()

        def function_body(name):
            start = source.index(f"{name}() {{")
            return source[start:source.index("\n}\n", start) + 3]

        for name in ("validate_sys_accs", "print_model_name", "check_arch"):
            body = function_body(name)
            self.assertIn('INSTALL_RP1_GPCLK_DKMS" == "true"', body)
            self.assertIn("explicitly requested", body)
        self.assertIn('"$file" == "/proc/device-tree/compatible"', function_body("validate_sys_accs"))

    def test_temporary_cleanup_is_scoped_and_uninstall_uses_owned_provider_step(self):
        source = (ROOT / "scripts/install.sh").read_text()
        cleanup_start = source.index("cleanup_rp1_gpclk_dkms_state() {")
        cleanup = source[cleanup_start:source.index("\n}\n", cleanup_start) + 3]
        self.assertIn("/tmp/wsprrypi-rp1-gpclk-dkms.*", cleanup)
        self.assertIn("Refusing to remove unexpected", cleanup)
        self.assertIn('"prepare_owned_rp1_gpclk_runtime_removal"\n            "remove_owned_rp1_gpclk_dkms_provider"', source)

    def run_uninstall_orchestration(self, failing_step="", provider_unproven=False):
        install_script = ROOT / "scripts" / "install.sh"
        capture = self.root / f"uninstall-{failing_step or 'success'}.txt"
        shell = r'''
source "$INSTALL_SCRIPT"
ACTION=uninstall
DRY_RUN=false
NO_WEB=false

record_step() {
    printf '%s\n' "$1" >>"$CAPTURE"
    [[ "$1" != "$FAILING_STEP" ]]
}
manage_apache() { record_step manage_apache; }
manage_web() { record_step manage_web; }
manage_service() { record_step manage_service; }
manage_udp_logging() { record_step manage_udp_logging; }
manage_i2c() { record_step manage_i2c; }
manage_config() { record_step manage_config; }
manage_route_application() { record_step manage_route_application; }
remove_application_ownership() { record_step remove_application_ownership; }
manage_support_bundle_runtime() { record_step manage_support_bundle_runtime; }
manage_pico_usb_runtime() { record_step manage_pico_usb_runtime; }
manage_exe() { record_step manage_exe; }
manage_sound() { record_step manage_sound; }
prepare_owned_rp1_gpclk_runtime_removal() { record_step prepare_owned_rp1_gpclk_runtime_removal; }
remove_owned_rp1_gpclk_dkms_provider() {
    record_step remove_owned_rp1_gpclk_dkms_provider || return $?
    if [[ "$PROVIDER_UNPROVEN" == "true" ]]; then
        RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN=true
    fi
}
flag_need_reboot() { record_step flag_need_reboot; }
eval "$(declare -f finish_script | sed '1s/finish_script/original_finish_script/')"
finish_script() {
    printf 'finish_status=%s\n' "$1" >>"$CAPTURE"
    original_finish_script "$@"
}
logE() { :; }
warn() { printf 'warning=%s\n' "$*" >>"$CAPTURE"; }

status=0
manage_wsprry_pi || status=$?
printf 'return_status=%s\n' "$status" >>"$CAPTURE"
'''
        environment = os.environ.copy()
        environment.update({
            "INSTALL_SCRIPT": str(install_script),
            "CAPTURE": str(capture),
            "FAILING_STEP": failing_step,
            "PROVIDER_UNPROVEN": "true" if provider_unproven else "false",
        })
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return capture.read_text().splitlines(), result.stdout

    def test_provider_removal_precedes_teardown_and_later_failures_fail_uninstall(self):
        expected_teardown = {
            "manage_apache", "manage_web", "manage_service", "manage_udp_logging", "manage_i2c",
            "manage_config", "manage_route_application",
            "manage_support_bundle_runtime", "manage_pico_usb_runtime",
            "manage_exe", "manage_sound",
            "flag_need_reboot", "prepare_owned_rp1_gpclk_runtime_removal",
        }
        for failing_step in (
            "manage_service", "manage_udp_logging", "manage_route_application", "manage_pico_usb_runtime", "manage_exe",
        ):
            with self.subTest(failing_step=failing_step):
                lines, stdout = self.run_uninstall_orchestration(failing_step)
                self.assertTrue(expected_teardown.issubset(lines))
                self.assertIn("remove_owned_rp1_gpclk_dkms_provider", lines)
                self.assertLess(
                    lines.index("remove_owned_rp1_gpclk_dkms_provider"),
                    lines.index(failing_step),
                )
                self.assertIn("finish_status=1", lines)
                self.assertIn("return_status=1", lines)
                self.assertNotIn("Uninstallation successful", stdout)
                self.assertNotIn("has been uninstalled", stdout)

    def test_successful_teardown_removes_owned_provider_before_application(self):
        lines, stdout = self.run_uninstall_orchestration()
        provider = lines.index("remove_owned_rp1_gpclk_dkms_provider")
        self.assertGreater(provider, lines.index("prepare_owned_rp1_gpclk_runtime_removal"))
        self.assertLess(provider, lines.index("manage_udp_logging"))
        self.assertLess(lines.index("manage_udp_logging"), lines.index("manage_service"))
        self.assertLess(provider, lines.index("manage_service"))
        self.assertLess(provider, lines.index("manage_exe"))
        self.assertIn("finish_status=0", lines)
        self.assertIn("return_status=0", lines)
        self.assertIn("Uninstallation successful", stdout)
        self.assertNotIn("RP1-GPIO-CLK-DKMS provider was not removed", stdout)

    def test_successful_uninstall_reports_unproven_provider_preservation(self):
        lines, stdout = self.run_uninstall_orchestration(provider_unproven=True)
        self.assertIn("finish_status=0", lines)
        self.assertIn(
            "The RP1-GPIO-CLK-DKMS provider was not removed, as I was unable to prove\n"
            "WsprryPi's secure ownership of the module.",
            stdout,
        )

    def test_provider_helper_output_drives_only_unproven_summary_state(self):
        install_script = ROOT / "scripts" / "install.sh"
        shell = r'''
source "$INSTALL_SCRIPT"
ACTION=uninstall
DRY_RUN=false
REMOVE_RP1_GPCLK_DKMS=auto
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
python3() {
    if [[ "$1" == -I ]]; then command python3 "$@"; return; fi
    while (( $# )); do
        if [[ "$1" == --result-file ]]; then shift; (umask 077; printf '%s\n' "$HELPER_OUTPUT" >"$1"); break; fi
        shift
    done
}
remove_owned_rp1_gpclk_dkms_provider || exit $?
printf '%s\n' "$RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN"
'''
        outcomes = {
            json.dumps({'schemaVersion':1,'contract':'wsprrypi-rp1-provider-removal-v1','outcome':outcome}): expected
            for outcome,expected in [('absent','false'),('unmanaged','true'),('preserved','true')]
        }
        for helper_output, expected in outcomes.items():
            with self.subTest(helper_output=helper_output):
                environment = os.environ.copy()
                environment.update({
                    "INSTALL_SCRIPT": str(install_script),
                    "HELPER_OUTPUT": helper_output,
                })
                result = subprocess.run(
                    ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, env=environment, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip().splitlines()[-1], expected)
        for bad in ['human success wording', '{}', '{"schemaVersion":true,"contract":"wsprrypi-rp1-provider-removal-v1","outcome":"absent"}']:
            result=subprocess.run(['bash','-c',shell],text=True,capture_output=True,
                env={**os.environ,'INSTALL_SCRIPT':str(install_script),'HELPER_OUTPUT':bad})
            self.assertNotEqual(result.returncode,0)

    def test_owned_provider_removal_failure_fails_uninstall(self):
        lines, stdout = self.run_uninstall_orchestration("remove_owned_rp1_gpclk_dkms_provider")
        self.assertIn("remove_owned_rp1_gpclk_dkms_provider", lines)
        self.assertNotIn("manage_service", lines)
        self.assertNotIn("manage_exe", lines)
        self.assertIn("finish_status=1", lines)
        self.assertIn("return_status=1", lines)
        self.assertNotIn("Uninstallation successful", stdout)
        self.assertNotIn("has been uninstalled", stdout)

    def test_runtime_recovery_failure_stops_before_application_teardown(self):
        lines, stdout = self.run_uninstall_orchestration(
            "prepare_owned_rp1_gpclk_runtime_removal"
        )
        self.assertIn("prepare_owned_rp1_gpclk_runtime_removal", lines)
        self.assertNotIn("manage_service", lines)
        self.assertNotIn("manage_exe", lines)
        self.assertNotIn("remove_owned_rp1_gpclk_dkms_provider", lines)
        self.assertIn("finish_status=1", lines)
        self.assertIn("return_status=1", lines)
        self.assertNotIn("Uninstallation successful", stdout)


def write_record(path, value):
    path.write_text(json.dumps(value))
    path.chmod(0o600)


def development_record(version: str = "1.0.0") -> dict[str, object]:
    return {
        "schema": MOD.RECORD_SCHEMA, "repository": MOD.REPOSITORY,
        "owner": "WsprryPi", "channel": "development",
        "installationMethod": "upstream-development-rollback",
        "sourceCommit": "a" * 40, "productVersion": version,
        "sourceTree": "b" * 40, "uapiSha256": "c" * 64,
        "versionSource": "include/rp1_gpclk/version.h",
        "versionSourceSha256": "d" * 64,
        "targetKernel": MOD.platform.release(),
        "compatibilityIdentity": MOD.development_compatibility(version),
        "upstreamEvidence": "/var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence",
        "rollbackRecord": "/var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence/ROLLBACK.json",
        "rollbackRecordSha256": "e" * 64,
        "rollbackEntrypoint": "/var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence/rendered-source/scripts/development-rollback",
        "rollbackEntrypointSha256": "f" * 64,
        "installedModulePath": f"/lib/modules/{MOD.platform.release()}/updates/dkms/rp1_gpclk_dkms.ko",
        "installedModuleSha256": "1" * 64,
        "decompressedModuleSha256": "2" * 64,
        "routeActivation": "disabled", "output": "disabled",
        "qualificationClaim": False,
    }


def release_record() -> dict[str, object]:
    return {
        "schema": MOD.RECORD_SCHEMA, "repository": MOD.REPOSITORY,
        "owner": "WsprryPi", "channel": "release",
        "installationMethod": "debian-package", "tag": "v2.1.0",
        "sourceCommit": "a" * 40, "manifest": valid_manifest(),
        "moduleArtifacts": [{
            "path": f"/lib/modules/fixture/updates/dkms/{name}.ko",
            "sha256": "c" * 64,
        } for name in (MOD.MODULE_NAME, MOD.CONTROLLER_MODULE_NAME)],
        "routeActivation": "disabled", "output": "disabled",
        "qualificationClaim": False,
    }


if __name__=="__main__": unittest.main(verbosity=2)
