"""Assess recorded controller completions and the second SDR observation group."""
import datetime
import json
import re
import sys
from pathlib import Path

import numpy as np

stage = Path(sys.argv[1])
plan = json.loads((stage / 'run-plan.json').read_text())
meta = json.loads((stage / 'capture.json').read_text())
expected_slots = {str(value * 1000000000) for value in plan['slots']}
reports = {}
active = {value: [] for value in expected_slots}
overlap = []
for line in (stage / 'observations.jsonl').read_text().splitlines():
    observation = json.loads(line)
    fleet = observation['fleet']
    for assignment in fleet['assignments']:
        report = fleet['outputs'][assignment['device_id']]['wtp'].get('last_report')
        if report:
            reports[(assignment['device_id'], report['job_id'])] = {
                'name': assignment['name'], 'device_id': assignment['device_id'], **report}
    for slot_ns in expected_slots:
        all_active = all(fleet['outputs'][a['device_id']]['wtp'].get('start_utc_ns') == slot_ns
            and (fleet['outputs'][a['device_id']]['wtp'].get('remote') or {}).get('output_active') is True
            for a in fleet['assignments'])
        if all_active:
            active[slot_ns].append(observation['utc_ns'])
            if observation['local_endpoint']['local_work_active']:
                overlap.append(observation['utc_ns'])
final = json.loads((stage / 'final-controller.json').read_text())
per_output = {}
for assignment in final['fleet']['assignments']:
    completed = [report for report in reports.values()
        if report['device_id'] == assignment['device_id']
        and report['start_utc_ns'] in expected_slots and report['outcome'] == 'complete']
    passed = len(completed) == 3 and {r['start_utc_ns'] for r in completed} == expected_slots and all(
        r['execution']['ok'] and r['execution']['cleanup']['ok'] and
        r['job']['state'] == 'complete' and not r['job']['output_active'] for r in completed)
    per_output[assignment['device_id']] = {'name': assignment['name'], 'passed': passed,
                                         'completed': completed}
endpoint = final['local_endpoint']
known_off = all(not a['enabled'] and not a['in_flight'] and
    not final['fleet']['outputs'][a['device_id']]['wtp']['owns'] and
    not final['fleet']['outputs'][a['device_id']]['wtp']['uncertain']
    for a in final['fleet']['assignments']) and not endpoint['local_requested'] and not endpoint['local_work_active'] and not endpoint['output_unknown']
journal = (stage / 'controller-journal.log').read_text()
completion_lines = [line for line in journal.splitlines() if 'Completed transmission' in line]
durations = [float(match.group(1)) for line in completion_lines
    if (match := re.search(r'([0-9]+(?:\.[0-9]+)?) seconds', line))]
local_completed = len(durations) == 1 and abs(durations[0] - 110.592) < 0.2
controller = {'five_remote_targets_three_slots_passed': all(v['passed'] for v in per_output.values()) and all(active.values()),
    'completed_remote_jobs': sum(len(v['completed']) for v in per_output.values()),
    'per_output': per_output, 'simultaneously_active_observations': active,
    'local_work_overlap_observations': overlap, 'local_frame_completed': local_completed,
    'local_completion_lines': completion_lines, 'all_outputs_paused_known_off': known_off,
    'runner_error': (stage / 'runner-error.json').read_text() if (stage / 'runner-error.json').exists() else None}
(stage / 'controller-analysis.json').write_text(json.dumps(controller, indent=2) + '\n')

rate = plan['sample_rate']
center = plan['capture_center_hz']
n = 65536
step = 125000
data = np.memmap(stage / 'capture.cf32', dtype='<c8', mode='r')
t0 = datetime.datetime.fromisoformat(meta['timestamps']['retained_capture_start_utc'].replace('Z', '+00:00')).timestamp()
frequencies = np.fft.fftfreq(n, 1 / rate) + center
targets = plan['observed_outputs']
all_frequencies = [14097100, 14098100, 14099100, 14100100, 14101100, 14102100]
masks = {name: np.abs(frequencies - hz) < 300 for name, hz in targets.items()}
noise = {}
for name, hz in targets.items():
    distance = np.abs(frequencies - hz)
    mask = (distance > 500) & (distance < 3000)
    for carrier in all_frequencies:
        mask &= np.abs(frequencies - carrier) > 400
    noise[name] = mask
series = []
window = np.hanning(n)
for offset in range(0, len(data) - n, step):
    power = np.abs(np.fft.fft(np.array(data[offset:offset+n]) * window)) ** 2
    row = {'utc_seconds': t0 + (offset + n / 2) / rate}
    for name in targets:
        bins = np.flatnonzero(masks[name])
        peak = bins[np.argmax(power[bins])]
        row[name] = {'snr_db': float(10 * np.log10(power[peak] /
            max(float(np.median(power[noise[name]])), 1e-30))),
            'peak_hz': float(frequencies[peak])}
    series.append(row)


def assess(name, begin, end, off_begin, off_end):
    during = [r[name] for r in series if begin <= r['utc_seconds'] <= end]
    after = [r[name] for r in series if off_begin <= r['utc_seconds'] <= off_end]
    on = [r['snr_db'] for r in during]
    off = [r['snr_db'] for r in after]
    return {'samples': len(on), 'minimum_on_snr_db': min(on, default=None),
        'median_on_snr_db': float(np.median(on)) if on else None,
        'maximum_off_snr_db': max(off, default=None),
        'median_off_snr_db': float(np.median(off)) if off else None,
        'median_peak_hz': float(np.median([r['peak_hz'] for r in during])) if during else None,
        'corresponding_signal_observed': bool(on and off and min(on) > 30
            and float(np.median(on)) - float(np.median(off)) > 15)}


remote_slots = []
for start in plan['slots']:
    outputs = {name: assess(name, start + 1, start + 9, start + 13, start + 18)
               for name in ('wspr2', 'Pico A')}
    remote_slots.append({'start_utc_seconds': start, 'outputs': outputs,
        'both_signals_observed': all(v['corresponding_signal_observed'] for v in outputs.values())})
start = plan['slot_utc_seconds']
local = assess('wspr5', start + 1, start + 109, start + 114, start + 119)
local['intended_duration_ns'] = plan['local_frame_duration_ns']
local['precision_edge_timing_claimed'] = False
rf = {'criterion': 'Corresponding SDR signal, 30 dB peak over masked nearby median noise throughout slot interior, and 15 dB median on/off contrast. No RF-chain, frequency calibration or decode qualification.',
    'capture_metadata': meta, 'capture_exit': json.loads((stage / 'capture-exit.json').read_text()),
    'remote_slots': remote_slots, 'local_wspr': local,
    'all_second_group_signals_observed': all(r['both_signals_observed'] for r in remote_slots)
        and local['corresponding_signal_observed'],
    'fft_size': n, 'step_samples': step, 'sample_rate': rate,
    'timestamps': 'FFT window centres from retained capture UTC; not precision edge timing.'}
(stage / 'rf-analysis.json').write_text(json.dumps(rf, indent=2) + '\n')
(stage / 'rf-series.json').write_text(json.dumps(series) + '\n')
print(json.dumps({'remote_batch_passed': controller['five_remote_targets_three_slots_passed'],
    'completed_remote_jobs': controller['completed_remote_jobs'],
    'local_frame_completed': local_completed, 'known_off': known_off,
    'second_group_rf_passed': rf['all_second_group_signals_observed'],
    'local_wspr_rf': local, 'remote_rf': remote_slots}, indent=2))
