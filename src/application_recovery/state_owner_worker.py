#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Internal backend-owner child; its inherited Unix socket must belong to root."""
from pathlib import Path
import sys

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = Path(__file__).resolve().parent.name
from .state_owner import serve
import os
import struct

if __name__ == '__main__':
    if sys.platform == 'win32':
        from . import archive
        import msvcrt
        archive.require(len(sys.argv) == 4 and sys.argv[1] == '--windows-service-owner' and
                        all(value.isascii() and value.isdecimal() and str(int(value)) == value and 0 < int(value) < 2**64-1 for value in sys.argv[2:]))
        descriptor = msvcrt.open_osfhandle(int(sys.argv[2]), os.O_RDONLY | os.O_BINARY)
        os.set_inheritable(descriptor, False)
        with os.fdopen(descriptor, 'rb') as source:
            length = struct.unpack('!I', archive.exact(source, 4))[0]
            archive.require(0 < length <= 32768)
            bootstrap = archive.strict_json(archive.exact(source, length))
            archive.require(not source.read(1))
        from .windows_state_owner import serve as windows_serve
        raise SystemExit(windows_serve(bootstrap, int(sys.argv[3])))
    if len(sys.argv) != 3 or sys.argv[1] != '--serve' or not sys.argv[2].isdecimal():
        raise SystemExit(2)
    raise SystemExit(serve(int(sys.argv[2])))
