"""Finite physical WTP acceptance client; run on wspr4, targeting wspr5."""
import json
import socket
import sys
import time
from pathlib import Path

from probe import Wtp, http

stage, mode, job = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
duration = 110_592_000_000 if mode == 'wspr' else (10 if mode == 'tone' else 5) * 1_000_000_000
frame = json.loads((stage / 'frame.json').read_text())
patterns = {'tone': [0], 'qrss': [0, None, 0, None, 0],
            'fskcw': [0, 1, 0, 1, 0], 'dfcw': [0, None, 1, None, 0],
            'wspr': [int(v) for v in frame['symbols']]}
assert len(patterns['wspr']) == 162
events = []
for i, tone in enumerate(patterns[mode]):
    begin = duration * i // len(patterns[mode])
    event = {'offset_ns': str(begin),
             'duration_ns': str(duration * (i + 1) // len(patterns[mode]) - begin),
             'rf_on': tone is not None}
    if tone is not None:
        event['frequency_nhz'] = str(14_097_100_000_000_000 + tone * (1_464_843_750 if mode == 'wspr' else 4_000_000_000))
    events.append(event)
wire = Wtp('192.168.1.54', 31417)
record = {'client_host': socket.gethostname(), 'local_address': wire.sock.getsockname(),
          'peer_address': wire.sock.getpeername(), 'hello': wire.hello,
          'job_id': job, 'mode': mode, 'duration_ns': str(duration), 'events': events,
          'frame_metadata': {k: v for k, v in frame.items() if k != 'symbols'}, 'observations': []}
path = stage / (mode + '-protocol.json')
claimed = False
complete = False
def save():
    path.write_text(json.dumps(record, indent=2) + '\n')
try:
    record['before_endpoint'] = http('192.168.1.54', 31415, 'GET', 'wtp-endpoint')[2]
    before = wire.request('STATUS')['body']
    assert before['state'] == 'empty' and not before['output_active'] and not before['owner_id'], before
    assert not record['before_endpoint']['local_requested'] and not record['before_endpoint']['output_unknown']
    record['caps'] = wire.request('CAPS')
    record['clock'] = wire.request('GET_CLOCK')
    wire.request('CLAIM', {'owner_id': wire.owner, 'lease_ms': 60000})
    claimed = True
    record['load'] = wire.request('LOAD', {'job_id': job, 'profile': 'rf-events/1', 'mode': mode,
        'total_duration_ns': str(duration), 'allow_frequency_adjustment': True, 'events': events})
    start = (int(time.time()) + 5) * 1_000_000_000
    record['start_utc_ns'] = str(start)
    record['arm'] = wire.request('ARM', {'job_id': job, 'start_utc_ns': str(start),
        'max_start_uncertainty_ns': '500000000'})
    deadline = time.monotonic() + duration / 1e9 + 15
    renew = time.monotonic() + 15
    seen = False
    while time.monotonic() < deadline:
        state = wire.request('STATUS')['body']
        record['observations'].append({'utc_ns': str(time.time_ns()), 'status': state})
        save()
        seen |= state['state'] == 'running' and state['output_active']
        if state['state'] not in ('armed', 'running'):
            break
        if time.monotonic() >= renew:
            wire.request('RENEW', {'owner_id': wire.owner, 'lease_ms': 60000})
            renew = time.monotonic() + 15
        time.sleep(.2)
    assert seen and state['state'] == 'complete' and not state['output_active'], state
    complete = True
    record['completed'] = state
    record['passed'] = True
except Exception as error:
    record['failure'] = str(error)
    raise
finally:
    if claimed:
        if not complete:
            record['cleanup_abort'] = wire.request('ABORT', {'job_id': job}, okay=False)
        record['release'] = wire.request('RELEASE', okay=False)
    record['after'] = wire.request('STATUS')['body']
    record['after_endpoint'] = http('192.168.1.54', 31415, 'GET', 'wtp-endpoint')[2]
    record['wire'] = wire.records
    save()
    wire.close()
assert record['after']['state'] == 'empty' and not record['after']['owner_id'] and not record['after']['output_active']
assert not record['after_endpoint']['output_unknown']
print(json.dumps({'mode': mode, 'job_id': job, 'passed': record['passed']}), flush=True)
