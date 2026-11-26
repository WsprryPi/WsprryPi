#!/usr/bin/env python3
"""Actual main, protected APIs, simulated output, and durable restart fences."""
import argparse
import copy
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import time
from controller_only_test import free_port, request as raw_request
from software_backend_admission import backend_admission
from software_web_service import start_software_web_service, stop_software_web_service


_last_request = {}
_temporarily_unavailable_reads = (
    (409, 'capacity'),
    (429, 'rate_limited'),
    (503, 'authority_pending'),
)

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
    with backend_admission(args.fixture), tempfile.TemporaryDirectory(prefix='native-recurrence-main-') as temporary:
        root = Path(temporary)
        state, ledger = root / 'state', root / 'ledger'
        trace = Path(str(ledger) + '.simulated-trace.json')
        launch_signals = Path(str(ledger) + '.simulated-launch')
        progress = state / 'generation-1/configuration.recurrence-progress.json'
        port = free_port()
        fixture = str(Path(args.fixture).resolve())
        initialized = subprocess.run([fixture, str(ledger), '--initialize-state', '--state-directory', str(state)],
                                     capture_output=True, text=True, timeout=15)
        assert initialized.returncode == 0, initialized.stderr
        # The dedicated fixture supplies only the final launch samples. The
        # application still selects, reserves, and accepts real future slots.
        command = [fixture, str(ledger), '--backend', 'simulated', '--state-directory', str(state),
            '--web-port', str(port), '--socket-port', str(port + 1),
            '--socket-loopback-only', '--socket-loopback-family', 'ipv4', '--debug-logging']
        web = None
        process, log = None, (root / 'service.log').open('w')
        headers = {'Origin': f'http://127.0.0.1:{port}', 'Content-Type': 'application/json', 'X-WsprryPico-Request': '1'}

        def launch():
            nonlocal process, web
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env={**os.environ, "TZ": "UTC"})
            if web is None:
                web = start_software_web_service([fixture,str(ledger)],ledger,log)

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
                if time.monotonic() >= deadline:
                    diagnostics = {'progress_path': str(progress), 'progress_exists': progress.exists()}
                    if progress.exists():
                        diagnostics['progress'] = json.loads(progress.read_text())
                    if 'occurrence' in label:
                        for resource in ('status', 'settings'):
                            try:
                                status, body = request(port, 'GET', base + resource)
                                diagnostics[resource] = [status, body[:1024].decode('utf-8', errors='replace')]
                            except OSError as error:
                                diagnostics[resource] = str(error)
                    raise AssertionError(label + '\n' + json.dumps(diagnostics, sort_keys=True) + '\n' +
                                         (root / 'service.log').read_text()[-12000:])
                time.sleep(.04)

        def read(path, *, waiting_for_readiness=False):
            # The owner may temporarily withhold a snapshot while it updates
            # authority. Poll only explicit pre-effect refusals on these GETs.
            deadline = time.monotonic() + 5
            while True:
                status, body = request(port, 'GET', path)
                if waiting_for_readiness and status != 200:
                    return None
                try:
                    document = json.loads(body)
                except (json.JSONDecodeError, UnicodeError) as error:
                    raise AssertionError((path, status, body[:1024])) from error
                assert isinstance(document, dict) and document.get('contract') == 'Wsprry-Control/1', (
                    path, status, body[:1024])
                if status == 200:
                    return document
                detail = document.get('error')
                assert (document.get('schema') == 'target-error' and isinstance(detail, dict)
                        and (status, detail.get('code')) in _temporarily_unavailable_reads
                        and time.monotonic() < deadline), (path, status, body[:1024])

        def ready(state):
            response = read('/api/control/v1/host/readiness', waiting_for_readiness=True)
            return response and response['state'] == state

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

        def action(resource, schema, *, settings=None):
            if settings is None:
                settings = read(base + 'settings')
            body = {'contract': 'Wsprry-Control/1', 'schema': schema, 'binding': settings['binding'],
                'capability_revision': settings['capability_revision'], 'operation_id': secrets.token_hex(16), 'expected_revision': settings['revision']}
            if resource == 'enable':
                body['candidate_digest'] = settings['candidate_digest']
            if resource == 'stop':
                body['observed_output'] = settings['observed_output']
            return mutate(base + resource, 'POST', body)

        try:
            launch()
            wait(lambda: ready('configuration_recovery'), 'Initial setup readiness')
            recovery = read('/api/control/v1/host/configuration-recovery')
            assert recovery['validation'] == 'initialization'
            session()
            candidate = copy.deepcopy(recovery['draft'])
            candidate['Operation'].update({'Transmit': False, 'Enable on Boot': 'Never', 'Web Port': port, 'Socket Port': port + 1})
            candidate['WSPR'].update({'Call Sign': 'AA0NT', 'Grid Square': 'EM18', 'TX Power': 20})
            unavailable = [(section, key) for section, group in candidate.items() for key, value in group.items()
                if isinstance(value, dict) and value.get('state') == 'unavailable']
            assert not unavailable, unavailable
            mutate('/api/control/v1/host/configuration-recovery', 'PUT', {'contract': 'Wsprry-Control/1', 'schema': 'configuration-recovery-save',
                'binding': recovery['binding'], 'capability_revision': recovery['capability_revision'], 'operation_id': secrets.token_hex(16),
                'expected_revision': recovery['revision'], 'configuration': candidate})
            wait(lambda: ready('ready'), 'Normal startup after setup')
            self_id = read('/api/control/v1/targets')['targets'][0]['device_id']
            base = f'/api/control/v1/targets/{self_id}/'
            def progress_record():
                return json.loads(progress.read_text())['record']

            def read_trace():
                try:
                    return json.loads(trace.read_text())
                except (FileNotFoundError, json.JSONDecodeError):
                    return None

            def stage_occurrence(start_seconds):
                saved = read(base + 'settings')
                candidate = copy.deepcopy(saved['saved'])
                slot = time.gmtime(start_seconds)
                candidate['configuration']['Operation'].update({'Mode': 'QRSS', 'Transmit': True, 'Enable on Boot': 'Follow'})
                candidate['configuration']['CW'].update({'Message': 'E', 'Base Frequency': 14097000, 'Dot Seconds': .02,
                    'Start Minute': slot.tm_min, 'Start Second': slot.tm_sec, 'Repeat Minutes': 60,
                    'Fade Shape': 'none', 'Fade In Ms': 0, 'Fade Out Ms': 0, 'Fade Slice Ms': 5})
                mutate(base + 'settings', 'PUT', {'contract': 'Wsprry-Control/1', 'schema': 'target-save', 'binding': saved['binding'],
                    'capability_revision': saved['capability_revision'], 'operation_id': secrets.token_hex(16),
                    'expected_revision': saved['revision'], 'candidate': candidate})
                stored = read(base + 'settings')
                assert stored['saved']['configuration']['Operation']['Transmit'] is True
                assert stored['saved']['configuration']['Operation']['Enable on Boot'] == 'Follow'
                assert stored['saved']['scheduling']['enabled'] is False
                old_trace = read_trace()
                action('enable', 'target-enable', settings=stored)
                start_ns = str(start_seconds * 1_000_000_000)
                waiting = launch_signals / (start_ns + '.waiting')
                reserved = wait(lambda: progress.exists() and progress_record()['in_flight'], 'Durable occurrence reservation')
                assert reserved['last_start_ns'] == start_ns, reserved
                assert progress_record()['consumed_through_utc_ns'] == start_ns
                wait(waiting.exists, 'Reserved occurrence reaches the launch boundary')
                return start_ns, old_trace['plan_id'] if old_trace else None

            def release_occurrence(start_ns, previous_plan, offset_ns):
                release = launch_signals / (start_ns + '.release')
                staged_sample = release.with_suffix('.tmp')
                staged_sample.write_text(str(int(start_ns) + offset_ns))
                staged_sample.replace(release)

                def terminal():
                    observed = read_trace()
                    if not observed or observed['plan_id'] == previous_plan:
                        return False
                    record = progress_record()
                    if record['in_flight'] is None and record['consumed_through_utc_ns'] == start_ns and not release.exists():
                        return observed
                    return False

                return wait(terminal, 'Released occurrence completes or refuses')

            def assert_output_inactive():
                output = read(base + 'status')['observed_output']
                assert output['owner_id'] is None and output['job_id'] is None, output

            def assert_completed(observed):
                events = observed['events']
                assert {'execution_start', 'complete'} <= {event['kind'] for event in events} and any(
                    event['rf_on'] for event in events), {
                        'trace': observed, 'progress': progress_record(), 'status': read(base + 'status'),
                        'service_log': (root / 'service.log').read_text()[-12000:],
                    }

            # A future slot is necessary for actual reservation. The test never
            # sleeps until that time, and each subsequent slot has a new identity.
            first_start = int(time.time()) + 180
            start_ns, previous_plan = stage_occurrence(first_start)
            status = read(base + 'status')
            assert status['execution_owner'] == self_id and status['destination_id'] == self_id
            assert status['selected_frequencies_hz'] == ['14097000'] and status['active_revision']
            action('disable', 'target-action')
            assert read(base + 'status')['phase'] == 'draining'
            observed = release_occurrence(start_ns, previous_plan, 0)
            assert_completed(observed)
            assert read(base + 'settings')['saved']['scheduling']['enabled'] is False
            assert_output_inactive()

            # Exercise both sides of the existing 50 ms cutoff. The accepted
            # worker is already waiting, and no API read competes with launch.
            start_ns, previous_plan = stage_occurrence(first_start + 1)
            observed = release_occurrence(start_ns, previous_plan, 50_000_000)
            assert_completed(observed)
            action('disable', 'target-action')
            assert_output_inactive()

            start_ns, previous_plan = stage_occurrence(first_start + 2)
            observed = release_occurrence(start_ns, previous_plan, 50_001_000)
            assert [event['kind'] for event in observed['events']] == ['configure', 'configured', 'cleanup'], observed
            assert not any(event['rf_on'] for event in observed['events']), observed
            action('disable', 'target-action')
            assert_output_inactive()

            start_ns, _ = stage_occurrence(first_start + 3)
            action('disable', 'target-action')
            action('stop', 'target-stop')
            wait(lambda: progress_record()['in_flight'] is None, 'Stop reconciles the unreleased occurrence')
            assert progress_record()['consumed_through_utc_ns'] == start_ns
            assert not any(event['rf_on'] for event in read_trace()['events'])
            assert_output_inactive()

            saved_progress = progress.read_bytes()
            stop()
            launch()
            wait(lambda: ready('ready'), 'Disabled restart')
            assert read(base + 'settings')['saved']['scheduling']['enabled'] is False
            assert progress.read_bytes() == saved_progress
            assert not any(event['kind'] == 'execution_start' for event in read_trace()['events'])

            # Lose a process after its exact worker reached the barrier but
            # before release. Restart must reconcile the reservation once.
            start_ns, _ = stage_occurrence(first_start + 4)
            stop(True)
            waiting = launch_signals / (start_ns + '.waiting')
            waiting.unlink()
            launch()
            wait(lambda: ready('ready'), 'Interrupted restart')
            wait(lambda: progress_record()['in_flight'] is None, 'Positive startup reconciliation')
            assert progress_record()['consumed_through_utc_ns'] == start_ns
            assert not any(event['kind'] == 'execution_start' for event in read_trace()['events'])
            action('disable', 'target-action')
            assert not waiting.exists(), 'Restart must not send the consumed occurrence back to the worker'
            print('PASS: actual main protected native enable/disable, drain, deterministic on-time and late admission, Stop, and interrupted-reservation no replay')
        finally:
            stop_software_web_service(web)
            stop()
            log.close()


if __name__ == '__main__':
    main()
