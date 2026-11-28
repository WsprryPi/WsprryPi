#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise native controller image, proxy, and archive admission without services."""

import json
import os
from pathlib import Path
import struct
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'src')]
import controller_apache as apache
import controller_bundle as bundle
import controller_native_binary as binary
from controller_source import fingerprint
from application_recovery import proxy


def command(code, value):
    string = value.encode() + b'\0'
    padded = (12 + len(string) + 7) & ~7
    return struct.pack('<III', code, padded, 12) + string.ljust(padded - 12, b'\0')


def macho(machine=0x0100000C, target=27 << 16, dependencies=(), kind=2, search=()):
    commands = [struct.pack('<6I', 0x32, 24, 1, target, 27 << 16, 0)]
    commands.extend(command(0xC, item) for item in dependencies)
    commands.extend(command(0x8000001C, item) for item in search)
    return struct.pack('<8I', 0xFEEDFACF, machine, 0, kind, len(commands), sum(map(len, commands)), 0, 0) + b''.join(commands)


def pe(library='kernel32.dll', delay=False, machine=0x8664, terminate=True):
    image = bytearray(0x600)
    image[:2] = b'MZ'
    struct.pack_into('<I', image, 0x3C, 0x80)
    struct.pack_into('<4sHHIIIHH', image, 0x80, b'PE\0\0', machine, 1, 0, 0, 0, 240, 2)
    optional = 0x98
    struct.pack_into('<H', image, optional, 0x20B)
    struct.pack_into('<I', image, optional + 108, 16)
    index, width = (13, 32) if delay else (1, 20)
    struct.pack_into('<II', image, optional + 112 + index * 8, 0x1000, width * (2 if terminate else 1))
    struct.pack_into('<8sIIII', image, optional + 240, b'.rdata', 0x400, 0x1000, 0x400, 0x200)
    if delay:
        struct.pack_into('<II', image, 0x200, 1, 0x1080)
    else:
        struct.pack_into('<I', image, 0x20C, 0x1080)
    encoded = library.encode() + b'\0'
    image[0x280:0x280 + len(encoded)] = encoded
    return bytes(image)


class NativeBinaryTests(unittest.TestCase):
    def test_source_fingerprint_binds_local_build_and_browser_inputs(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            for name in ('src/example.cpp', 'WsprryPi-UI/data/example.js'):
                path = root / name
                path.parent.mkdir(parents=True)
                path.write_bytes(b'original\n')
            names = b'src/example.cpp\0WsprryPi-UI/data/example.js\0'
            with patch('controller_source.subprocess.check_output', return_value=names):
                original = fingerprint(root)
                (root / 'src/example.cpp').write_bytes(b'edited\n')
                self.assertNotEqual(fingerprint(root), original)
                (root / 'src/example.cpp').write_bytes(b'original\n')
                (root / 'WsprryPi-UI/data/example.js').write_bytes(b'edited\n')
                self.assertNotEqual(fingerprint(root), original)

    def test_macos_uses_each_executable_origin_and_rejects_global_libraries(self):
        cpu = 0x0100000C
        image = macho(dependencies=('@executable_path/../lib/libpython.dylib', '/usr/lib/libSystem.B.dylib'))
        allowed = {'runtime/lib/libpython.dylib'}
        self.assertEqual(len(binary.mach(image, cpu, 'macos-27', 'runtime/bin/python3', allowed, executable=True)), 2)
        with self.assertRaises(ValueError):
            binary.mach(image, cpu, 'macos-27', 'runtime/bin/python3', {'lib/libpython.dylib'}, executable=True)
        with self.assertRaises(ValueError):
            binary.mach(macho(target=(27 << 16) + (1 << 8)), cpu, 'macos-27', 'runtime/bin/python3', allowed)
        for invalid in (macho(target=26 << 16), macho(kind=8),
                        macho(dependencies=('/opt/homebrew/lib/libcrypto.dylib',)),
                        macho(dependencies=('@loader_path/../../../libbad.dylib',))):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    binary.mach(invalid, cpu, 'macos-27', 'bin/wsprrypi', allowed, main=True)

    def test_macos_rpath_is_confined_and_platform_specific(self):
        image = macho(machine=0x01000007, target=26 << 16, dependencies=('@rpath/libapr.dylib',),
                      search=('@loader_path/../lib',))
        self.assertEqual(binary.mach(image, 0x01000007, 'macos-26', 'runtime/bin/httpd',
                                    {'runtime/lib/libapr.dylib'}, main=True), ['@rpath/libapr.dylib'])
        with self.assertRaises(ValueError):
            binary.mach(image, 0x0100000C, 'macos-26', 'runtime/bin/httpd', {'runtime/lib/libapr.dylib'})

    def test_windows_resolves_only_the_role_loader_search_paths(self):
        cpu = binary.PROFILES['windows-26300']['amd64']
        self.assertEqual(binary.pe(pe(), cpu, 'bin/wsprrypi.exe', {}, executable=True), ['kernel32.dll'])
        self.assertEqual(binary.pe(pe('libhttpd.dll', delay=True), cpu, 'runtime/apache/modules/mod_alias.so',
                                  {'runtime/apache/bin/libhttpd.dll'}), ['libhttpd.dll'])
        for image, files in ((pe('untrusted.dll'), {'runtime/php/untrusted.dll'}),
                             (pe('..\\native.dll'), {'bin/native.dll'}),
                             (pe('kernel32.dll', terminate=False), {}),
                             (pe('kernel32.dll', machine=0xAA64), {})):
            with self.assertRaises(ValueError):
                binary.pe(image, cpu, 'bin/wsprrypi.exe', files, executable=True)

    def test_tar_case_aliases_and_dos_device_names_refuse(self):
        import io
        for names in (('runtime/Bin/example', 'runtime/bin/other'), ('runtime/COM0.dll',),
                      ('file', 'file/child'), ('directory/file', 'directory')):
            with tempfile.TemporaryDirectory() as work:
                archive = io.BytesIO()
                with tarfile.open(fileobj=archive, mode='w', format=tarfile.USTAR_FORMAT) as writer:
                    for name in names:
                        item = tarfile.TarInfo(name)
                        item.size = 1
                        writer.addfile(item, io.BytesIO(b'x'))
                archive.seek(0)
                with tarfile.open(fileobj=archive) as reader, self.assertRaises(ValueError):
                    bundle.unpack_members(reader, Path(work))


@unittest.skipIf(sys.platform == 'win32', 'Uses POSIX private-file fixtures')
class NativeProxyTests(unittest.TestCase):
    def test_preserves_public_origin_and_replays_interrupted_publication(self):
        import subprocess
        with tempfile.TemporaryDirectory() as work:
            job = Path(work).resolve()
            site = job / 'proxy.conf'
            original = apache.proxy_config('/read-only/controller/ui', 31415, 31416).encode()
            self.assertIn(b'ProxyPreserveHost On', original)
            self.assertIn(b'ProxyAddHeaders Off', original)
            self.assertIn(b'/api/pair/v1', original)
            site.write_bytes(original)
            commands = (['/fixed/config-test'], ['/fixed/activate'])
            arguments = (job, 31450, 31451, site, proxy.read_site, proxy.replace_site, commands, {})
            with patch.object(proxy.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)):
                with self.assertRaises(proxy.archive.RecoveryError):
                    proxy.publish_native(*arguments)
            recorded = json.loads((job / 'native-proxy-publication.json').read_text())
            self.assertFalse(recorded['complete'])
            self.assertEqual(proxy.update_ports(original, 31450, 31451)[0], site.read_bytes())
            with patch.object(proxy.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
                with patch.object(proxy.http.client, 'HTTPConnection') as connection:
                    response = connection.return_value.getresponse.return_value
                    response.status = 200
                    response.read.return_value = b'{"host_status":{"controller_ready":true}}'
                    proxy.publish_native(*arguments)
                    self.assertEqual(run.call_count, 2)
                    proxy.publish_native(*arguments)
                    self.assertEqual(run.call_count, 2)
            self.assertTrue(json.loads((job / 'native-proxy-publication.json').read_text())['complete'])

    def test_rejects_multiple_proxy_sections(self):
        value = apache.proxy_config('/read-only/controller/ui', 31415, 31416).encode()
        with self.assertRaises(proxy.archive.RecoveryError):
            proxy.update_ports(value + value, 31450, 31451)


@unittest.skipUnless(sys.platform == 'darwin', 'Uses macOS installation paths')
class MacActivationTests(unittest.TestCase):
    def test_bootout_waits_for_the_job_and_its_process(self):
        import controller_macos as mac

        with (patch.object(mac, 'observed_job', side_effect=['pid = 231\n', 'state = stopping', None, None]),
              patch.object(mac, 'launchctl') as launchctl, patch.object(mac.time, 'sleep') as sleep,
              patch.object(mac.os, 'kill', side_effect=[None, ProcessLookupError]) as probe):
            mac.bootout(mac.MAC_LABEL)
        launchctl.assert_called_once_with('bootout', 'system/' + mac.MAC_LABEL)
        self.assertEqual([call.args for call in probe.call_args_list], [(231, 0), (231, 0)])
        self.assertEqual(sleep.call_count, 2)

    def test_launch_jobs_follow_the_checked_program_and_retire_on_failure(self):
        import controller_macos as mac

        with tempfile.TemporaryDirectory() as work:
            root = Path(work).resolve()
            jobs = {root / (label + '.plist'): b'job' for label in mac.START_ORDER}
            for path in jobs:
                path.write_bytes(b'previous job')
            program = root / 'Program'
            program.symlink_to(root / 'old')
            state = root / 'state'
            state.mkdir()
            owners = dict.fromkeys(mac.ACCOUNT_NAMES, object())
            manifest = {'files': {'bin/recovery-age': bundle.digest(b'helper')}}

            def run(_program, operation, *_args, **_kwargs):
                if operation == '--storage-format':
                    return b'application-state/1'
                if operation == '--export-recovery-age-helper':
                    return b'helper'
                if operation == '--controller-only':
                    self.assertFalse(any(path.exists() for path in jobs))
                return b''

            def select(path, target):
                self.assertFalse(any(job.exists() for job in jobs))
                path.unlink()
                path.symlink_to(target)

            def bootstrap(_label):
                self.assertEqual(program.readlink(), root / 'selected')
                self.assertTrue(all(path.read_bytes() == data for path, data in jobs.items()))

            with (patch.multiple(mac, LAUNCHD=root, PROGRAM=str(program), BACKEND_STATE=state, RELEASES=root),
                  patch.object(mac, 'check_managed'), patch.object(mac, 'provision_accounts', return_value=owners),
                  patch.object(mac, 'provision_directories'), patch.object(mac, 'stage_slot'),
                  patch.object(mac, 'run', side_effect=run), patch.object(mac, 'quiesce'),
                  patch.object(mac, 'listener_configuration', return_value={'web_port': 1, 'socket_port': 2, 'role': 'controller'}),
                  patch.object(mac, 'files', return_value=jobs), patch.object(mac, 'atomic', side_effect=Path.write_bytes),
                  patch.object(mac, 'symlink', side_effect=select), patch.object(mac, 'bootstrap', side_effect=bootstrap),
                  patch.object(mac, 'bootout') as bootout, patch.object(mac, 'wait_ready', side_effect=ValueError('startup failed'))):
                with self.assertRaisesRegex(ValueError, 'startup failed'):
                    mac.install(root, manifest, 'selected', 'macos-27', {'slots': ['selected']})
            self.assertFalse(any(path.exists() for path in jobs))
            self.assertEqual([call.args[0] for call in bootout.call_args_list], list(reversed(mac.START_ORDER)))


@unittest.skipUnless(sys.platform == 'win32', 'Requires native Windows file observations')
class WindowsProxyTests(unittest.TestCase):
    def test_reads_stable_single_link_configuration(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / 'proxy.conf'
            before = b'ProxyRequests Off\n'
            path.write_bytes(before)
            os.utime(path, ns=(1_791_000_000_000_000_000, 1_791_000_000_000_000_000))
            self.assertEqual(proxy.windows_site(path), (before, None))
            os.link(path, Path(work) / 'second-name.conf')
            with self.assertRaises(proxy.archive.RecoveryError):
                proxy.windows_site(path)


if __name__ == '__main__':
    unittest.main()
