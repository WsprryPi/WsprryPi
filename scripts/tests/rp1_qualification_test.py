#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline bundled-evidence installation and attribution; no target access."""
from contextlib import contextmanager, redirect_stdout
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rp1_qualification as Q

ROOT = Path(__file__).resolve().parents[2]


def observed(metadata, route):
    """Synthetic provider observation, never a live inspection."""
    software = metadata['software']
    binding = {'sourceCommit': software['sourceCommit'], 'productVersion': '1.0.0',
        'uapiSha256': software['uapiSha256'], 'compatibilityIdentities': software['compatibilityIdentities'],
        'files': metadata['runtimeFiles'], 'externalFiles': {'/companion': 'e'*64},
        'kernel': 'fixture-kernel', 'consumerNoteSha256': 'a'*64, 'controllerNoteSha256': 'b'*64,
        'modules': {n: {'path': '/modules/'+n, 'installedFileSha256': d*64,
                       'buildNoteSha256': note*64, 'version': '1.0.0', 'kernel': 'fixture-kernel'}
                    for n, d, note in [('rp1_gpclk_dkms', '1', 'a'), ('rp1_route_controller', '2', 'b')]}}
    binding['artifactSetSha256'] = Q.sha(json.dumps(binding, sort_keys=True, separators=(',', ':')).encode())
    expected = {**binding['files'], **binding['externalFiles'],
                **{m['path']: m['installedFileSha256'] for m in binding['modules'].values()}}
    report = {'contract': 'rp1-gpclk-runtime-readiness-v1', 'schemaVersion': 1,
        'identities': {'installedBinding': {'status': 'valid', 'value': binding}},
        'artifacts': {p: {'status': 'exact', 'expectedSha256': h, 'actualSha256': h}
                      for p, h in expected.items()}, 'routes': {'active': route},
        'modules': {n: {'status': 'loaded', 'version': '1.0.0', 'buildNoteSha256': d*64}
                    for n, d in [('rp1_gpclk_dkms', 'a'), ('rp1_route_controller', 'b')]},
        'manager': {'idle': {'state': {'outputLifecycle': {'snapshot': {
            'route': 1 if route == 'gpio4' else 2, 'compatibility': 3}}}}},
        'safety': {'operationalReady': False, 'outputInhibited': True}}
    owner = {'channel': 'release', 'sourceCommit': software['sourceCommit'], 'tag': 'v1.0.0',
             'manifest': {'package': {'sha256': software['assets']['rp1-gpclk-dkms_1.0.0-1_all.deb']}}}
    return report, owner


class Tests(unittest.TestCase):
    def setUp(self):
        self.lock = Q.strict_json(Q.lock_path().read_bytes())
        self.assets = Q.evidence_assets(Q.bundle_path(), self.lock)
        self.metadata, _ = Q.validate_assets(self.assets, self.lock)

    @contextmanager
    def deployment(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary).resolve()
            source = base/'source'; source.mkdir()
            for name, raw in self.assets.items(): (source/name).write_bytes(raw)
            reference = base/'lock.json'; reference.write_text(json.dumps(self.lock))
            runtime = base/'runtime'
            original_read, original_write = Q.read, Q.write_atomic
            # Filesystem and ownership effects stay inside this fixture.
            with patch.object(Q, 'RUNTIME', runtime), patch.object(Q, 'OWNER_UID', os.geteuid()), \
                 patch.object(Q, 'lock_path', return_value=reference), \
                 patch.object(Q, 'bundle_path', return_value=source), \
                 patch.object(Q, 'read', side_effect=lambda p, **kw: original_read(p, **{**kw, 'trusted': False})), \
                 patch.object(Q, 'write_atomic', side_effect=lambda p, raw, mode=0o644, **kw:
                              original_write(p, raw, mode, trusted=False)), \
                 patch.object(socket, 'socket', side_effect=AssertionError('network forbidden')):
                yield runtime, source

    def test_bundle_preserves_original_release_and_independent_route_attribution(self):
        original = Q.strict_json((ROOT/'scripts/tests/fixtures/rp1-release-1.0.0.json').read_bytes())
        for key, value in original.items():
            self.assertEqual(self.lock['software'][key], value, key)
        self.assertEqual(self.metadata['metadataSource'], self.lock['metadataSource'])
        for route in ('gpio4', 'gpio20'):
            report, owner = observed(self.metadata, route)
            with patch.object(socket, 'socket', side_effect=AssertionError('network forbidden')):
                result = Q.status(self.lock, route, report, owner)
            self.assertEqual(result['status'], 'verified')
            self.assertEqual(result['scope']['frequencyHz'], 14097100)
            self.assertEqual(result['structuralCompatibility'], 'Compatible-unqualified')
            self.assertFalse(result['permissionGate'])
            self.assertFalse(result['operationalReady'])
            report['modules']['rp1_gpclk_dkms']['buildNoteSha256'] = '0'*64
            self.assertEqual(Q.status(self.lock, route, report, owner)['status'], 'unattributed')

    def test_substituted_bytes_never_execute(self):
        for name in self.assets:
            changed = dict(self.assets); changed[name] += b'wrong'
            with self.subTest(name=name), self.assertRaises(ValueError):
                Q.validate_assets(changed, self.lock)
        with self.assertRaises(ValueError): Q.strict_json('{"x":1,"x":2}')

    def test_missing_corrupt_oversized_symlink_and_fifo_inputs(self):
        report, owner = observed(self.metadata, 'gpio4')
        with self.deployment() as (runtime, source):
            self.assertEqual(Q.status(self.lock, 'gpio4', report, owner, runtime)['status'], 'missing')
            self.assertEqual(Q.status(self.lock, 'gpio4', report, owner)['status'], 'verified')
            (source/'rp1-qualification.json').write_bytes(b'{}')
            self.assertEqual(Q.status(self.lock, 'gpio4', report, owner)['status'], 'invalid')
            path = source/'large'; path.write_bytes(b'a'*20)
            with self.assertRaises(ValueError): Q.read(path, limit=10)
            link = source/'link'; link.symlink_to(path)
            with self.assertRaises(OSError): Q.read(link)
            pipe = source/'fifo'; os.mkfifo(pipe)
            with self.assertRaises(ValueError): Q.read(pipe)

    def test_install_repeat_and_remove_own_the_complete_bundle(self):
        with self.deployment() as (runtime, source):
            Q.install_runtime(self.lock); Q.install_runtime(self.lock)
            receipt = Q.strict_json((runtime/'rp1-qualification-files.json').read_bytes())
            self.assertEqual(set(receipt), Q.INSTALLED_FILES)
            for name, digest in receipt.items(): self.assertEqual(Q.sha((runtime/name).read_bytes()), digest)
            self.assertEqual(Q.evidence_assets(runtime/Q.BUNDLE_NAME, self.lock), self.assets)
            Q.remove_runtime(); Q.remove_runtime()
            self.assertEqual(list(runtime.iterdir()), [])

    def legacy_install(self, runtime):
        runtime.mkdir()
        files = {'rp1_qualification.py': b'legacy helper', 'rp1-qualification-lock.json': b'{}'}
        for name, raw in files.items(): (runtime/name).write_bytes(raw)
        (runtime/'rp1-qualification-files.json').write_text(json.dumps({n: Q.sha(b) for n, b in files.items()}))
        return files

    def test_legacy_helper_upgrade_and_removal(self):
        for upgrade in (True, False):
            with self.subTest(upgrade=upgrade), self.deployment() as (runtime, source):
                self.legacy_install(runtime)
                if upgrade:
                    Q.install_runtime(self.lock)
                    self.assertEqual(Q.evidence_assets(runtime/Q.BUNDLE_NAME, self.lock), self.assets)
                Q.remove_runtime()
                self.assertEqual(list(runtime.iterdir()), [])

    def test_interrupted_upgrade_is_retryable(self):
        with self.deployment() as (runtime, source):
            legacy = self.legacy_install(runtime)
            writer = Q.write_atomic
            calls = []
            def interrupted(path, *args, **kwargs):
                calls.append(path)
                if len(calls) == 3: raise OSError('fixture interruption')
                return writer(path, *args, **kwargs)
            with patch.object(Q, 'write_atomic', side_effect=interrupted), self.assertRaises(OSError):
                Q.install_runtime(self.lock)
            self.assertEqual((runtime/'rp1_qualification.py').read_bytes(), legacy['rp1_qualification.py'])
            Q.install_runtime(self.lock)
            self.assertEqual(Q.evidence_assets(runtime/Q.BUNDLE_NAME, self.lock), self.assets)
            Q.remove_runtime()

    def test_foreign_installed_files_and_invalid_source_are_preserved(self):
        with self.deployment() as (runtime, source):
            legacy = self.legacy_install(runtime)
            (source/'rp1-qualification.json').write_bytes(b'corrupt source')
            with self.assertRaises(ValueError): Q.install_runtime(self.lock)
            self.assertEqual((runtime/'rp1_qualification.py').read_bytes(), legacy['rp1_qualification.py'])
        for change in ('modified', 'extra', 'symlink'):
            with self.subTest(change=change), self.deployment() as (runtime, source):
                Q.install_runtime(self.lock)
                bundle = runtime/Q.BUNDLE_NAME
                if change == 'modified': (bundle/'rp1-qualification.json').write_bytes(b'foreign')
                if change == 'extra': (bundle/'foreign').write_bytes(b'foreign')
                if change == 'symlink':
                    (bundle/'rp1-qualification.json').unlink()
                    (bundle/'rp1-qualification.json').symlink_to(source/'rp1-qualification.json')
                before = (runtime/'rp1_qualification.py').read_bytes()
                with self.assertRaises((OSError, ValueError)): Q.install_runtime(self.lock)
                with self.assertRaises((OSError, ValueError)): Q.remove_runtime()
                self.assertEqual((runtime/'rp1_qualification.py').read_bytes(), before)

    def test_legacy_resolve_and_refresh_are_offline(self):
        with self.deployment() as (runtime, source):
            for operation in ('resolve', 'refresh'):
                for export in (False, True):
                    destination = source.parent/(operation+'-export')
                    argv = ['rp1_qualification.py', operation]
                    if export: argv += ['--directory', str(destination)]
                    output = io.StringIO()
                    with patch.object(sys, 'argv', argv), redirect_stdout(output): Q.main()
                    result = json.loads(output.getvalue())
                    self.assertEqual(result['status'], 'verified')
                    self.assertEqual(result['source'], 'bundled')
                    if export: self.assertEqual(Q.evidence_assets(destination, self.lock), self.assets)

    def test_installed_cli_needs_neither_source_checkout_nor_old_cache(self):
        with self.deployment() as (runtime, source):
            Q.install_runtime(self.lock)
            source.rename(source.with_name('retired-source'))
            for route in ('gpio4', 'gpio20'):
                report, owner = observed(self.metadata, route)
                report_path, owner_path = runtime.parent/'report.json', runtime.parent/'owner.json'
                report_path.write_text(json.dumps(report)); owner_path.write_text(json.dumps(owner))
                result = subprocess.run([sys.executable, '-B', str(runtime/'rp1_qualification.py'), 'status',
                    '--provider-report', str(report_path), '--ownership', str(owner_path), '--route', route],
                    check=True, capture_output=True, text=True, timeout=10)
                self.assertEqual(json.loads(result.stdout)['status'], 'verified')

    def test_malformed_receipts_never_replace_or_remove_owned_files(self):
        for receipt in ([], list(Q.HELPER_FILES), {'../../foreign': '0'*64}):
            with self.subTest(receipt=receipt), self.deployment() as (runtime, source):
                Q.install_runtime(self.lock)
                (runtime/'rp1-qualification-files.json').write_text(json.dumps(receipt))
                before = (runtime/'rp1_qualification.py').read_bytes()
                with self.assertRaises(ValueError): Q.install_runtime(self.lock)
                with self.assertRaises(ValueError): Q.remove_runtime()
                self.assertEqual((runtime/'rp1_qualification.py').read_bytes(), before)

    def test_live_inspection_imports_installed_siblings_without_pythonpath(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()
            provider = directory/'runtime_provider.py'
            provider.write_text('import json, sibling\nprint(json.dumps({"origin": sibling.ORIGIN}))\n')
            (directory/'sibling.py').write_text('ORIGIN = "installed"\n')
            injected = directory/'injected'; injected.mkdir()
            (injected/'sibling.py').write_text('raise RuntimeError("PYTHONPATH was used")\n')
            original_read = Q.read
            with patch.object(Q, 'PROVIDER', provider), \
                 patch.object(Q, 'read', side_effect=lambda p, **kw: original_read(p)), \
                 patch.object(Q, 'validate_assets', return_value=({}, lambda metadata, report, route, owner: report)), \
                 patch.dict(os.environ, {'PYTHONPATH': str(injected)}):
                result = Q.status(self.lock, 'gpio20', ownership={})
            self.assertEqual(result['origin'], 'installed')
            self.assertFalse((directory/'__pycache__').exists())


if __name__ == '__main__': unittest.main()
