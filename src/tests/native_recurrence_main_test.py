#!/usr/bin/env python3
"""Actual main, protected APIs, simulated output, and durable restart fences."""
import argparse
import configparser
import copy
import json
from pathlib import Path
import secrets
import subprocess
import tempfile
import time
from controller_only_test import free_port, request as raw_request


_last_request = {}

def request(port, *args, **kwargs):
    # Honor the real protected API's ten-requests/second budget. This fixture
    # paces initial setup too; it never relaxes production rate or CSRF policy.
    delay = .12 - (time.monotonic() - _last_request.get(port, 0))
    if delay > 0:
        time.sleep(delay)
    _last_request[port] = time.monotonic()
    return raw_request(port, *args, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='native-recurrence-main-') as temporary:
        root = Path(temporary)
        ini, ledger = root / 'native.ini', root / 'ledger'
        trace = Path(str(ledger) + '.simulated-trace.json')
        progress = Path(str(ini) + '.recurrence-progress.json')
        source = configparser.ConfigParser(interpolation=None)
        source.optionxform = str
        source.read(Path(__file__).resolve().parents[2] / 'config/wsprrypi.ini')
        port = free_port()
        source['Operation'].update({'Transmit Backend': 'gpio', 'Transmit': 'false', 'Enable on Boot': 'Never',
            'Use LED': 'false', 'Use Amp': 'false', 'Use Shutdown': 'false', 'Web Port': str(port), 'Socket Port': str(port + 1)})
        source['WSPR'].update({'Call Sign': 'AA0NT', 'Grid Square': 'EM18', 'TX Power': '20'})
        source['WTP Server']['Enabled'] = 'false'
        source['WTP'].update({'Transport': 'network_plain', 'Hostname': '127.0.0.1', 'TCP Port': '31417', 'Device ID': 'b' * 32})
        with ini.open('w') as output:
            source.write(output)
        ini.chmod(0o600)
        stock = Path(str(ini) + '.stock')
        stock.write_bytes(ini.read_bytes())
        stock.chmod(0o600)
        command = [str(Path(args.fixture).resolve()), str(ledger), '--backend', 'simulated', '-i', str(ini),
            '--socket-loopback-only', '--socket-loopback-family', 'ipv4']
        process, log = None, (root / 'service.log').open('w')
        headers = {'Origin': f'http://127.0.0.1:{port}', 'Content-Type': 'application/json', 'X-WsprryPico-Request': '1'}

        def launch():
            nonlocal process
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

        def stop(kill=False):
            if process and process.poll() is None:
                process.kill() if kill else process.terminate()
                process.wait(timeout=8)

        def wait(check, label, seconds=12):
            deadline = time.monotonic() + seconds
            while True:
                assert process.poll() is None, (root / 'service.log').read_text()
                try:
                    value = check()
                    if value:
                        return value
                except OSError:
                    pass
                assert time.monotonic() < deadline, label + '\n' + (root / 'service.log').read_text()[-12000:]
                time.sleep(.04)

        def read(path):
            for attempt in range(20):
                status, body = request(port, 'GET', path)
                if status != 409 or json.loads(body).get('error', {}).get('code') != 'capacity':
                    return json.loads(body) if status == 200 else None
                time.sleep(0.025)
            raise AssertionError(('read stayed busy', path, status, body))

        def session():
            status, reply, values = request(port, 'POST', '/api/control/v1/host/management-session',
                json.dumps({'contract': 'Wsprry-Control/1', 'schema': 'controller-browser-session'}), headers, with_headers=True)
            assert status == 200, (status, reply)
            headers.update({'Cookie': values['Set-Cookie'].split(';')[0], 'X-WsprryPi-CSRF': values['X-WsprryPi-CSRF']})

        def mutate(path, method, body):
            session()
            payload = json.dumps(body)
            mutation_headers = {**headers, 'If-Match': '"' + body['expected_revision'] + '"'}
            deadline = time.monotonic() + 5
            while True:
                status, reply = request(port, method, path, payload, mutation_headers)
                if status == 200:
                    break
                error = json.loads(reply)
                assert status == 409 and error.get('schema') == 'target-error' and error.get('error', {}).get('code') == 'capacity', (status, reply)
                assert time.monotonic() < deadline, (status, reply)
                time.sleep(.025)
            return json.loads(reply)

        def action(resource, schema, digest=False):
            context, saved = read(base + 'context'), read(base + 'settings')
            body = {'contract': 'Wsprry-Control/1', 'schema': schema, 'binding': context['binding'],
                'capability_revision': context['capability_revision'], 'operation_id': secrets.token_hex(16), 'expected_revision': saved['revision']}
            if digest:
                body['candidate_digest'] = saved['candidate_digest']
            if resource == 'stop':
                body['observed_output'] = read(base + 'status')['observed_output']
            return mutate(base + resource, 'POST', body)

        try:
            launch()
            wait(lambda: (read('/api/control/v1/host/readiness') or {}).get('state') == 'configuration_recovery', 'Initial setup readiness')
            recovery = read('/api/control/v1/host/configuration-recovery')
            assert recovery['validation'] == 'initialization'
            session()
            candidate = copy.deepcopy(recovery['draft'])
            candidate['Operation'].update({'Transmit': False, 'Enable on Boot': 'Never'})
            candidate['WSPR'].update({'Call Sign': 'AA0NT', 'Grid Square': 'EM18', 'TX Power': 20})
            unavailable = [(section, key) for section, group in candidate.items() for key, value in group.items()
                if isinstance(value, dict) and value.get('state') == 'unavailable']
            assert not unavailable, unavailable
            mutate('/api/control/v1/host/configuration-recovery', 'PUT', {'contract': 'Wsprry-Control/1', 'schema': 'configuration-recovery-save',
                'binding': recovery['binding'], 'capability_revision': recovery['capability_revision'], 'operation_id': secrets.token_hex(16),
                'expected_revision': recovery['revision'], 'configuration': candidate})
            wait(lambda: (read('/api/control/v1/host/readiness') or {}).get('state') == 'ready', 'Normal startup after setup')
            self_id = read('/api/control/v1/targets')['targets'][0]['device_id']
            base = f'/api/control/v1/targets/{self_id}/'
            context, saved = read(base + 'context'), read(base + 'settings')
            # Test disable/drain separately from autonomous execution. Reads or
            # writes can legitimately contend with the fail-closed launch guard;
            # an occurrence disabled before launch is not an execution promise.
            # Each autonomous attempt is a fresh future slot, never a replay.
            for launch_attempt in range(9):
                if progress.exists():
                    horizon = int(json.loads(progress.read_text())['record']['consumed_through_utc_ns']) / 1e9
                    wait(lambda: time.time() > horizon, 'Consumed future slot passes before a new attempt',
                         max(12, horizon - time.time() + 3))
                context, saved = read(base + 'context'), read(base + 'settings')
                candidate = copy.deepcopy(saved['saved'])
                local = time.localtime(time.time() + 5)
                candidate['configuration']['Operation'].update({'Mode': 'QRSS', 'Transmit': True, 'Enable on Boot': 'Follow'})
                candidate['configuration']['CW'].update({'Message': 'E', 'Base Frequency': 14097000, 'Dot Seconds': .02,
                    'Start Minute': local.tm_min, 'Start Second': local.tm_sec, 'Repeat Minutes': 1,
                    'Fade Shape': 'none', 'Fade In Ms': 0, 'Fade Out Ms': 0, 'Fade Slice Ms': 5})
                mutate(base + 'settings', 'PUT', {'contract': 'Wsprry-Control/1', 'schema': 'target-save', 'binding': context['binding'],
                    'capability_revision': context['capability_revision'], 'operation_id': secrets.token_hex(16), 'expected_revision': saved['revision'], 'candidate': candidate})
                assert read(base + 'settings')['saved']['scheduling']['enabled'] is False
                previous_trace = json.loads(trace.read_text()) if trace.exists() else None
                previous_plan = previous_trace['plan_id'] if previous_trace else None
                action('enable', 'target-enable', True)
                reserved = wait(lambda: progress.exists() and json.loads(progress.read_text())['record']['in_flight'], 'Durable accepted occurrence')
                if launch_attempt == 0:
                    status = read(base + 'status')
                    assert status['execution_owner'] == self_id and status['destination_id'] == self_id
                    assert status['selected_frequencies_hz'] == ['14097000'] and status['active_revision']
                else:
                    # Observe private fixture files only until this occurrence
                    # completes/refuses. No API lock can perturb its launch.
                    def terminal():
                        try:
                            observed = json.loads(trace.read_text())
                        except FileNotFoundError:
                            observed = None
                        except json.JSONDecodeError:
                            return False
                        record = json.loads(progress.read_text())['record']
                        # Reservation advances the no-replay watermark before
                        # launch. Completion additionally requires this exact
                        # occurrence to leave the in-flight record; a different
                        # future occurrence may already have replaced it.
                        pending = record['in_flight']
                        if (pending is None or pending['occurrence_id'] != reserved['occurrence_id']) and \
                                int(record['consumed_through_utc_ns']) >= int(reserved['last_start_ns']):
                            return {'trace': observed}
                        return False
                    observed_trace = wait(terminal, 'Autonomous terminal occurrence', 10)['trace']
                action('disable', 'target-action')
                if launch_attempt == 0:
                    assert read(base + 'status')['phase'] == 'draining'
                else:
                    # An enabled scheduler may already have accepted the next
                    # future occurrence. Disable cannot abort it; explicit Stop
                    # reconciles that work before the restart checks below.
                    consumed_before_stop = json.loads(progress.read_text())['record']['consumed_through_utc_ns']
                    action('stop', 'target-stop')
                wait(lambda: json.loads(progress.read_text())['record']['in_flight'] is None, 'Accepted message drains', 10)
                if launch_attempt == 0:
                    assert read(base + 'settings')['saved']['scheduling']['enabled'] is False
                    assert json.loads(progress.read_text())['record']['consumed_through_utc_ns'] == reserved['last_start_ns']
                    output = read(base + 'status')['observed_output']
                    assert output['owner_id'] is None and output['job_id'] is None, output
                    continue
                assert json.loads(progress.read_text())['record']['consumed_through_utc_ns'] == consumed_before_stop
                if observed_trace and observed_trace['plan_id'] != previous_plan and any(event['kind'] == 'complete' for event in observed_trace['events']):
                    assert any(event['kind'] == 'execution_start' for event in observed_trace['events'])
                    assert any(event['rf_on'] for event in observed_trace['events'])
                    break
                if observed_trace is None or observed_trace['plan_id'] == previous_plan:
                    assert observed_trace == previous_trace, 'A refused preconfiguration slot cannot change prior trace evidence'
                else:
                    assert [event['kind'] for event in observed_trace['events']] == ['configure', 'configured', 'cleanup'], observed_trace
                    assert not any(event['rf_on'] for event in observed_trace['events']), observed_trace
                assert read(base + 'status')['phase'] == 'waiting'
                assert launch_attempt < 8, {
                    'trace': observed_trace, 'progress': json.loads(progress.read_text()),
                    'status': read(base + 'status'), 'service_log': (root / 'service.log').read_text()[-12000:]}
                print('Safe prelaunch refusal; testing a fresh future occurrence', launch_attempt + 1, flush=True)
            saved_progress = progress.read_bytes()
            stop()
            launch()
            wait(lambda: (read('/api/control/v1/host/readiness') or {}).get('state') == 'ready', 'Disabled restart')
            assert read(base + 'settings')['saved']['scheduling']['enabled'] is False
            time.sleep(1.2)
            assert progress.read_bytes() == saved_progress
            assert not any(event['kind'] == 'execution_start' for event in json.loads(trace.read_text())['events'])
            session()
            # Freeze a future accepted occurrence, then lose the process before
            # launch. Startup must reconcile and consume it, never replay it.
            context, saved = read(base + 'context'), read(base + 'settings')
            candidate = copy.deepcopy(saved['saved'])
            consumed_through = int(json.loads(progress.read_text())['record']['consumed_through_utc_ns']) / 1e9
            local = time.localtime(max(time.time(), consumed_through) + 8)
            candidate['configuration']['Operation']['Transmit'] = True
            candidate['configuration']['CW'].update({'Start Minute': local.tm_min, 'Start Second': local.tm_sec, 'Repeat Minutes': 60})
            mutate(base + 'settings', 'PUT', {'contract': 'Wsprry-Control/1', 'schema': 'target-save', 'binding': context['binding'],
                'capability_revision': context['capability_revision'], 'operation_id': secrets.token_hex(16), 'expected_revision': saved['revision'], 'candidate': candidate})
            action('enable', 'target-enable', True)
            reserved = wait(lambda: (json.loads(progress.read_text())['record']['in_flight'] or None), 'Second durable reservation')
            consumed = json.loads(progress.read_text())['record']['consumed_through_utc_ns']
            assert int(consumed) == int(reserved['last_start_ns'])
            stop(True)
            launch()
            wait(lambda: (read('/api/control/v1/host/readiness') or {}).get('state') == 'ready', 'Interrupted restart')
            wait(lambda: json.loads(progress.read_text())['record']['in_flight'] is None, 'Positive startup reconciliation')
            assert json.loads(progress.read_text())['record']['consumed_through_utc_ns'] == consumed
            assert not any(event['kind'] == 'execution_start' for event in json.loads(trace.read_text())['events'])
            session()
            action('disable', 'target-action')
            print('PASS: actual main protected native enable/disable, independent Transmit/boot, drain, autonomous output, restart retention, and interrupted-reservation no replay')
        finally:
            stop()
            log.close()


if __name__ == '__main__':
    main()
