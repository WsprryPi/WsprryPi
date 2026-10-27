#!/usr/bin/env python3
"""Bound informational CLI subprocess exit while another service may be running."""
import os
from pathlib import Path
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())
checks = 0
for option, required in [('--version', 'Compiled backends:'), ('-v', 'Ancillary GPIO:'),
                         ('--help', 'Usage:'), ('-h', 'Usage:'), ('--list-backends', 'wtp')]:
    for logging in ([], ['--journald'], ['-J']):
        for repeat in range(3):
            result = subprocess.run([binary, *logging, option], text=True, capture_output=True,
                                    timeout=3, env=dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
            assert result.returncode == 0, (option, result.returncode, result.stderr)
            assert required.lower() in (result.stdout + result.stderr).lower(), option
            checks += 1
print(f'{checks} informational CLI exit checks passed')
