"""Bind finite inbound WTP results to corresponding SDR observations."""
import datetime
import hashlib
import json
import sys
from pathlib import Path

import numpy as np

stage = Path(sys.argv[1])
results = {}
manifest = []
for mode in ('tone', 'qrss', 'fskcw', 'dfcw', 'wspr'):
    protocol = json.loads((stage / (mode + '-protocol.json')).read_text())
    meta = json.loads((stage / (mode + '-capture.json')).read_text())
    path = stage / (mode + '.cf32')
    data = np.memmap(path, dtype='<c8', mode='r')
    n, step, rate, center = 65536, 62500, 250000, 14075100
    assert meta['actual_settings']['sample_rate_hz'] == rate
    assert meta['actual_settings']['center_frequency_hz'] == center
    frequencies = np.fft.fftfreq(n, 1 / rate) + center
    mask = np.abs(frequencies - 14097100) < 300
    distance = np.abs(frequencies - 14097100)
    noise = (distance > 500) & (distance < 3000)
    t0 = datetime.datetime.fromisoformat(meta['timestamps']['retained_capture_start_utc'].replace('Z', '+00:00')).timestamp()
    window = np.hanning(n)
    series = []
    for offset in range(0, len(data) - n + 1, step):
        ps = np.abs(np.fft.fft(np.array(data[offset:offset + n]) * window)) ** 2
        series.append({'utc_s': t0 + (offset + n / 2) / rate,
            'snr_db': float(10 * np.log10(max(float(ps[mask].max()), 1e-30) /
                max(float(np.median(ps[noise])), 1e-30)))})
    start = int(protocol['start_utc_ns']) / 1e9
    end = start + int(protocol['duration_ns']) / 1e9
    event_results = []
    # Interior windows avoid pretending that FFT observations measure RF edges.
    for event in protocol['events']:
        begin = start + int(event['offset_ns']) / 1e9
        finish = begin + int(event['duration_ns']) / 1e9
        values = [r['snr_db'] for r in series if begin + .2 <= r['utc_s'] <= finish - .2]
        assert values, (mode, event)
        event_results.append({'rf_on': event['rf_on'], 'samples': len(values),
            'minimum_snr_db': min(values), 'median_snr_db': float(np.median(values))})
    after = [r['snr_db'] for r in series if end + 1 <= r['utc_s'] <= end + 4]
    assert after
    on = [e for e in event_results if e['rf_on']]
    off = [e for e in event_results if not e['rf_on']]
    off_level = float(np.median(after))
    corresponding = all(e['minimum_snr_db'] > 30 and e['median_snr_db'] - off_level > 15 for e in on)
    gaps = all(e['median_snr_db'] < min(x['median_snr_db'] for x in on) - 15 for e in off)
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(4 * 1024 * 1024):
            h.update(chunk)
    raw_hash = h.hexdigest()
    assert raw_hash == meta['output']['sha256'] and len(data) == meta['retained_sample_count']
    receiver_ok = meta['primary_outcome'] == 'success' and meta['cleanup']['outcome'] == 'verified' and meta['process_exit_code'] == 0
    protocol_ok = protocol['passed'] and protocol['completed']['state'] == 'complete' and not protocol['completed']['output_active'] and protocol['after']['state'] == 'empty' and not protocol['after']['owner_id'] and not protocol['after']['output_active'] and not protocol['after_endpoint']['output_unknown']
    assert protocol['client_host'] == 'wspr4' and protocol['local_address'][0] == '192.168.1.120'
    passed = bool(corresponding and gaps and receiver_ok and protocol_ok)
    results[mode] = {'passed': passed, 'job_id': protocol['job_id'], 'start_utc_ns': protocol['start_utc_ns'],
        'duration_ns': protocol['duration_ns'], 'event_count': len(event_results),
        'minimum_on_snr_db': min(e['minimum_snr_db'] for e in on),
        'post_job_median_snr_db': off_level, 'corresponding_signal_observed': corresponding,
        'keyed_off_gaps_observed': gaps if off else None, 'complete_output_off_released': protocol_ok,
        'receiver_cleanup_verified': receiver_ok, 'overflow_count': meta['overflow_count'],
        'timeout_count': meta['timeout_count'], 'clipped_samples': meta['clipping']['sample_count']}
    (stage / (mode + '-rf-series.json')).write_text(json.dumps(series) + '\n')
    raw_protocol = stage / (mode + '-protocol.json')
    manifest += [{'path': str(path), 'bytes': path.stat().st_size, 'sha256': raw_hash},
                 {'path': str(raw_protocol), 'bytes': raw_protocol.stat().st_size,
                  'sha256': hashlib.sha256(raw_protocol.read_bytes()).hexdigest()}]
    transitions = []
    for observation in protocol['observations']:
        if not transitions or observation['status']['state'] != transitions[-1]['status']['state']:
            transitions.append(observation)
    compact = {k: v for k, v in protocol.items() if k not in ('wire', 'observations')}
    compact['transitions'] = transitions
    compact['running_observations'] = sum(r['status']['state'] == 'running' and r['status']['output_active'] for r in protocol['observations'])
    (stage / (mode + '-summary.json')).write_text(json.dumps(compact, indent=2) + '\n')
result = {'all_five_modes_passed': all(r['passed'] for r in results.values()), 'modes': results,
    'criterion': 'Corresponding SDR signal; no RF-chain, calibrated frequency, RF-edge timing or message-decode qualification.',
    'method': {'fft_size': 65536, 'step_seconds': .25, 'sample_rate_hz': 250000,
        'signal_threshold_db': 30, 'minimum_on_off_contrast_db': 15, 'interior_margin_s': .2}}
(stage / 'acceptance.json').write_text(json.dumps(result, indent=2) + '\n')
(stage / 'private-evidence-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(result, indent=2))
assert result['all_five_modes_passed'], result
