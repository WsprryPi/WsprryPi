#!/usr/bin/env python3
"""Vendor reviewed Pico Control/1 metadata and generate bounded Pi constants."""
import argparse
import hashlib
import json
import importlib.util
import os
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'src/wtp_integration/contract'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--stable-release', action='store_true', help='Require the reviewed released control snapshot')
    args = parser.parse_args()
    spec_path = DEST / 'control-1.schema.json'
    if args.source:
        original = args.source / 'engineering/protocol/control-1.schema.json'
        contents = original.read_bytes()
        for name, relative in [('control-policy.json', 'engineering/protocol/control-policy.json'),
                               ('network-diagnostics.schema.json', 'engineering/protocol/network-diagnostics.schema.json'),
                               ('check_control_policy.py', 'scripts/check_control_policy.py')]:
            source = (args.source / relative).read_bytes()
            if args.check:
                if (DEST / name).read_bytes() != source:
                    raise ValueError('Vendored lifecycle guard differs from selected Pico source: ' + name)
            else:
                DEST.mkdir(parents=True, exist_ok=True)
                (DEST / name).write_bytes(source)
        vectors=(args.source/'engineering/protocol/control-controller-vectors.json').read_bytes()
        if args.check:
            if (DEST/'control-controller-vectors.json').read_bytes()!=vectors:
                raise ValueError('Controller vectors differ from selected Pico source')
        else:
            (DEST/'control-controller-vectors.json').write_bytes(vectors)
        DEST.mkdir(parents=True, exist_ok=True)
        if not args.check:
            spec_path.write_bytes(contents)
            javascript=(args.source/'src/network/web/control-contract.js').read_bytes()
            (ROOT/'WsprryPi-UI/data/control-contract.js').write_bytes(javascript)
            si5351=(args.source/'src/network/web/si5351.js').read_bytes()
            (ROOT/'WsprryPi-UI/data/si5351.js').write_bytes(si5351)
            gps=(args.source/'src/network/web/gps.js').read_bytes()
            (ROOT/'WsprryPi-UI/data/gps.js').write_bytes(gps)
            (ROOT/'WsprryPi-UI/data/gray-line.js').write_bytes((args.source/'src/network/web/gray-line.js').read_bytes())
            revision = subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
            dirty = bool(subprocess.check_output(['git','-C',str(args.source),'status','--porcelain'],text=True))
            (DEST / 'PROVENANCE.json').write_text(json.dumps({'repository':'https://github.com/WsprryPi/WsprryPico','source_base_revision':revision,'source_uncommitted':dirty,'upstream':'engineering/protocol/control-1.schema.json','sha256':hashlib.sha256(contents).hexdigest(),'javascript_sha256':hashlib.sha256(javascript).hexdigest(),'si5351_javascript_sha256':hashlib.sha256(si5351).hexdigest(),'gps_javascript_sha256':hashlib.sha256(gps).hexdigest(),'gray_line_javascript_sha256':hashlib.sha256((ROOT/'WsprryPi-UI/data/gray-line.js').read_bytes()).hexdigest(),'controller_vectors_sha256':hashlib.sha256(vectors).hexdigest(),'network_diagnostics_sha256':hashlib.sha256((DEST/'network-diagnostics.schema.json').read_bytes()).hexdigest(),'policy_sha256':hashlib.sha256((DEST/'control-policy.json').read_bytes()).hexdigest(),'policy_checker_sha256':hashlib.sha256((DEST/'check_control_policy.py').read_bytes()).hexdigest()},indent=2)+'\n')
        elif spec_path.read_bytes() != contents:
            raise ValueError('Vendored Control/1 differs from selected Pico source')
        if args.check and (ROOT / 'WsprryPi-UI/data/control-contract.js').read_bytes() != (args.source / 'src/network/web/control-contract.js').read_bytes():
            raise ValueError('Shared browser registry differs from selected Pico source')
    if args.source and args.check and (ROOT/'WsprryPi-UI/data/si5351.js').read_bytes() != (args.source/'src/network/web/si5351.js').read_bytes():
        raise ValueError('Shared Si5351 controls differ from selected Pico source')
    if args.source and args.check and (ROOT/'WsprryPi-UI/data/gps.js').read_bytes() != (args.source/'src/network/web/gps.js').read_bytes():
        raise ValueError('Shared GPS controls differ from selected Pico source')
    if args.source and args.check and (ROOT/'WsprryPi-UI/data/gray-line.js').read_bytes() != (args.source/'src/network/web/gray-line.js').read_bytes():
        raise ValueError('Shared gray-line controls differ from selected Pico source')
    contents = spec_path.read_bytes()
    spec = json.loads(contents)
    provenance = json.loads((DEST / 'PROVENANCE.json').read_text())
    if provenance['sha256'] != hashlib.sha256(contents).hexdigest():
        raise ValueError('Control contract provenance hash mismatch')
    if provenance.get('javascript_sha256') != hashlib.sha256((ROOT/'WsprryPi-UI/data/control-contract.js').read_bytes()).hexdigest():
        raise ValueError('Generated browser registry provenance mismatch')
    if provenance.get('si5351_javascript_sha256') != hashlib.sha256((ROOT/'WsprryPi-UI/data/si5351.js').read_bytes()).hexdigest():
        raise ValueError('Shared Si5351 control provenance mismatch')
    if provenance.get('gps_javascript_sha256') != hashlib.sha256((ROOT/'WsprryPi-UI/data/gps.js').read_bytes()).hexdigest():
        raise ValueError('Shared GPS control provenance mismatch')
    if provenance.get('gray_line_javascript_sha256') != hashlib.sha256((ROOT/'WsprryPi-UI/data/gray-line.js').read_bytes()).hexdigest():
        raise ValueError('Shared gray-line control provenance mismatch')
    if provenance.get('controller_vectors_sha256') != hashlib.sha256((DEST/'control-controller-vectors.json').read_bytes()).hexdigest():
        raise ValueError('Controller vector provenance mismatch')
    for name, key in [('control-policy.json', 'policy_sha256'), ('check_control_policy.py', 'policy_checker_sha256'), ('network-diagnostics.schema.json', 'network_diagnostics_sha256')]:
        if provenance.get(key) != hashlib.sha256((DEST / name).read_bytes()).hexdigest():
            raise ValueError('Vendored lifecycle guard provenance mismatch: ' + name)
    module_spec = importlib.util.spec_from_file_location('control_lifecycle', DEST / 'check_control_policy.py')
    lifecycle = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(lifecycle)
    stable_tag = bool(re.fullmatch(r'refs/tags/v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?', os.environ.get('GITHUB_REF', '')))
    lifecycle.check_files(spec_path, DEST / 'control-policy.json', args.stable_release or stable_tag)
    routes = spec['management_routes']
    if set(routes) != {'GET','PUT','POST'}:
        raise ValueError('Unsupported control methods')
    header = '// Generated by scripts/generate_control_contract.py. Do not edit.\n#pragma once\n#include <string>\n#include <string_view>\nnamespace wsprrypi {\n'
    header += 'inline constexpr std::string_view control_contract = '+json.dumps(spec['contract'])+';\n'
    header += 'inline constexpr std::string_view control_schema_sha256 = '+json.dumps(lifecycle.schema_digest(spec))+';\n'
    header += 'inline constexpr std::string_view control_prefix = '+json.dumps(spec['namespace'])+';\n'
    header += 'inline bool control_route(std::string_view resource, std::string_view method) {\n'
    for method, names in routes.items():
        if len(names) > 128 or len(set(names)) != len(names):
            raise ValueError('Invalid bounded route registry')
        header += '  if (method == '+json.dumps(method)+') return '+' || '.join(('(resource.size()==43 && resource.starts_with(\"jobs/\") && resource.ends_with(\"/abort\") && resource.substr(5,32).find_first_not_of(\"0123456789abcdef\")==std::string_view::npos)' if n=='jobs/{id}/abort' else 'resource == '+json.dumps(n)) for n in names)+';\n'
    header += '  return false;\n}\n'
    for role in ('pi_management', 'pi_consumer_session'):
        scoped = spec['client_routes'][role]
        if not set(scoped).issubset({'GET','PUT','POST'}):
            raise ValueError('Unsupported client role methods')
        header += 'inline bool '+role+'_route(std::string_view resource, std::string_view method) {\n'
        for method, names in scoped.items():
            if not names or len(names)>128 or len(set(names))!=len(names) or not set(names).issubset(routes[method]):
                raise ValueError('Invalid scoped client role registry')
            header += '  if (method == '+json.dumps(method)+') return '+' || '.join('resource == '+json.dumps(name) for name in names)+';\n'
        header += '  return false;\n}\n'
    header += 'inline std::size_t control_request_limit(std::string_view resource) {\n'
    for name, maximum in spec['request_limits'].items():
        header += '  if (resource == '+json.dumps(name)+') return '+str(maximum)+';\n'
    header += '  return '+str(spec['maximum_body_bytes'])+';\n}\n'
    for function, key in [('control_descriptor','resource_descriptors'),('control_descriptor_policy','descriptor_policies'),('control_descriptor_runtime','descriptor_runtime_fields')]:
        header += 'inline std::string_view '+function+'(std::string_view name) {\n'
        for name, document in spec[key].items():
            body=json.dumps(document,separators=(',',':'))
            header += '  if (name == '+json.dumps(name)+') return R"CTRL('+body+')CTRL";\n'
        header += '  return "{}";\n}\n'
    capshape=json.dumps(spec['$defs']['capabilities'],separators=(',',':'))
    header += 'inline std::string_view control_capabilities_shape() { return R\"CTRL('+capshape+')CTRL\"; }\n'
    # Existing native reply definitions for the paired-Pi consumer. Resolve
    # only references owned by this exact authoritative Control/1 document.
    native={}
    def include_native(name):
        if name in native:return
        native[name]=spec['$defs'][name]
        def visit(value):
            if isinstance(value,dict):
                if '$ref' in value:
                    ref=value['$ref']
                    if not ref.startswith('#/$defs/'):raise ValueError('External native reply schema reference')
                    include_native(ref.split('/')[2])
                for child in value.values():visit(child)
            elif isinstance(value,list):
                for child in value:visit(child)
        visit(native[name])
    for name in ['application','progress','gpio-drive','filter-selector-resource','amplifier-timing-resource',
                 'gps','si5351','gray-line','udp-logging','udp-logging-capabilities','udp-logging-test','udp-logging-test-capabilities']:
        include_native(name)
    native_bytes=json.dumps(native,separators=(',',':'))
    if len(native_bytes)>1048576 or ')CTRL"' in native_bytes:raise ValueError('Native reply definitions exceed generation bound')
    header += 'inline std::string_view pico_management_response_definitions() {\n  static const std::string document = [] {\n    std::string value; value.reserve('+str(len(native_bytes))+');\n'
    for start in range(0,len(native_bytes),16384):
        header += '    value.append(R"CTRL('+native_bytes[start:start+16384]+')CTRL");\n'
    header += '    return value;\n  }();\n  return document;\n}\n'
    adapter=spec['controller_adapter']
    diagnostics=json.loads((DEST/'network-diagnostics.schema.json').read_text())
    header += 'inline std::string_view network_diagnostics_shape() { return R"CTRL('+json.dumps(diagnostics,separators=(',',':'))+')CTRL"; }\n'
    header += 'inline std::string_view controller_adapter_contract() { return R"CTRL('+json.dumps(adapter,separators=(',',':'))+')CTRL"; }\n'
    definitions={name:spec['$defs'][name] for name in adapter['definitions']}
    # Keep every literal below the portable C++ limit without weakening strict
    # compiler diagnostics. Adjacent literals are concatenated before Clang's
    # length check, so assemble the bounded immutable document once instead.
    body=json.dumps(definitions,separators=(',',':'))
    if len(body)>1048576 or ')CTRL"' in body:
        raise ValueError('Controller definition closure exceeds generation bound')
    header += 'inline std::string_view controller_document_definitions() {\n  static const std::string document = [] {\n    std::string value; value.reserve('+str(len(body))+');\n'
    for start in range(0,len(body),16384):
        header += '    value.append(R"CTRL('+body[start:start+16384]+')CTRL");\n'
    header += '    return value;\n  }();\n  return document;\n}\n'
    header += 'inline std::string_view pi_resource_descriptor(std::string_view resource) {\n'
    for resource,descriptor in lifecycle.pi_resource_descriptors(spec).items():
        header += '  if(resource=='+json.dumps(resource)+') return R"CTRL('+json.dumps(descriptor,separators=(',',':'))+')CTRL";\n'
    header += '  return {};\n}\n'
    header += 'inline bool controller_pico_route(std::string_view resource,std::string_view method) {\n'
    for method,names in adapter['pico_routes'].items():
        header += '  if(method=='+json.dumps(method)+') return '+' || '.join('resource=='+json.dumps(name) for name in names)+';\n'
    header += '  return false;\n}\n'
    header += 'inline std::string_view controller_pico_recovery_read(std::string_view resource) {\n'
    for resource,read in adapter['reconciliation_reads'].items():
        header += '  if(resource=='+json.dumps(resource)+') return '+json.dumps(read)+';\n'
    header += '  return {};\n}\n'
    header += '}\n'
    generated = ROOT/'src/wtp_integration/control_contract.hpp'
    if args.check:
        if generated.read_text() != header:
            raise ValueError('Stale generated control metadata')
    else:
        generated.write_text(header)

if __name__ == '__main__':
    main()
