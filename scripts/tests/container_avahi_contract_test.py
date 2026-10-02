#!/usr/bin/env python3
"""Exercise the real artifact script with hardware-free command fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
TARGETS = ('armv6-bookworm', 'armv6-trixie', 'aarch64-bookworm', 'aarch64-trixie')
STUB = r'''
import os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
if name == 'pkg-config':
    sys.exit(1 if os.environ.get('NO_AVAHI') else 0)
if name == 'git':
    sys.exit(0)
if name == 'dpkg':
    print(os.environ['ARCH'])
elif name == 'make':
    pathlib.Path('compiled').touch()
    p = pathlib.Path('src/build/bin/wsprrypi')
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('fixture executable - never execute')
elif name == 'readelf':
    if '--dynamic' in args:
        missing = os.environ.get('MISSING', '')
        for library in ('libavahi-client.so.3', 'libavahi-common.so.3'):
            if os.environ.get('SONAME_ONLY'):
                print(' 0x0000000e (SONAME) Library soname: [' + library + ']')
            elif missing != library:
                print(' 0x00000001 (NEEDED) Shared library: [' + library + ']')
    elif '--file-header' in args:
        print('Class: ELF32\nMachine: ARM' if os.environ['ARCH'] == 'armhf'
              else 'Class: ELF64\nMachine: AArch64')
    elif '--arch-specific' in args:
        print('Tag_CPU_arch: v6\nTag_FP_arch: VFPv2\nTag_ABI_VFP_args: VFP registers')
elif name == 'ldd':
    # Always report both as resolved: transitive resolution must not satisfy NEEDED.
    print('libavahi-client.so.3 => /lib/libavahi-client.so.3\n'
          'libavahi-common.so.3 => /lib/libavahi-common.so.3')
elif name == 'file':
    print('ELF fixture')
elif name == 'dpkg-query':
    print('libavahi-client-dev\tfixture')
elif name == 'sha256sum':
    import hashlib
    print(hashlib.sha256(pathlib.Path(args[0]).read_bytes()).hexdigest() + '  ' + args[0])
'''


class ContainerAvahiContractTest(unittest.TestCase):
    def exercise(self, target, *, no_avahi=False, missing='', soname_only=False):
        cpu, release = target.split('-')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            commands = root / 'commands'
            commands.mkdir()
            for name in ('pkg-config', 'git', 'dpkg', 'make', 'readelf', 'ldd',
                         'file', 'dpkg-query', 'sha256sum'):
                p = commands / name
                p.write_text('#!' + sys.executable + '\n' + STUB)
                p.chmod(0o755)
            os_release = root / 'os-release'
            os_release.write_text(f'ID={"raspbian" if cpu == "armv6" else "debian"}\nVERSION_CODENAME={release}\n')
            script = root / 'build-artifact.sh'
            script.write_text((REPO / 'containers/build-artifact.sh').read_text()
                              .replace('/etc/os-release', str(os_release))
                              .replace('/artifact', str(root / 'artifact')))
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
                       ARCH='armhf' if cpu == 'armv6' else 'arm64', MISSING=missing)
            env.pop('NO_AVAHI', None)
            env.pop('SONAME_ONLY', None)
            if soname_only:
                env['SONAME_ONLY'] = '1'
            if no_avahi:
                env['NO_AVAHI'] = '1'
            result = subprocess.run(['sh', str(script), cpu, release], cwd=root,
                                    env=env, capture_output=True, text=True)
            if no_avahi:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('requires Avahi development files', result.stderr)
                self.assertFalse((root / 'compiled').exists())
            elif missing or soname_only:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('must directly link ' + (missing or 'libavahi-client.so.3'), result.stderr)
                self.assertFalse((root / 'artifact/SHA256SUMS').exists())
            else:
                self.assertEqual(result.returncode, 0, result.stderr)
                evidence = (root / 'artifact/elf-dynamic.txt').read_text()
                for library in ('libavahi-client.so.3', 'libavahi-common.so.3'):
                    self.assertIn('[' + library + ']', evidence)
                self.assertTrue((root / 'artifact/SHA256SUMS').is_file())

    def test_supported_recipes_include_avahi(self):
        for target in TARGETS:
            with self.subTest(target=target):
                self.assertIn('libavahi-client-dev',
                              (REPO / 'containers' / target / 'Dockerfile').read_text())

    def test_success_all_targets(self):
        for target in TARGETS:
            with self.subTest(target=target):
                self.exercise(target)

    def test_missing_dependency_fails_before_compile(self):
        self.exercise('armv6-bookworm', no_avahi=True)

    def test_non_needed_mentions_do_not_satisfy_contract(self):
        self.exercise('aarch64-trixie', soname_only=True)

    def test_each_missing_direct_library_fails(self):
        for library in ('libavahi-client.so.3', 'libavahi-common.so.3'):
            with self.subTest(library=library):
                self.exercise('aarch64-trixie', missing=library)


if __name__ == '__main__':
    unittest.main()
