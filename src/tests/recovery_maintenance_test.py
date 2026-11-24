# SPDX-License-Identifier: MIT
"""Actual backend, web service and independent maintenance broker; no hardware."""
import hashlib
import base64
import http.client
import json
import os
import re
from pathlib import Path
import socket
import signal
import stat
import shutil
import subprocess
import sys
import tempfile
import time
from software_backend_admission import backend_admission
from browser_session_backend_test import Connection
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from application_recovery import archive, evidence


def run(fixture, helper, fault_phase=None):
    controller_crash = bool(fault_phase and fault_phase.startswith('--controller-crash='))
    controller = controller_crash or fault_phase in ('--controller', '--controller-browser', '--controller-move')
    browser_mode = fault_phase in ('--browser', '--controller-browser')
    moving = fault_phase in ('--move', '--controller-move')
    overridden = fault_phase == '--listener-override'
    history = fault_phase == '--history'
    roundtrip = fault_phase in ('--roundtrip', '--history')
    purpose = 'move' if moving else 'recover'
    if controller_crash:
        fault_phase = fault_phase.split('=', 1)[1]
        assert fault_phase in ('stopping_backend', 'retaining_evidence', 'publishing_generation', 'starting_backend', 'verifying_backend', 'complete')
    elif controller or moving or overridden or roundtrip:
        fault_phase = '--browser' if browser_mode else None
    role = 'controller' if controller else 'pi'
    environment = dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1')
    with tempfile.TemporaryDirectory(prefix='recovery-three-process-', dir='/tmp') as temporary:
        root = Path(temporary).resolve()
        ledger, state, runtime = root / 'identity', root / 'state', root / 'backend'
        listener_cache = root / 'listeners.json'
        # Unix-domain paths must fit the Pi/macOS common bound.
        maintenance_runtime = Path('/tmp').resolve() / ('recovery-ctl-' + hashlib.sha256(str(root).encode()).hexdigest()[:16])
        prefix = [fixture, str(ledger), helper, str(state), str(runtime), str(maintenance_runtime)]
        logs = []
        processes = []
        restarted_pid = None
        def start(name, arguments):
            output = (root / (name + '.log')).open('w')
            logs.append(output)
            child = subprocess.Popen(prefix + arguments, env=environment, stdout=output, stderr=subprocess.STDOUT)
            processes.append(child)
            return child
        def review_request(method, body=None):
            # Busy is an explicit pre-effect refusal, unlike an unknown/lost
            # mutation reply. Other outcomes are never automatically retried.
            deadline = time.monotonic() + 5
            while True:
                result = request(method, '/api/recovery/review', body)
                if result[0] != 409 or json.loads(result[2]).get('error') != 'busy':
                    return result
                assert time.monotonic() < deadline, result
                time.sleep(.05)
        def cli(arguments, phrases=()):
            descriptors = []
            try:
                for option, secret in phrases:
                    read, write = os.pipe()
                    descriptors.append(read)
                    os.write(write, secret.encode() + b'\n');os.close(write)
                    arguments += [option, str(read)]
                return subprocess.run(prefix + ['--maintenance'] + arguments, env=environment, pass_fds=descriptors,
                                      capture_output=True, text=True, timeout=20)
            finally:
                for descriptor in descriptors:
                    os.close(descriptor)
        help_result = cli(['help'])
        assert help_result.returncode == 0, help_result.stderr
        assert 'release --review-digest SHA256 [--local] [--remote]' in help_result.stdout
        assert 'select at least one hold' in help_result.stdout
        assert not state.exists() and not Path(str(ledger) + '.maintenance').exists()
        reserved = []
        # Restored listeners bind only after browser/proxy traffic has begun.
        # Keep them outside Linux's ephemeral client range so active or
        # TIME_WAIT client sockets cannot occupy a future listener port.
        ephemeral = range(0)
        if sys.platform == 'linux':
            first, last = map(int, Path('/proc/sys/net/ipv4/ip_local_port_range').read_text().split())
            ephemeral = range(first, last + 1)
        for port in range(22000, 32000):
            if port in ephemeral:
                continue
            descriptor = socket.socket()
            try:
                descriptor.bind(('127.0.0.1', port))
            except OSError:
                descriptor.close()
                continue
            reserved.append(descriptor)
            if len(reserved) == 4:
                break
        assert len(reserved) == 4, 'Four non-ephemeral recovery fixture listener ports are required'
        ports = [item.getsockname()[1] for item in reserved]
        maintenance_arguments = ['--fixture-listener-override', str(ports[0]), str(ports[1])] if overridden else []
        for item in reserved:
            item.close()
        source = root / 'source.ini'
        source.write_text((Path(__file__).resolve().parents[2] / 'config/wsprrypi.ini').read_text()
                          .replace('Call Sign = NXXX', 'Call Sign = AA0NT').replace('Grid Square = ZZ99', 'Grid Square = EM18')
                          .replace('Fade In Ms = 20', 'Fade In Ms = 17').replace('Fade Shape = raised_cosine', 'Fade Shape = linear')
                          .replace('Web Port = 31415', 'Web Port = ' + str(ports[2])).replace('Socket Port = 31416', 'Socket Port = ' + str(ports[3])))
        source.chmod(0o600)
        result = subprocess.run(prefix + ['--migrate-state', '--state-directory', str(state), '--ini-source', str(source)] + (['--controller-only'] if controller else []),
                                env=environment, capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode()
        origin = f'http://127.0.0.1:{ports[0]}'
        cookie, csrf, recovery_cookie, recovery_csrf = '', '', '', ''
        def request(method, path, body=None, admitted=True, override=None):
            headers = {}
            if method == 'POST':
                headers.update({'Origin': origin, 'Content-Type': 'application/json', 'X-WsprryPico-Request': '1'})
            if admitted:
                headers.update({'Cookie': '; '.join(item for item in (cookie, recovery_cookie) if item),
                                'X-WsprryPi-CSRF': csrf, 'X-WsprryPi-Recovery-CSRF': recovery_csrf})
            headers.update(override or {})
            connection = http.client.HTTPConnection('127.0.0.1', ports[0], timeout=7)
            connection.request(method, path, None if body is None else json.dumps(body), headers)
            response = connection.getresponse()
            data, fields = response.read(), dict(response.getheaders())
            status = response.status
            connection.close()
            return status, fields, data
        try:
            backend = start('backend', ['--state-directory', str(state), '--runtime-directory', str(runtime), '--backend', 'simulated',
                                      '--socket-loopback-only', '--web-port', str(ports[0]), '--socket-port', str(ports[1]),
                                      '--socket-loopback-family', 'ipv4'] + (['--controller-only'] if controller else []))
            maintenance = start('maintenance', maintenance_arguments + (['--fixture-fault-phase', fault_phase] if fault_phase and fault_phase != '--browser' else []) + ['--maintenance-worker', '--role', role])
            web = start('web', ['--web-service', '--runtime-directory', str(runtime), '--listener-cache', str(listener_cache)])
            deadline = time.monotonic() + 25
            while True:
                assert all(item.poll() is None for item in (backend, maintenance, web)), [path.read_text() for path in root.glob('*.log')]
                try:
                    status, _, _ = request('GET', '/version')
                    if status == 200 and (maintenance_runtime / 'http.sock').exists():
                        break
                except OSError:
                    pass
                assert time.monotonic() < deadline, [path.read_text() for path in root.glob('*.log')]
                time.sleep(.05)
            if fault_phase == '--browser':
                subprocess.run(['node', str(Path(__file__).resolve().parents[2] / 'WsprryPi-UI/tests/recovery_actual_browser_test.js'),
                                str(ports[0]), str(root), role], env=environment, check=True, timeout=180)
                return
            assert request('GET', '/api/recovery/jobs', admitted=False)[0] == 403
            diagnostic = subprocess.run(prefix + ['--diagnostic-export', '--runtime-directory', str(runtime)], env=environment,
                                        capture_output=True, text=True, timeout=15)
            assert diagnostic.returncode == 0, diagnostic.stdout + diagnostic.stderr
            exported = json.loads(diagnostic.stdout)
            assert exported['schema'] == 'diagnostic-settings/1' and exported['status'] == 'available' and exported['role'] == role
            assert exported['storage']['format'] == 'application-state/1' and exported['saved_settings']['role'] == role
            if not controller:
                assert exported['advanced_saved']['CW.Fade In Ms'] == 17
                assert exported['advanced_saved']['CW.Fade Shape'] == 'linear'
            assert request('POST', '/api/recovery/session', {}, admitted=False)[0] == 403
            status, fields, data = request('POST', '/api/control/v1/host/management-session',
                                        {'contract': 'Wsprry-Control/1', 'schema': 'controller-browser-session'})
            assert status == 200, (status, data)
            cookie, csrf = fields['Set-Cookie'].split(';')[0], fields['X-WsprryPi-CSRF']
            status, fields, data = request('POST', '/api/recovery/session', {})
            assert status == 200, (status, data)
            recovery_cookie, recovery_csrf = fields['Set-Cookie'].split(';')[0], json.loads(data)['csrf']
            assert request('POST', '/api/recovery/jobs', {'kind': 'backup'}, override={'X-WsprryPi-Recovery-CSRF': '0' * 64})[0] == 403
            phrase = 'synthetic actual maintenance encryption phrase'
            # Actual owner try-lock contention is an explicit busy result. A new
            # operator admission uses a new request ID; unknown jobs never replay.
            for attempt in range(12):
                if attempt == 0:
                    created = cli(['backup'], [('--passphrase-fd', phrase), ('--confirmation-fd', phrase)])
                    assert created.returncode == 0, created.stderr
                    status, data = 200, created.stdout
                else:
                    status, _, data = request('POST', '/api/recovery/jobs', {'kind': 'backup', 'request_id': f'{attempt+1:032x}',
                                                                         'passphrase': phrase, 'confirmation': phrase})
                assert status == 200, (status, data)
                job = json.loads(data)
                deadline = time.monotonic() + 30
                while True:
                    status, _, data = request('GET', '/api/recovery/jobs/' + job['job_id'])
                    assert status == 200, (status, data)
                    job = json.loads(data)
                    if job['state'] in ('ready', 'failed'):
                        break
                    assert time.monotonic() < deadline, job
                    time.sleep(.05)
                if job['state'] == 'ready':
                    break
                assert job['error'] == 'capture_busy', job
            assert job['state'] == 'ready', job
            assert 'stamp' not in job['archive']
            download = '/api/recovery/jobs/' + job['job_id'] + '/download'
            assert request('GET', download, admitted=False)[0] == 403
            status, _, encrypted = request('GET', download)
            assert status == 200 and hashlib.sha256(encrypted).hexdigest() == job['archive']['sha256']
            saved_backup = root / 'cli-download.age'
            copied = cli(['download', '--job-id', job['job_id'], '--output', str(saved_backup)])
            assert copied.returncode == 0 and saved_backup.read_bytes() == encrypted, copied.stderr
            collision = cli(['download', '--job-id', job['job_id'], '--output', str(saved_backup)])
            assert collision.returncode != 0 and saved_backup.read_bytes() == encrypted
            readback = cli(['status', '--job-id', job['job_id']])
            assert readback.returncode == 0 and json.loads(readback.stdout)['state'] == 'ready', readback.stderr
            status, _, partial = request('GET', download, override={'Range': 'bytes=0-63'})
            assert status == 206 and partial == encrypted[:64]
            if moving:
                # Distinct installation, private identity, state and authority.
                # Stop the owned source fixture before starting another actual
                # application singleton; no remote device is involved.
                for child in (backend, web, maintenance):
                    child.terminate();child.wait(timeout=15)
                source_identity = job['preview']['source']['identity'] if job.get('preview') else json.loads((state / 'authority.json').read_text())['identity']
                ledger, state = root / 'destination-identity', root / 'destination-state'
                listener_cache = root / 'destination.web-listeners.json'
                prefix = [fixture, str(ledger), helper, str(state), str(runtime), str(maintenance_runtime)]
                destination_ini = root / 'destination.ini'
                text = source.with_name(source.name + '.sqlite-migrated').read_text()
                text = re.sub(r'(?m)^PPM = .*$', 'PPM = 1.25', text)
                text = re.sub(r'(?m)^Power Level = .*$', 'Power Level = 3', text)
                destination_ini.write_text(text.replace('Call Sign = AA0NT', 'Call Sign = K1ABC').replace('Fade In Ms = 17', 'Fade In Ms = 9'))
                destination_ini.chmod(0o600)
                admitted = subprocess.run(prefix + ['--migrate-state', '--state-directory', str(state), '--ini-source', str(destination_ini)] + (['--controller-only'] if controller else []),
                                          env=environment, capture_output=True, timeout=20)
                assert admitted.returncode == 0, admitted.stderr.decode()
                assert json.loads((state / 'authority.json').read_text())['identity'] != source_identity
                backend = start('destination-backend', ['--state-directory', str(state), '--runtime-directory', str(runtime), '--backend', 'simulated',
                                                       '--socket-loopback-only', '--web-port', str(ports[0]), '--socket-port', str(ports[1]), '--socket-loopback-family', 'ipv4'] + (['--controller-only'] if controller else []))
                maintenance = start('destination-maintenance', ['--maintenance-worker', '--role', role])
                web = start('destination-web', ['--web-service', '--runtime-directory', str(runtime), '--listener-cache', str(listener_cache)])
                cookie, csrf, recovery_cookie, recovery_csrf = '', '', '', ''
                deadline = time.monotonic() + 25
                while True:
                    try:
                        status, fields, data = request('POST', '/api/control/v1/host/management-session',
                                                      {'contract': 'Wsprry-Control/1', 'schema': 'controller-browser-session'})
                        if status == 200:
                            break
                    except OSError:
                        pass
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                cookie, csrf = fields['Set-Cookie'].split(';')[0], fields['X-WsprryPi-CSRF']
                status, fields, data = request('POST', '/api/recovery/session', {})
                assert status == 200, (status, data)
                recovery_cookie, recovery_csrf = fields['Set-Cookie'].split(';')[0], json.loads(data)['csrf']
            # Validation is a separate explicit action. Wrong credentials and
            # a subsequent valid preview must leave the active generation alone.
            before = (state / 'authority.json').read_bytes()
            uploaded = cli(['upload', '--file', str(saved_backup)])
            assert uploaded.returncode == 0, uploaded.stderr
            restore = json.loads(uploaded.stdout)
            path = '/api/recovery/jobs/' + restore['job_id']
            def wait_state(expected):
                deadline = time.monotonic() + 30
                while True:
                    status, _, data = request('GET', path)
                    if status == 503:
                        assert time.monotonic() < deadline, (status, data)
                        time.sleep(.05)
                        continue
                    assert status == 200, (status, data)
                    observed = json.loads(data)
                    if observed['state'] in expected:
                        return observed
                    assert time.monotonic() < deadline, observed
                    time.sleep(.05)
            wait_state(('uploaded', 'failed'))
            assert request('POST', path + '/validate', {'purpose': purpose, 'passphrase': 'wrong phrase'})[0] == 200
            assert wait_state(('failed',))['error'] == 'archive_invalid'
            time.sleep(.05)
            def explicit_validation(action, job_id=None):
                # The owning backend can temporarily hold its configuration
                # gate. Only a durable, definite busy refusal permits another
                # explicit validation; never replay an unknown outcome.
                selected_id = restore['job_id'] if job_id is None else job_id
                proof_path = Path(str(ledger) + '.maintenance/jobs/' + selected_id) / 'validation-result.json'
                for attempt in range(5):
                    action()
                    observed = wait_state(('preview', 'failed'))
                    if observed['state'] == 'preview':
                        return observed
                    proof = json.loads(proof_path.read_text()) if proof_path.exists() else None
                    assert observed['error'] == 'validation_failed' and proof is not None, (observed, proof)
                    assert proof['schema'] == 'recovery-validation-result/1' and proof['job_id'] == selected_id \
                        and proof['state'] == 'failed' and proof['error'] == 'busy', (observed, proof)
                    assert attempt < 4, (observed, proof)
                    time.sleep(.05)
            def validate_cli():
                checked = cli(['validate', '--job-id', restore['job_id'], '--purpose', purpose], [('--passphrase-fd', phrase)])
                assert checked.returncode == 0, checked.stderr
            observed = explicit_validation(validate_cli)
            preview = observed['preview']
            assert preview['purpose'] == purpose and preview['local_output_held'] and preview['remote_authority_held'] and not preview['remote_jobs_adopted']
            assert preview['listeners']['current_web_port'] == ports[0]
            assert preview['listeners']['restored_web_port'] == ports[2]
            assert (state / 'authority.json').read_bytes() == before
            # An explicit preview refresh reuses the admitted encrypted upload,
            # reruns the actual owner validators, and leaves destination state alone.
            time.sleep(.05)
            def validate_http():
                status, _, data = request('POST', path + '/validate', {'purpose': purpose, 'passphrase': phrase})
                assert status == 200, (status, data)
            observed = explicit_validation(validate_http)
            preview = observed['preview']
            assert (state / 'authority.json').read_bytes() == before
            private_job = Path(str(ledger) + '.maintenance/jobs/' + restore['job_id'])
            candidate_evidence = root / 'inactive-evidence-check'
            candidate_evidence.mkdir(mode=0o700)
            retained = evidence.retain_source(candidate_evidence, private_job / 'validated', private_job / 'source-records',
                                              json.loads((private_job / 'archive-manifest.json').read_text()), job['archive']['sha256'])
            assert retained['source_identity'] == preview['source']['identity']
            assert not (candidate_evidence / 'state.sqlite3').exists()
            checked = subprocess.run([str(Path(fixture).parent / 'recovery_evidence_test'), str(candidate_evidence)],
                                     env=environment, capture_output=True, text=True, timeout=15)
            assert checked.returncode == 0 and int(checked.stdout) > retained['files'], checked.stdout + checked.stderr
            assert request('POST', path + '/confirm', {'preview_digest': '0' * 64})[0] == 400
            assert (state / 'authority.json').read_bytes() == before
            # Confirmation is exact and explicit. The same frontend/worker stay
            # alive while the old actual backend exits and a held one restarts.
            time.sleep(.05)
            status, _, data = request('POST', path + '/confirm', {'preview_digest': preview['digest']})
            assert status in ((200, 503) if fault_phase else (200,)), (status, data)
            if fault_phase:
                marker = Path(str(ledger) + '.maintenance/fixture-crash-used.json')
                deadline = time.monotonic() + 30
                while not marker.exists():
                    assert time.monotonic() < deadline, request('GET', path)
                    time.sleep(.02)
                assert json.loads(marker.read_text())['phase'] == fault_phase
                maintenance.terminate();maintenance.wait(timeout=15)
                maintenance = start('maintenance-recovered', maintenance_arguments + ['--maintenance-worker', '--role', role])
            observed = wait_state(('completed', 'failed'))
            assert observed['state'] == 'completed', (observed, [(proof.name, proof.read_text()) for proof in private_job.glob('*result.json')])
            assert observed['result']['state'] == 'completed_held' and observed['result']['generation'] == '2'
            assert observed['result']['saved_web_port'] == ports[2] and observed['result']['web_port'] == (ports[0] if overridden else ports[2])
            backend.wait(timeout=15)
            connection = Connection(runtime, 'control')
            try:
                readback = connection.request('recovery.observe', {})
            finally:
                connection.close()
            assert readback['ok'] and readback['value']['generation'] == '2'
            restarted_pid = readback['value']['pid']
            assert readback['value']['restore_hold']['local_output_held'] and readback['value']['restore_hold']['remote_authority_held']
            deadline = time.monotonic() + 15
            while True:
                try:
                    connection = http.client.HTTPConnection('127.0.0.1', ports[0] if overridden else ports[2], timeout=2)
                    connection.request('GET', '/version')
                    response = connection.getresponse();response.read()
                    if response.status == 200:
                        break
                except OSError:
                    pass
                finally:
                    connection.close()
                assert time.monotonic() < deadline
                time.sleep(.05)
            assert request('GET', path)[0] == 200  # Original admitted endpoint.
            assert request('GET', '/version')[0] == (200 if overridden else 410)
            web.terminate();web.wait(timeout=10)
            web = start('web-restarted', ['--web-service', '--runtime-directory', str(runtime), '--listener-cache', str(listener_cache)])
            deadline = time.monotonic() + 15
            while True:
                try:
                    if request('GET', path)[0] == 200:
                        break
                except OSError:
                    pass
                assert time.monotonic() < deadline
                time.sleep(.05)
            assert (state / 'generation-1/state.sqlite3').is_file() and (state / 'generation-2/state.sqlite3').is_file()
            assert (state / 'generation-2/evidence' / retained['group_id'] / 'index.json').is_file()
            status, _, data = review_request('GET')
            assert status == 200, (status, data)
            review = json.loads(data)
            assert review['hold']['local_output_held'] and review['hold']['remote_authority_held']
            if moving:
                assert review['identity'] != preview['source']['identity']
                if not controller:
                    assert review['settings']['Calibration']['PPM'] == 1.25 and review['settings']['GPIO']['Power Level'] == 3
                if controller:
                    assert review['settings']['Operation']['Web Port'] == preview['listeners']['restored_web_port']
                else:
                    assert review['settings']['CW']['Fade In Ms'] == 17 and review['settings']['CW']['Fade Shape'] == 'linear'
            assert review_request('POST', {'action': 'release', 'digest': '0' * 64,
                                                          'release_local': True, 'release_remote': False})[0] != 200
            assert review_request('GET')[2] == data
            status, _, data = review_request('POST', {'action': 'release', 'digest': review['digest'],
                                                                    'release_local': True, 'release_remote': False})
            assert status == 200, (status, data)
            released = json.loads(data)
            assert released['hold']['local_output_held'] is False and released['hold']['remote_authority_held'] is True
            assert not released['local_transmission_started'] and not released['remote_authority_reconciled']
            assert review_request('POST', {'action': 'release', 'digest': review['digest'],
                                                          'release_local': True, 'release_remote': True})[0] != 200
            if moving:
                if controller:
                    print('PASS actual separate-controller move: distinct destination identity and backend-owner roots, destination authority retained, source evidence, no remote adoption, and exact held review')
                else:
                    print('PASS actual separate-installation move: distinct destination identity, retained GPIO/calibration/authority, portable custom CW settings, source evidence, no remote adoption, exact review and stopped local output')
                return
            if roundtrip:
                # A restored installation, including inactive source evidence,
                # must itself produce an archive accepted by the actual owners.
                for attempt in range(12):
                    created = cli(['backup'], [('--passphrase-fd', phrase), ('--confirmation-fd', phrase)])
                    assert created.returncode == 0, created.stderr
                    second = json.loads(created.stdout)
                    path = '/api/recovery/jobs/' + second['job_id']
                    second = wait_state(('ready', 'failed'))
                    if second['state'] == 'ready':
                        break
                    assert second['error'] == 'capture_busy', second
                assert second['state'] == 'ready', second
                second_file = root / 'restored-backup.age'
                copied = cli(['download', '--job-id', second['job_id'], '--output', str(second_file)])
                assert copied.returncode == 0, copied.stderr
                uploaded = cli(['upload', '--file', str(second_file)])
                assert uploaded.returncode == 0, uploaded.stderr
                again = json.loads(uploaded.stdout)
                path = '/api/recovery/jobs/' + again['job_id']
                assert wait_state(('uploaded', 'failed'))['state'] == 'uploaded'
                def validate_again_cli():
                    checked = cli(['validate', '--job-id', again['job_id'], '--purpose', 'recover'], [('--passphrase-fd', phrase)])
                    assert checked.returncode == 0, checked.stderr
                validated = explicit_validation(validate_again_cli, again['job_id'])
                staged = Path(str(ledger) + '.maintenance/jobs/' + again['job_id'])
                manifest_again = json.loads((staged / 'archive-manifest.json').read_text())
                assert any(item['name'].startswith('application/evidence/') for item in manifest_again['files'])
                assert evidence.inspect(staged / 'validated/application/evidence')
                if history:
                    for generation in (3, 4):
                        if generation == 4:
                            uploaded = cli(['upload', '--file', str(second_file)])
                            assert uploaded.returncode == 0, uploaded.stderr
                            again = json.loads(uploaded.stdout)
                            path = '/api/recovery/jobs/' + again['job_id']
                            assert wait_state(('uploaded', 'failed'))['state'] == 'uploaded'
                            validated = explicit_validation(validate_again_cli, again['job_id'])
                        confirmed = cli(['confirm', '--job-id', again['job_id'], '--preview-digest', validated['preview']['digest']])
                        assert confirmed.returncode == 0, confirmed.stderr
                        finished = wait_state(('completed', 'failed'))
                        assert finished['state'] == 'completed' and finished['result']['generation'] == str(generation), finished
                    assert {item.name for item in state.glob('generation-*') if item.is_dir()} == {'generation-1', 'generation-3', 'generation-4'}
                    assert not (state / 'history-pruning.json').exists()
                    assert not (state / 'generation-2.owner.json').exists()
                    assert (state / 'generation-3.owner.json').is_file() and (state / 'generation-4.owner.json').is_file()
                    assert not any('.owner.json' in item['name'] for item in manifest_again['files'])
                    print('PASS actual repeated restore retains original, selected and immediate predecessor only; backup excludes private retention markers')
                    return

                cancelled = cli(['cancel', '--job-id', again['job_id']])
                assert cancelled.returncode == 0, cancelled.stderr
                job, encrypted = second, second_file.read_bytes()
                path = '/api/recovery/jobs/' + second['job_id']
                download = path + '/download'
            # The actual backend stops. The separate web/maintenance domains
            # retain the admitted endpoint, status and retryable encrypted bytes.
            assert restarted_pid is not None
            os.kill(restarted_pid, signal.SIGTERM)
            deadline = time.monotonic() + 15
            while (runtime / 'control.sock').exists():
                assert time.monotonic() < deadline
                time.sleep(.05)
            # A status command lets the owning worker reap its software child.
            assert request('GET', path)[0] == 200
            restarted_pid = None
            assert web.poll() is None and maintenance.poll() is None
            diagnostic = subprocess.run(prefix + ['--diagnostic-export', '--runtime-directory', str(runtime)], env=environment,
                                        capture_output=True, text=True, timeout=15)
            assert diagnostic.returncode == 3 and json.loads(diagnostic.stdout)['status'] == 'unavailable'
            assert request('GET', '/version')[0] == (503 if overridden else 410)
            assert request('GET', download)[2] == encrypted
            maintenance.terminate();maintenance.wait(timeout=10)
            maintenance = start('maintenance-restarted', maintenance_arguments + ['--maintenance-worker', '--role', role])
            deadline = time.monotonic() + 10
            while True:
                try:
                    status, _, data = request('GET', '/api/recovery/jobs/' + job['job_id'])
                    if status == 200:
                        break
                except OSError:
                    pass
                assert time.monotonic() < deadline
                time.sleep(.05)
            assert json.loads(data)['state'] == 'ready'
            assert request('GET', download)[2] == encrypted
            recovered = root / 'download.age';recovered.write_bytes(encrypted);recovered.chmod(0o600)
            decoded = root / 'decoded';decoded.mkdir(mode=0o700)
            manifest = archive.validate_archive(helper, recovered, decoded, phrase.encode())
            assert manifest['source']['role'] == role and (decoded / 'application/state.sqlite3').is_file()
            print(('PASS recovery after worker death at ' + fault_phase + ': ' if fault_phase else 'PASS ') + 'actual backend/web/maintenance: existing-session admission, CSRF refusal, coherent encrypted backup, bounded download/range/retry, backend outage, worker restart, portable decryption, wrong-passphrase retry and nonmutating actual-owner preview, exact confirmation, preserved prior generation, inactive source evidence and verified held backend restart')
        finally:
            try:
                cleanup_connection = Connection(runtime, 'control')
                try:
                    cleanup_observation = cleanup_connection.request('recovery.observe', {})
                finally:
                    cleanup_connection.close()
                if cleanup_observation.get('ok') and cleanup_observation['value']['pid'] != backend.pid:
                    restarted_pid = cleanup_observation['value']['pid']
            except (OSError, AssertionError, UnboundLocalError):
                pass
            if restarted_pid is not None:
                try:
                    os.kill(restarted_pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                deadline = time.monotonic() + 15
                while (runtime / 'control.sock').exists() and time.monotonic() < deadline:
                    time.sleep(.05)
            for child in reversed(processes):
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        child.kill();child.wait(timeout=5)
            for output in logs:
                output.close()
            if sys.exc_info()[0]:
                diagnostic = Path(tempfile.mkdtemp(prefix='recovery-failed-', dir='/tmp')).resolve()
                shutil.copytree(root, diagnostic / 'fixture', symlinks=True,
                                ignore=lambda directory, names: [name for name in names if stat.S_ISSOCK((Path(directory) / name).lstat().st_mode)])
                print('Retained synthetic failure fixture:', diagnostic, file=sys.stderr)
                for path in root.glob('*.log'):
                    print(path.name, path.read_text(), file=sys.stderr)
            if maintenance_runtime.exists():
                for entry in maintenance_runtime.iterdir():
                    entry.unlink()
                maintenance_runtime.rmdir()

if __name__ == '__main__':
    with backend_admission(sys.argv[1]):
        run(str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()), sys.argv[3] if len(sys.argv) == 4 else None)
