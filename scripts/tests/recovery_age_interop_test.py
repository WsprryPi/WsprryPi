#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent installed age CLI decrypts the bounded WsprryPi profile."""
import os
import fcntl
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time


def main(helper, age):
    phrase = b'synthetic interoperability passphrase, never an operator secret'
    with tempfile.TemporaryDirectory(prefix='wsprrypi-age-interop-') as temporary:
        directory = Path(temporary)
        source, encrypted, plain = directory / 'source', directory / 'backup.age', directory / 'decoded'
        expected = (b'synthetic private archive\x00' * 10000)
        source.write_bytes(expected)
        source.chmod(0o600)
        read_secret, write_secret = os.pipe()
        original = read_secret
        read_secret = fcntl.fcntl(original, fcntl.F_DUPFD_CLOEXEC, 10)
        os.close(original)
        frame = struct.pack('!I', len(phrase)) + phrase
        os.write(write_secret, frame)
        os.close(write_secret)
        # posix_spawn performs the descriptor mapping without a Python preexec
        # callback in a potentially threaded caller.
        input_fd = os.open(source, os.O_RDONLY)
        output_fd = os.open(encrypted, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        null = os.open(os.devnull, os.O_WRONLY)
        actions = [(os.POSIX_SPAWN_DUP2, input_fd, 0), (os.POSIX_SPAWN_DUP2, output_fd, 1),
                   (os.POSIX_SPAWN_DUP2, null, 2), (os.POSIX_SPAWN_DUP2, read_secret, 3)]
        actions += [(os.POSIX_SPAWN_CLOSE, fd) for fd in (input_fd, output_fd, null, read_secret) if fd > 3]
        try:
            pid = os.posix_spawn(helper, [helper, 'encrypt'], {'PATH': '/usr/bin:/bin'}, file_actions=actions)
            assert os.waitpid(pid, 0)[1] == 0, 'Recovery helper encryption failed'
        finally:
            for fd in (read_secret, input_fd, output_fd, null):
                os.close(fd)
        assert b'-> scrypt ' in encrypted.read_bytes()[:200]
        pid, terminal = pty.fork()
        if pid == 0:
            environment = dict(os.environ)
            environment.pop('AGE_PASSPHRASE', None)
            os.execve(age, [age, '--decrypt', '--output', str(plain), str(encrypted)], environment)
        status = None
        try:
            captured = b''
            supplied = False
            deadline = time.monotonic() + 30
            while status is None:
                assert time.monotonic() < deadline, 'Independent age CLI exceeded fixture deadline'
                if select.select([terminal], [], [], 0.05)[0]:
                    try:
                        captured = (captured + os.read(terminal, 4096))[-8192:]
                    except OSError:
                        pass
                    if not supplied and b'passphrase' in captured.lower():
                        os.write(terminal, phrase + b'\n')
                        supplied = True
                finished, value = os.waitpid(pid, os.WNOHANG)
                if finished:
                    status = value
            assert status == 0, 'Independent age CLI refused the recovery archive'
            assert plain.read_bytes() == expected, 'Independent age CLI changed bytes'
        finally:
            if status is None:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
            os.close(terminal)
    print('PASS bounded recovery profile decrypted by independent installed age CLI; no passphrase in argv/environment/files')


if __name__ == '__main__':
    main(str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()))
