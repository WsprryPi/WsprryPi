#!/usr/bin/env python3
"""Enforce the shared control lifecycle, independently of build and wire versions."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = {'policy': 'wsprry-control-lifecycle', 'major': 1,
            'contract': 'Wsprry-Control/1', 'namespace': '/api/control/v1/',
            'schema_id': 'urn:wsprry:control:1',
            'public_prefix': '/api/consumer/v1/read/'}

def schema_digest(schema):
    return hashlib.sha256(json.dumps(schema, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=False).encode()).hexdigest()

def validate(policy, schema, stable_release=False):
    if set(policy) != {*IDENTITY, 'phase', 'first_release', 'schema_sha256'}:
        raise ValueError('Unknown or missing control lifecycle policy field')
    if any(type(policy[k]) is not type(v) or policy[k] != v for k, v in IDENTITY.items()):
        raise ValueError('Control major and identifiers are frozen at 1; changing them requires explicit policy review')
    if policy['phase'] not in ('prerelease', 'released'):
        raise ValueError('Unknown control lifecycle phase')
    if (schema.get('contract'), schema.get('namespace'), schema.get('$id'), schema.get('public_prefix')) != (
            policy['contract'], policy['namespace'], policy['schema_id'], policy['public_prefix']):
        raise ValueError('Schema identity differs from frozen control policy')
    independent_slots = {
        ('$defs', 'udp-logging-capabilities', 'oneOf', '0', 'properties', 'wire_schema'): {'wsprry-udp/1'},
        ('$defs', 'udp-logging-capabilities', 'oneOf', '1', 'properties', 'wire_schema'): {'wsprry-udp/1'},
        ('$defs', 'udp-logging-capabilities', 'oneOf', '1', 'properties', 'settings_schema'): {'host-udp-logging/1'},
    }
    def schema_names(rule):
        if isinstance(rule, str): return [rule]
        if not isinstance(rule, dict): raise ValueError('Invalid schema selector')
        if 'const' in rule: return [rule['const']]
        if 'enum' in rule and isinstance(rule['enum'], list): return rule['enum']
        for key in ('oneOf', 'anyOf'):
            if key in rule:
                return [name for branch in rule[key] for name in schema_names(branch)]
        raise ValueError('Control schema selectors must have closed discriminators')
    def inspect(value, path=()):
        if path == ('$defs', 'wtp'):
            return # Only this canonical independently versioned WTP bundle is exempt.
        if isinstance(value, dict):
            for key, item in value.items():
                selected = path + (key,)
                if key == 'version' or re.search(r'(?:^|_)(?:v[0-9]+|versions?|schemas)$', key):
                    raise ValueError('Per-resource version selectors are unsupported')
                if key == 'contract' and schema_names(item) != [policy['contract']]:
                    raise ValueError('Logical control documents must retain the frozen contract identifier')
                if key == 'schema' or key.endswith('_schema'):
                    names = schema_names(item)
                    if not names or any(not isinstance(name, str) for name in names):
                        raise ValueError('Invalid schema discriminator')
                    if selected in independent_slots:
                        if set(names) != independent_slots[selected]:
                            raise ValueError('Independent wire/schema boundary changed without policy review')
                    elif any(not re.fullmatch(r'[a-z][a-z0-9-]*', name) or
                             re.search(r'[-_/](?:v)?[0-9]+$', name) for name in names):
                        raise ValueError('Control resource schema selectors must be unversioned')
                inspect(item, selected)
        elif isinstance(value, list):
            for index, item in enumerate(value): inspect(item, path + (str(index),))
        elif isinstance(value, str) and value.startswith('/api/'):
            if value.startswith(policy['namespace']):
                relative = value[len(policy['namespace']):]
            elif value.startswith(policy['public_prefix']):
                relative = value[len(policy['public_prefix']):]
                if relative == 'consumer/setup/api/owner/v1/public-status':
                    return
            else:
                raise ValueError('Control route differs from frozen logical/read namespaces')
            if re.search(r'(?:(?:^|/)(?:v)?[0-9]+|[_-]v[0-9]+)(?:/|$)', relative):
                raise ValueError('Per-resource route generations are unsupported')
    inspect(schema)
    def route_map(value, complete=False):
        if not isinstance(value, dict) or not value or not set(value) <= {'GET', 'PUT', 'POST'} or (complete and set(value) != {'GET', 'PUT', 'POST'}):
            raise ValueError('Invalid bounded control route methods')
        for method, names in value.items():
            if not isinstance(names, list) or not 1 <= len(names) <= 128 or any(
                    not isinstance(name, str) or not 1 <= len(name) <= 128 for name in names):
                raise ValueError('Invalid bounded control route names')
            if len(names) != len(set(names)):
                raise ValueError('Duplicate control routes')
    route_map(schema.get('management_routes'), True)
    scopes = schema.get('client_routes')
    if not isinstance(scopes, dict) or set(scopes) != {'pi_management', 'pi_consumer_session'}:
        raise ValueError('Unknown or missing control client authority scope')
    for scoped in scopes.values():
        route_map(scoped)
        for method, names in scoped.items():
            if not set(names) <= set(schema['management_routes'][method]):
                raise ValueError('Scoped client routes exceed producer registry authority')
    relative_routes = list(schema['public_resources']) + list(schema['request_limits'])
    relative_routes += [name for names in schema['management_routes'].values() for name in names]
    relative_routes += [name for scoped in scopes.values() for names in scoped.values() for name in names]
    for name in relative_routes:
        if name == 'consumer/setup/api/owner/v1/public-status':
            continue # Independent commissioning API, forwarded read-only.
        if re.search(r'(?:(?:^|/)(?:v)?[0-9]+|[_-]v[0-9]+)(?:/|$)', name):
            raise ValueError('Per-resource route generations are unsupported')
    if policy['phase'] == 'prerelease':
        if policy['first_release'] is not None or policy['schema_sha256'] is not None:
            raise ValueError('Prerelease policy cannot contain released snapshot markers')
        if stable_release:
            raise ValueError('First stable release requires an explicit reviewed control-policy release transition')
    else:
        if not isinstance(policy['first_release'], str) or not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?', policy['first_release']):
            raise ValueError('First release must identify a stable release')
        if policy['schema_sha256'] != schema_digest(schema):
            raise ValueError('Released control schema is immutable; schema digest drift requires policy review and a new major')

def check_files(schema_path=None, policy_path=None, stable_release=False):
    schema_path = schema_path or ROOT / 'engineering/protocol/control-1.schema.json'
    policy_path = policy_path or ROOT / 'engineering/protocol/control-policy.json'
    policy, schema = json.loads(Path(policy_path).read_text()), json.loads(Path(schema_path).read_text())
    validate(policy, schema, stable_release)
    return policy, schema

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema', type=Path)
    parser.add_argument('--policy', type=Path)
    parser.add_argument('--stable-release', action='store_true')
    parser.add_argument('--record-first-release', metavar='SEMVER', help='Record a reviewed stable transition; does not publish')
    args = parser.parse_args()
    policy, schema = check_files(args.schema, args.policy, False if args.record_first_release else args.stable_release)
    if args.record_first_release:
        if policy['phase'] != 'prerelease':
            raise ValueError('First release is already recorded')
        policy.update(phase='released', first_release=args.record_first_release, schema_sha256=schema_digest(schema))
        validate(policy, schema, True)
        path = args.policy or ROOT / 'engineering/protocol/control-policy.json'
        path.write_text(json.dumps(policy, indent=2) + '\n')
    print('Control lifecycle policy valid: ' + policy['phase'])

if __name__ == '__main__':
    main()
