#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline tests of supplemental resolution; fake network and no target access."""
import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rp1_qualification as Q
import rp1_gpclk_dkms_install as installer


def fixture():
    # A tiny deterministic interpreter exercises pin-before-execution. The real
    # interpreter's independent attribution tests belong to the provider.
    tool = b'def validate(raw, lock):\n import json\n return json.loads(raw)\ndef interpret(*args):\n return {"status":"verified","permissionGate":False}\n'
    assets = {'rp1-qualification.json': b'{"fixture":true}',
              'rp1-qualification.schema.json': b'{}', 'qualification_status.py': tool}
    assets['SHA256SUMS'] = ''.join(Q.sha(assets[n])+'  '+n+'\n' for n in sorted(assets)).encode()
    lock = {'revision': 1, 'metadataTag': 'qualification-v1.0.0-r1',
            'metadataSource': {'commit': 'a'*40, 'tree': 'd'*40}, 'software': {'sourceCommit': 'b'*40, 'tagObject': 'c'*40},
            'assets': {n: Q.sha(raw) for n, raw in assets.items()}}
    release = {'tag_name': lock['metadataTag'], 'draft': False, 'prerelease': False,
        'published_at': '2026-10-16T00:00:00Z', 'immutable': True,
        'html_url': Q.BASE+'tag/'+lock['metadataTag'], 'assets': [
            {'name': n, 'digest': 'sha256:'+lock['assets'][n], 'state': 'uploaded',
             'browser_download_url': Q.BASE+'download/'+lock['metadataTag']+'/'+n} for n in assets]}
    return assets, lock, release


class Client:
    def __init__(self): self.assets, self.lock, self.release = fixture()
    def releases(self): return [self.release]
    def tag_commit(self, tag): return 'b'*40 if tag == 'v1.0.0' else 'a'*40
    def json(self, url):
        return ({'tree': {'sha': 'd'*40}} if '/git/commits/' in url else
                {'object': {'type': 'tag', 'sha': 'c'*40}})
    def bytes(self, url, **kwargs): return self.assets[url.rsplit('/', 1)[1]]


class Tests(unittest.TestCase):
    def test_maintained_reference_preserves_original_release(self):
        root = Path(__file__).resolve().parents[2]
        lock = Q.strict_json((root/'release_tools/rp1-qualification-lock.json').read_bytes())
        original = Q.strict_json((root/'scripts/tests/fixtures/rp1-release-1.0.0.json').read_bytes())
        self.assertEqual(lock['schema'], 'wsprrypi-rp1-qualification-lock-v1')
        self.assertEqual(lock['metadataTag'], 'qualification-v1.0.0-r1')
        self.assertEqual(set(lock['assets']), Q.ASSETS)
        for key, value in original.items():
            self.assertEqual(lock['software'][key], value, key)
        self.assertNotEqual(lock['metadataSource']['commit'], original['sourceCommit'])
        self.assertEqual(lock['software']['tagObject'], '52c5d42cf6e3f9c8a77fe9a03e1f012c3e8304a0')

    def test_resolution_and_original_software_selector(self):
        client = Client()
        self.assertEqual(Q.resolve(client, client.lock), client.assets)
        self.assertFalse(installer.release_candidate(client.release))
        original = {'tag_name': 'v1.0.0', 'draft': False, 'prerelease': False,
            'published_at': '2026-10-16T00:00:00Z', 'immutable': True,
            'assets': [{'name': n, 'state': 'uploaded', 'browser_download_url': 'https://example.invalid/'+n}
                       for n in [installer.MANIFEST_NAME, 'SHA256SUMS', 'rp1-gpclk-dkms_1.0.0-1_all.deb']]}
        self.assertEqual(installer.select_release([client.release, original]), original)
        self.assertEqual(installer.source_selector('devel', 'devel'), ('devel', None))

    def test_publication_refusals(self):
        for key, bad in [('immutable', False), ('draft', True), ('prerelease', True),
                         ('tag_name', 'qualification-v1.0.0-r0'), ('html_url', 'https://wrong')]:
            client = Client(); client.release[key] = bad
            with self.subTest(key=key), self.assertRaises(ValueError): Q.resolve(client, client.lock)
        for change in ('missing', 'duplicate', 'digest', 'url', 'source'):
            client = Client()
            if change == 'missing': client.release['assets'].pop()
            if change == 'duplicate': client.release['assets'][1] = client.release['assets'][0]
            if change == 'digest': client.release['assets'][0]['digest'] = 'sha256:'+'0'*64
            if change == 'url': client.release['assets'][0]['browser_download_url'] += '?wrong'
            if change == 'source': client.lock['metadataSource']['commit'] = '0'*40
            with self.subTest(change=change), self.assertRaises(ValueError): Q.resolve(client, client.lock)

    def test_substituted_bytes_never_execute(self):
        assets, lock, _ = fixture()
        for name in assets:
            changed = dict(assets); changed[name] += b'wrong'
            with self.subTest(name=name), self.assertRaises(ValueError): Q.validate_assets(changed, lock)
        with self.assertRaises(ValueError): Q.strict_json('{"x":1,"x":2}')

    def test_missing_invalid_and_bounded_cache(self):
        assets, lock, _ = fixture()
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()/'cache'
            self.assertEqual(Q.status(lock, 'gpio4', {}, {}, directory)['status'], 'missing')
            directory.mkdir()
            for n, raw in assets.items(): (directory/n).write_bytes(raw)
            self.assertEqual(Q.cached(directory, lock, trusted=False)[0], {'fixture': True})
            self.assertEqual(Q.status(lock, 'gpio4', {}, {}, directory)['status'], 'verified')
            (directory/'rp1-qualification.json').write_bytes(b'{}')
            with self.assertRaises(ValueError): Q.cached(directory, lock, trusted=False)
            path = Path(temporary).resolve()/'large'; path.write_bytes(b'a'*20)
            with self.assertRaises(ValueError): Q.read(path, limit=10)
            link = Path(temporary).resolve()/'link'; link.symlink_to(path)
            with self.assertRaises(OSError): Q.read(link)

    def test_helper_ownership_install_retry_and_removal(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()/'runtime'
            reference = directory.parent/'lock.json'; reference.write_text('{}')
            original_read, original_write = Q.read, Q.write_atomic
            with patch.object(Q, 'RUNTIME', directory), patch.object(Q, 'OWNER_UID', Q.os.geteuid()), \
                 patch.object(Q, 'lock_path', return_value=reference), \
                 patch.object(Q, 'read', side_effect=lambda p, **kw: original_read(p)), \
                 patch.object(Q, 'write_atomic', side_effect=lambda p, raw, mode=0o644:
                              original_write(p, raw, mode, trusted=False)):
                Q.install_runtime({}); Q.install_runtime({})
                installed = directory/'rp1_qualification.py'
                self.assertEqual(installed.read_bytes(), Path(Q.__file__).read_bytes())
                installed.write_bytes(b'foreign')
                with self.assertRaises(ValueError): Q.remove_runtime()
                self.assertEqual(installed.read_bytes(), b'foreign')
                installed.write_bytes(Path(Q.__file__).read_bytes())
                Q.remove_runtime(); Q.remove_runtime()
                self.assertEqual(list(directory.iterdir()), [])

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
                 patch.object(Q, 'cached', return_value=({}, lambda metadata, report, route, owner: report)), \
                 patch.dict(os.environ, {'PYTHONPATH': str(injected)}):
                result = Q.status({}, 'gpio20', ownership={})
            self.assertEqual(result['origin'], 'installed')
            self.assertFalse((directory/'__pycache__').exists())


if __name__ == '__main__': unittest.main()
