# SPDX-License-Identifier: MIT
"""One admitted local recovery job; called only through the root worker pipe."""
import base64
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import selectors
import shutil
import subprocess
import threading
import time

from . import archive, evidence, proxy
from .lifecycle import BackendLifecycle
from .publication import GenerationPublisher, recover_hold_preparation, decimal
from .durable import canonical, directory_lock, RecoveryLockBusy, publish_record, read_record, sync_directory


class BackendUnavailable(archive.RecoveryError):
    pass


class RecoveryBusy(archive.RecoveryError):
    pass


class BackendRefused(archive.RecoveryError):
    def __init__(self, code):
        super().__init__('Backend refused admission')
        self.code = code


def backend_command(config, action, job_id=None, payload=None):
    command = [*config['backend_command'], '--recovery-backend', action]
    if job_id is not None:
        command += ['--job-id', job_id]
    command += ['--runtime-directory', config['backend_runtime']]
    process = subprocess.Popen(command, stdin=subprocess.PIPE if payload is not None else subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
    output = bytearray()
    try:
        if payload is not None:
            data = canonical(payload)
            archive.require(len(data) <= 8192)
            process.stdin.write(data)
            process.stdin.close()
        os.set_blocking(process.stdout.fileno(), False)
        with selectors.DefaultSelector() as selection:
            selection.register(process.stdout, selectors.EVENT_READ)
            # Offline candidate construction/sealing streams the bounded archive
            # on the destination card. It is not a small live control query.
            deadline = time.monotonic() + (1800 if action in ('build-candidate', 'seal-candidate') else 12)
            while True:
                if time.monotonic() >= deadline:
                    raise BackendUnavailable('Backend response unavailable')
                for key, _ in selection.select(0.1):
                    chunk = os.read(key.fd, 32769 - len(output))
                    if chunk:
                        output.extend(chunk)
                        archive.require(len(output) <= 32768)
                    else:
                        selection.unregister(process.stdout)
                if process.poll() is not None and not selection.get_map():
                    break
        if process.returncode != 0:
            try:
                refusal = archive.strict_json(output)
                archive.closed(refusal, ('ok', 'error'))
                if refusal['ok'] is False and refusal['error'] == 'busy':
                    raise RecoveryBusy('Backend capture is busy')
                if refusal['ok'] is False and refusal['error'] in ('invalid_request', 'storage_unavailable', 'command_refused', 'revision_conflict', 'setup_required'):
                    raise BackendRefused(refusal['error'])
            except archive.RecoveryError as error:
                if isinstance(error, (RecoveryBusy, BackendRefused)):
                    raise
            raise BackendUnavailable('Backend response unavailable')
        return archive.strict_json(output)
    finally:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=5)
        process.stdout.close()


def process_alive(pid):
    if type(pid) is not int or pid <= 0:
        return True
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def private_job_cleanup(directory):
    archive.private_directory(directory)
    count = 0
    for path in directory.rglob('*'):
        count += 1
        archive.require(count <= 8192 and not path.is_symlink())
        if path.is_dir():
            archive.private_directory(path)
        else:
            with archive.private_input(path, archive.MAX_ARCHIVE):
                pass
    shutil.rmtree(directory)
    sync_directory(directory.parent)


class RecoveryJobs:
    def __init__(self, config):
        archive.closed(config, ('root', 'application_root', 'backend_command', 'backend_runtime', 'age', 'identity', 'role', 'lifecycle'))
        archive.require(type(config['identity']) is str and archive.ID.fullmatch(config['identity']) and config['role'] in ('pi', 'controller'))
        archive.require(type(config['backend_command']) is list and 0 < len(config['backend_command']) <= 8
                        and all(type(item) is str and '\0' not in item and len(item) <= 4096 for item in config['backend_command'])
                        and Path(config['backend_command'][0]).is_absolute())
        self.config = config
        self.lifecycle = BackendLifecycle(config)
        self.root = archive.private_directory(config['root'])
        self.directory = self.root / 'jobs'
        self.directory.mkdir(mode=0o700, exist_ok=True)
        archive.private_directory(self.directory)
        self.lock = threading.RLock()
        self.thread = None
        self.uploads = {}
        self.registry_path = self.root / 'jobs.json'
        registry, _ = read_record(self.registry_path)
        if registry is None:
            archive.require(not any(self.directory.iterdir()))
            registry = {'schema': 'recovery-jobs/1', 'identity': config['identity'], 'role': config['role'], 'active': None, 'jobs': []}
            publish_record(self.registry_path, registry, None)
        self.validate_registry(registry)
        self.registry = registry
        self.recover_pruning()
        self.recover_admission()
        # Before confirmation, interruption never applies state. Confirmed
        # restore recovery is handled by the publication owner before readiness.
        for job_id in registry['jobs']:
            job = self.read(job_id)
            if job['state'] in ('created', 'capturing', 'encrypting', 'uploading', 'upload_checking', 'validating'):
                self.save(dict(job, state='failed', error='interrupted'))
                if self.registry['active'] == job_id:
                    self.registry['active'] = None
            elif job['state'] in ('ready', 'completed', 'failed') and self.registry['active'] == job_id:
                self.registry['active'] = None
        self.save_registry()
        pending = [item for item in self.registry['jobs'] if self.read(item)['state'] in ('applying', 'awaiting_backend')]
        archive.require(len(pending) <= 1 and (not pending or self.registry['active'] == pending[0]))
        if pending:
            self.spawn(self.recover_restore, pending[0])

    def recover_pruning(self):
        intent, _ = read_record(self.root / 'pruning.json')
        if intent is None:
            return
        archive.closed(intent, ('schema', 'job_id', 'identity', 'role'))
        archive.require(intent['schema'] == 'recovery-pruning/1' and intent['identity'] == self.config['identity']
                        and intent['role'] == self.config['role'] and type(intent['job_id']) is str
                        and archive.ID.fullmatch(intent['job_id']) and intent['job_id'] != self.registry['active'])
        job_id = intent['job_id']
        if job_id in self.registry['jobs']:
            self.registry['jobs'].remove(job_id)
            self.save_registry()
        path = self.directory / job_id
        if path.exists():
            # No worker/download child survives broker process-group teardown.
            private_job_cleanup(path)
        (self.root / 'pruning.json').unlink()
        sync_directory(self.root)

    def recover_admission(self):
        # An admission can die between durable job creation and registry update.
        # Preserve its status as interrupted; it was never a confirmed restore.
        entries = list(self.directory.iterdir())
        archive.require(len(entries) <= 4)
        for path in entries:
            archive.require(archive.ID.fullmatch(path.name))
            archive.private_directory(path)
            if path.name in self.registry['jobs']:
                continue
            if not any(path.iterdir()):
                path.rmdir()
                sync_directory(self.directory)
                continue
            job = self.read(path.name)
            archive.require(job['state'] == 'created')
            self.save(dict(job, state='failed', error='interrupted'))
            self.registry['jobs'].append(path.name)
            self.save_registry()

    def validate_registry(self, value):
        archive.closed(value, ('schema', 'identity', 'role', 'active', 'jobs'))
        archive.require(value['schema'] == 'recovery-jobs/1' and value['identity'] == self.config['identity'] and value['role'] == self.config['role']
                        and type(value['jobs']) is list and len(value['jobs']) <= 4
                        and all(type(item) is str and archive.ID.fullmatch(item) for item in value['jobs'])
                        and len(set(value['jobs'])) == len(value['jobs'])
                        and (value['active'] is None or value['active'] in value['jobs']))

    def save_registry(self):
        self.validate_registry(self.registry)
        path = self.registry_path
        old, observation = read_record(path)
        if old != self.registry:
            publish_record(path, self.registry, observation)

    def read(self, job_id):
        archive.require(type(job_id) is str and archive.ID.fullmatch(job_id))
        job, _ = read_record(self.directory / job_id / 'job.json')
        archive.closed(job, ('schema', 'job_id', 'request_id', 'identity', 'role', 'kind', 'state', 'created_utc', 'error',
                             'backend', 'archive', 'purpose', 'preview', 'confirmation_sha256', 'phase'))
        archive.require(job['schema'] == 'recovery-job/1' and job['job_id'] == job_id and job['identity'] == self.config['identity']
                        and job['role'] == self.config['role'] and job['kind'] in ('backup', 'restore')
                        and type(job['request_id']) is str and archive.ID.fullmatch(job['request_id'])
                        and type(job['created_utc']) is int and job['state'] in ('created', 'capturing', 'encrypting', 'ready', 'uploading',
                            'upload_checking', 'uploaded', 'validating', 'preview', 'applying', 'awaiting_backend', 'completed', 'failed')
                        and (job['error'] is None or job['error'] in ('interrupted', 'backend_unavailable', 'capture_busy',
                            'storage_unavailable', 'archive_invalid', 'validation_failed', 'destination_recovery_required', 'destination_changed', 'restore_failed', 'cancelled')))
        archive.require(job['phase'] is None or job['phase'] in ('holding_output', 'stopping_backend', 'building_candidate',
                        'retaining_evidence', 'validating_candidate', 'publishing_generation', 'starting_backend', 'verifying_backend', 'complete'))
        return job

    def save(self, job):
        with self.lock:
            path = self.directory / job['job_id'] / 'job.json'
            previous, observation = read_record(path)
            if previous != job:
                publish_record(path, job, observation)
                fault = self.config['lifecycle'].get('crash_after_phase')
                if fault is not None and self.config['lifecycle']['kind'] == 'software_fixture' and job['phase'] == fault:
                    marker = self.root / 'fixture-crash-used.json'
                    used, _ = read_record(marker)
                    if used is None:
                        publish_record(marker, {'schema': 'recovery-fixture-crash/1', 'phase': fault, 'job_id': job['job_id']}, None)
                        os._exit(97)
            return job

    def public(self, job):
        # No private paths, captured key records, upload contents, or passphrases.
        value = {key: job[key] for key in ('schema', 'job_id', 'request_id', 'kind', 'state', 'created_utc', 'error', 'purpose', 'preview', 'phase')}
        value['archive'] = None if job['archive'] is None else {key: job['archive'][key] for key in ('size', 'sha256')}
        value['result'] = read_record(self.directory / job['job_id'] / 'result.json')[0]
        value['confirmed'] = job['confirmation_sha256'] is not None
        return value

    def recovery_pinned(self, job):
        if job['kind'] != 'restore' or job['confirmation_sha256'] is None or job['state'] == 'completed' or job['error'] == 'destination_changed':
            return False
        # A failed admitted restore still owns its recovery proof. Only a
        # complete destination review (or a newer admitted hold) can retire it.
        try:
            pending, _ = read_record(Path(self.config['application_root']) / 'restore-preparation.json')
            held, _ = read_record(Path(self.config['application_root']) / 'restore-hold.json')
            if pending is not None or type(held) is not dict:
                return True
            archive.closed(held, ('schema', 'identity', 'role', 'generation', 'job_id', 'local_output', 'remote_authority', 'revision', 'stage'))
            archive.require(held['schema'] == 'application-restore-hold/1' and held['identity'] == self.config['identity']
                            and held['role'] == self.config['role'] and decimal(held['generation']) and decimal(held['revision'])
                            and type(held['job_id']) is str and archive.ID.fullmatch(held['job_id'])
                            and type(held['local_output']) is bool and type(held['remote_authority']) is bool
                            and held['stage'] in ('review', 'maintenance'))
            return held.get('job_id') == job['job_id'] and (held.get('local_output') is not False or held.get('remote_authority') is not False)
        except (OSError, archive.RecoveryError):
            return True

    def prune(self):
        while len(self.registry['jobs']) >= 4:
            candidates = [item for item in self.registry['jobs'] if item != self.registry['active']
                          and self.read(item)['state'] in ('ready', 'completed', 'failed') and not self.recovery_pinned(self.read(item))]
            if not candidates:
                raise RecoveryBusy('Retained restore recovery still requires destination review')
            job_id = candidates[0]
            job = self.read(job_id)
            archive.require(job_id != self.registry['active'] and job['state'] in ('ready', 'completed', 'failed'))
            path = self.directory / job_id
            proof, _ = read_record(path / ('capture-result.json' if job['kind'] == 'backup' else 'validation-result.json'))
            archive.require(job['backend'] is None or (proof is not None and proof.get('job_id') == job_id
                            and proof.get('backend_boot') == job['backend'].get('boot')) or not process_alive(job['backend'].get('pid')))
            descriptor = None
            try:
                artifact = path / ('backup.age' if job['kind'] == 'backup' else 'upload.age')
                if artifact.exists():
                    descriptor = os.open(artifact, os.O_RDONLY | os.O_NOFOLLOW)
                    try:
                        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError as error:
                        raise RecoveryBusy('A completed archive is still being downloaded') from error
                intent = {'schema': 'recovery-pruning/1', 'job_id': job_id,
                          'identity': self.config['identity'], 'role': self.config['role']}
                publish_record(self.root / 'pruning.json', intent, None)
                self.registry['jobs'].remove(job_id)
                self.save_registry()
                private_job_cleanup(path)
                (self.root / 'pruning.json').unlink()
                sync_directory(self.root)
            finally:
                if descriptor is not None:
                    os.close(descriptor)

    def ensure_idle(self):
        if self.registry['active'] is not None or (self.thread is not None and self.thread.is_alive()):
            raise RecoveryBusy('A recovery job is already active')

    def new_job(self, request_id, kind, backend=None, total=None):
        archive.require(type(request_id) is str and archive.ID.fullmatch(request_id))
        self.ensure_idle()
        archive.require(not any(self.read(item)['request_id'] == request_id for item in self.registry['jobs']))
        self.prune()
        job_id = secrets.token_hex(16)
        directory = self.directory / job_id
        directory.mkdir(mode=0o700)
        sync_directory(self.directory)
        job = {'schema': 'recovery-job/1', 'job_id': job_id, 'request_id': request_id, 'identity': self.config['identity'],
               'role': self.config['role'], 'kind': kind, 'state': 'created', 'created_utc': int(time.time()), 'error': None,
               'backend': backend, 'archive': None if total is None else {'size': str(total), 'sha256': None, 'stamp': None},
               'purpose': None, 'preview': None, 'confirmation_sha256': None, 'phase': None}
        self.save(job)
        self.registry['jobs'].append(job_id)
        self.registry['active'] = job_id
        self.save_registry()
        return job

    def fail(self, job_id, code):
        with self.lock:
            job = self.read(job_id)
            self.save(dict(job, state='failed', error=code))
            if self.registry['active'] == job_id:
                self.registry['active'] = None
                self.save_registry()

    def spawn(self, action, *arguments):
        archive.require(self.thread is None or not self.thread.is_alive())
        self.thread = threading.Thread(target=action, args=arguments, name='application-recovery', daemon=False)
        self.thread.start()

    def observe(self):
        value = backend_command(self.config, 'observe')
        archive.closed(value, ('schema', 'identity', 'role', 'boot', 'pid', 'generation', 'restore_hold', 'storage_scope', 'configuration_digest', 'ready', 'listeners'))
        archive.require(value['schema'] == 'recovery-backend-observation/1' and value['identity'] == self.config['identity']
                        and value['role'] == self.config['role'] and type(value['boot']) is str and archive.ID.fullmatch(value['boot'])
                        and type(value['pid']) is int and value['pid'] > 0
                        and value['storage_scope'] == hashlib.sha256(str(Path(self.config['application_root']).resolve()).encode()).hexdigest()
                        and type(value['configuration_digest']) is str and archive.HASH.fullmatch(value['configuration_digest']) and type(value['ready']) is bool)
        listeners = value['listeners']
        archive.closed(listeners, ('web_port', 'socket_port', 'http_enabled', 'websocket_enabled', 'http_bound', 'websocket_bound'))
        archive.require(all(type(listeners[key]) is int and 1024 <= listeners[key] <= 49151 for key in ('web_port', 'socket_port'))
                        and all(type(listeners[key]) is bool for key in ('http_enabled', 'websocket_enabled'))
                        and all(listeners[key] is None or (type(listeners[key]) is int and 1024 <= listeners[key] <= 49151) for key in ('http_bound', 'websocket_bound')))
        return value

    def capture_proof(self, job):
        path = self.directory / job['job_id'] / 'capture-result.json'
        while True:
            proof, _ = read_record(path)
            if proof is not None:
                archive.closed(proof, ('schema', 'job_id', 'backend_boot', 'state', 'inventory', 'error'))
                archive.require(proof['schema'] == 'recovery-capture-result/1' and proof['job_id'] == job['job_id']
                                and proof['backend_boot'] == job['backend']['boot'])
                return proof
            if not process_alive(job['backend']['pid']):
                raise BackendUnavailable('Capture owner stopped before publication')
            time.sleep(0.1)

    def backup(self, job_id, phrase):
        try:
            job = self.read(job_id)
            backend = self.observe()
            job = self.save(dict(job, backend=backend))
            directory = self.directory / job_id
            publish_record(directory / 'capture-admission.json', {'schema': 'recovery-capture-admission/1', 'job_id': job_id,
                           'backend_boot': job['backend']['boot'], 'identity': job['identity']}, None)
            try:
                backend_command(self.config, 'capture', job_id)
            except BackendUnavailable:
                # A missing reply is not permission to send the capture again.
                pass
            proof = self.capture_proof(job)
            if proof['state'] != 'ready':
                self.fail(job_id, 'capture_busy' if proof['error'] == 'busy' else 'storage_unavailable')
                return
            inventory = proof['inventory']
            archive.closed(inventory, ('schema', 'source', 'files', 'storage_revision'))
            archive.require(inventory['schema'] == 'recovery-snapshot/1')
            manifest = archive.manifest_for_snapshot(directory / 'snapshot', inventory['source'], inventory['files'])
            self.save(dict(job, state='encrypting'))
            output = directory / 'backup.age'
            archive.create_archive(self.config['age'], directory / 'snapshot', output, manifest, phrase)
            with archive.private_input(output, archive.MAX_ARCHIVE) as (stream, info):
                receipt = {'size': str(info.st_size), 'sha256': archive.file_digest(stream), 'stamp': list(archive.observation(info))}
            sync_directory(directory)
            private_job_cleanup(directory / 'snapshot')
            with self.lock:
                self.save(dict(self.read(job_id), state='ready', archive=receipt))
                self.registry['active'] = None
                self.save_registry()
        except RecoveryBusy:
            self.fail(job_id, 'capture_busy')
        except BackendUnavailable:
            self.fail(job_id, 'backend_unavailable')
        except BaseException:
            self.fail(job_id, 'storage_unavailable')
        finally:
            phrase = b''

    def check_upload(self, job_id):
        try:
            directory = self.directory / job_id
            with archive.private_input(directory / 'upload.age', archive.MAX_ARCHIVE) as (stream, info):
                receipt = {'size': str(info.st_size), 'sha256': archive.file_digest(stream), 'stamp': list(archive.observation(info))}
            sync_directory(directory)
            self.save(dict(self.read(job_id), state='uploaded', archive=receipt))
        except BaseException:
            self.fail(job_id, 'storage_unavailable')

    def validation_proof(self, job):
        path = self.directory / job['job_id'] / 'validation-result.json'
        while True:
            proof, _ = read_record(path)
            if proof is not None:
                archive.closed(proof, ('schema', 'job_id', 'backend_boot', 'state', 'preview_digest', 'error'))
                archive.require(proof['schema'] == 'recovery-validation-result/1' and proof['job_id'] == job['job_id']
                                and proof['backend_boot'] == job['backend']['boot'])
                return proof
            if not process_alive(job['backend']['pid']):
                raise BackendUnavailable('Validation owner stopped before publication')
            time.sleep(0.1)

    def validate(self, job_id, phrase, purpose):
        try:
            directory = self.directory / job_id
            decoded = directory / 'decoded'
            decoded.mkdir(mode=0o700)
            job = self.read(job_id)
            manifest = archive.validate_archive(self.config['age'], directory / 'upload.age', decoded, phrase, job['archive']['stamp'])
            evidence.inspect(decoded / 'application/evidence')
            publish_record(directory / 'archive-manifest.json', manifest, None)
            job = self.read(job_id)
            backend = self.observe()
            job = self.save(dict(job, backend=backend))
            publish_record(directory / 'validation-admission.json', {'schema': 'recovery-validation-admission/1', 'job_id': job_id,
                           'backend_boot': backend['boot'], 'identity': backend['identity'], 'role': backend['role'],
                           'purpose': purpose, 'archive_sha256': job['archive']['sha256'],
                           'manifest_sha256': hashlib.sha256(canonical(manifest)).hexdigest()}, None)
            try:
                backend_command(self.config, 'validate', job_id)
            except BackendUnavailable:
                # A lost reply does not cause a second owner validation.
                pass
            proof = self.validation_proof(job)
            if proof['state'] != 'ready':
                self.fail(job_id, 'destination_recovery_required' if proof['error'] == 'destination_recovery_required' else 'validation_failed')
                return
            preview, _ = read_record(directory / 'preview.json')
            archive.require(type(preview) is dict and preview.get('schema') == 'recovery-preview/1'
                            and preview.get('job_id') == job_id and preview.get('backend_boot') == backend['boot']
                            and preview.get('archive_sha256') == job['archive']['sha256']
                            and preview.get('purpose') == purpose and preview.get('digest') == proof['preview_digest'])
            with self.lock:
                self.save(dict(self.read(job_id), state='preview', backend=backend, purpose=purpose, preview=preview))
        except (RecoveryBusy, BackendRefused):
            self.fail(job_id, 'validation_failed')
        except BackendUnavailable:
            self.fail(job_id, 'backend_unavailable')
        except BaseException:
            self.fail(job_id, 'archive_invalid')
        finally:
            phrase = b''

    def reset_validation(self, job):
        # Explicit operator refresh only. Serialize with the actual validator,
        # and never erase a proof while a lost request may still be executing.
        archive.require(job['confirmation_sha256'] is None)
        directory = self.directory / job['job_id']
        with directory_lock(directory):
            proof, _ = read_record(directory / 'validation-result.json')
            archive.require(job['backend'] is None or proof is not None or not process_alive(job['backend']['pid']))
            # Commit the pre-validation state before deleting old staging. A
            # crash during cleanup remains explicitly retryable without a lost
            # owner proof, and a queued old validation cannot use this state.
            job = self.save(dict(job, state='failed', error='interrupted', backend=None, preview=None, purpose=None))
            for name in ('decoded', 'validated', 'source-records'):
                path = directory / name
                if path.exists():
                    private_job_cleanup(path)
            for name in ('archive-manifest.json', 'validation-admission.json', 'validation-result.json', 'preview.json', 'proposed-settings.json'):
                path = directory / name
                if path.exists():
                    read_record(path)  # Refuse unexpected file type/ownership.
                    path.unlink()
            sync_directory(directory)
            return job

    def stop_proof(self, job):
        while True:
            proof, _ = read_record(self.directory / job['job_id'] / 'stop-result.json')
            if proof is not None:
                archive.closed(proof, ('schema', 'job_id', 'backend_boot', 'state', 'generation', 'error'))
                archive.require(proof['schema'] == 'recovery-stop-result/1' and proof['job_id'] == job['job_id']
                                and proof['backend_boot'] == job['backend']['boot'])
                return proof
            if not process_alive(job['backend']['pid']):
                raise BackendUnavailable('Backend stopped before drain publication')
            time.sleep(.1)

    def verify_started(self, job_id, prepared):
        deadline = time.monotonic() + 60
        while True:
            try:
                observed = self.observe()
                held = observed['restore_hold']
                if not observed['ready']:
                    archive.require(time.monotonic() < deadline)
                    time.sleep(.1)
                    continue
                listeners = observed['listeners']
                if ((listeners['http_enabled'] and listeners['http_bound'] != listeners['web_port'])
                        or (listeners['websocket_enabled'] and listeners['websocket_bound'] != listeners['socket_port'])):
                    archive.require(time.monotonic() < deadline)
                    time.sleep(.1)
                    continue
                archive.require(observed['generation'] == prepared['generation'] and observed['boot'] != prepared['backend_boot']
                                and type(held) is dict and held.get('job_id') == job_id and held.get('local_output_held') is True
                                and held.get('remote_authority_held') is True and held.get('maintenance_active') is False)
                built, _ = read_record(self.directory / job_id / 'candidate-built.json')
                archive.require(built is not None and observed['configuration_digest'] == built['settings_sha256'])
                return observed
            except BackendUnavailable:
                archive.require(time.monotonic() < deadline)
                time.sleep(.1)

    def complete_candidate(self, job_id, previous_generation):
        job = self.read(job_id)
        directory = self.directory / job_id
        self.save(dict(self.read(job_id), phase='building_candidate'))
        backend_command(self.config, 'build-candidate', job_id)
        manifest, _ = read_record(directory / 'archive-manifest.json')
        self.save(dict(self.read(job_id), phase='retaining_evidence'))
        evidence.copy_groups(Path(self.config['application_root']) / ('generation-' + previous_generation) / 'evidence', directory / 'candidate/evidence')
        evidence.retain_source(directory / 'candidate', directory / 'validated', directory / 'source-records', manifest, job['archive']['sha256'])
        self.save(dict(self.read(job_id), phase='validating_candidate'))
        backend_command(self.config, 'seal-candidate', job_id)
        prepared, _ = read_record(directory / 'prepared.json')
        archive.require(prepared is not None and prepared['job_id'] == job_id and prepared['archive_sha256'] == job['archive']['sha256'])
        self.save(dict(self.read(job_id), phase='publishing_generation'))
        outcome = GenerationPublisher(self.config['application_root'], directory).publish(prepared)
        archive.require(outcome == {'state': 'restored_generation_held', 'generation': prepared['generation']})
        self.save(dict(self.read(job_id), state='awaiting_backend', phase='starting_backend'))
        self.finish_published(job_id, prepared)

    def finish_published(self, job_id, prepared):
        job = self.read(job_id)
        directory = self.directory / job_id
        existing, _ = read_record(directory / 'result.json')
        if existing is None:
            try:
                current = self.observe()
            except BackendUnavailable:
                current = None
            if current is None:
                from .lifecycle import root_available
                if root_available(self.config['application_root']):
                    self.lifecycle.start()
            self.save(dict(self.read(job_id), state='awaiting_backend', phase='verifying_backend'))
            observed = self.verify_started(job_id, prepared)
            if self.config['lifecycle']['kind'] == 'systemd':
                proxy.reconcile(directory, observed['listeners']['web_port'], observed['listeners']['socket_port'])
            built, _ = read_record(directory / 'candidate-built.json')
            result = {'schema': 'recovery-result/1', 'job_id': job_id, 'state': 'completed_held', 'generation': prepared['generation'],
                      'backend_boot': observed['boot'], 'local_output_held': True, 'remote_authority_held': True,
                      'web_port': observed['listeners']['web_port'], 'socket_port': observed['listeners']['socket_port'],
                      'saved_web_port': job['preview']['listeners']['restored_web_port'], 'saved_socket_port': job['preview']['listeners']['restored_socket_port'],
                      'unresolved_definitions': built['unresolved_definitions']}
            publish_record(directory / 'result.json', result, None)
        else:
            archive.require(existing.get('schema') == 'recovery-result/1' and existing.get('job_id') == job_id
                            and existing.get('generation') == prepared['generation'] and existing.get('state') == 'completed_held')
        with self.lock:
            self.save(dict(self.read(job_id), state='completed', error=None, phase='complete'))
            self.registry['active'] = None
            self.save_registry()

    def stop_for_recovery(self, job_id):
        try:
            current = self.observe()
        except BackendUnavailable:
            current = None
        if current is not None:
            held = current['restore_hold']
            archive.require(held.get('local_output_held') is True and held.get('remote_authority_held') is True
                            and held.get('maintenance_active') is True and held.get('job_id') in (None, job_id))
            self.lifecycle.stop(current['pid'])
        else:
            from .lifecycle import root_available
            archive.require(root_available(self.config['application_root']))

    def recover_restore(self, job_id):
        try:
            job = self.read(job_id)
            directory = self.directory / job_id
            publication, _ = read_record(directory / 'publication.json')
            if publication is not None:
                publisher = GenerationPublisher(self.config['application_root'], directory)
                prepared = publisher.admit(publication['prepared'])
                if publication['phase'] == 'published':
                    selected, _ = read_record(Path(self.config['application_root']) / 'authority.json')
                    held, _ = read_record(Path(self.config['application_root']) / 'restore-hold.json')
                    archive.require(selected == prepared['selection'] and held == prepared['hold'])
                    if publisher.history_required(prepared):
                        # A reboot may have started the selected held backend.
                        # Only this restore's still-held generation can be stopped.
                        try:
                            current = self.observe()
                        except BackendUnavailable:
                            current = None
                        if current is not None:
                            view = current['restore_hold']
                            archive.require(current['generation'] == prepared['generation'] and view.get('job_id') == job_id
                                            and view.get('local_output_held') is True and view.get('remote_authority_held') is True)
                            self.lifecycle.stop(current['pid'])
                        publisher.finish_history(prepared)
                else:
                    self.stop_for_recovery(job_id)
                    outcome = publisher.recover()
                    if outcome['state'] == 'prior_generation_held':
                        outcome = publisher.publish(prepared)
                    archive.require(outcome == {'state': 'restored_generation_held', 'generation': prepared['generation']})
                self.finish_published(job_id, prepared)
                return
            # The directory lock closes the queued/lost-request race: a prior
            # backend preparation either finishes under this same lock, or sees
            # the terminal job state before it can mutate anything.
            while True:
                try:
                    with directory_lock(directory):
                        proof, _ = read_record(directory / 'stop-result.json')
                        if proof is None or proof.get('state') != 'held':
                            self.fail(job_id, 'interrupted')
                        break
                except RecoveryLockBusy:
                    time.sleep(.1)
            if proof is None or proof.get('state') != 'held':
                # No positive drain proof: retain the verified prior generation
                # and both holds, rather than inventing output reconciliation.
                pending, _ = read_record(Path(self.config['application_root']) / 'restore-preparation.json')
                admitted, _ = read_record(directory / 'hold-prepared.json')
                if pending is not None or admitted is not None:
                    self.stop_for_recovery(job_id)
                    recover_hold_preparation(self.config['application_root'], directory)
                    with directory_lock(self.config['application_root']):
                        path = Path(self.config['application_root']) / 'restore-hold.json'
                        held, observation = read_record(path)
                        archive.require(type(held) is dict and held.get('job_id') == job_id and held.get('stage') == 'maintenance'
                                        and held.get('local_output') is True and held.get('remote_authority') is True)
                        publish_record(path, dict(held, stage='review', revision=str(int(held['revision']) + 1)), observation)
                    self.lifecycle.start()
                return
            archive.require(proof.get('schema') == 'recovery-stop-result/1' and proof.get('job_id') == job_id
                            and proof.get('backend_boot') == job['backend']['boot'] and proof.get('error') is None)
            self.stop_for_recovery(job_id)
            recover_hold_preparation(self.config['application_root'], directory)
            prepared, _ = read_record(directory / 'prepared.json')
            if prepared is not None:
                outcome = GenerationPublisher(self.config['application_root'], directory).publish(prepared)
                archive.require(outcome == {'state': 'restored_generation_held', 'generation': prepared['generation']})
                self.finish_published(job_id, prepared)
                return
            # All private query children were killed/reaped with the old worker.
            # Unpublished staging is regenerated from the held destination and
            # already validated source, never replayed into active owners.
            for name in ('candidate', 'candidate-working'):
                path = directory / name
                if path.exists():
                    private_job_cleanup(path)
            for name in ('candidate-built.json', 'prepared.json'):
                path = directory / name
                record, _ = read_record(path)
                if record is not None:
                    path.unlink()
            sync_directory(directory)
            self.complete_candidate(job_id, proof['generation'])
        except BaseException:
            self.fail(job_id, 'restore_failed')

    def apply(self, job_id):
        try:
            job = self.read(job_id)
            directory = self.directory / job_id
            self.save(dict(self.read(job_id), phase='holding_output'))
            deadline = time.monotonic() + 5
            while True:
                try:
                    backend_command(self.config, 'prepare', job_id)
                    break
                except RecoveryBusy:
                    # A definite refusal precedes hold publication. The same
                    # admission still binds the backend boot and preview.
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(.1)
                except BackendUnavailable:
                    break  # Read the exact owner proof; never replay preparation.
            proof = self.stop_proof(job)
            archive.require(proof['state'] == 'held' and proof['error'] is None)
            # Re-observe immediately before a fixture signal. A cached PID is
            # never used after boot/process loss. Production stops only its one
            # fixed systemd unit and still requires root-lock exclusion.
            current = self.observe()
            archive.require(current['restore_hold'].get('job_id') == job_id and current['restore_hold'].get('maintenance_active') is True
                            and current['generation'] == proof['generation'])
            self.save(dict(self.read(job_id), phase='stopping_backend'))
            self.lifecycle.stop(current['pid'])
            archive.require(recover_hold_preparation(self.config['application_root'], directory) is not None)
            self.complete_candidate(job_id, proof['generation'])
        except BackendRefused as error:
            self.fail(job_id, 'destination_changed' if error.code == 'revision_conflict' else 'restore_failed')
        except BaseException:
            # Durable hold/publication records remain available independently
            # of backend startup. Failure never clears a hold or selects fresh
            # state, and never reports publication as verified completion.
            self.fail(job_id, 'restore_failed')

    def command(self, value):
        with self.lock:
            if self.lifecycle.child is not None:
                self.lifecycle.child.poll()  # Reap an owned software service; no mutation/replay.
            archive.require(type(value) is dict and type(value.get('action')) is str)
            action = value['action']
            if 'job_id' in value:
                archive.require(type(value['job_id']) is str and value['job_id'] in self.registry['jobs'])
            if action in ('list', 'list.summary'):
                archive.closed(value, ('action',))
                jobs = [self.public(self.read(item)) for item in reversed(self.registry['jobs'])]
                if action == 'list.summary':
                    # The local CLI's one-frame control reply lists bounded
                    # summaries; status reads one complete preview separately.
                    jobs = [{**{key: job[key] for key in ('job_id', 'request_id', 'kind', 'state', 'created_utc', 'error', 'purpose', 'phase', 'confirmed')},
                             'schema': 'recovery-job-summary/1'} for job in jobs]
                return {'schema': 'recovery-job-list/1', 'active': self.registry['active'],
                        'jobs': jobs}
            if action == 'review':
                archive.closed(value, ('action', 'review'))
                archive.require(self.registry['active'] is None)
                return backend_command(self.config, 'review', payload=value['review'])
            if action == 'status':
                archive.closed(value, ('action', 'job_id'))
                archive.require(value['job_id'] in self.registry['jobs'])
                return self.public(self.read(value['job_id']))
            if action == 'backup.create':
                archive.closed(value, ('action', 'request_id', 'passphrase', 'confirmation'))
                archive.require(type(value['passphrase']) is str and value['passphrase'] == value['confirmation'])
                phrase = value['passphrase'].encode('utf-8')
                archive.require(0 < len(phrase) <= 1024)
                self.ensure_idle()
                job = self.new_job(value['request_id'], 'backup')
                job = self.save(dict(job, state='capturing'))
                self.spawn(self.backup, job['job_id'], phrase)
                return self.public(job)
            if action == 'restore.upload.start':
                archive.closed(value, ('action', 'request_id', 'size'))
                archive.require(type(value['size']) is str and len(value['size']) <= 10 and value['size'].isascii() and value['size'].isdecimal()
                                and 0 < int(value['size']) <= archive.MAX_ARCHIVE and str(int(value['size'])) == value['size'])
                total = int(value['size'])
                archive.require(shutil.disk_usage(self.root).free >= total)
                job = self.new_job(value['request_id'], 'restore', total=total)
                descriptor = os.open(self.directory / job['job_id'] / 'upload.age', os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
                self.uploads[job['job_id']] = descriptor
                return self.public(self.save(dict(job, state='uploading')))
            if action == 'restore.upload.chunk':
                archive.closed(value, ('action', 'job_id', 'offset', 'bytes'))
                job = self.read(value['job_id'])
                archive.require(job['state'] == 'uploading' and value['job_id'] in self.uploads
                                and type(value['offset']) is str and len(value['offset']) <= 20
                                and value['offset'].isascii() and value['offset'].isdecimal()
                                and str(int(value['offset'])) == value['offset'] and type(value['bytes']) is str and len(value['bytes']) <= 87384)
                data = base64.b64decode(value['bytes'], validate=True)
                archive.require(0 < len(data) <= 65536 and base64.b64encode(data).decode() == value['bytes'])
                descriptor, offset = self.uploads[value['job_id']], int(value['offset'])
                position = os.lseek(descriptor, 0, os.SEEK_END)
                archive.require(offset <= position and offset + len(data) <= int(job['archive']['size']))
                if offset < position:
                    archive.require(offset + len(data) <= position and os.pread(descriptor, len(data), offset) == data)
                else:
                    remaining = memoryview(data)
                    while remaining:
                        count = os.write(descriptor, remaining)
                        archive.require(count > 0)
                        remaining = remaining[count:]
                    position += len(data)
                return {'offset': str(position)}
            if action == 'restore.upload.finish':
                archive.closed(value, ('action', 'job_id'))
                job = self.read(value['job_id'])
                archive.require(job['state'] == 'uploading' and value['job_id'] in self.uploads)
                descriptor = self.uploads[value['job_id']]
                archive.require(os.fstat(descriptor).st_size == int(job['archive']['size']))
                os.fsync(descriptor)
                os.close(descriptor)
                del self.uploads[value['job_id']]
                job = self.save(dict(job, state='upload_checking'))
                self.spawn(self.check_upload, value['job_id'])
                return self.public(job)
            if action == 'cancel':
                archive.closed(value, ('action', 'job_id'))
                job = self.read(value['job_id'])
                archive.require(job['kind'] == 'restore' and job['confirmation_sha256'] is None
                                and job['state'] in ('created', 'uploading', 'uploaded', 'preview', 'failed'))
                descriptor = self.uploads.pop(value['job_id'], None)
                if descriptor is not None:
                    os.close(descriptor)
                self.fail(value['job_id'], 'cancelled')
                return self.public(self.read(value['job_id']))
            if action == 'restore.resume':
                archive.closed(value, ('action', 'job_id'))
                job = self.read(value['job_id'])
                archive.require(job['kind'] == 'restore' and job['confirmation_sha256'] is not None
                                and job['state'] in ('applying', 'awaiting_backend', 'completed', 'failed') and job['error'] != 'destination_changed')
                if job['state'] == 'completed':
                    return self.public(job)
                if self.thread is not None and self.thread.is_alive():
                    raise RecoveryBusy('Restore recovery is already active')
                archive.require(self.registry['active'] in (None, job['job_id']))
                self.registry['active'] = job['job_id']
                self.save_registry()
                job = self.save(dict(job, state='applying', error=None))
                self.spawn(self.recover_restore, job['job_id'])
                return self.public(job)
            if action == 'restore.confirm':
                archive.closed(value, ('action', 'job_id', 'preview_digest'))
                job = self.read(value['job_id'])
                archive.require(job['kind'] == 'restore' and job['state'] == 'preview' and self.registry['active'] == job['job_id']
                                and type(value['preview_digest']) is str and archive.HASH.fullmatch(value['preview_digest'])
                                and type(job['preview']) is dict and job['preview']['digest'] == value['preview_digest'])
                archive.require(self.thread is None or not self.thread.is_alive())
                observed = self.observe()
                archive.require(observed['boot'] == job['backend']['boot'])
                confirmation = {'schema': 'recovery-confirmation/1', 'job_id': job['job_id'], 'identity': job['identity'],
                                'backend_boot': observed['boot'], 'preview_digest': value['preview_digest'], 'archive_sha256': job['archive']['sha256']}
                publish_record(self.directory / job['job_id'] / 'confirmation-admission.json', confirmation, None)
                job = self.save(dict(job, state='applying', confirmation_sha256=hashlib.sha256(canonical(confirmation)).hexdigest()))
                self.spawn(self.apply, job['job_id'])
                return self.public(job)
            if action == 'restore.validate':
                archive.closed(value, ('action', 'job_id', 'purpose', 'passphrase'))
                job = self.read(value['job_id'])
                archive.require(job['kind'] == 'restore' and job['confirmation_sha256'] is None
                                and job['state'] in ('uploaded', 'preview', 'failed') and job['error'] != 'cancelled'
                                and job['archive'] is not None and job['archive'].get('stamp') is not None
                                and job['archive'].get('sha256') is not None and value['purpose'] in ('recover', 'move')
                                and type(value['passphrase']) is str)
                phrase = value['passphrase'].encode('utf-8')
                archive.require(0 < len(phrase) <= 1024)
                archive.require(self.thread is None or not self.thread.is_alive())
                archive.require(self.registry['active'] in (None, job['job_id']))
                if job['state'] in ('failed', 'preview'):
                    job = self.reset_validation(job)
                    self.registry['active'] = job['job_id']
                    self.save_registry()
                job = self.save(dict(job, state='validating', error=None, purpose=value['purpose']))
                self.spawn(self.validate, value['job_id'], phrase, value['purpose'])
                return self.public(job)
            raise archive.RecoveryError('Unsupported recovery action')
