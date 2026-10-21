#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Supplemental read-only report interpreter, independent of frozen package diagnostics.

Consumes verified publication bytes and a separately collected runtime-provider
inspection. It neither runs the old diagnostic executable nor contacts a device.
"""
import argparse
import hashlib
import json
from pathlib import Path


def require(value, message):
    if not value:
        raise ValueError(message)


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'duplicate JSON field: ' + key)
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda _: require(False, 'nonfinite JSON number'))


def validate(raw, lock):
    require(hashlib.sha256(raw).hexdigest() == lock['assets']['rp1-qualification.json'],
            'metadata digest differs from accepted reference')
    value = strict_json(raw)
    require(value['schema'] == 'rp1-qualification-v1' and type(value['schemaVersion']) is int and
            value['schemaVersion'] == 1 and type(value['revision']) is int and value['revision'] == lock['revision'] and
            value['metadataTag'] == lock['metadataTag'] and value['metadataSource'] == lock['metadataSource'],
            'stale or mismatched supplemental identity')
    require(value['software'] == lock['software'], 'original software binding differs')
    entries = value['evidence']['routes']
    require(len(entries) == 2 and {e['route'] for e in entries} == {'gpio4', 'gpio20'},
            'missing, conflicting or duplicate routes')
    for entry in entries:
        route = entry['route']
        require(entry['routeId'] == value['software']['compatibilityIdentities'][route] and
                entry['overlaySha256'] == value['software']['overlaySha256'][route] and
                entry['state'] == 'Qualified' and entry['scope']['kind'] == 'finite-conducted-tone',
                'route evidence identity/scope differs')
    return value


def interpret(metadata, report, route, ownership=None):
    """Keep evidence attribution, structural state and readiness independent."""
    result = {'status': 'missing', 'state': 'Evidence missing', 'route': route,
              'structuralCompatibility': 'Unknown', 'operationalReady': 'Unknown',
              'applicationEligibility': 'WsprryPi band/mode policy', 'permissionGate': False}
    if report:
        result['operationalReady'] = report.get('safety', {}).get('operationalReady', 'unknown')
        result['currentOutput'] = report.get('safety', {})
        try:
            observed = report['manager']['idle']['state']['outputLifecycle']['snapshot']
            result['structuralCompatibility'] = {3: 'Compatible-unqualified', 4: 'Unavailable',
                                                5: 'Rejected'}.get(observed['compatibility'], 'Unknown')
        except (KeyError, TypeError):
            pass
    if metadata is None:
        return result
    result['metadataTag'] = metadata['metadataTag']
    if route not in ('gpio4', 'gpio20'):
        return {**result, 'status': 'unattributed', 'state': 'No active route'}
    try:
        require(report.get('contract') == 'rp1-gpclk-runtime-readiness-v1' and
                report.get('schemaVersion') == 1, 'runtime inspection contract differs')
        binding = report['identities']['installedBinding']
        require(binding['status'] == 'valid', 'installed binding unavailable')
        value = binding['value']
        software = metadata['software']
        require(isinstance(ownership, dict) and ownership.get('channel') == 'release' and
                ownership.get('sourceCommit') == software['sourceCommit'] and
                ownership.get('tag') == software['releaseTag'] and
                ownership.get('manifest', {}).get('package', {}).get('sha256') ==
                software['assets']['rp1-gpclk-dkms_1.0.0-1_all.deb'],
                'installed package/source release differs or is unverified')
        require(value['sourceCommit'] == software['sourceCommit'] and
                value['productVersion'] == software['productVersion'] and
                value['uapiSha256'] == software['uapiSha256'] and
                value['compatibilityIdentities'] == software['compatibilityIdentities'],
                'installed source/UAPI/release differs')
        require(report['routes']['active'] == route, 'active route differs')
        require(value['files'] == metadata['runtimeFiles'], 'runtime inventory differs from released source')
        expected = {**value['files'], **value['externalFiles'],
                    **{m['path']: m['installedFileSha256'] for m in value['modules'].values()}}
        require(set(report['artifacts']) == set(expected) and all(
            report['artifacts'][p]['status'] == 'exact' and
            report['artifacts'][p]['expectedSha256'] == digest and
            report['artifacts'][p]['actualSha256'] == digest for p, digest in expected.items()),
            'installed artifact identity differs or inventory is incomplete')
        identity = dict(value)
        artifact_digest = identity.pop('artifactSetSha256')
        require(hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
                == artifact_digest, 'runtime binding integrity differs')
        # Loaded build notes must authenticate each exact kernel-built module.
        for name, key in [('rp1_gpclk_dkms', 'consumerNoteSha256'),
                          ('rp1_route_controller', 'controllerNoteSha256')]:
            module = report['modules'][name]
            require(module['status'] == 'loaded' and module['version'] == '1.0.0' and
                    module['buildNoteSha256'] == value[key] == value['modules'][name]['buildNoteSha256'] and
                    value['modules'][name]['version'] == '1.0.0' and
                    value['modules'][name]['kernel'] == value['kernel'], 'loaded module build differs')
        entry = next(e for e in metadata['evidence']['routes'] if e['route'] == route)
        overlay = '/usr/lib/rp1-gpclk-dkms/runtime-overlays/' + route + '.dtbo'
        require(value['files'][overlay] == entry['runtimeOverlaySha256'], 'installed runtime overlay differs')
        snapshot = report['manager']['idle']['state']['outputLifecycle']['snapshot']
        require(snapshot['route'] == {'gpio4': 1, 'gpio20': 2}[route], 'module route differs')
        require(snapshot['compatibility'] == 3, 'structural compatibility unavailable')
    except (KeyError, TypeError, ValueError) as error:
        return {**result, 'status': 'unattributed', 'state': 'Evidence does not match installed provider',
                'reason': str(error)}
    entry = next(e for e in metadata['evidence']['routes'] if e['route'] == route)
    return {**result, 'status': 'verified', 'state': 'Qualified (scoped TONE evidence)',
            'scope': entry['scope'], 'evidenceId': entry['evidenceId'],
            'evidenceObservations': metadata['evidence']['observations'],
            'limitations': metadata['evidence']['limitations'],
            'applicationQualification': metadata['evidence']['applicationQualification']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--lock', required=True, type=Path)
    parser.add_argument('--provider-report', required=True, type=Path)
    parser.add_argument('--ownership', required=True, type=Path)
    parser.add_argument('--route', required=True, choices=['gpio4', 'gpio20'])
    args = parser.parse_args()
    value = validate(args.metadata.read_bytes(), strict_json(args.lock.read_bytes()))
    print(json.dumps(interpret(value, strict_json(args.provider_report.read_bytes()), args.route,
                               strict_json(args.ownership.read_bytes())),
                     indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
