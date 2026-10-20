#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install/remove the fixed application startup files with attributable retries."""
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

import rp1_gpclk_dkms_install as installer
import runtime_reconcile as recovery

ROOT = Path(__file__).resolve().parents[1]
STATE = Path('/var/lib/wsprrypi/rp1-startup-files.json')
OWNER_UID = 0
FILES = {
    '/usr/local/lib/wsprrypi/runtime_reconcile.py': ('scripts/runtime_reconcile.py', 0o755),
    '/usr/local/lib/wsprrypi/rp1_gpclk_dkms_install.py': ('scripts/rp1_gpclk_dkms_install.py', 0o755),
    '/etc/systemd/system/wsprrypi-rp1-reconcile.service': ('systemd/wsprrypi-rp1-reconcile.service', 0o644),
    '/etc/systemd/system/wsprrypi.service.d/92-rp1-reconcile.conf': ('systemd/92-rp1-reconcile.conf', 0o644),
}


def read(path):
    for parent in path.parents:
        if parent.exists():
            info = parent.lstat()
            if not stat.S_ISDIR(info.st_mode) or info.st_uid != OWNER_UID or info.st_mode & 0o022:
                raise ValueError('unsafe startup integration directory: '+str(parent))
    try:
        info = path.lstat()
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(info.st_mode) or info.st_uid != OWNER_UID or info.st_mode & 0o022 or info.st_nlink != 1:
        raise ValueError('unsafe startup integration file: '+str(path))
    return path.read_bytes()


def digest(data):
    return None if data is None else hashlib.sha256(data).hexdigest()


def apply(remove=False):
    target = {name: None if remove else digest((ROOT/source).read_bytes())
              for name, (source, unused) in FILES.items()}
    raw = read(STATE)
    record = recovery.strict_json(raw) if raw is not None else None
    if record is not None:
        if (set(record) != {'version', 'phase', 'before', 'after'} or
                record['version'] != 1 or record['phase'] not in ('pending', 'complete') or
                any(not isinstance(record[key], dict) or set(record[key]) != set(FILES)
                    for key in ('before', 'after')) or
                any(value is not None and (not isinstance(value, str) or not installer.SHA256.fullmatch(value))
                    for key in ('before', 'after') for value in record[key].values())):
            raise ValueError('invalid startup installation ownership record')
        if record['phase'] == 'pending' and record['after'] != target:
            raise ValueError('retry the exact pending startup installation first')
    current = {name: digest(read(Path(name))) for name in FILES}
    for name, actual in current.items():
        allowed = ({None, target[name]} if record is None else
                   {record['before'][name], record['after'][name]} if record['phase'] == 'pending' else
                   {record['after'][name]})
        if actual not in allowed:
            raise ValueError('startup file differs from recorded ownership: '+name)
    STATE.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    installer.atomic_json(STATE, {'version': 1, 'phase': 'pending',
                                  'before': current, 'after': target})
    for name, (source, mode) in FILES.items():
        path = Path(name)
        if digest(read(path)) != current[name]:
            raise ValueError('startup destination changed after planning: '+name)
        if remove:
            if path.exists(): path.unlink()
        else:
            path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix='.rp1-startup-', dir=path.parent)
            try:
                with os.fdopen(fd, 'wb') as stream:
                    os.fchmod(stream.fileno(), mode)
                    stream.write((ROOT/source).read_bytes())
                    stream.flush()
                    os.fsync(stream.fileno())
                if digest(Path(temporary).read_bytes()) != target[name]:
                    raise ValueError('startup source changed during installation')
                os.replace(temporary, path)
            finally:
                if os.path.exists(temporary): os.unlink(temporary)
        if path.parent.exists():
            fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try: os.fsync(fd)
            finally: os.close(fd)
    subprocess.run(['/usr/bin/systemctl', 'daemon-reload'], check=True)
    installer.atomic_json(STATE, {'version': 1, 'phase': 'complete',
                                  'before': target, 'after': target})


def install(remove=False):
    if os.geteuid() != 0: raise ValueError('root required')
    with recovery.Linux().lock():
        apply(remove)


if __name__ == '__main__':
    try:
        if sys.argv[1:] not in (['install'], ['remove']): raise ValueError('expected install or remove')
        install(sys.argv[1] == 'remove')
    except (OSError, ValueError, installer.ContractError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
