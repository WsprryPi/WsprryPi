# SPDX-License-Identifier: MIT
"""Bounded framing for selector-driven, unbuffered subprocess stdout pipes."""
import os
import re
import selectors
import time


class PipeLines:
    """Drain every complete line from one OS read; retain only a bounded tail."""
    limit = 4096

    def __init__(self, stream):
        self.stream = stream
        self.pending = b''
        self.eof = False

    def read_available(self):
        data = os.read(self.stream.fileno(), self.limit)
        if not data:
            self.eof = True
            if self.pending:
                raise RuntimeError('Incomplete subprocess control line')
            return []
        parts = (self.pending + data).split(b'\n')
        self.pending = parts.pop()
        if len(self.pending) > self.limit or any(len(part) > self.limit for part in parts):
            raise RuntimeError('Subprocess control line exceeds 4096 bytes')
        return [(part + b'\n').decode('utf-8') for part in parts]


def wait_ready(process, timeout=15):
    """Wait for one complete readiness line without blocking on a partial line."""
    reader = PipeLines(process.stdout)
    started = time.monotonic()
    deadline = started + timeout
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        while not reader.eof:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not selector.select(remaining):
                raise RuntimeError(f'Pico loopback startup deadline: elapsed={time.monotonic() - started:.3f}s, budget={timeout}s, child_exit={process.poll()}, pending_bytes={len(reader.pending)}')
            for line in reader.read_available():
                if re.fullmatch(r'READY ([1-9][0-9]{0,4})\n', line) and int(line.split()[1]) <= 65535:
                    return
                raise RuntimeError(f'Pico loopback host server did not start: elapsed={time.monotonic() - started:.3f}s, child_exit={process.poll()}, complete_line_bytes={len(line)}')
    raise RuntimeError(f'Pico loopback host server closed stdout before readiness: elapsed={time.monotonic() - started:.3f}s, child_exit={process.poll()}, eof={reader.eof}')
