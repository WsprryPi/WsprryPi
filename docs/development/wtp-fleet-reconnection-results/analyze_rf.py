"""Assess corresponding SDR carriers in the finite reconnection captures.

Run on wspr5 with NumPy. Raw IQ stays outside the repository.
"""
import datetime
import json
import sys
from pathlib import Path

import numpy as np

stage = Path(sys.argv[1])
plan = json.loads((stage / 'plan.json').read_text())
accepted_first_four = '--accepted-first-four' in sys.argv[2:]
names = ['controller-network', 'target-subset-one', 'target-subset-two',
         'controller-crash', 'target-restart']
all_series = []
captures = []
rate, center, n, step = 250000, 14075100, 65536, 62500
frequencies = np.fft.fftfreq(n, 1 / rate) + center
targets = plan['receiver_outputs']
carriers = [14098100, 14099100, 14100100, 14101100, 14102100]
masks, noise = {}, {}
for name, hz in targets.items():
    distance = np.abs(frequencies - hz)
    masks[name] = distance < 300
    noise[name] = (distance > 500) & (distance < 3000)
    for carrier in carriers:
        noise[name] &= np.abs(frequencies - carrier) > 400
for name in names:
    if not (stage / (name + '-capture.json')).exists():
        continue
    meta = json.loads((stage / (name + '-capture.json')).read_text())
    captures.append({'name': name, 'metadata': meta})
    data = np.memmap(stage / (name + '.cf32'), dtype='<c8', mode='r')
    t0 = datetime.datetime.fromisoformat(meta['timestamps']['retained_capture_start_utc'].replace('Z', '+00:00')).timestamp()
    window = np.hanning(n)
    for offset in range(0, len(data) - n, step):
        power = np.abs(np.fft.fft(np.array(data[offset:offset+n]) * window)) ** 2
        row = {'utc_seconds': t0 + (offset + n / 2) / rate, 'capture': name}
        for target in targets:
            bins = np.flatnonzero(masks[target])
            peak = bins[np.argmax(power[bins])]
            row[target] = {'snr_db': float(10 * np.log10(power[peak] /
                max(float(np.median(power[noise[target]])), 1e-30))), 'peak_hz': float(frequencies[peak])}
        all_series.append(row)
    del data
results = []
last_slot = plan['cases'][3]['clean_slot'] if accepted_first_four else plan['last_slot']
for slot in range(plan['first_slot'], last_slot + 1, 120):
    outputs = {}
    for target in targets:
        on = [r[target] for r in all_series if slot+1 <= r['utc_seconds'] <= slot+9]
        off = [r[target] for r in all_series if slot+13 <= r['utc_seconds'] <= slot+18]
        on_db = [r['snr_db'] for r in on]
        off_db = [r['snr_db'] for r in off]
        interrupted = target == 'wspr2' and slot == plan['cases'][-1]['fault_slot']
        outputs[target] = {'samples': len(on), 'minimum_on_snr_db': min(on_db, default=None),
            'median_on_snr_db': float(np.median(on_db)) if on_db else None,
            'maximum_off_snr_db': max(off_db, default=None),
            'median_off_snr_db': float(np.median(off_db)) if off_db else None,
            'median_peak_hz': float(np.median([r['peak_hz'] for r in on])) if on else None,
            'intentionally_interrupted': interrupted,
            'corresponding_signal_observed': bool(on_db and off_db and min(on_db) > 30
                and float(np.median(on_db)) - float(np.median(off_db)) > 15)}
    results.append({'slot': slot, 'outputs': outputs})
result = {'criterion': 'Corresponding SDR signal; 30 dB peak over masked nearby median noise and 15 dB median on/off contrast. No RF-chain, calibrated frequency, decoding or precision edge-timing claim.',
    'scope':'Original first four cases through controller-crash clean slot only.' if accepted_first_four else 'Full recorded plan',
    'slots': results, 'captures': captures, 'fft_size': n, 'step_samples': step,
    'sample_rate': rate, 'center_hz': center,
    'all_uninterrupted_signals_observed': all(v['corresponding_signal_observed'] for r in results
        for v in r['outputs'].values() if not v['intentionally_interrupted'])}
recovery_slots = [c['clean_slot'] for c in (plan['cases'][:4] if accepted_first_four else plan['cases'])]
if plan.get('restart_only'):
    recovery_slots.append(plan['last_slot'])
result['all_recovery_round_signals_observed'] = all(
    v['corresponding_signal_observed'] for r in results if r['slot'] in recovery_slots
    for v in r['outputs'].values()) and all(s in [r['slot'] for r in results] for s in recovery_slots)
if plan.get('restart_only'):
    slot = plan['first_slot']
    result['interrupted_slot_initial_signals'] = {}
    for target in targets:
        initial = [r[target]['snr_db'] for r in all_series if slot+.2 <= r['utc_seconds'] <= slot+1.2]
        after_restart = [r[target]['snr_db'] for r in all_series if slot+3 <= r['utc_seconds'] <= slot+9]
        result['interrupted_slot_initial_signals'][target] = {
            'minimum_initial_snr_db':min(initial, default=None),
            'median_after_restart_snr_db':float(np.median(after_restart)) if after_restart else None,
            'initial_corresponding_signal_observed':bool(initial and min(initial)>30)}
    result['note'] = ('The interrupted slot retains the full-window conservative verdict. '
        'Initial carriers and subsequent clean recovery rounds are evaluated separately; '
        'these summaries do not qualify continuous RF or precise edges during the restart.')
(stage / ('accepted-rf-analysis.json' if accepted_first_four else 'rf-analysis.json')).write_text(json.dumps(result, indent=2) + '\n')
(stage / 'rf-series.json').write_text(json.dumps(all_series) + '\n')
print(json.dumps({'all_uninterrupted_signals_observed': result['all_uninterrupted_signals_observed'],
                  'all_recovery_round_signals_observed':result['all_recovery_round_signals_observed'],
                  'captures': len(captures), 'slots': len(results)}))
