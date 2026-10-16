#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deterministic rejection tests; real release rebuilding is a separate gate."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('accept_rp1', ROOT/'scripts/maintainer/accept_rp1_release.py')
A = importlib.util.module_from_spec(spec); spec.loader.exec_module(A)


class Tests(unittest.TestCase):
    def setUp(self):
        self.lock = json.loads(A.LOCK_PATH.read_bytes())
        self.source = Path('/fixture/provider')
        self.assets = {name: name.encode() for name in self.lock['assets']}
        self.metadata = {name: self.lock[name] for name in
            ('sourceCommit', 'sourceTree', 'productVersion', 'debianVersion', 'classification', 'published', 'releaseTag', 'releaseUrl')}
        self.metadata.update(qualification={name: False for name in
            ('hardware', 'lifecycle', 'product', 'rf', 'wsprrypiAcceptance')},
            runtime=self.lock['runtime'], routes={route: {'compatibilityId': identity,
                'dtboSha256': self.lock['overlaySha256'][route]}
                for route, identity in self.lock['compatibilityIdentities'].items()},
            uapi={file: {'sha256': self.lock['uapiSha256'][name]} for name, file in
                (('consumer', 'rp1_gpclk.h'), ('controller', 'rp1_route_admin.h'))})
        self.package = 'rp1-gpclk-dkms_1.0.0-1_all.deb'
        inventory = [{'path': 'usr/src/rp1-gpclk-dkms-1.0.0/include/uapi/linux/rp1_gpclk.h',
                      'type': 'file', 'mode': '0644', 'sha256': self.lock['uapiSha256']['consumer']}]
        self.manifest = {'schema': A.installer.MANIFEST_SCHEMA, 'schemaVersion': 1,
            'repository': A.installer.REPOSITORY, 'releaseTag': 'v1.0.0',
            'sourceCommit': self.lock['sourceCommit'], 'productVersion': '1.0.0',
            'debianVersion': '1.0.0-1', 'dkmsModule': 'rp1-gpclk-dkms',
            'kernelModule': 'rp1_gpclk_dkms', 'releaseChannel': 'release',
            'administrationProtocol': A.installer.ADMIN_PROTOCOL,
            'package': {'name': 'rp1-gpclk-dkms', 'filename': self.package,
                        'sha256': A.installer.sha256_bytes(self.assets[self.package])},
            'uapi': {'path': inventory[0]['path'], 'sha256': inventory[0]['sha256']},
            'installationBehavior': {'routeNeutral': True, 'outputDisabled': True,
                'loadsModule': False, 'appliesOverlay': False, 'editsBootConfiguration': False,
                'operatesServices': False}, 'packageInventory': inventory,
            'packageInventorySha256': A.installer.sha256_bytes(A.installer.canonical(inventory))}
        self.encode()

    def encode(self):
        self.assets['release-metadata.json'] = A.installer.canonical(self.metadata)
        self.assets[A.installer.MANIFEST_NAME] = A.installer.canonical(self.manifest)
        self.relock()

    def relock(self):
        self.assets['SHA256SUMS'] = ''.join(A.installer.sha256_bytes(value)+'  '+name+'\n'
            for name, value in sorted(self.assets.items()) if name != 'SHA256SUMS').encode()
        self.lock['assets'] = {name: A.installer.sha256_bytes(value) for name, value in self.assets.items()}

    def validate(self):
        with mock.patch.object(A.installer, 'validate_package') as package:
            result = A.validate_snapshot(self.assets, Path('/fixture/candidate'), self.lock, A.installer.Runner())
            package.assert_called_once()
            return result

    def test_scoped_positive_does_not_install_or_claim_publication(self):
        result = self.validate()
        self.assertEqual(result['providerSelection'], 'release')
        self.assertTrue(result['releaseProfileVerified'])
        for field in ('published', 'qualification', 'productionSelectable', 'installationPerformed'):
            self.assertIs(result[field], False)

    def test_wrong_source_commit_tree_dirty_or_attached_never_read_assets(self):
        identity = {'commit': self.lock['sourceCommit'], 'tree': self.lock['sourceTree']}
        for field in ('commit', 'tree'):
            wrong = {**identity, field: 'f'*40}
            with self.subTest(field=field), mock.patch.object(A.installer, 'validate_checkout', return_value=wrong), \
                 mock.patch.object(A, 'snapshot') as snapshot:
                with self.assertRaises(A.installer.ContractError): A.validate_assets(Path('/fixture/candidate'), self.source, self.lock)
                snapshot.assert_not_called()
        with mock.patch.object(A.installer, 'validate_checkout', side_effect=A.installer.ContractError('dirty source')):
            with self.assertRaises(A.installer.ContractError): A.validate_assets(Path('/fixture/candidate'), self.source, self.lock)
        with mock.patch.object(A.installer, 'validate_checkout', return_value=identity), \
             mock.patch.object(A.installer.Runner, 'run', return_value=A.installer.CommandResult('', '', 0)):
            with self.assertRaisesRegex(A.installer.ContractError, 'detached'):
                A.validate_assets(Path('/fixture/candidate'), self.source, self.lock)

    def test_missing_extra_substituted_package_and_checksum_assets(self):
        for mutation in ('missing', 'extra', 'package', 'checksum'):
            original = copy.deepcopy(self.assets)
            if mutation == 'missing': self.assets.pop('release-metadata.json')
            elif mutation == 'extra': self.assets['foreign.deb'] = b'foreign'
            elif mutation == 'package': self.assets[self.package] += b'substitution'
            else: self.assets['SHA256SUMS'] += b'malformed'
            with self.subTest(mutation=mutation), self.assertRaises(A.installer.ContractError): self.validate()
            self.assets = original

    def test_manifest_mismatch_even_with_self_consistent_asset_hash(self):
        self.manifest['sourceCommit'] = 'f'*40; self.encode()
        with self.assertRaises(A.installer.ContractError): self.validate()

    def test_runtime_uapi_route_and_publication_mismatches(self):
        original = copy.deepcopy(self.metadata)
        mutations = [lambda x: x.update(published=False), lambda x: x.update(classification='candidate'),
            lambda x: x.update(releaseTag='v0.9.0'), lambda x: x.update(releaseUrl='https://example.invalid'),
            lambda x: x['qualification'].update(rf=True), lambda x: x.update(qualification={}),
            lambda x: x['runtime'].update(binding='rp1-gpclk-runtime-binding-v2'),
            lambda x: x['runtime'].update(readiness='wrong'), lambda x: x['runtime'].update(update='wrong'),
            lambda x: x['routes']['gpio4'].update(compatibilityId='v0.9.0-pi5'),
            lambda x: x['routes']['gpio20'].update(compatibilityId='v1.0.0-rp1-gpio4'),
            lambda x: x['uapi']['rp1_gpclk.h'].update(sha256='f'*64),
            lambda x: x['uapi']['rp1_route_admin.h'].update(sha256='f'*64)]
        for index, mutate in enumerate(mutations):
            self.metadata = copy.deepcopy(original); mutate(self.metadata); self.encode()
            with self.subTest(index=index), self.assertRaises(A.installer.ContractError): self.validate()

    def test_duplicate_checksums_and_json_fields(self):
        self.assets['SHA256SUMS'] += self.assets['SHA256SUMS'].splitlines(keepends=True)[0]
        self.lock['assets']['SHA256SUMS'] = A.installer.sha256_bytes(self.assets['SHA256SUMS'])
        with self.assertRaises(A.installer.ContractError): self.validate()
        with self.assertRaises(A.installer.ContractError): A.strict_json('{"published":false,"published":true}')

    def test_snapshot_refuses_symlink_and_unexpected_file(self):
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw).resolve()
            for name, data in self.assets.items(): (directory/name).write_bytes(data)
            self.assertEqual(A.snapshot(directory, self.lock), self.assets)
            (directory/'foreign').write_bytes(b'foreign')
            with self.assertRaises(A.installer.ContractError): A.snapshot(directory, self.lock)
            (directory/'foreign').unlink()
            (directory/self.package).unlink(); (directory/self.package).symlink_to('/dev/null')
            with self.assertRaises(OSError): A.snapshot(directory, self.lock)

    def publication(self):
        tag = self.lock['releaseTag']
        return {'tag_name': tag, 'html_url': self.lock['releaseUrl'],
                'published_at': '2026-10-16T04:25:49Z', 'immutable': True,
                'draft': False, 'prerelease': False, 'assets': [
                    {'name': name, 'state': 'uploaded', 'digest': 'sha256:' + digest,
                     'browser_download_url': A.installer.REPOSITORY_URL.removesuffix('.git') +
                        '/releases/download/' + tag + '/' + name}
                    for name, digest in self.lock['assets'].items()]}

    def test_publication_requires_production_policy_exact_tag_and_all_digests(self):
        release = self.publication()
        client = mock.Mock()
        client.json.return_value = release
        client.tag_commit.return_value = self.lock['sourceCommit']
        result = A.validate_publication(client, self.lock)
        self.assertTrue(result['publicationVerified'])
        self.assertTrue(result['productionSelectable'])
        client.json.assert_called_once_with(A.installer.API_BASE + '/releases/tags/v1.0.0')
        mutations = [lambda x: x.update(draft=True), lambda x: x.update(prerelease=True),
            lambda x: x.update(immutable=False), lambda x: x.update(published_at=None),
            lambda x: x.update(tag_name='v1.0.1'), lambda x: x.update(html_url='https://example.invalid'),
            lambda x: x['assets'].pop(),
            lambda x: x['assets'][0].update(digest='sha256:' + '0'*64),
            lambda x: x['assets'][0].update(browser_download_url='https://example.invalid/asset'),
            lambda x: x['assets'].append(copy.deepcopy(x['assets'][0]))]
        for index, mutate in enumerate(mutations):
            bad = copy.deepcopy(release); mutate(bad)
            client.json.return_value = bad
            with self.subTest(index=index), self.assertRaises(A.installer.ContractError):
                A.validate_publication(client, self.lock)
        client.json.return_value = release
        client.tag_commit.return_value = '0'*40
        with self.assertRaisesRegex(A.installer.ContractError, 'tag commit'):
            A.validate_publication(client, self.lock)

    def test_unpublished_release_selection_remains_unavailable(self):
        with self.assertRaisesRegex(A.installer.ContractError, 'no eligible'):
            A.installer.select_release([])
        self.assertFalse(A.installer.release_candidate({'tag_name': 'v1.0.0', 'published_at': None,
            'draft': False, 'prerelease': False, 'immutable': False, 'assets': []}))


if __name__ == '__main__': unittest.main()
