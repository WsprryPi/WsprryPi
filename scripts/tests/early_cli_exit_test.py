#!/usr/bin/env python3
"""Bound informational CLI subprocess exit while another service may be running."""
import json
import os
from pathlib import Path
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())
expected_transport = sys.argv[2] if len(sys.argv) > 2 else "linux-i2c"
checks = 0
for option, required in [('--version', 'Compiled backends:'), ('-v', 'Ancillary GPIO:'),
                         ('--help', 'Usage:'), ('-h', 'Usage:'), ('--list-backends', 'wtp'),
                         ('--list-i2c-adapters', expected_transport)]:
    for logging in ([], ['--journald'], ['-J']):
        for repeat in range(3):
            result = subprocess.run([binary, *logging, option], text=True, capture_output=True,
                                    timeout=3, env=dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
            assert result.returncode == 0, (option, result.returncode, result.stderr)
            assert required.lower() in (result.stdout + result.stderr).lower(), option
            if option == '--list-i2c-adapters':
                body = json.loads(result.stdout)
                assert set(body) == {'Transport', 'I2C Adapters', 'Discovery Error'}
                assert body['Transport'] == expected_transport and isinstance(body['I2C Adapters'], list)
                assert isinstance(body['Discovery Error'], str)
            checks += 1
print(f'{checks} informational CLI exit checks passed')
