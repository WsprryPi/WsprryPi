#!/usr/bin/env python3
"""Check unstripped application symbols for the saved-settings notification boundary."""
from pathlib import Path
import subprocess
import sys

if len(sys.argv)<2:
    raise SystemExit("Usage: application_monitor_audit.py <unstripped application> [...]")

for argument in sys.argv[1:]:
    binary = Path(argument).resolve(strict=True)
    symbols = subprocess.check_output(['nm', '-C', str(binary)], text=True)
    assert 'notify_saved_configuration_changed' in symbols, 'Audit requires the current unstripped application'
    assert 'MonitorFile' not in symbols and 'iniMonitor' not in symbols, 'Legacy file watcher linked into application'
    print(f'PASS no MonitorFile application symbols: {binary.name}')
