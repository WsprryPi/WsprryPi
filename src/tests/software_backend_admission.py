# SPDX-License-Identifier: MIT
"""Serialize actual-main tests sharing the production singleton socket."""
from contextlib import contextmanager
import fcntl
import os
from pathlib import Path
import stat
import time


def backend_admission_path(executable):
    binary_directory = Path(executable).resolve().parent
    # Profile fixtures link separately but still bind the same singleton port
    # as the fixtures directly under the build's binary directory.
    if binary_directory.parent.name == 'profiles':
        binary_directory = binary_directory.parent.parent
    return binary_directory / '.backend-singleton-tests.lock'


@contextmanager
def backend_admission(executable):
    descriptor = os.open(backend_admission_path(executable), os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
    try:
        info = os.fstat(descriptor)
        assert stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and info.st_nlink == 1 and not info.st_mode & 0o077
        deadline = time.monotonic() + 90
        while True:
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                assert time.monotonic() < deadline, 'Another actual-main test retained singleton admission'
                time.sleep(0.05)
        yield
    finally:
        os.close(descriptor)
