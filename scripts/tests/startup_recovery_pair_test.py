#!/usr/bin/env python3
"""Offline installer rollback paired with the installed production startup hook."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

# Only external command/service and filesystem trust boundaries are replaced.
# The installed hook, marker checks, and environment writer are real.
# Backend-owned route state is injected; its actual executable has separate tests.
DRIVER = r'''
import os
from pathlib import Path
from unittest.mock import patch
import runtime_reconcile as recovery
root = Path(os.environ['FIXTURE'])
service = '{ path=/usr/local/bin/wsprrypi ; argv[]=/usr/local/bin/wsprrypi -J --state-directory /var/lib/wsprrypi/application-state ; ignore_errors=no ; }'
def trusted(path):
    mapped = {recovery.companion.BINARY: root/'bin/wsprrypi'}.get(path, path)
    if not mapped.is_relative_to(root):
        raise AssertionError('unexpected host read: '+str(path))
    return mapped.read_bytes(), mapped.stat()
with patch.object(recovery.companion, 'state', return_value={'backend':os.environ['BACKEND'],'route':'gpio4','transmit':False}), \
     patch.object(recovery.companion, 'trusted', side_effect=trusted), \
     patch.object(recovery.companion.subprocess, 'check_output', return_value=service), \
     patch.object(recovery.installer, 'secure_record_parent'), \
     patch.object(recovery, 'ENVIRONMENT', root/'startup.env'), \
     patch.object(recovery.os, 'geteuid', return_value=0), \
     patch.object(recovery.Linux, 'ownership', return_value=None) as ownership:
    recovery.main()
    if os.environ['BACKEND'] != 'rp1-gpclk':
        ownership.assert_not_called()
'''

HARNESS = r'''
set -euo pipefail
source "$SOURCE/scripts/install.sh"
ACTION=install
WSPR_EXE=wsprrypi
DRY_RUN=false
BINARY_SOURCE="$SELECTION"
BINARY_INSTALL_DIR="$FIXTURE/bin"
BINARY_STAGE="$FIXTURE/stage"
LOCAL_REPO_DIR="$FIXTURE/repo"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
logD() { :; }
logI() { printf '%s\n' "$1" >>"$FIXTURE/messages"; }
logE() { printf '%s\n' "$1" >>"$FIXTURE/messages"; }
exec_command() {
    printf '%s\n' "$1" >>"$FIXTURE/messages"
    shift
    local args=() arg
    for arg in "$@"; do
        [[ -z "$arg" || "$arg" == debug ]] || args+=("$arg")
    done
    "${args[@]}"
}
install() {
    local destination="${@: -1}"
    if [[ "$1" == -d ]]; then
        [[ "$destination" == /usr/local/share/licenses/wsprrypi ]] || return 97
        command mkdir -p "$FIXTURE/licenses"
        return
    fi
    if [[ "$destination" == /usr/local/share/licenses/wsprrypi/BOTAN-LICENSE.txt ]]; then
        destination="$FIXTURE/licenses/BOTAN-LICENSE.txt"
    fi
    [[ "$destination" == "$FIXTURE/"* ]] || return 98
    command cp "${@: -2:1}" "$destination"
}
hook() { PYTHONPATH="$FIXTURE/helpers" "$PYTHON" "$FIXTURE/hook.py" prepare; }
systemctl() {
    case "$1" in
        is-active) [[ $(cat "$FIXTURE/active") == 1 ]];;
        stop) printf 0 >"$FIXTURE/active";;
        list-unit-files) printf 'wsprrypi.service enabled\n';;
        start)
            hook || return $?
            [[ "$RECOVERY_FAIL" == 0 ]] || return 19
            printf 1 >"$FIXTURE/active";;
        *) return 99;;
    esac
}
for step in capture_install_sem_ver remove_legacy_services remove_legacy_files_and_dirs \
    prepare_install_executable validate_application_state_executable manage_support_bundle_runtime manage_pico_usb_runtime \
    manage_application_state manage_maintenance_service manage_i2c manage_udp_logging activate_rp1_gpclk_runtime_administration \
    manage_web manage_apache validate_wsprrypi_runtime manage_sound \
    cleanup_files_in_directories complete_application_ownership flag_need_reboot; do
    eval "$step() { :; }"
done
manage_route_application() {
    mkdir -p "$FIXTURE/helpers"
    cp "$SOURCE/scripts/route_application.py" "$SOURCE/scripts/runtime_reconcile.py" \
        "$SOURCE/scripts/rp1_gpclk_dkms_install.py" "$FIXTURE/helpers/"
    cp "$SOURCE/systemd/92-rp1-reconcile.conf" "$FIXTURE/drop-in.conf"
}
manage_service() {
    hook || return $?
    # Inject the original installation failure after candidate preparation.
    return 17
}
finish_script() { printf '%s\n' "$1" >"$FIXTURE/finish-status"; }
if manage_wsprry_pi; then exit 91; else status=$?; fi
printf '%s\n' "$status" >"$FIXTURE/status"
# Repeated exit recovery must not replace/delete the restored executable.
rollback_executable
rollback_executable
printf '%s\n' "$BINARY_INSTALLED" >"$FIXTURE/binary-state"
'''


class Tests(unittest.TestCase):
    def test_provider_selection_is_independent_of_backend(self):
        script = r"""
source "$SOURCE/scripts/install.sh"
eval "$(declare -f is_rp1_system | sed '1s/is_rp1_system/fixture_detect_rp1/')"
is_rp1_system() { fixture_detect_rp1 "$COMPAT"; }
if rp1_gpclk_dkms_installation_selected; then printf selected; else printf excluded; fi
"""
        for model in ('5-model-b', '5-compute-module', '3-model-b-plus'):
            for backend in ('gpio', 'si5351', 'rp1-gpclk'):
                for policy in ('auto', 'true', 'false'):
                    with self.subTest(model=model, backend=backend, policy=policy), tempfile.TemporaryDirectory() as raw:
                        compat = Path(raw)/'compatible'
                        chip = 'brcm,bcm2712' if model.startswith('5-') else 'brcm,bcm2837'
                        compat.write_bytes(('raspberrypi,'+model+'\0'+chip+'\0').encode())
                        result = subprocess.run(['bash', '-c', script], capture_output=True, text=True, timeout=30,
                            env={**os.environ, 'SOURCE': str(ROOT), 'COMPAT': str(compat),
                                 'INSTALL_RP1_GPCLK_DKMS': policy, 'BACKEND': backend})
                        self.assertEqual(result.returncode, 0, result.stderr)
                        selected = policy == 'true' or (policy == 'auto' and model.startswith('5-'))
                        self.assertEqual(result.stdout, 'selected' if selected else 'excluded')

    def test_failed_upgrade_restores_old_binary_with_current_hook(self):
        import sys
        for backend in ('gpio', 'si5351', 'rp1-gpclk'):
            for selection in ('build', 'local', 'release'):
                for recovery_fail in (0, 1):
                    with self.subTest(backend=backend, selection=selection, recovery_fail=recovery_fail), tempfile.TemporaryDirectory() as raw:
                        root = Path(raw)
                        for name in ('bin', 'stage', 'repo/executables', 'repo/docs/licenses/botan'):
                            (root/name).mkdir(parents=True)
                        notice = (ROOT/'docs/licenses/botan/BSD-2-Clause.txt').read_bytes()
                        (root/'repo/docs/licenses/botan/BSD-2-Clause.txt').write_bytes(notice)
                        old = b'older application without restoration markers'
                        new = b'WSPRRYPI_ROUTE_RESTORE_IDLE WSPRRYPI_RP1_REBOOT_IDLE'
                        (root/'bin/wsprrypi').write_bytes(old)
                        (root/'stage/wsprrypi').write_bytes(new)
                        (root/'repo/executables/wsprrypi').write_bytes(new)
                        config = (ROOT/'config/wsprrypi.ini').read_bytes().replace(
                            b'Transmit Backend = gpio', b'Transmit Backend = '+backend.encode())
                        if backend != 'rp1-gpclk':
                            config = config.replace(b'Transmit = False', b'Transmit = True').replace(
                                b'Enable on Boot = Never', b'Enable on Boot = Always')
                            self.assertIn(b'Transmit = True', config)
                            self.assertIn(b'Enable on Boot = Always', config)
                        (root/'wsprrypi.ini').write_bytes(config)
                        (root/'active').write_text('1')
                        (root/'hook.py').write_text(DRIVER)
                        result = subprocess.run(['bash', '-c', HARNESS], capture_output=True, text=True, timeout=30,
                            env={**os.environ, 'SOURCE': str(ROOT), 'FIXTURE': str(root),
                                 'PYTHON': sys.executable, 'BACKEND': backend,
                                 'SELECTION': selection, 'RECOVERY_FAIL': str(recovery_fail),
                                 'PYTHONDONTWRITEBYTECODE': '1'})
                        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                        self.assertEqual((root/'bin/wsprrypi').read_bytes(), old)
                        self.assertEqual((root/'licenses/BOTAN-LICENSE.txt').read_bytes(), notice)
                        self.assertEqual((root/'wsprrypi.ini').read_bytes(), config)
                        self.assertEqual((root/'status').read_text().strip(), '1')
                        self.assertEqual((root/'finish-status').read_text().strip(), '1')
                        self.assertEqual((root/'binary-state').read_text().strip(), 'false')
                        self.assertFalse(list((root/'bin').glob('*.previous.*')))
                        self.assertTrue((root/'helpers').exists(), (root/'messages').read_text()+result.stderr)
                        for name in ('route_application.py', 'runtime_reconcile.py', 'rp1_gpclk_dkms_install.py'):
                            self.assertEqual(hashlib.sha256((root/'helpers'/name).read_bytes()).digest(),
                                             hashlib.sha256((ROOT/'scripts'/name).read_bytes()).digest())
                        self.assertEqual((root/'drop-in.conf').read_bytes(), (ROOT/'systemd/92-rp1-reconcile.conf').read_bytes())
                        messages = (root/'messages').read_text()
                        self.assertIn('failed with status 17', messages)
                        failed = backend == 'rp1-gpclk' or recovery_fail == 1
                        self.assertEqual((root/'active').read_text(), '0' if failed else '1')
                        if failed:
                            self.assertIn('Unable to restart', messages)
                        else:
                            self.assertEqual((root/'startup.env').read_bytes(), b'')


if __name__ == '__main__':
    unittest.main()
