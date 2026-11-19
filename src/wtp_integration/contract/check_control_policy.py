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

def pi_resource_descriptors(schema):
    """Bind each Pi resource to the closed current types and approved policies.

    The full controller definition closure deliberately invalidates all Pi resource
    digests on any coordinated controller-schema change. Immutable policy-source
    revisions bind the incorporated value/authority semantics as well as shape.
    These are type identities, never resource permission advertisements on Pico.
    """
    definitions = {name:schema['$defs'][name] for name in schema['controller_adapter']['definitions']}
    result = {}
    for resource, definition in [('pi-config','pi-config'), ('pi-scheduling','pi-scheduling'), ('stop','target-stop'), ('migration','target-migration')]:
        identity = 'urn:wsprry:control:1:' + resource
        document = {'$id':identity, '$ref':'#/$defs/'+definition, '$defs':definitions,
                    'policy_dependencies':schema['pi_management_policies']}
        result[resource] = {'schema_id':identity, 'schema_digest':schema_digest(document)}
    return result

def validate(policy, schema, stable_release=False):
    sources = schema.get('pi_management_policies')
    if not isinstance(sources,dict) or set(sources) != {'repository','configuration','management'} or sources['repository'] != 'https://github.com/WsprryPi/WsprryPi':
        raise ValueError('Missing or unknown Pi management policy dependencies')
    for name in ('configuration','management'):
        dependency=sources[name]
        if not isinstance(dependency,dict) or set(dependency) != {'revision','paths'} or not isinstance(dependency['revision'],str) or not re.fullmatch('[0-9a-f]{40}',dependency['revision']):
            raise ValueError('Invalid Pi management policy dependency')
        paths=dependency['paths']
        if not isinstance(paths,list) or not paths or len(paths)>16 or any(not isinstance(path,str) or not re.fullmatch(r'(?:src|docs)/[A-Za-z0-9_./-]+',path) or '..' in path for path in paths) or len(set(paths))!=len(paths):
            raise ValueError('Invalid Pi management policy dependency paths')
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
    adapter = schema.get('controller_adapter')
    if not isinstance(adapter, dict) or set(adapter) != {'scope', 'headers', 'limits', 'pico_routes', 'documents', 'definitions', 'browser_session', 'reconciliation_reads', 'host_routes'}:
        raise ValueError('Missing or unknown controller adapter contract fields')
    if adapter['scope'] != 'wsprrypi-controller' or adapter['headers'] != {
            'profile':'X-WsprryPi-Profile-ID', 'context':'X-WsprryPi-Target-Context',
            'operation':'X-WsprryPi-Operation-ID', 'readback':'X-WsprryPi-Readback-Revision', 'csrf':'X-WsprryPi-CSRF',
            'recovery':'X-WsprryPi-Recovery-Resource'}:
        raise ValueError('Controller context header contract differs')
    if adapter['browser_session'] != 'host/management-session':
        raise ValueError('Controller browser-session path differs')
    if adapter['limits'] != {'maximum_targets':64, 'maximum_connections':4,
            'maximum_body_bytes':32768, 'maximum_header_bytes':8192, 'maximum_nesting':16,
            'terminal_receipts_per_target':256, 'maximum_unresolved':32, 'operation_deadline_ms':24000}:
        raise ValueError('Controller management bounds differ')
    if set(adapter['pico_routes']) != {'GET','PUT','POST','DELETE'} or adapter['pico_routes']['DELETE'] != ['wifi']:
        raise ValueError('Controller Pico route methods differ')
    route_map({method:adapter['pico_routes'][method] for method in ('GET','PUT','POST')}, True)
    if any(not re.fullmatch(r'[a-z][a-z0-9-]*(?:/[a-z][a-z0-9-]*)*', name)
           for names in adapter['pico_routes'].values() for name in names):
        raise ValueError('Invalid controller Pico resource name')
    mutations = set().union(*(set(adapter['pico_routes'][method]) for method in ('PUT','POST','DELETE'))) - {'management','pins/validate'}
    reconciliation = {name:name for name in mutations}
    reconciliation.update({'progress/update':'progress','progress/apply':'progress','udp-logging/test':'udp-logging','restart':'hostname','ap':'hostname'})
    if adapter['reconciliation_reads'] != reconciliation or any(value not in adapter['pico_routes']['GET'] for value in reconciliation.values()):
        raise ValueError('Controller reconciliation read contract differs')
    if adapter['documents'] != ['targets', 'target-operation', 'controller-pico-selection', 'controller-browser-session', 'target-error', 'target-save', 'target-enable', 'target-action', 'target-stop', 'target-settings', 'target-context', 'target-status', 'host-readiness', 'configuration-recovery', 'configuration-recovery-save', 'configuration-recovery-action', 'target-migration', 'target-migration-rollback', 'target-migration-read', 'controller-retirement', 'controller-retirement-operation', 'controller-retirements', 'pairing-state', 'pair-window-request', 'pair-window-response', 'pair-remove', 'pair-connect', 'pair-discovery', 'target-operation-review', 'target-operation-reviewed'] or adapter['definitions'] != ['target-id', 'target-digest', 'target-generation', 'target', 'target-binding', 'targets', 'target-operation', 'controller-pico-selection', 'controller-browser-session', 'target-error', 'pi-band-preferences', 'pi-wspr-list', 'pi-config', 'pi-solar-ordinary', 'pi-solar-gray', 'pi-scheduling', 'pi-candidate', 'target-observed-output', 'target-save', 'target-enable', 'target-action', 'target-stop', 'target-settings', 'target-counter', 'target-capability', 'target-context', 'target-clock', 'target-status', 'pi-recovery-unavailable', 'pi-recovery-preserved', 'pi-recovery-path', 'pi-recovery-config', 'pi-recovery-candidate', 'host-readiness', 'configuration-recovery', 'configuration-recovery-save', 'configuration-recovery-action', 'migration-controller', 'migration-retirement', 'migration-job', 'target-migration', 'target-migration-rollback', 'target-migration-read', 'controller-retirement', 'controller-retirement-operation', 'controller-retirement-entry', 'controller-retirements', 'pair-authorization', 'pair-gateway-resource', 'pair-gateway-binding', 'pair-manager', 'pair-browser-window', 'pairing-state', 'pair-window-request', 'pair-window-response', 'pair-remove', 'pair-connect', 'pair-discovery-entry', 'pair-discovery', 'target-operation-review', 'target-operation-reviewed', 'pi-wtp-binding']:
        raise ValueError('Controller document registry differs')
    if adapter['host_routes'] != {'GET': ['host/readiness', 'host/configuration-recovery', 'host/retirements', 'host/pairing', 'host/pairing/discovery', 'host/pairing/operations/{operation_id}'], 'PUT': ['host/configuration-recovery'], 'POST': ['host/configuration-recovery', 'host/retirements', 'host/pairing/window', 'host/pairing/remove', 'host/pairing/connect']}:
        raise ValueError('Controller host recovery routes differ')
    if any(name not in schema['$defs'] for name in adapter['definitions']):
        raise ValueError('Missing controller document definition')
    authorization = schema['$defs']['pair-authorization']
    if authorization.get('additionalProperties') is not False or set(authorization.get('required', [])) != {'pair_id', 'authorization_generation', 'renewal_required'} or set(authorization.get('properties', {})) != {'pair_id', 'authorization_generation', 'renewal_required'} or authorization['properties']['renewal_required'] != {'type': 'boolean'}:
        raise ValueError('Paired authorization renewal boundary differs')
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
