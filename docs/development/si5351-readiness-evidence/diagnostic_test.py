"""Deterministic hardware-free checks for the reviewed bus-only diagnostic."""
import contextlib
import importlib.util
import io
import json
import pathlib
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('diagnostic', pathlib.Path(__file__).with_name('wspr1-inhibited-status.py'))
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)


class DiagnosticTests(unittest.TestCase):
    def run_case(self, fault=None):
        selected = None
        closes = []
        reads = {0: 0, 3: 0}

        def write(_fd, data):
            nonlocal selected
            if len(data) == 2:
                self.assertEqual(data, bytes([3, 255]), 'only all-output inhibition writes allowed')
                if fault == 'inhibit_write':
                    raise OSError(5, 'inhibit failed')
                return 2
            self.assertEqual(len(data), 1, 'no clock/reference/drive programming')
            self.assertIn(data[0], (0, 3))
            selected = data[0]
            return 0 if fault == 'short_select' and selected == 0 else 1

        def read(_fd, size):
            self.assertEqual(size, 1)
            reads[selected] += 1
            if selected == 3:
                if fault == 'sysinit_and_cleanup' and reads[3] == 2:
                    raise OSError(5, 'cleanup read failed')
                return bytes([254 if fault == 'not_inhibited' else 255])
            if fault == 'failed_read':
                raise OSError(5, 'status read failed')
            if fault == 'short_read':
                return b''
            return bytes([193 if fault in ('sysinit', 'sysinit_and_cleanup') else 65])

        class Lib:
            def ioctl(self, _fd, request, transfer):
                if fault == 'short_combined':
                    return 1
                transfer._obj.msgs[1].buf[0] = 65
                return 2

        def select_slave(*args):
            if fault == 'slave_select':
                raise OSError(16, 'address busy')

        output = io.StringIO()
        with contextlib.ExitStack() as stack:
            for target, value in [
                ('socket.gethostname', lambda: 'wspr1'), ('os.open', lambda *a: 42),
                ('os.write', write), ('os.read', read), ('os.close', lambda fd: closes.append(fd)),
                ('fcntl.ioctl', select_slave), ('ctypes.CDLL', lambda *a, **k: Lib()),
                ('signal.signal', lambda *a: None), ('time.sleep', lambda *a: None),
            ]:
                stack.enter_context(patch.object(getattr(diagnostic, target.split('.')[0]), target.split('.')[1], value))
            with contextlib.redirect_stdout(output):
                code = diagnostic.main()
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(closes, [42])
        self.assertEqual(code == 0, fault is None)
        if fault:
            self.assertIsNotNone(records[-1]['first_fault'])
        if fault == 'slave_select':
            self.assertEqual(reads[3], 0, 'do not read from an unselected slave during cleanup')
        if fault == 'sysinit_and_cleanup':
            self.assertIn('SYS_INIT', records[-1]['first_fault']['error'])
            self.assertIn('cleanup read failed', records[-1]['cleanup_error'])
        if fault is None:
            self.assertEqual(records[-1]['counts'], {'split': {'65': 500}, 'combined': {'65': 500}})
        return records

    def test_success_and_fault_receipts(self):
        for fault in (None, 'sysinit', 'sysinit_and_cleanup', 'failed_read', 'short_read',
                      'short_select', 'short_combined', 'not_inhibited', 'slave_select', 'inhibit_write'):
            with self.subTest(fault=fault):
                self.run_case(fault)


if __name__ == '__main__':
    unittest.main()
