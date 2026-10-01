"""Keep Pico B's browser Pause test one hour in the future, then restore paused."""
import json
import sys
import time
import urllib.request
from pathlib import Path

base = 'http://127.0.0.1:31415'
path = '/api/v1/host/fleet'
device = '29f20b7342051ef947aa56cb9d4fab42'
stage = Path(sys.argv[1])

def read():
    with urllib.request.urlopen(base + path, timeout=10) as response:
        return json.load(response), response.headers['ETag']

def write(body):
    _, etag = read()
    request = urllib.request.Request(base + path, data=json.dumps(body).encode(),
        method='POST', headers={'Origin': base, 'X-WsprryPico-Request': '1',
        'Content-Type': 'application/json', 'If-Match': etag})
    with urllib.request.urlopen(request, timeout=45) as response:
        return json.load(response)

data, _ = read()
row = next((row for row in data['assignments'] if row['device_id'] == device), None)
if sys.argv[2] == 'prepare':
    assert row and not row['enabled'] and not row['in_flight']
    original = stage / 'browser-pause-original.json'
    assert not original.exists(), 'Preserve the original fixture snapshot'
    original.write_text(json.dumps(row, indent=2) + '\n')
    schedule = dict(row['schedule'], period_seconds=86400,
                    phase_seconds=(int(time.time()) + 3600) % 86400)
    write({'operation': 'schedule', 'device_id': device, 'schedule': schedule})
    print('Future schedule prepared; no RF start is due for one hour')
elif sys.argv[2] == 'restore':
    original = json.loads((stage / 'browser-pause-original.json').read_text())
    if row:
        assert not row['enabled'] and not row['in_flight'], 'Pause through the browser first'
        # An enabled future fixture consumes its selected slot. Explicitly
        # recreate the assignment rather than editing private consumed history.
        write({'operation': 'remove', 'device_id': device})
    restored = write({'operation': 'assign', 'name': original['name'],
        'settings': original['settings'], 'schedule': original['schedule'],
        'enabled': False, 'management_port': original['management_port'],
        'consent_plain': True})
    actual = next(row for row in restored['assignments'] if row['device_id'] == device)
    assert actual['schedule'] == original['schedule'] and not actual['enabled']
    assert actual['last_start_ns'] == '0'
    (stage / 'browser-pause-restored.json').write_text(json.dumps(actual, indent=2) + '\n')
    print('Original schedule restored paused')
else:
    raise ValueError('Use prepare or restore')
