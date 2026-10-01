"""Observe managed RP1 preparation across repeated Enable, then stop before RF."""
import json
import sys
import time
import urllib.request
from pathlib import Path

base = 'http://127.0.0.1:31415'
stage = Path(sys.argv[1])

def read(path):
    with urllib.request.urlopen(base + path, timeout=3) as response:
        return json.load(response), response.headers.get('ETag')

def enable(value):
    _, etag = read('/api/v1/host/config')
    request = urllib.request.Request(base + '/api/v1/host/config',
        data=json.dumps({'Operation': {'Transmit': value}}).encode(), method='PUT',
        headers={'Origin': base, 'X-WsprryPico-Request': '1',
                 'Content-Type': 'application/json', 'If-Match': etag})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)

config, _ = read('/api/v1/host/config')
assert config['config']['Operation']['Mode'] == 'WSPR'
assert config['config']['Operation']['Transmit Backend'] == 'rp1-gpclk'
assert not config['config']['Operation']['Transmit']
assert config['config']['WSPR']['Frequency'] == '20m'
remaining = 120 - time.time() % 120
assert remaining > 40, 'Keep this short check well before the next WSPR slot'
rf_slot = (int(time.time()) // 120 + 1) * 120 + 1
observations = []
try:
    enable(True)
    deadline = time.time() + 8
    repeated = False
    while time.time() < deadline:
        status, _ = read('/api/v1/host/wtp-endpoint')
        observations.append({'utc_ns': str(time.time_ns()), 'endpoint': status})
        if status['local_work_active'] and not repeated:
            enable(True)
            repeated = True
        time.sleep(0.4)
    assert repeated and all(row['endpoint']['local_effective'] and
        row['endpoint']['local_work_active'] for row in observations[-5:]), \
        'Repeated Enable must preserve the already enabled waiting schedule'
finally:
    enable(False)
    time.sleep(1)
    final, _ = read('/api/v1/host/wtp-endpoint')
    result = {'observations': observations, 'final': final,
              'rf_slot_utc_seconds': rf_slot,
              'rf_start_not_reached': time.time() < rf_slot,
              'scope': 'Preparation/admission only; not physical-frame completion'}
    (stage / 'managed-wait-check.json').write_text(json.dumps(result, indent=2) + '\n')
    assert not final['local_requested'] and not final['local_work_active'] and not final['output_unknown']
print('Managed preparation survived repeated Enable and stopped before its RF window')
