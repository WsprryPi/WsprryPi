#!/usr/bin/env python3
"""Read-only installed-journal regression: valid tail resumes never replay old rows."""
import json
import os
import subprocess
from urllib.parse import urlencode

endpoint = os.environ.get('WSPRRYPI_LOG_STREAM_ENDPOINT', 'http://127.0.0.1/wsprrypi/log_stream.php')
source = os.environ.get('WSPRRYPI_LOG_SOURCE', 'all')

def read(cursor=None):
    query = {'source': source, 'backlog': '100', 'heartbeat': '5'}
    if cursor is not None:
        query['cursor'] = cursor
    response = subprocess.run(['curl', '-fsS', '--max-time', '3', endpoint + '?' + urlencode(query)], capture_output=True, timeout=6)
    if response.returncode not in (0, 28):
        raise RuntimeError(response.stderr.decode())
    return [json.loads(line[6:]) for line in response.stdout.decode().splitlines() if line.startswith('data: ')]

initial = [row for row in read() if row.get('type') == 'journal']
if not initial:
    raise RuntimeError('This regression requires retained service journal records for the selected source')
old_cursors = {row['__CURSOR'] for row in initial}
resumed = read(initial[-1]['__CURSOR'])
assert not old_cursors.intersection(row['__CURSOR'] for row in resumed if row.get('type') == 'journal'), 'Valid tail cursor replayed retained records'
assert not any('Falling back' in row.get('MESSAGE', '') for row in resumed if row.get('type') == 'internal')
recovered = read('not-a-cursor')
assert any('Falling back' in row.get('MESSAGE', '') for row in recovered if row.get('type') == 'internal'), 'Invalid/rotated cursor did not recover backlog'
assert any(row.get('type') == 'journal' for row in recovered)
if source.startswith('pico:'):
    assert all(row.get('source') == source for row in initial + [r for r in resumed + recovered if r.get('type') == 'journal'])
print('PASS: retained tail cursor does not replay; invalid cursor recovers scoped backlog')
