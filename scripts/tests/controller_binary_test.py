#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check real ELF artifacts through the offline controller executable verifier."""

import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ARCHITECTURES = {'x86_64': 'amd64', 'aarch64': 'arm64'}
REVISION = '7' * 40
VERSION = '4.0.0-rc.1+7777777'
INPUTS = 'd' * 64


@unittest.skipUnless(sys.platform == 'linux' and platform.machine() in ARCHITECTURES,
                     'requires a Linux amd64 or arm64 ELF toolchain')
class ControllerBinaryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='controller-binary-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        cls.architecture = ARCHITECTURES[platform.machine()]
        cls.source = cls.directory / 'main.cpp'
        # Artifact inspection must succeed without executing even this harmless fixture.
        cls.source.write_text('int main() { return 123; }\n')
        (cls.directory / 'build_metadata.hpp').write_text(
            f'#define MAKE_COMMIT "{REVISION}"\n#define MAKE_TAG "{VERSION}"\n#define MAKE_DIRTY "true"\n'
            f'#define MAKE_CONTROLLER_INPUTS "{INPUTS}"\n'
        )
        cls.controller = cls.build('controller')
        cls.transmitter = cls.build('transmitter', profile='transmitter')
        cls.runpath = cls.build('runpath', '-Wl,-rpath,/tmp')

    @classmethod
    def build(cls, name, *linker_options, profile='controller'):
        subprocess.run(
            [sys.executable, str(ROOT / 'scripts/generate_backend_capabilities.py'),
             '--profile', profile, '--backends', '' if profile == 'controller' else 'simulated',
             '--ancillary-gpio', '0', '--output', str(cls.directory / 'backend_capabilities.hpp')],
            check=True,
        )
        executable = cls.directory / name
        subprocess.run(
            [*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++20', '-O2',
             '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-Wl,--strip-all',
             f'-I{cls.directory}', f'-I{ROOT / "src/LCBLog/src"}',
             str(ROOT / 'src/version.cpp'), str(cls.source), *linker_options, '-o', str(executable)],
            check=True,
        )
        return executable

    def check(self, binary, architecture=None, checksum=None):
        if checksum is None:
            checksum = hashlib.sha256(binary.read_bytes()).hexdigest()
        return subprocess.run(
            [sys.executable, '-B', str(ROOT / 'scripts/controller_binary.py'), str(binary),
             '--architecture', architecture or self.architecture, '--sha256', checksum],
            capture_output=True, text=True, check=False, timeout=30,
        )

    def assert_refused(self, binary, message, **arguments):
        result = self.check(binary, **arguments)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn(message, result.stderr)

    def test_stripped_controller_identity_and_library_inventory(self):
        self.controller.chmod(0o644)
        result = self.check(self.controller)
        self.assertEqual(result.returncode, 0, result)
        report = json.loads(result.stdout)
        self.assertEqual(report['role'], 'controller')
        self.assertEqual(report['platform'], 'ubuntu-26.04')
        self.assertEqual(report['source_commit'], REVISION)
        self.assertEqual(report['application_version'], VERSION)
        self.assertTrue(report['tracked_source_dirty'])
        self.assertEqual(report['controller_inputs_sha256'], INPUTS)
        self.assertEqual(report['architecture'], self.architecture)
        self.assertIn('libc6', report['runtime_library_packages'])

    def test_refuses_checksum_architecture_or_wrong_role(self):
        self.assert_refused(self.controller, 'checksum mismatch', checksum='0' * 64)
        other = 'amd64' if self.architecture == 'arm64' else 'arm64'
        self.assert_refused(self.controller, 'wrong ELF machine', architecture=other)
        self.assert_refused(self.transmitter, 'invalid or missing section')

    def test_refuses_dynamic_loader_and_library_search_overrides(self):
        self.assert_refused(self.runpath, 'alternate library search')
        executable = self.directory / 'wrong-loader'
        loader = (b'/lib64/ld-linux-x86-64.so.2' if self.architecture == 'amd64'
                  else b'/lib/ld-linux-aarch64.so.1')
        self.assertIn(loader, self.controller.read_bytes())
        executable.write_bytes(self.controller.read_bytes().replace(loader, loader[:-1] + b'9'))
        self.assert_refused(executable, 'Unexpected ELF interpreter')

    def test_refuses_future_glibc_requirement(self):
        executable = self.directory / 'new-glibc'
        original = self.controller.read_bytes()
        self.assertIn(b'GLIBC_2.34\0', original)
        executable.write_bytes(original.replace(b'GLIBC_2.34\0', b'GLIBC_2.99\0'))
        self.assert_refused(executable, 'outside the Ubuntu 26.04 baseline')

    def test_refuses_aliases_special_files_and_group_writable_artifacts(self):
        linked = self.directory / 'symlink'
        linked.symlink_to(self.controller)
        self.assert_refused(linked, 'without aliases')
        copied = self.directory / 'original'
        copied.write_bytes(self.controller.read_bytes())
        os.link(copied, self.directory / 'hardlink')
        self.assert_refused(copied, 'without aliases')
        fifo = self.directory / 'fifo'
        os.mkfifo(fifo)
        self.assert_refused(fifo, 'bounded regular executable', checksum='0' * 64)
        writable = self.directory / 'group-writable'
        writable.write_bytes(self.controller.read_bytes())
        writable.chmod(0o664)
        self.assert_refused(writable, 'shared write access')


if __name__ == '__main__':
    unittest.main()
