"""Bounded physical Fleet reconnection campaign, run on the Mac.

Uses the installed scheduler and original five assignments. No direct job
submission, local Enable, binary installation or endpoint reassignment.
"""
import concurrent.futures
import json
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/wtp-pi'))
from probe import Wtp

stage = sys.argv[1]
out = Path(sys.argv[2])
base = 'http://192.168.1.54:31415'
targets = {
    'wspr1': ('192.168.1.44', 'd133ed890b90f6945b41a235429ff155'),
    'wspr2': ('192.168.1.123', 'e2a4d273f7d8f24f6696f7c6fb95ff11'),
    'wspr4': ('192.168.1.120', 'f21e530af8a4a210ec57898c0ebd32d2'),
    'Pico A': ('192.168.1.47', 'fd6127d11d6aca42a9905fa3fb1bf1d5'),
    'Pico B': ('192.168.1.53', '29f20b7342051ef947aa56cb9d4fab42'),
}
first = ((int(time.time()) + 45) // 120 + 1) * 120 + 1
cases = [
    {'name': 'controller-network', 'fault_slot': first + 120, 'clean_slot': first + 240, 'blocked': list(targets)},
    {'name': 'target-subset-one', 'fault_slot': first + 360, 'clean_slot': first + 480, 'blocked': ['wspr2', 'wspr4', 'Pico A']},
    {'name': 'target-subset-two', 'fault_slot': first + 600, 'clean_slot': first + 720, 'blocked': ['wspr1', 'Pico B']},
    {'name': 'controller-crash', 'fault_slot': first + 840, 'clean_slot': first + 960, 'blocked': []},
    {'name': 'target-restart', 'fault_slot': first + 1080, 'clean_slot': first + 1200, 'blocked': []},
]
focused = len(sys.argv) > 3 and sys.argv[3] == 'restart-only'
if focused:
    cases = [{'name':'target-restart', 'fault_slot':first, 'clean_slot':first+120, 'blocked':[]}]
plan = {'stage': stage, 'source_head': '3dd0a79894782b5dfd59fbd14ef6a282223eec6b',
        'first_slot': first, 'last_slot': first + (240 if focused else 1320),
        'deadline': first + (370 if focused else 1450), 'restart_only':focused,
        'cases': cases, 'addresses': {k: v[0] for k, v in targets.items()},
        'expected_ids': {k: v[1] for k, v in targets.items()},
        'duration_ms': 10000, 'period_seconds': 120, 'phase_seconds': 1,
        'local_output_enabled': False, 'receiver_outputs': {'wspr2': 14101100, 'Pico A': 14100100},
        'network_fault': 'Temporary controller IPv4 host blackhole routes; physical links and mDNS remain up.',
        'guard_unit': 'wtp-fleet-reconnect-guard-' + out.name.replace('_', '-')}
def save(name, value):
    (out / name).write_text(json.dumps(value, indent=2) + '\n')
save('plan.json', plan)
def ssh(host, command, timeout=45):
    result = subprocess.run(['ssh', '-o', 'BatchMode=yes', host, command], capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout
def http(path, method='GET', body=None, etag=None):
    headers = {'Origin': base, 'X-WsprryPico-Request': '1', 'Content-Type': 'application/json'}
    if etag:
        headers['If-Match'] = etag
    r = urllib.request.Request(base + '/api/v1/host/' + path,
        data=json.dumps(body).encode() if body is not None else None, headers=headers, method=method)
    try:
        with urllib.request.urlopen(r, timeout=8) as response:
            return json.load(response), response.headers.get('ETag')
    except urllib.error.HTTPError as error:
        raise RuntimeError(f'{error.code}: {error.read().decode()}') from error
def action(name, operation):
    for attempt in range(5):
        _, revision = http('fleet')
        try:
            result = http('fleet', 'POST', {'operation': operation, 'device_id': targets[name][1]}, revision)[0]
            event({'action': operation, 'name': name})
            return result
        except RuntimeError as error:
            if '412:' not in str(error) or attempt == 4:
                raise
def event(value):
    value = {'utc_ns': str(time.time_ns()), **value}
    with (out / 'actions.jsonl').open('a') as stream:
        stream.write(json.dumps(value) + '\n')
    print(json.dumps(value), flush=True)
stop = threading.Event()
observer_failed = threading.Event()
lock = threading.Lock()
latest = {}
threads = []
usb_process = None
def observe_usb():
    global usb_process
    usb_process = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', 'wspr5',
        f'python3 -u {stage}/usb_observer.py {plan["deadline"]}'], stdout=subprocess.PIPE,
        stderr=(out / 'usb-observer-errors.log').open('w'), text=True)
    streams = {n: (out / (n.replace(' ', '-') + '-observer.jsonl')).open('w', buffering=1)
               for n in ('Pico A', 'Pico B')}
    try:
        for line in usb_process.stdout:
            row = json.loads(line)
            name = row['name']
            streams[name].write(line)
            if 'status' in row:
                assert row['hello']['device_id'] == targets[name][1]
                assert row['caps']['engine'] == 'pio-dma-gp2'
                with lock:
                    latest[name] = row
            else:
                observer_failed.set()
            if stop.is_set():
                break
    finally:
        for stream in streams.values():
            stream.close()
        usb_process.terminate()
        usb_process.wait(timeout=10)
def observe(name, address, expected):
    wire = None
    with (out / (name.replace(' ', '-') + '-observer.jsonl')).open('w', buffering=1) as stream:
        while not stop.is_set() and time.time() < plan['deadline']:
            try:
                if wire is None:
                    wire = Wtp(address, 31417)
                    assert wire.hello['body']['device_id'] == expected, wire.hello
                status = wire.request('STATUS')['body']
                row = {'utc_ns': str(time.time_ns()), 'hello': wire.hello['body'], 'status': status,
                       'events': [r['message'] for r in wire.records if r['message'].get('type') == 'event']}
                wire.records.clear()
                with lock:
                    latest[name] = row
            except Exception as error:
                row = {'utc_ns': str(time.time_ns()), 'error': repr(error)}
                if wire:
                    wire.close()
                wire = None
            stream.write(json.dumps(row) + '\n')
            stop.wait(.5)
        if wire:
            wire.close()
def controller_observer():
    last_summary = 0
    with (out / 'controller-observations.jsonl').open('w', buffering=1) as stream:
        while not stop.is_set() and time.time() < plan['deadline']:
            try:
                fleet, _ = http('fleet')
                endpoint, _ = http('wtp-endpoint')
                assert not endpoint['local_requested'] and not endpoint['local_work_active'] and not endpoint['output_unknown']
                row = {'utc_ns': str(time.time_ns()), 'fleet': fleet, 'endpoint': endpoint}
                if time.time() - last_summary >= 30:
                    print(json.dumps({'progress': int(time.time()), 'states': {k: v['state'] for k,v in fleet['outputs'].items()}}), flush=True)
                    last_summary = time.time()
            except Exception as error:
                row = {'utc_ns': str(time.time_ns()), 'error': repr(error)}
            stream.write(json.dumps(row) + '\n')
            stop.wait(.8)
def wait_until(when):
    while time.time() < when:
        if observer_failed.is_set():
            raise RuntimeError('Independent USB observer failed; withdraw schedules')
        if time.time() >= plan['deadline']:
            raise RuntimeError('Campaign deadline reached')
        time.sleep(min(.5, when - time.time()))
def snapshot():
    fleet, _ = http('fleet')
    with lock:
        remote = dict(latest)
    return {'fleet': fleet, 'remote': remote, 'endpoint': http('wtp-endpoint')[0]}
def require_running(slot):
    wait_until(slot + .6)
    deadline = slot + 2.5
    while time.time() < deadline:
        with lock:
            current = dict(latest)
        if len(current) == 5 and all(int(r['utc_ns']) / 1e9 >= slot and r['status']['state'] == 'running' and r['status']['output_active'] for r in current.values()):
            return current
        time.sleep(.1)
    raise RuntimeError(f'Five physical jobs were not all running in slot {slot}: {current}')
def wait_off(names, deadline):
    while time.time() < deadline:
        with lock:
            current = dict(latest)
        if all(n in current and time.time() - int(current[n]['utc_ns']) / 1e9 < 3 and
               not current[n]['status']['output_active'] and not current[n]['status'].get('owner_id') and
               current[n]['status']['state'] in ('empty','complete','aborted','missed') for n in names):
            return
        time.sleep(.3)
    raise RuntimeError(f'Targets did not become known off/unowned: {names}')
def reconcile(names, deadline):
    for name in names:
        action(name, 'recover')
    retry_at = time.time() + 8
    while time.time() < deadline:
        fleet, _ = http('fleet')
        rows = {r['device_id']: r for r in fleet['assignments']}
        if all(not rows[targets[n][1]]['in_flight'] and
               not fleet['outputs'][targets[n][1]]['wtp'].get('uncertain') and
               not fleet['outputs'][targets[n][1]]['wtp'].get('safety_fault') and
               not fleet['outputs'][targets[n][1]]['wtp'].get('owns') and
               fleet['outputs'][targets[n][1]]['wtp'].get('remote') is not None and
               not fleet['outputs'][targets[n][1]]['wtp']['remote']['owner_id'] and
               not fleet['outputs'][targets[n][1]]['wtp']['remote']['output_active'] for n in names):
            for name in names:
                action(name, 'enable')
            return
        if time.time() >= retry_at:
            # Reconcile may initially meet a still-closing TCP session.
            # Retry only this explicit read/cleanup action, never a job.
            for name in names:
                if rows[targets[name][1]]['in_flight'] or fleet['outputs'][targets[name][1]]['wtp']['phase'] == 'blocked':
                    action(name, 'recover')
            retry_at = time.time() + 8
        time.sleep(.5)
    save('failed-reconciliation.json', snapshot())
    raise RuntimeError(f'Reconciliation did not clear barriers: {names}')
capture = None
capture_log = None
def start_capture(name, finish):
    global capture, capture_log
    seconds = int(finish - time.time()) + 2
    helper = '/home/pi/.cache/wsprrypi-qualification/native-v2/82562c1b937ba98816eb3ae1d27aa270729b35be862b1cf8d87d00b497b95438/wspq-capture-soapy'
    args = [helper, '--enable-physical-sdr', 'sdrplay', '2404058C60', '14075100', str(seconds * 250000),
            '20', '250000', '200000', '0', 'false', 'false', '500000', str(seconds + 15),
            f'{stage}/{name}.cf32', f'{stage}/{name}-capture.json', 'fleet-reconnection-' + name]
    save(name + '-capture-command.json', args)
    capture_log = (out / (name + '-capture.log')).open('w')
    capture = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', 'wspr5', ' '.join(args)], stdout=capture_log, stderr=subprocess.STDOUT)
    time.sleep(3)
    assert capture.poll() is None, 'Receiver failed at startup'
def finish_capture(name):
    global capture, capture_log
    result = capture.wait(timeout=35)
    save(name + '-capture-exit.json', {'exit_code': result})
    capture_log.close()
    capture = None
    assert result == 0, result
def block(names, value):
    verb = 'add' if value else 'del'
    command = 'set -eu\n' + '\n'.join(f'sudo ip -4 route {verb} blackhole {targets[n][0]}/32' for n in names)
    receipt = ssh('wspr5', command + '\nip -4 route show\nss -Htnp')
    event({'network_block': value, 'names': names, 'receipt': receipt})
failure = None
try:
    before = snapshot()
    assert len(before['fleet']['assignments']) == 5
    assert all(not r['enabled'] and not r['in_flight'] for r in before['fleet']['assignments'])
    save('before-controller.json', before)
    payload = json.dumps(plan)
    ssh('wspr5', f"python3 - <<'PY'\nfrom pathlib import Path\nPath('{stage}/plan.json').write_text({payload!r})\nPY")
    ssh('wspr5', f'sudo systemd-run --unit={plan["guard_unit"]} --on-active=1800 /usr/bin/python3 {stage}/guard.py {stage}')
    for name, (address, expected) in targets.items():
        if name.startswith('Pico'):
            continue
        thread = threading.Thread(target=observe, args=(name,address,expected), daemon=True)
        thread.start();threads.append(thread)
    thread = threading.Thread(target=observe_usb, daemon=True)
    thread.start();threads.append(thread)
    thread = threading.Thread(target=controller_observer, daemon=True)
    thread.start();threads.append(thread)
    ready_until = time.time() + 15
    while time.time() < ready_until:
        with lock:
            ready = len(latest) == 5 and all(time.time() - int(r['utc_ns']) / 1e9 < 2 for r in latest.values())
        if ready:
            break
        time.sleep(.1)
    assert ready, 'Five independent observers must be ready before any schedule is enabled'
    time.sleep(3)
    with lock:
        assert not observer_failed.is_set() and all(time.time() - int(r['utc_ns']) / 1e9 < 2 for r in latest.values())
    # Enable after the preceding slot's preparation window has passed.
    # Otherwise startup can schedule an extra round before the baseline.
    wait_until(first - 100)
    start_capture('target-restart' if focused else 'controller-network',
                  plan['last_slot']+20 if focused else cases[0]['clean_slot']+20)
    for name in targets:
        action(name, 'enable')
    if not focused:
        event({'baseline_slot': first})
        baseline = require_running(first)
        wait_until(first + 14)
        save('baseline.json', {'running': baseline, 'after': snapshot()})
    for case in cases:
        name, slot = case['name'], case['fault_slot']
        if not focused and name != 'controller-network':
            finish = case['clean_slot'] + (140 if name == 'target-restart' else 20)
            start_capture(name, finish)
        running = require_running(slot)
        record = {'case': case, 'running_before_fault': running, 'before_fault': snapshot()}
        save(name + '-result.json', record)
        if case['blocked']:
            block(case['blocked'], True)
        elif name == 'controller-crash':
            record['kill_receipt'] = ssh('wspr5', 'sudo systemctl kill --kill-whom=main --signal=SIGKILL wsprrypi')
            event({'controller_crash': True})
        else:
            record['restart_receipt'] = ssh('wspr2', 'sudo systemctl restart wsprrypi; systemctl show wsprrypi -p MainPID -p ActiveState', timeout=45)
            event({'target_restart': 'wspr2'})
        wait_until(slot + 75)
        if case['blocked']:
            block(case['blocked'], False)
        affected = case['blocked'] or (list(targets) if name == 'controller-crash' else ['wspr2'])
        wait_off(affected, slot + 87)
        record['after_reconnect_before_recover'] = snapshot()
        reconcile(affected, slot + 102)
        record['after_recovery'] = snapshot()
        recovered = require_running(case['clean_slot'])
        wait_until(case['clean_slot'] + 14)
        record['recovered_running'] = recovered
        record['after_clean_slot'] = snapshot()
        record['passed'] = True
        save(name + '-result.json', record)
        event({'case_complete': name})
        if name == 'target-restart':
            last = require_running(plan['last_slot'])
            wait_until(plan['last_slot'] + 14)
            save('final-repeat.json', {'running': last, 'after': snapshot()})
        finish_capture(name)
except BaseException as error:
    failure = repr(error)
    save('failure.json', {'error': failure, 'utc_ns': str(time.time_ns())})
    event({'failure': failure})
finally:
    try:
        receipt = ssh('wspr5', f'sudo python3 {stage}/guard.py {stage}', timeout=100)
        save('cleanup-receipt.json', {'receipt': receipt})
        wait_off(list(targets), time.time() + 85)
        for name in targets:
            action(name, 'recover')
        deadline = time.time() + 45
        while time.time() < deadline:
            final = snapshot()
            if all(not r['enabled'] and not r['in_flight'] for r in final['fleet']['assignments']):
                break
            time.sleep(1)
        save('final-controller.json', final)
        assert all(not r['enabled'] and not r['in_flight'] for r in final['fleet']['assignments'])
        ssh('wspr5', 'sudo systemctl restart wsprrypi')
        ssh('wspr5', f'sudo systemctl stop {plan["guard_unit"]}.timer')
        save('final-after-restart.json', snapshot())
    except Exception as error:
        save('cleanup-failure.json', {'error': repr(error)})
        failure = failure or repr(error)
    stop.set()
    for thread in threads:
        thread.join(timeout=12)
    if capture is not None:
        try:
            rc = capture.wait(timeout=25)
        except subprocess.TimeoutExpired:
            capture.terminate();rc = capture.wait(timeout=10)
        save('interrupted-capture-exit.json', {'exit_code': rc})
        capture_log.close()
    event({'finished': True, 'failed': failure})
if failure:
    raise SystemExit(1)
