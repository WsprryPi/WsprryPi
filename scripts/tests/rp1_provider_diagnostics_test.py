#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the standalone collector's actual bounded identity whitelist."""
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
collector = (ROOT / 'scripts/collect-support-bundle.sh').read_text()
code = collector.split("<<'PY_RP1_IDENTITY'\n", 1)[1].split('\nPY_RP1_IDENTITY', 1)[0]
scope = {'__name__': 'collector_identity_test'}
exec(compile(code, 'collector-rp1-identity', 'exec'), scope)
scope['OWNER_UID'] = os.geteuid()  # Internal fixture only, never a collector control.


class Tests(unittest.TestCase):
    def record(self, channel='development'):
        return {'schema': 'wsprrypi-rp1-gpclk-dkms-ownership-v3',
                'owner': 'WsprryPi', 'repository': 'WsprryPi/RP1-GPCLK-DKMS',
                'channel': channel, 'sourceCommit': 'a'*40, 'productVersion': '1.0.0',
                'sourceTree': 'b'*40, 'uapiSha256': 'c'*64, 'qualificationClaim': False,
                'runtime': {'bindingSha256': 'd'*64, 'artifactSetSha256': 'e'*64,
                    'compatibilityIdentities': {'gpio4': 'v1.0.0-rp1-gpio4',
                                               'gpio20': 'v1.0.0-rp1-gpio20'}},
                'privateKey': 'NEVER EXPOSE', 'lease': 'NEVER EXPOSE',
                'upstreamEvidence': '/private/NEVER EXPOSE',
                'configuration': {'callsign': 'NEVER EXPOSE'}}

    def test_identity_is_observational_and_excludes_private_fields(self):
        for channel in ('development', 'release'):
            value = scope['summarize'](self.record(channel))
            self.assertEqual(value['selection'], channel)
            self.assertEqual(value['sourceCommit'], 'a'*40)
            self.assertFalse(value['publicationVerified'])
            self.assertFalse(value['qualificationEstablished'])
            self.assertFalse(value['liveValidated'])
            self.assertNotIn('NEVER EXPOSE', json.dumps(value))
            self.assertNotEqual(value['compatibilityIdentities']['gpio4'],
                                value['compatibilityIdentities']['gpio20'])

    def test_invalid_identity_never_becomes_free_text(self):
        for field, bad in (('sourceCommit', 'NEVER EXPOSE'), ('qualificationClaim', True),
                           ('channel', 'candidate'), ('manifest', [])):
            value = self.record(); value[field] = bad
            with self.assertRaises(ValueError): scope['summarize'](value)
        value = self.record()
        value['runtime']['compatibilityIdentities']['gpio20'] = 'v1.0.0-rp1-gpio4'
        with self.assertRaises(ValueError): scope['summarize'](value)

    def test_bounded_file_reader_refuses_unsafe_input(self):
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw)/'record.json'
            path.write_text(json.dumps(self.record())); path.chmod(0o600)
            self.assertEqual(scope['read_summary'](path)['status'], 'recorded-only')
            link = path.with_name('link'); link.symlink_to(path)
            self.assertEqual(scope['read_summary'](link)['status'], 'unavailable-or-invalid')
            path.chmod(0o644)
            self.assertEqual(scope['read_summary'](path)['status'], 'unavailable-or-invalid')
            path.chmod(0o600); path.write_text('{"owner":"WsprryPi","owner":"foreign"}')
            self.assertEqual(scope['read_summary'](path)['status'], 'unavailable-or-invalid')
            path.write_bytes(b'x'*(4*1024*1024+1))
            self.assertEqual(scope['read_summary'](path)['status'], 'unavailable-or-invalid')
            path.unlink()
            self.assertEqual(scope['read_summary'](path)['status'], 'absent')
            os.mkfifo(path, 0o600)
            self.assertEqual(scope['read_summary'](path)['status'], 'unavailable-or-invalid')


if __name__ == '__main__': unittest.main()
