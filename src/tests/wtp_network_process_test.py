#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise real subprocess coalescing, fragmented control lines and bounds."""
import os
import selectors
import subprocess
import sys
import time
import unittest
from network_process import PipeLines, wait_ready


class ProcessLinesTest(unittest.TestCase):
    def test_coalesced_notice_and_restart_cannot_hide_in_text_buffer(self):
        script = """
import os, sys
os.write(1, b'address verification passed\\nRESTART boot\\n')
assert sys.stdin.buffer.readline() == b'READY\\n'
os.write(1, b'boot verification passed\\nRESTART device\\n')
assert sys.stdin.buffer.readline() == b'READY\\n'
os.write(1, b'DONE\\n')
"""
        child = subprocess.Popen([sys.executable, '-u', '-c', script],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, bufsize=0)
        observed = []
        reader = PipeLines(child.stdout)
        deadline = time.monotonic() + 5
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(child.stdout, selectors.EVENT_READ)
                while 'DONE\n' not in observed:
                    self.assertLess(time.monotonic(), deadline, 'coalesced restart command was stranded')
                    if not selector.select(.1):
                        continue
                    for line in reader.read_available():
                        observed.append(line)
                        if line.startswith('RESTART '):
                            child.stdin.write(b'READY\n')
                self.assertEqual(child.wait(timeout=2), 0)
            self.assertEqual(observed, ['address verification passed\n', 'RESTART boot\n',
                                        'boot verification passed\n', 'RESTART device\n', 'DONE\n'])
        finally:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=2)
            child.stdin.close(); child.stdout.close(); child.stderr.close()

    def test_startup_complete_line(self):
        child = subprocess.Popen([sys.executable, '-u', '-c', "print('READY 18443')"],
                                 stdout=subprocess.PIPE, bufsize=0)
        try:
            wait_ready(child, timeout=2)
            self.assertEqual(child.wait(timeout=2), 0)
        finally:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=2)
            child.stdout.close()

    def test_startup_partial_line_deadline_and_closed_stdout(self):
        for script, error in [
            ("import os, time; os.write(1, b'READY'); time.sleep(5)", 'deadline'),
            ("import os, time; os.close(1); time.sleep(5)", 'closed stdout'),
        ]:
            child = subprocess.Popen([sys.executable, '-u', '-c', script],
                                     stdout=subprocess.PIPE, bufsize=0)
            started = time.monotonic()
            try:
                with self.assertRaisesRegex(RuntimeError, error):
                    wait_ready(child, timeout=.2)
                self.assertLess(time.monotonic() - started, 2)
            finally:
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=2)
                child.stdout.close()

    def test_fragmented_and_coalesced_lines(self):
        read_fd, write_fd = os.pipe()
        with os.fdopen(read_fd, 'rb', buffering=0) as pipe:
            try:
                reader = PipeLines(pipe)
                os.write(write_fd, b'RESTART bo')
                self.assertEqual(reader.read_available(), [])
                os.write(write_fd, b'ot\nnotice\n')
                self.assertEqual(reader.read_available(), ['RESTART boot\n', 'notice\n'])
            finally:
                os.close(write_fd)

    def test_partial_eof_and_oversized_line_fail(self):
        for value, error in [(b'partial', 'Incomplete'), (b'x' * 4097, 'exceeds')]:
            read_fd, write_fd = os.pipe()
            with os.fdopen(read_fd, 'rb', buffering=0) as pipe:
                os.write(write_fd, value)
                os.close(write_fd)
                reader = PipeLines(pipe)
                reader.read_available()
                with self.assertRaisesRegex(RuntimeError, error):
                    reader.read_available()


if __name__ == '__main__':
    unittest.main()
