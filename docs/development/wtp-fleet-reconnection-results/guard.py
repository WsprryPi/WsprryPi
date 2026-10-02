"""Independent bounded cleanup timer, run as root on wspr5."""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

stage = Path(sys.argv[1])
plan = json.loads((stage / 'plan.json').read_text())
records = []
for address in plan['addresses'].values():
    route = subprocess.run(['ip', '-4', 'route', 'show', 'exact', address + '/32'], capture_output=True, text=True)
    if route.stdout.startswith('blackhole ' + address):
        result = subprocess.run(['ip', '-4', 'route', 'del', 'blackhole', address + '/32'], capture_output=True, text=True)
        records.append({'address': address, 'remove_exit': result.returncode})
base = 'http://127.0.0.1:31415'
def request(path, body=None, revision=None):
    headers = {'Origin': base, 'X-WsprryPico-Request': '1', 'Content-Type': 'application/json'}
    if revision:
        headers['If-Match'] = revision
    r = urllib.request.Request(base + '/api/v1/host/' + path,
        data=json.dumps(body).encode() if body is not None else None, headers=headers,
        method='POST' if body is not None else 'GET')
    with urllib.request.urlopen(r, timeout=15) as response:
        return json.load(response), response.headers.get('ETag')
for attempt in range(3):
    try:
        fleet, _ = request('fleet')
        for row in fleet['assignments']:
            _, revision = request('fleet')
            request('fleet', {'operation': 'pause', 'device_id': row['device_id']}, revision)
        fleet, _ = request('fleet')
        records.append({'attempt': attempt, 'paused': all(not r['enabled'] for r in fleet['assignments'])})
        if all(not r['enabled'] for r in fleet['assignments']):
            break
    except Exception as error:
        records.append({'attempt': attempt, 'error': repr(error)})
        time.sleep(3)
(stage / 'guard-cleanup.json').write_text(json.dumps(records, indent=2) + '\n')
