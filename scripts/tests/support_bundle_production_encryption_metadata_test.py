#!/usr/bin/env python3
"""Compare compiled production encryption bindings with public provisioning metadata."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
metadata = json.loads((ROOT / 'config/support-bundle-intake/wsprrypi-bundle-2026-01.public.json').read_text())
assert type(metadata['schema_version']) is int and metadata['schema_version'] == 1 and metadata['project_id'] == 'wsprrypi'
assert metadata['purpose'] == 'support_bundle_encryption' and metadata['algorithm'] == 'age-x25519'
header = ROOT / 'src/support_bundle_encryption_production.hpp'
# Architectural policy: production bindings may contain only public encryption material.
assert 'AGE-SECRET-KEY-' not in header.read_text()
with tempfile.TemporaryDirectory(prefix='support-production-key-') as raw:
    work = Path(raw)
    source = work / 'bindings.cpp'
    source.write_text('#include "support_bundle_encryption_production.hpp"\n#include <iostream>\n'
                      'int main() { std::cout << kSupportBundleProductionKeyId << "\\n" '
                      '<< kSupportBundleProductionAgeRecipient << "\\n"; }\n')
    binary = work / 'bindings'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-I' + str(header.parent),
                    str(source), '-o', str(binary)], check=True, timeout=30)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=5)
    assert result.stdout.splitlines() == [metadata['key_id'], metadata['recipient']], result.stdout
print('Compiled support encryption bindings agree exactly with public metadata')
