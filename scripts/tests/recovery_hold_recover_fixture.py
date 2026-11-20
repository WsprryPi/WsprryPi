#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Called by the actual C++ crash fixture with disposable private paths only."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery.publication import recover_hold_preparation
assert len(sys.argv) == 3
assert recover_hold_preparation(sys.argv[1], sys.argv[2]) == {'state': 'maintenance_held', 'generation': '1'}
assert recover_hold_preparation(sys.argv[1], sys.argv[2]) is None
