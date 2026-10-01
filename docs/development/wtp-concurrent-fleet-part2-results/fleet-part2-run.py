"""Bounded live acceptance: five remote tones plus one managed local WSPR frame."""
import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

stage = Path(sys.argv[1])
slot = int(sys.argv[2])
base = 'http://127.0.0.1:31415'
stopping = False
expected = {
    'd133ed890b90f6945b41a235429ff155': 14099100,
    'f21e530af8a4a210ec57898c0ebd32d2': 14098100,
    'e2a4d273f7d8f24f6696f7c6fb95ff11': 14101100,
    'fd6127d11d6aca42a9905fa3fb1bf1d5': 14100100,
    '29f20b7342051ef947aa56cb9d4fab42': 14102100,
}


def http(path, method='GET', body=None, etag=None):
    headers = {'Origin': base, 'X-WsprryPico-Request': '1'}
    if body is not None:
        headers['Content-Type'] = 'application/json'
    if etag:
        headers['If-Match'] = etag
    request = urllib.request.Request(base + path,
        data=None if body is None else json.dumps(body).encode(),
        headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=8) as response:
            return json.load(response), response.headers.get('ETag')
    except urllib.error.HTTPError as error:
        raise RuntimeError(f'{method} {path}: {error.code}: '
                           + error.read().decode()) from error


def local_enable(value):
    _, etag = http('/api/v1/host/config')
    return http('/api/v1/host/config', 'PUT',
                {'Operation': {'Transmit': value}}, etag)[0]


def assignment_action(device_id, operation):
    _, etag = http('/api/v1/host/fleet')
    return http('/api/v1/host/fleet', 'POST',
                {'operation': operation, 'device_id': device_id}, etag)[0]


def pause_all():
    errors = []
    for _ in range(3):
        try:
            local_enable(False)
        except Exception as error:
            errors.append({'action': 'local_disable', 'error': repr(error)})
        for device_id in expected:
            try:
                assignment_action(device_id, 'pause')
            except Exception as error:
                errors.append({'action': 'pause', 'device_id': device_id,
                               'error': repr(error)})
        try:
            fleet, _ = http('/api/v1/host/fleet')
            endpoint, _ = http('/api/v1/host/wtp-endpoint')
            if all(not a['enabled'] and not a['in_flight'] for a in fleet['assignments']) and not endpoint['local_requested'] and not endpoint['local_work_active'] and not endpoint['output_unknown']:
                return errors
        except Exception as error:
            errors.append({'action': 'verify', 'error': repr(error)})
        time.sleep(1)
    errors.append({'action': 'verify', 'error': 'Stopped state was not established'})
    return errors


def interrupted(signum, frame):
    global stopping
    stopping = True


for signum in (signal.SIGINT, signal.SIGTERM):
    signal.signal(signum, interrupted)

started = time.time()
plan = {'slot_utc_seconds': slot, 'slots': [slot, slot + 120, slot + 240],
        'local_stop_utc_seconds': slot + 114,
        'remote_stop_utc_seconds': slot + 255,
        'capture_center_hz': 14075100, 'sample_rate': 250000,
        'observed_outputs': {'wspr5': 14097100, 'Pico A': 14100100, 'wspr2': 14101100},
        'local_frame_duration_ns': 110592000000,
        'source_commit': '9a1b8c10036f77f8446b912c481b3ea5dc3d39e8',
        'stage': str(stage)}
(stage / 'run-plan.json').write_text(json.dumps(plan, indent=2) + '\n')
capture = None
failure = None
try:
    assert 35 <= slot - time.time() <= 120, 'Need 35 seconds of lead for this run'
    assert slot % 120 == 1
    config, _ = http('/api/v1/host/config')
    assert config['config']['Operation']['Mode'] == 'WSPR'
    assert config['config']['Operation']['Transmit Backend'] == 'rp1-gpclk'
    assert not config['config']['Operation']['Transmit']
    assert config['config']['GPIO']['Transmit Pin'] == 20
    assert config['config']['WSPR']['Frequency'] == '20m'
    assert not config['config']['WSPR']['Use Random Offset']
    fleet, _ = http('/api/v1/host/fleet')
    assert {a['device_id'] for a in fleet['assignments']} == set(expected)
    for assignment in fleet['assignments']:
        assert not assignment['enabled'] and not assignment['in_flight']
        assert assignment['schedule'] == {'mode': 'tone', 'duration_ms': 10000,
            'period_seconds': 120, 'phase_seconds': 1,
            'frequency_hz': expected[assignment['device_id']]}
    (stage / 'pretest-controller.json').write_text(json.dumps(
        {'config': config, 'fleet': fleet}, indent=2) + '\n')
    seconds = int(slot + 269 - time.time())
    helper = '/home/pi/.cache/wsprrypi-qualification/native-v2/82562c1b937ba98816eb3ae1d27aa270729b35be862b1cf8d87d00b497b95438/wspq-capture-soapy'
    args = [helper, '--enable-physical-sdr', 'sdrplay', '2404058C60', '14075100',
            str(seconds * 250000), '20', '250000', '200000', '0', 'false',
            'false', '500000', str(seconds + 15), str(stage / 'capture.cf32'),
            str(stage / 'capture.json'), 'concurrent-fleet-second-group']
    (stage / 'capture-command.json').write_text(json.dumps(args, indent=2) + '\n')
    capture_log = (stage / 'capture.log').open('w')
    capture = subprocess.Popen(args, stdout=capture_log, stderr=subprocess.STDOUT)
    time.sleep(5)
    assert capture.poll() is None, 'SDR capture exited during preparation'
    assert not stopping and time.time() < slot - 20
    local_enable(True)
    (stage / 'local-enabled').write_text(str(time.time_ns()))
    for device_id in expected:
        assert not stopping and time.time() < slot - 10
        assignment_action(device_id, 'enable')
    (stage / 'observer-ready').write_text(str(os.getpid()))
    local_stopped = False
    last_progress = 0
    with (stage / 'observations.jsonl').open('w', buffering=1) as out:
        while time.time() < slot + 255 and not stopping:
            now = time.time()
            if capture.poll() is not None:
                raise RuntimeError('SDR capture stopped before completion')
            if now >= slot + 114 and not local_stopped:
                local_enable(False)
                local_stopped = True
                (stage / 'local-disabled').write_text(str(time.time_ns()))
            fleet, _ = http('/api/v1/host/fleet')
            endpoint, _ = http('/api/v1/host/wtp-endpoint')
            observation = {'utc_ns': str(time.time_ns()), 'fleet': fleet,
                           'local_endpoint': endpoint}
            out.write(json.dumps(observation) + '\n')
            summary = {'utc_seconds': time.time(),
                       'local_requested': endpoint['local_requested'],
                       'local_work_active': endpoint['local_work_active'],
                       'output_unknown': endpoint['output_unknown'],
                       'outputs': {k: {'state': v['state'], 'message': v['message'],
                           'output_active': (v['wtp'].get('remote') or {}).get('output_active'),
                           'last_report': v['wtp'].get('last_report')}
                           for k, v in fleet['outputs'].items()}}
            (stage / 'progress.json').write_text(json.dumps(summary, indent=2) + '\n')
            if now - last_progress >= 20:
                print(json.dumps({'utc_seconds': int(now),
                    'local': endpoint['local_work_active'],
                    'states': {k: v['state'] for k, v in fleet['outputs'].items()}}), flush=True)
                last_progress = now
            assert not endpoint['output_unknown'], 'Local output state became unknown'
            time.sleep(0.8)
except Exception as error:
    failure = repr(error)
    (stage / 'runner-error.json').write_text(json.dumps(
        {'error': failure, 'utc_ns': str(time.time_ns())}) + '\n')
finally:
    errors = pause_all()
    (stage / 'cleanup-errors.json').write_text(json.dumps(errors, indent=2) + '\n')
    (stage / 'outputs-stopped').write_text(str(time.time_ns()))
    if capture is not None:
        try:
            capture.wait(timeout=max(1, slot + 300 - time.time()))
        except subprocess.TimeoutExpired:
            capture.terminate()
            try:
                capture.wait(timeout=5)
            except subprocess.TimeoutExpired:
                capture.kill()
                capture.wait(timeout=5)
        (stage / 'capture-exit.json').write_text(json.dumps({'exit_code': capture.returncode}) + '\n')
    try:
        fleet, _ = http('/api/v1/host/fleet')
        endpoint, _ = http('/api/v1/host/wtp-endpoint')
        (stage / 'final-controller.json').write_text(json.dumps(
            {'fleet': fleet, 'local_endpoint': endpoint}, indent=2) + '\n')
    except Exception as error:
        (stage / 'final-controller-error.json').write_text(json.dumps({'error': repr(error)}) + '\n')
    journal = subprocess.run(['journalctl', '-u', 'wsprrypi', '--since',
        '@' + str(int(started) - 30), '--no-pager', '-o', 'short-iso-precise'],
        capture_output=True, text=True)
    (stage / 'controller-journal.log').write_text(journal.stdout)
    (stage / 'observer-finished').write_text(str(time.time_ns()))
if failure or errors or stopping:
    raise SystemExit(1)
