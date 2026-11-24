#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Typed same-UID peer fixture; production requires a root maintenance peer."""
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from application_recovery.state_owner import serve


def fixture_home(context):
    ledger = Path(context['backend_command'][1])
    return ledger.parent / (ledger.name + '-backend-home')


if __name__ == '__main__':
    raise SystemExit(serve(int(sys.argv[2]), maintenance_uid=os.geteuid(), owner_home=fixture_home))
