#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Private worker child. Only the application's root broker owns its pipes."""
import json
import os
from pathlib import Path
import struct
import sys

# The packaged module directory is fixed by the installer and verified by the
# C++ broker. Isolated Python startup excludes user site/PYTHONPATH imports.
if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = Path(__file__).resolve().parent.name
from . import archive
from .jobs import BackendUnavailable, RecoveryBusy, RecoveryJobs


def framed_config():
    with os.fdopen(4, 'rb') as source:
        length = struct.unpack('!I', archive.exact(source, 4))[0]
        archive.require(0 < length <= 32768)
        config = archive.strict_json(archive.exact(source, length))
        archive.require(not source.read(1))
        return config


def main():
    archive.require(sys.argv[1:] == ['--serve'])
    owner = RecoveryJobs(framed_config())
    while True:
        line = sys.stdin.buffer.readline(144 * 1024 + 1)
        if not line:
            return 0
        if len(line) > 144 * 1024 or not line.endswith(b'\n'):
            return 2
        try:
            request = archive.strict_json(line)
            result = {'ok': True, 'value': owner.command(request)}
        except RecoveryBusy:
            result = {'ok': False, 'error': 'busy'}
        except BackendUnavailable:
            result = {'ok': False, 'error': 'backend_unavailable'}
        except (archive.RecoveryError, ValueError, UnicodeError, TypeError):
            result = {'ok': False, 'error': 'invalid_request'}
        except BaseException:
            # No exception text or request/body data enters service diagnostics.
            result = {'ok': False, 'error': 'storage_unavailable'}
        encoded = json.dumps(result, separators=(',', ':')).encode()
        if len(encoded) > 1024 * 1024:
            return 2
        sys.stdout.buffer.write(encoded + b'\n')
        sys.stdout.buffer.flush()
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception:
        print('Recovery worker unavailable; inspect its private job state and packaged runtime.', file=sys.stderr)
        raise SystemExit(2)
