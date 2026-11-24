#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package the fixed maintenance runtime from the selected verified executable.

The installer stops the independent worker before invoking this helper. No
service, database, application identity, archive, or hardware is operated here.
"""
import argparse
import os
from pathlib import Path
import stat
import subprocess
import tempfile

MODULES = ('__init__.py', 'archive.py', 'durable.py', 'evidence.py', 'jobs.py', 'lifecycle.py', 'publication.py', 'proxy.py', 'private_acl.py', 'state_owner.py', 'state_owner_worker.py', 'worker.py')


def directory(path):
    if path.is_symlink():
        raise ValueError('unsafe runtime directory')
    path.mkdir(mode=0o755, parents=True, exist_ok=True)
    for current in (path, *path.parents):
        info = current.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid not in (0, os.geteuid()) or (info.st_mode & 0o022 and not (info.st_uid == 0 and info.st_mode & stat.S_ISVTX)):
            raise ValueError('unsafe runtime parent')


def source_bytes(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or before.st_mode & 0o022 or before.st_size > 1024 * 1024:
            raise ValueError('unsafe packaged source')
        with os.fdopen(os.dup(descriptor), 'rb') as stream:
            data = stream.read(1024 * 1024 + 1)
        after = os.fstat(descriptor)
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) or len(data) != before.st_size:
            raise ValueError('packaged source changed')
        return data
    finally:
        os.close(descriptor)


def publish(path, data, mode):
    if path.exists() or path.is_symlink():
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_nlink != 1 or info.st_mode & 0o022:
            raise ValueError('unsafe installed runtime member')
        if path.read_bytes() == data and stat.S_IMODE(info.st_mode) == mode:
            return
    descriptor, temporary = tempfile.mkstemp(prefix='.maintenance-install-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data);stream.flush();os.fchmod(stream.fileno(), mode);os.fsync(stream.fileno())
        os.replace(temporary, path)
        parent = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
    finally:
        Path(temporary).unlink(missing_ok=True)


def install(binary, source, destination):
    binary, source, destination = binary.absolute(), source.absolute(), destination.absolute()
    directory(destination)
    package = destination / 'recovery'
    directory(package)
    # Read all modules before changing installed contents. Existing private
    # recovery jobs live elsewhere and are never traversed by this helper.
    modules = {name: source_bytes(source / name) for name in MODULES}
    notices = source_bytes(source / 'THIRD_PARTY_NOTICES.txt')
    with tempfile.TemporaryFile(dir=destination) as output:
        result = subprocess.run([str(binary), '--export-recovery-age-helper'], stdin=subprocess.DEVNULL,
                                stdout=output, stderr=subprocess.DEVNULL, timeout=30, check=False)
        size = output.tell()
        if result.returncode != 0 or not 0 < size <= 64 * 1024 * 1024:
            raise ValueError('recovery helper export unavailable')
        output.seek(0)
        helper = output.read()
    publish(destination / 'recovery-age', helper, 0o755)
    for name, data in modules.items():
        publish(package / name, data, 0o644)
    publish(package / 'THIRD_PARTY_NOTICES.txt', notices, 0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path, default=Path('/usr/local/lib/wsprrypi'))
    args = parser.parse_args()
    install(args.binary, args.source, args.destination)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        raise SystemExit('Maintenance runtime packaging failed; installed state and recovery jobs were retained.')
