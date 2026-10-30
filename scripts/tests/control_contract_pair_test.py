#!/usr/bin/env python3
"""Reject drift between the selected producer and the Pi's shared metadata."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / 'src/wtp_integration/contract'
FILES = {
    'engineering/protocol/control-1.schema.json': VENDOR / 'control-1.schema.json',
    'engineering/protocol/control-policy.json': VENDOR / 'control-policy.json',
    'scripts/check_control_policy.py': VENDOR / 'check_control_policy.py',
    'src/network/web/control-contract.js': ROOT / 'WsprryPi-UI/data/control-contract.js',
}
environment = dict(os.environ, GITHUB_REF='refs/heads/control-pair-test')
with tempfile.TemporaryDirectory() as directory:
    producer = Path(directory)
    for relative, source in FILES.items():
        destination = producer / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(source.read_bytes())
    command = [sys.executable, str(ROOT / 'scripts/generate_control_contract.py'),
               '--source', str(producer), '--check']
    result = subprocess.run(command, capture_output=True, text=True, env=environment)
    assert result.returncode == 0, result.stderr
    for relative in FILES:
        destination = producer / relative
        original = destination.read_bytes()
        destination.write_bytes(original + b'\n ')
        result = subprocess.run(command, capture_output=True, text=True, env=environment)
        assert result.returncode != 0, 'Selected-source drift accepted: ' + relative
        assert 'differs from selected Pico source' in result.stderr, result.stderr
        destination.write_bytes(original)
print('PASS selected-source schema, lifecycle, checker and browser registry parity')
