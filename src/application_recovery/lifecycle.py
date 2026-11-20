# SPDX-License-Identifier: MIT
"""Fixed backend service lifecycle; the browser never supplies a PID or command."""
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import time
from . import archive


def validate(value):
    archive.require(type(value) is dict and value.get('kind') in ('systemd', 'software_fixture'))
    if value['kind'] == 'systemd':
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


def root_available(root):
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
    def __init__(self, config):
        self.config = validate(config['lifecycle'])
        self.root = config['application_root']
        self.child = None

    def stop(self, observed_pid):
        if self.config['kind'] == 'systemd':
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
        while not root_available(self.root):
            archive.require(time.monotonic() < deadline)
            time.sleep(.05)

    def start(self):
        if self.config['kind'] == 'systemd':
            archive.require(os.geteuid() == 0)
            result = subprocess.run(['/usr/bin/systemctl', 'start', 'wsprrypi.service'], stdin=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60,
                                    env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
            archive.require(result.returncode == 0)
        else:
            if self.child is not None and self.child.poll() is None:
                return
            archive.require(root_available(self.root))
            # Model an independent service. This backend survives browser and
            # worker process loss; the fixture owns its eventual cleanup.
            self.child = subprocess.Popen(self.config['start_command'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                          stderr=subprocess.DEVNULL, start_new_session=True,
                                          env={'PATH': '/usr/bin:/bin', 'LANG': 'C', 'WSPRRYPI_DISABLE_HARDWARE_ACCESS': '1'})
