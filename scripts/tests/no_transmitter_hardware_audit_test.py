#!/usr/bin/env python3
"""Exercise the audit with synthetic trace files, without hardware or builds."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
AUDIT = ROOT / "scripts/ci/assert-no-transmitter-hardware.sh"


class HardwareAccessAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-audit-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.clean = self.directory / "clean trace.strace"
        self.clean.write_text('openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY) = 3\n')

    def run_audit(self, *paths, env=None):
        return subprocess.run(["/bin/sh", str(AUDIT), *map(str, paths)],
                              text=True, capture_output=True, env=env, check=False)

    def test_clean_trace_with_spaces(self):
        result = self.run_audit(self.clean)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("No prohibited", result.stdout)

    def test_no_arguments(self):
        self.assertEqual(self.run_audit().returncode, 2)

    def test_missing_empty_directory_and_leading_space(self):
        empty = self.directory / "empty.strace"
        empty.touch()
        for trace in (self.directory / "missing", empty, self.directory,
                      " " + str(self.clean)):
            with self.subTest(trace=trace):
                result = self.run_audit(self.clean, trace)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn("No prohibited", result.stdout)

    def test_prohibited_access_in_any_trace(self):
        for target in ("/dev/gpiomem", "/dev/i2c-1", "/dev/gpiochip0",
                       "/dev/rp1-gpclk", "/sys/class/gpio/export"):
            with self.subTest(target=target):
                trace = self.directory / "hardware.strace"
                trace.write_text(f'openat(AT_FDCWD, "{target}", O_RDWR) = -1 EACCES\n')
                result = self.run_audit(self.clean, trace)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("Prohibited", result.stderr)

    def test_relative_opens_and_resolved_descriptors(self):
        attempts = (
            'openat(3</dev>, "gpiochip0", O_RDWR) = -1 EACCES',
            'openat2(AT_FDCWD</>, "dev/i2c-1", {flags=O_RDWR}, 24) = -1 ENOENT',
            'openat(AT_FDCWD</dev>, "./mem", O_RDONLY) = -1 EACCES',
            'openat2(AT_FDCWD</sys>, "class/gpio/export", {}, 24) = -1 EACCES',
            'openat(4</sys/class>, "gpio/export", O_WRONLY) = -1 EACCES',
            'openat(AT_FDCWD</sys/kernel>, "debug", O_RDONLY) = -1 EACCES',
            'openat(AT_FDCWD</tmp>, "device-alias", O_RDONLY) = 3</dev/vcio>',
            'creat("/dev/gpiomem4", 0600) = -1 EACCES',
        )
        trace = self.directory / "hardware.strace"
        for attempt in attempts:
            with self.subTest(attempt=attempt):
                trace.write_text(attempt + "\n")
                result = self.run_audit(self.clean, trace)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("Prohibited", result.stderr)

    def test_relative_non_hardware_paths(self):
        trace = self.directory / "ordinary.strace"
        trace.write_text('openat(3</dev>, "null", O_RDWR) = 4</dev/null>\n'
                         'openat(AT_FDCWD</tmp>, "gpiochip0", O_RDONLY) = -1 ENOENT\n')
        self.assertEqual(self.run_audit(trace).returncode, 0)

    def test_missing_trace_does_not_hide_a_prohibited_access(self):
        trace = self.directory / "hardware.strace"
        trace.write_text('openat(3</dev>, "gpiochip0", O_RDWR) = -1 EACCES\n')
        result = self.run_audit(self.directory / "missing", trace)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Missing", result.stderr)
        self.assertIn("Prohibited", result.stderr)

    def test_grep_read_error_is_not_success(self):
        # A deterministic grep error also works when the test runs as root;
        # chmod-based unreadability tests cannot establish that case there.
        binary = self.directory / "bin"
        binary.mkdir()
        grep = binary / "grep"
        grep.write_text("#!/bin/sh\nexit 2\n")
        grep.chmod(0o755)
        result = self.run_audit(self.clean, env={**os.environ, "PATH": str(binary)})
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("Unable to inspect", result.stderr)
        self.assertNotIn("No prohibited", result.stdout)


if __name__ == "__main__":
    unittest.main()
