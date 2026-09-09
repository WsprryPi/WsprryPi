#!/usr/bin/env python3
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('binary', Path(__file__).resolve().parents[1] / 'precompiled_binary.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class BinaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.binary = self.root / 'wsprrypi'
        self.data = bytearray(64)
        self.data[:6] = b'\x7fELF\x01\x01'
        struct.pack_into('<HH', self.data, 16, 3, 40)
        struct.pack_into('<I', self.data, 36, 0x400)
        self.binary.write_bytes(self.data)

    def test_standalone(self):
        m.elf_header(self.binary, 'armv6')
        self.assertEqual(list(self.root.iterdir()), [self.binary])

    def test_wrong_architecture(self):
        with self.assertRaises(ValueError):
            m.elf_header(self.binary, 'aarch64')

    def test_wrong_float_abi(self):
        struct.pack_into('<I', self.data, 36, 0)
        self.binary.write_bytes(self.data)
        with self.assertRaises(ValueError):
            m.elf_header(self.binary, 'armv6')

    def test_truncated(self):
        self.binary.write_bytes(b'\x7fELF')
        with self.assertRaises(ValueError):
            m.elf_header(self.binary, 'armv6')

    def test_host_uses_userspace(self):
        with patch.object(m, 'os_release', return_value={'VERSION_CODENAME': 'bookworm'}), patch.object(m, 'output', return_value='armhf\n'):
            self.assertEqual(m.host_target(), ('armv6', 'bookworm'))

    def test_unsupported_host(self):
        with patch.object(m, 'os_release', return_value={'VERSION_CODENAME': 'bullseye'}), patch.object(m, 'output', return_value='armhf\n'):
            with self.assertRaises(ValueError):
                m.host_target()

    def test_reject_armv7(self):
        with patch.object(m, 'output', return_value='Tag_CPU_arch: v7\nTag_FP_arch: VFPv3\nTag_ABI_VFP_args: VFP registers\n'):
            with self.assertRaises(ValueError):
                m.inspect(self.binary, 'armv6', 'bookworm')

    def inspect_libraries(self, cpu, release, libraries):
        data = self.data.copy()
        if cpu == 'aarch64':
            data[4] = 2
            struct.pack_into('<H', data, 18, 183)
        self.binary.write_bytes(data)
        loader = 'armhf' if cpu == 'armv6' else 'aarch64'
        responses = [f'[Requesting program interpreter: /lib/ld-linux-{loader}.so.{3 if cpu == "armv6" else 1}]',
                     '\n'.join(f'(NEEDED) Shared library: [{name}]' for name in libraries)]
        if cpu == 'armv6':
            responses.insert(0, 'Tag_CPU_arch: v6\nTag_FP_arch: VFPv2\nTag_ABI_VFP_args: VFP registers\n')
        with patch.object(m, 'output', side_effect=responses):
            return m.inspect(self.binary, cpu, release)

    def test_current_release_libraries(self):
        for cpu in ('armv6', 'aarch64'):
            for release in ('bookworm', 'trixie'):
                with self.subTest(cpu=cpu, release=release):
                    gpiod = '1' if release == 'bookworm' else '2'
                    libraries = ['libatomic.so.1', f'libgpiodcxx.so.{gpiod}',
                                 'libssl.so.3', 'libcrypto.so.3', 'libsystemd.so.0',
                                 'libstdc++.so.6', 'libm.so.6', 'libgcc_s.so.1', 'libc.so.6']
                    expected = ['libatomic1', 'libc6', 'libgcc-s1', 'libstdc++6', 'libsystemd0',
                                'libgpiod2' if release == 'bookworm' else 'libgpiod3',
                                'libssl3' if release == 'bookworm' else 'libssl3t64']
                    self.assertEqual(self.inspect_libraries(cpu, release, libraries), sorted(expected))

    def test_unknown_library_still_rejected(self):
        for release in ('bookworm', 'trixie'):
            with self.subTest(release=release), self.assertRaises(ValueError) as error:
                self.inspect_libraries('armv6', release, ['libcrypto.so.3', 'libunknown.so.1'])
            self.assertEqual(str(error.exception), f"unsupported libraries for {release}: ['libunknown.so.1']")

    def test_missing_libraries_still_rejected(self):
        with self.assertRaisesRegex(ValueError, 'no required shared libraries'):
            self.inspect_libraries('armv6', 'bookworm', [])

    def test_missing_runtime_symbols(self):
        import subprocess
        argv = ['helper', 'check', '--binary', str(self.binary), '--full', '--runtime-user', 'pi', '--field', 'version']
        result = subprocess.CompletedProcess([], 0, "GLIBC_2.38 => not found", "")
        with patch.object(m, 'host_target', return_value=('armv6', 'bookworm')), patch.object(m, 'inspect'), patch.object(m.subprocess, 'run', return_value=result) as run, patch.object(sys, 'argv', argv):
            with self.assertRaises(SystemExit) as error:
                m.main()
            self.assertEqual(error.exception.code, 1)
            self.assertEqual(run.call_count, 1)

    def test_actual_binary_version(self):
        import contextlib
        import subprocess
        argv = ['helper', 'check', '--binary', str(self.binary), '--full', '--runtime-user', 'pi', '--field', 'version']
        results = [subprocess.CompletedProcess([], 0, "libc.so.6 => /lib/libc.so.6", ""), subprocess.CompletedProcess([], 0, "WsprryPi version 3.2.0-devel+different (devel).", "")]
        stream = io.StringIO()
        with patch.object(m, 'host_target', return_value=('armv6', 'bookworm')), patch.object(m, 'inspect'), patch.object(m.subprocess, 'run', side_effect=results), patch.object(sys, 'argv', argv), contextlib.redirect_stdout(stream):
            m.main()
        self.assertEqual(stream.getvalue().strip(), '3.2.0-devel+different')

    def test_download_size_bound(self):
        with patch.object(m.urllib.request, 'urlopen', return_value=io.BytesIO(b'12345')):
            with self.assertRaises(ValueError):
                m.download('https://example.invalid/asset', self.root / 'download', 4)

    def test_release_asset_selection(self):
        for cpu in ('armv6', 'aarch64'):
            for release in ('bookworm', 'trixie'):
                with self.subTest(cpu=cpu, release=release), patch.object(m, 'host_target', return_value=(cpu, release)), patch.object(m, 'download') as download, patch.object(sys, 'argv', ['helper', 'fetch', '--repo', 'WsprryPi/WsprryPi', '--tag', 'v3.2.0', '--directory', str(self.root)]):
                    m.main()
                    download.assert_called_once_with(f'https://github.com/WsprryPi/WsprryPi/releases/download/v3.2.0/wsprrypi-{cpu}-{release}', self.root / 'wsprrypi', m.MAX_BINARY)


if __name__ == '__main__':
    unittest.main()
