# SPDX-License-Identifier: MIT
"""Private recovery records; callers serialize their fixed-root owner admission."""
from contextlib import contextmanager
import ctypes
import hashlib
import json
import os
import sys
if sys.platform != 'win32':
    import fcntl
from pathlib import Path
import stat
import tempfile

from .archive import MAX_MANIFEST, RecoveryError, observation, private_directory, private_input, require, safe_parent, strict_json, stat_descriptor


class RecoveryLockBusy(RecoveryError):
    pass


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def sync_directory(path):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.sync_directory(path)
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def read_record(path):
    if sys.platform == 'win32':
        from . import native_files
        saved = native_files.read_record(path)
        return (None, None) if saved is None else (strict_json(saved[0]), saved)
    try:
        with private_input(path, MAX_MANIFEST) as (stream, info):
            data = stream.read(MAX_MANIFEST + 1)
            require(len(data) <= MAX_MANIFEST and observation(stat_descriptor(stream.fileno())) == observation(info))
            return strict_json(data), (data, observation(info))
    except FileNotFoundError:
        return None, None


def move_no_replace(source, destination, owner=None):
    if sys.platform == 'win32':
        require(owner is not None)
        return owner.move(source, destination)
    library = ctypes.CDLL(None, use_errno=True)
    if sys.platform == 'darwin':
        operation = library.renamex_np
        operation.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        result = operation(os.fsencode(source), os.fsencode(destination), 4)  # RENAME_EXCL
    elif sys.platform == 'linux':
        operation = library.renameat2
        operation.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        result = operation(-100, os.fsencode(source), -100, os.fsencode(destination), 1)  # RENAME_NOREPLACE
    else:
        raise RecoveryError('Exclusive recovery publication is unsupported on this platform')
    if result:
        raise OSError(ctypes.get_errno(), 'Exclusive recovery publication refused')


def publish_record(path, value, expected):
    path = safe_parent(path)
    private_directory(path.parent)
    data = canonical(value)
    require(0 < len(data) <= MAX_MANIFEST)
    _, current = read_record(path)
    require(current == expected)
    if sys.platform == 'win32':
        from . import native_files
        native_files.publish_record(path, data, expected)
        return read_record(path)[1]
    descriptor, temporary = tempfile.mkstemp(prefix='.recovery-writing-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        require(read_record(path)[1] == expected)
        if expected is None:
            # The first publication must have one name even if the writer
            # exits before syncing the directory.
            move_no_replace(temporary, path)
        else:
            os.replace(temporary, path)
        sync_directory(path.parent)
        return read_record(path)[1]
    finally:
        Path(temporary).unlink(missing_ok=True)


@contextmanager
def directory_lock(path, *, generation=False):
    if sys.platform == 'win32':
        from . import native_files
        try:
            with (native_files.generation_lock(path) if generation else native_files.directory_lock(path)) as owner:
                yield owner
        except native_files.NativeLockBusy as error:
            raise RecoveryLockBusy('The application still owns the selected state') from error
        return
    path = private_directory(path)
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RecoveryLockBusy('The application still owns the selected state') from error
        info = stat_descriptor(descriptor)
        require(observation(info) == observation(path.lstat()) and stat.S_ISDIR(info.st_mode))
        yield
    finally:
        os.close(descriptor)
