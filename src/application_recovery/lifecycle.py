# SPDX-License-Identifier: MIT
"""Fixed backend service lifecycle; the browser never supplies a PID or command."""
import os
from pathlib import Path
import plistlib
import re
import signal
import stat
import sys
if sys.platform != 'win32':
    import fcntl
import subprocess
import time
from . import archive


MAC_LABEL = 'org.wsprrypi.controller'
MAC_TARGET = 'system/' + MAC_LABEL
MAC_PLIST = Path('/Library/LaunchDaemons') / (MAC_LABEL + '.plist')
MAC_BACKEND = '/Library/Application Support/WsprryPi/Program/bin/wsprrypi'
MAC_STATE = '/Library/Application Support/WsprryPi/Controller/application-state'
MAC_RUNTIME = '/Library/Application Support/WsprryPi/Run/Backend'
MAC_LOGS = '/Library/Logs/WsprryPi'


def validate(value):
    native = {'win32': 'windows_service', 'darwin': 'launchd', 'linux': 'systemd'}.get(sys.platform)
    archive.require(type(value) is dict and value.get('kind') in (native, 'software_fixture')
                    and value.get('kind') is not None)
    if value['kind'] != 'software_fixture':
        archive.closed(value, ('kind',))
    else:
        archive.closed(value, ('kind', 'start_command', 'crash_after_phase') if 'crash_after_phase' in value else ('kind', 'start_command'))
        if 'crash_after_phase' in value:
            archive.require(value['crash_after_phase'] in ('stopping_backend', 'retaining_evidence', 'publishing_generation',
                            'starting_backend', 'verifying_backend', 'complete'))
        command = value['start_command']
        archive.require(type(command) is list and 0 < len(command) <= 32 and Path(command[0]).is_absolute()
                        and all(type(item) is str and '\0' not in item and len(item) <= 4096 for item in command)
                        and '--backend' in command and command[command.index('--backend') + 1:command.index('--backend') + 2] == ['simulated'])
    return value


def launchd_child(*args):
    return subprocess.run(['/bin/launchctl', *args], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, timeout=60, check=True, text=True,
                          env={'PATH': '/usr/bin:/bin', 'LANG': 'C', 'LC_ALL': 'C'}).stdout


def mac_backend_arguments():
    return [MAC_BACKEND, '--controller-only', '--state-directory', MAC_STATE,
            '--runtime-directory', MAC_RUNTIME, '--socket-loopback-only']


def mac_service_definition(label, arguments, username, group, directory, logs):
    return {
        'Label': label, 'ProgramArguments': arguments,
        'UserName': username, 'GroupName': group,
        'WorkingDirectory': directory,
        'StandardOutPath': f'{MAC_LOGS}/{logs}/service.log',
        'StandardErrorPath': f'{MAC_LOGS}/{logs}/service-error.log',
        'RunAtLoad': True, 'KeepAlive': {'SuccessfulExit': False},
        'ThrottleInterval': 5, 'ExitTimeOut': 30, 'Umask': 0o077,
    }


def mac_backend_definition():
    return mac_service_definition(MAC_LABEL, mac_backend_arguments(), '_wsprrypi', '_wsprrypiweb',
                                  str(Path(MAC_STATE).parent), 'Controller')


def admit_launchd_plist():
    for path in MAC_PLIST.parents:
        info = path.lstat()
        archive.require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o022)
    descriptor = os.open(MAC_PLIST, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, 'rb') as stream:
        info = os.fstat(stream.fileno())
        archive.require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1
                        and not info.st_mode & 0o022 and 0 < info.st_size <= 65536)
        raw = stream.read(65537)
        archive.require(len(raw) == info.st_size and archive.observation(info) ==
                        archive.observation(os.fstat(stream.fileno())))
    archive.require(plistlib.loads(raw) == mac_backend_definition())


def root_available(root):
    if sys.platform == 'win32':
        from . import native_files
        try:
            with native_files.generation_lock(root):
                return True
        except native_files.NativeLockBusy:
            return False
    root = archive.private_directory(root)
    descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False
        return True
    finally:
        os.close(descriptor)


class BackendLifecycle:
    def __init__(self, config, *, available=None, native_service=None):
        self.config = validate(config['lifecycle'])
        self.root = config['application_root']
        self.child = None
        self.available = available if available is not None else lambda: root_available(self.root)
        self.backend_owner = config.get('backend_owner')
        self.native_service = native_service
        archive.require(self.config['kind'] != 'windows_service' or native_service is not None)

    def stop(self, observed_pid):
        if self.config['kind'] == 'windows_service':
            self.native_service.stop(observed_pid)
        elif self.config['kind'] == 'launchd':
            archive.require(os.geteuid() == 0 and type(observed_pid) is int and observed_pid > 1)
            admit_launchd_plist()
            job = launchd_child('print', MAC_TARGET)
            pid = re.findall(r'^\s*pid = (\d+)\s*$', job, re.MULTILINE)
            archive.require(pid == [str(observed_pid)])
            launchd_child('bootout', MAC_TARGET)
        elif self.config['kind'] == 'systemd':
            # No shell, caller-selected unit, arbitrary arguments, or remote
            # transport. The installed maintenance role is explicitly root.
            archive.require(os.geteuid() == 0)
            result = subprocess.run(['/usr/bin/systemctl', 'stop', 'wsprrypi.service'], stdin=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60,
                                    env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
            archive.require(result.returncode == 0)
        else:
            # Typed actual-main fixtures only. The PID comes from the verified
            # backend observation bound to the admitted boot, never HTTP input.
            archive.require(type(observed_pid) is int and observed_pid > 1 and observed_pid != os.getpid())
            try:
                os.kill(observed_pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 60
        while not self.available():
            archive.require(time.monotonic() < deadline)
            time.sleep(.05)

    def start(self):
        if self.config['kind'] == 'windows_service':
            state, pid = self.native_service.query()
            if state == self.native_service.RUNNING:
                # Only a helper mode established by the authenticated owner
                # greeting may be stopped during this restart operation.
                archive.require(self.native_service.helper_nonce is not None)
                self.native_service.verify_helper(pid)
                self.native_service.stop(pid)
            else:
                archive.require(state == self.native_service.STOPPED)
            self.native_service.start()
        elif self.config['kind'] == 'launchd':
            archive.require(os.geteuid() == 0 and self.available())
            admit_launchd_plist()
            launchd_child('bootstrap', 'system', str(MAC_PLIST))
        elif self.config['kind'] == 'systemd':
            archive.require(os.geteuid() == 0)
            result = subprocess.run(['/usr/bin/systemctl', 'start', 'wsprrypi.service'], stdin=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60,
                                    env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
            archive.require(result.returncode == 0)
        else:
            if self.child is not None and self.child.poll() is None:
                return
            archive.require(self.available())
            # Model an independent service. This backend survives browser and
            # worker process loss; the fixture owns its eventual cleanup.
            credentials = {}
            if self.backend_owner is not None and os.geteuid() == 0:
                import pwd
                account = pwd.getpwnam('_wsprrypiweb' if sys.platform == 'darwin' else 'wsprrypi-web')
                archive.require(account.pw_uid > 0 and account.pw_gid > 0 and
                                self.backend_owner['uid'] > 0 and self.backend_owner['gid'] > 0)
                credentials = {'user': self.backend_owner['uid'], 'group': self.backend_owner['gid'], 'extra_groups': [account.pw_gid]}
            self.child = subprocess.Popen(self.config['start_command'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                          stderr=subprocess.DEVNULL, start_new_session=True,
                                          env={'PATH': '/usr/bin:/bin', 'LANG': 'C', 'WSPRRYPI_DISABLE_HARDWARE_ACCESS': '1'}, **credentials)
