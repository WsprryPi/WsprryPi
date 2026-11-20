# SPDX-License-Identifier: MIT
"""Private recovery records; callers serialize their fixed-root owner admission."""
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile

from .archive import MAX_MANIFEST, RecoveryError, observation, private_directory, private_input, require, safe_parent, strict_json


class RecoveryLockBusy(RecoveryError):
    pass


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def sync_directory(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def read_record(path):
    try:
        with private_input(path, MAX_MANIFEST) as (stream, info):
            data = stream.read(MAX_MANIFEST + 1)
            require(len(data) <= MAX_MANIFEST and observation(os.fstat(stream.fileno())) == observation(info))
            return strict_json(data), (data, observation(info))
    except FileNotFoundError:
        return None, None


def publish_record(path, value, expected):
    path = safe_parent(path)
    private_directory(path.parent)
    data = canonical(value)
    require(0 < len(data) <= MAX_MANIFEST)
    _, current = read_record(path)
    require(current == expected)
    descriptor, temporary = tempfile.mkstemp(prefix='.recovery-writing-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        require(read_record(path)[1] == expected)
        if expected is None:
            # New records never overwrite a name that appeared unexpectedly.
            os.link(temporary, path, follow_symlinks=False)
            os.unlink(temporary)
        else:
            os.replace(temporary, path)
        sync_directory(path.parent)
        return read_record(path)[1]
    finally:
        Path(temporary).unlink(missing_ok=True)


@contextmanager
def directory_lock(path):
    path = private_directory(path)
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RecoveryLockBusy('The application still owns the selected state') from error
        info = os.fstat(descriptor)
        require(observation(info) == observation(path.lstat()) and stat.S_ISDIR(info.st_mode))
        yield
    finally:
        os.close(descriptor)
