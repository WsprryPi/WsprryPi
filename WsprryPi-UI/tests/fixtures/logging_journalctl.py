#!/usr/bin/env python3
"""Private, hardware-free journalctl fixture for the production PHP SSE adapter."""
import json
import os
import sys
import time

args = sys.argv[1:]

def values(flag):
    return [args[i + 1] for i, value in enumerate(args[:-1]) if value == flag]

def rows():
    with open(os.environ['LOGGING_JOURNAL_FIXTURE'], encoding='utf-8') as source:
        return json.load(source)

initial = rows()
cursor = (values('--after-cursor') or values('--cursor') or [None])[0]
positions = {row['__CURSOR']: i for i, row in enumerate(initial)}
if cursor is not None and cursor not in positions:
    sys.stderr.write('Failed to seek to cursor\n')
    sys.exit(1)
priority = (values('-p') or ['0..7'])[0].split('..')
low, high = int(priority[0]), int(priority[-1])
units = values('-u')
matches = [arg.split('=', 1) for arg in args if arg.startswith('WSPRRY_') and '=' in arg]

def selected(row):
    return (not units or row.get('_SYSTEMD_UNIT') in units) and low <= int(row['PRIORITY']) <= high and all(row.get(key) == value for key, value in matches)

start = positions.get(cursor, -1) + (1 if '--after-cursor' in args else 0)
chosen = [row for i, row in enumerate(initial) if i >= start and selected(row)]
if '-n' in args:
    chosen = chosen[-int(values('-n')[0]):]
for row in chosen:
    print(json.dumps(row), flush=True)
if '-f' in args:
    seen = len(initial)
    while True:
        current = rows()
        for row in current[seen:]:
            if selected(row):
                print(json.dumps(row), flush=True)
        seen = len(current)
        time.sleep(0.05)
