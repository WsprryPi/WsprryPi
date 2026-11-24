#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepared multi-UID Linux production recovery qualification; explicit approval required.

Only a disposable container with the companion launcher may run this test. The
fixture replaces service lifecycle through the existing typed software adapter;
native accounts, IPC, backend state, identity, crypto, and publication stay real.
"""
import http.client
import json
import os
from pathlib import Path
import pwd
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def require(condition):
    if not condition:
        raise RuntimeError('Native account recovery qualification failed; inspect private fixture logs')


def main():
    require(sys.argv[1:] in (['--isolated-container'], ['--isolated-container', '--crash-at=publishing_generation']))
    crash_phase = 'publishing_generation' if len(sys.argv) == 3 else None
    require(sys.platform == 'linux' and os.geteuid() == 0 and Path('/.dockerenv').is_file())
    credentials = dict(line.split(':', 1) for line in Path('/proc/self/status').read_text().splitlines() if ':' in line)
    required_caps = (1 << 0) | (1 << 5) | (1 << 6) | (1 << 7)  # CHOWN/KILL/SETGID/SETUID
    require(all(int(credentials[name].strip(), 16) == required_caps for name in ('CapEff', 'CapPrm', 'CapBnd')))
    require(int(credentials['CapAmb'].strip(), 16) == 0 and credentials['NoNewPrivs'].strip() == '1')
    backend, web, http_account = (pwd.getpwnam(name) for name in ('wsprrypi-controller', 'wsprrypi-web', 'www-data'))
    require([(entry.pw_uid, entry.pw_gid) for entry in (backend, web, http_account)] == [(51001, 51001), (51002, 51002), (51003, 51003)])
    require(backend.pw_dir == '/var/lib/wsprrypi/controller' and web.pw_dir == '/var/lib/wsprrypi-web')
    fixture = Path(tempfile.mkdtemp(prefix='native-accounts-', dir='/tmp'))
    fixture.chmod(0o755)
    program = fixture / 'program'
    program.mkdir(mode=0o755)
    shutil.copyfile('/source/controller_native_accounts_fixture', program / 'controller_native_accounts_fixture')
    shutil.copyfile('/source/recovery-age', program / 'recovery-age')
    for name in ('controller_native_accounts_fixture', 'recovery-age'):
        (program / name).chmod(0o755)
    shutil.copytree('/source/application_recovery', program / 'application_recovery',
        ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    for path in (program / 'application_recovery').rglob('*'):
        require(not path.is_symlink())
        path.chmod(0o755 if path.is_dir() else 0o644)
    (program / 'application_recovery').chmod(0o755)
    executable = str(program / 'controller_native_accounts_fixture')
    logs = fixture / 'logs'
    logs.mkdir(mode=0o700)
    state = '/var/lib/wsprrypi/application-state'
    runtime = '/run/wsprrypi'
    # Construct only private tmpfs namespaces. Parents are completed before
    # handing their ownership to another UID; root needs no DAC override.
    parent = Path('/var/lib/wsprrypi')
    parent.mkdir(mode=0o700)
    home = Path(backend.pw_dir)
    home.mkdir(mode=0o700)
    os.chown(home, backend.pw_uid, backend.pw_gid)
    os.chown(parent, backend.pw_uid, backend.pw_gid)
    for path, uid, gid, mode in ((web.pw_dir, web.pw_uid, web.pw_gid, 0o700),
            ('/var/lib/wsprrypi-maintenance', 0, 0, 0o700),
            (runtime, backend.pw_uid, web.pw_gid, 0o750),
            ('/run/wsprrypi-maintenance', 0, web.pw_gid, 0o750)):
        Path(path).mkdir(mode=mode)
        os.chmod(path, mode)
        os.chown(path, uid, gid)
    environment = {'PATH': '/usr/bin:/bin', 'LANG': 'C'}
    children, streams = [], []
    def actor(user):
        return {'user': user.pw_uid, 'group': user.pw_gid,
                'extra_groups': [web.pw_gid] if user.pw_uid == backend.pw_uid else []}
    def run(arguments, user=None, phrases=(), required=True):
        descriptors = []
        command = [executable, *arguments]
        try:
            for option, value in phrases:
                read, write = os.pipe()
                descriptors.append(read)
                os.write(write, value.encode() + b'\n')
                os.close(write)
                command += [option, str(read)]
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=20, pass_fds=descriptors, env=environment, **(actor(user) if user else {'extra_groups': [web.pw_gid]}))
            if required:
                require(result.returncode == 0)
            return result
        finally:
            for descriptor in descriptors:
                os.close(descriptor)
    def start(name, arguments, user=None):
        output = (logs / (name + '.log')).open('wb')
        streams.append(output)
        child = subprocess.Popen([executable, *arguments], stdin=subprocess.DEVNULL, stdout=output, stderr=output,
            env=environment, start_new_session=True, **(actor(user) if user else {'extra_groups': [web.pw_gid]}))
        children.append(child)
        return child
    def command(arguments, phrases=()):
        return json.loads(run(['--maintenance', *arguments], phrases=phrases).stdout)
    def job_state(job_id, states):
        deadline = time.monotonic() + 60
        while True:
            value = command(['status', '--job-id', job_id])
            if value['state'] in states:
                return value
            require(time.monotonic() < deadline)
            time.sleep(.1)
    def observe():
        return json.loads(run(['--recovery-backend', 'observe', '--runtime-directory', runtime]).stdout)
    def probe(user, paths):
        code = '''
import sys
from pathlib import Path
for path in sys.argv[1:]:
 for mode in ('rb','ab'):
  try:
   with Path(path).open(mode): pass
  except PermissionError: continue
  raise RuntimeError('Unexpected private access')
'''
        result = subprocess.run([sys.executable, '-I', '-c', code, *paths], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10, env=environment, **actor(user))
        require(result.returncode == 0)
    restarted_pid = None
    try:
        run(['--initialize-state', '--controller-only', '--state-directory', state], user=backend)
        backend_process = start('backend', ['--controller-only', '--state-directory', state,
            '--runtime-directory', runtime], backend)
        maintenance = start('maintenance', (['--fixture-fault-phase', crash_phase] if crash_phase else []) + ['--maintenance-worker', '--role', 'controller'])
        web_process = start('web', ['--web-service', '--runtime-directory', runtime], web)
        deadline = time.monotonic() + 30
        while True:
            try:
                first = observe()
                if first['ready'] and command(['jobs'])['active'] is None:
                    break
            except (RuntimeError, json.JSONDecodeError):
                pass
            require(backend_process.poll() is None and maintenance.poll() is None and web_process.poll() is None and time.monotonic() < deadline)
            time.sleep(.1)
        require(first['role'] == 'controller' and first['pid'] == backend_process.pid)
        while True:
            connection = http.client.HTTPConnection('127.0.0.1', first['listeners']['web_port'], timeout=3)
            try:
                connection.request('GET', '/status')
                response = connection.getresponse()
                status = json.loads(response.read())
                if response.status == 200 and status.get('controller_ready'):
                    require(status['role'] == 'controller-only' and status['device_id'] == first['identity'])
                    break
            except (OSError, http.client.HTTPException, json.JSONDecodeError):
                pass
            finally:
                connection.close()
            require(web_process.poll() is None and time.monotonic() < deadline)
            time.sleep(.1)
        for child, user in ((backend_process, backend), (web_process, web)):
            fields = dict(line.split(':', 1) for line in Path(f'/proc/{child.pid}/status').read_text().splitlines() if ':' in line)
            require(all(int(value) == user.pw_uid for value in fields['Uid'].split()))
            require(all(int(fields[name].strip(), 16) == 0 for name in ('CapEff', 'CapPrm', 'CapAmb')))
            require(fields['NoNewPrivs'].strip() == '1')
        connection = http.client.HTTPConnection('127.0.0.1', first['listeners']['web_port'], timeout=10)
        try:
            connection.request('GET', '/version')
            response = connection.getresponse()
            require(response.status == 200)
            version = json.loads(response.read())
            require(version['compiled_backends'] == '' and version['ancillary_gpio'] is False)
        finally:
            connection.close()
        for user in (web, http_account):
            probe(user, [state + '/authority.json', '/var/lib/wsprrypi-maintenance/jobs.json'])
            require(run(['--recovery-backend', 'observe', '--runtime-directory', runtime], user=user, required=False).returncode != 0)
        probe(backend, ['/var/lib/wsprrypi-maintenance/jobs.json'])
        phrase = 'disposable qualification passphrase'
        for attempt in range(6):
            backup = command(['backup'], [('--passphrase-fd', phrase), ('--confirmation-fd', phrase)])
            backup = job_state(backup['job_id'], ('ready', 'failed'))
            if backup['state'] == 'ready':
                break
            require(backup['error'] == 'capture_busy' and attempt < 5)
        archive = fixture / 'qualification.age'
        command(['download', '--job-id', backup['job_id'], '--output', str(archive)])
        restore = command(['upload', '--file', str(archive)])
        restore = job_state(restore['job_id'], ('uploaded', 'failed'))
        require(restore['state'] == 'uploaded')
        for attempt in range(5):
            command(['validate', '--job-id', restore['job_id'], '--purpose', 'recover'], [('--passphrase-fd', phrase)])
            restored = job_state(restore['job_id'], ('preview', 'failed'))
            if restored['state'] == 'preview':
                break
            proof = json.loads((Path('/var/lib/wsprrypi-maintenance/jobs') / restore['job_id'] / 'validation-result.json').read_text())
            require(restored['error'] == 'validation_failed' and proof['state'] == 'failed' and proof['error'] == 'busy' and attempt < 4)
            time.sleep(.1)
        preview = restored['preview']
        require(preview['destination_identity'] == first['identity'] and preview['destination_role'] == 'controller')
        try:
            command(['confirm', '--job-id', restore['job_id'], '--preview-digest', preview['digest']])
        except RuntimeError:
            # A deliberately lost confirmation reply is not permission to
            # replay. Require this job's durable fault marker below instead.
            if not crash_phase:
                raise
        if crash_phase:
            marker = Path('/var/lib/wsprrypi-maintenance/fixture-crash-used.json')
            deadline = time.monotonic() + 30
            while not marker.exists():
                require(time.monotonic() < deadline)
                time.sleep(.05)
            recorded = json.loads(marker.read_text())
            require(recorded['phase'] == crash_phase and recorded['job_id'] == restore['job_id'])
            # The production broker's liveness check must poison/tear down its
            # private child group even without another browser command.
            require(maintenance.wait(timeout=10) != 0)
            maintenance = start('maintenance-recovered', ['--maintenance-worker', '--role', 'controller'])
            while True:
                try:
                    command(['jobs'])
                    break
                except (RuntimeError, json.JSONDecodeError):
                    require(maintenance.poll() is None and time.monotonic() < deadline)
                    time.sleep(.1)
        restored = job_state(restore['job_id'], ('completed', 'failed'))
        require(restored['state'] == 'completed' and restored['result']['state'] == 'completed_held')
        backend_process.wait(timeout=15)
        final = observe()
        restarted_pid = final['pid']
        require(final['identity'] == first['identity'] and final['boot'] != first['boot'] and final['generation'] == '2')
        fields = dict(line.split(':', 1) for line in Path(f'/proc/{restarted_pid}/status').read_text().splitlines() if ':' in line)
        require(all(int(value) == backend.pw_uid for value in fields['Uid'].split()))
        require(all(int(fields[name].strip(), 16) == 0 for name in ('CapEff', 'CapPrm', 'CapAmb')))
        review = command(['review'])
        require(review['hold']['local_output_held'] is True and review['hold']['remote_authority_held'] is True)
        for user in (web, http_account):
            probe(user, [state + '/generation-2/state.sqlite3', '/var/lib/wsprrypi-maintenance/jobs.json'])
        probe(backend, ['/var/lib/wsprrypi-maintenance/jobs.json'])
        print(json.dumps({'result': 'pass', 'backend_uid': backend.pw_uid, 'web_uid': web.pw_uid,
            'http_probe_uid': http_account.pw_uid, 'maintenance_uid': 0, 'backend_capabilities': 'zero',
            'generation': final['generation'], 'worker_loss_phase': crash_phase,
            'scope': 'production native IPC and encrypted recovery with typed software lifecycle; no installed-service or hardware qualification'}))
    except BaseException:
        # These are disposable application diagnostics, never job/archive/key
        # contents. Keep failures reviewable after the container is removed.
        for path in sorted(logs.glob('*.log')):
            print(path.name + ':\n' + path.read_bytes()[-8192:].decode(errors='replace'), file=sys.stderr)
        raise
    finally:
        if restarted_pid:
            try:
                os.kill(restarted_pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill();child.wait(timeout=5)
        for stream in streams:
            stream.close()


if __name__ == '__main__':
    main()
