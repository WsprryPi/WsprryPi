#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native CPython/DLL private-file test; launched with a C++-owned private root."""
import ctypes
import json
import os
from pathlib import Path
import sys
import hashlib
import threading
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from application_recovery import archive, durable, lifecycle, native_files


def refuses(action, exceptions=(OSError, ValueError, archive.RecoveryError)):
    try:
        action()
    except exceptions:
        return
    raise AssertionError('Unsafe native operation was accepted')


def existing_owner_lookup(root):
    from ctypes import wintypes
    from application_recovery.state_owner import stage_job
    home = native_files.make_directory(root / 'owner-lookup')
    application = native_files.make_directory(home / 'application')
    owner_root = home / '.wsprrypi-recovery'
    context = {'owner': {'home': str(home), 'root': str(owner_root), 'sid': native_files.path_info(home).st_uid},
               'identity': 'a'*32, 'role': 'controller', 'application_root': str(application), 'job_id': 'b'*32}
    expected = stage_job(context, allow_create=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                  wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.restype = wintypes.BOOL
    # A retained reader admits lookup but excludes a directory write/flush.
    held = kernel.CreateFileW('\\\\?\\' + str(owner_root), 0x20081, 1, None, 3, 0x02200000, None)
    assert held not in (None, ctypes.c_void_p(-1).value)
    try:
        refuses(lambda: native_files.sync_directory(owner_root))
        assert stage_job(context) == expected
    finally:
        assert kernel.CloseHandle(held)


def retained_evidence(root):
    from application_recovery import evidence
    base = native_files.make_directory(root / 'retained-evidence')
    source, records, candidate, copied = [native_files.make_directory(base / name)
                                          for name in ('source', 'records', 'candidate', 'copied')]
    native_files.make_directory(source / 'application')
    source_names = ['application/authority.json', 'application/state.sqlite3']
    for name in source_names:
        with archive.private_output(source / name, 2) as output:
            output.write(b'{}')  # Synthetic SQL bytes are never opened as a database.
    manifest = archive.manifest_for_snapshot(source,
        {'identity': 'a'*32, 'role': 'controller', 'application_version': 'native-test',
         'platform': 'windows', 'storage': 'application-state/1'}, source_names, 1800000000)
    files = []
    for kind in ('configuration', 'catalog', 'initialization'):
        name = 'state-' + kind + '.json'
        with archive.private_output(records / name, 2) as output:
            output.write(b'{}')
        files.append({'name': name, 'size': 2, 'sha256': hashlib.sha256(b'{}').hexdigest()})
    durable.publish_record(records / 'index.json', {'schema': 'recovery-source-records/1',
        'identity': 'a'*32, 'role': 'controller', 'files': files}, None)
    result = evidence.retain_source(candidate, source, records, manifest, 'b'*64)
    groups = evidence.inspect(candidate / 'evidence')
    assert len(groups) == 1 and groups[0][0].name == result['group_id']
    evidence.copy_groups(candidate / 'evidence', copied / 'evidence')
    assert evidence.inspect(copied / 'evidence')[0][1] == groups[0][1]
    for parent in (candidate, copied):
        for directory in (parent / 'evidence', parent / 'evidence' / result['group_id']):
            native_files.private_directory(directory)


def retry_cleanup(root):
    from application_recovery.jobs import RecoveryJobs
    from application_recovery.state_owner import OwnerBusy
    folder = native_files.make_directory(root / 'retry-cleanup')
    config = {'root': str(folder), 'application_root': str(folder / 'application'), 'backend_runtime': str(folder / 'Run'),
              'backend_command': [sys.executable], 'backend_owner': None, 'age': sys.executable,
              'identity': 'a'*32, 'role': 'controller',
              'lifecycle': {'kind': 'software_fixture', 'start_command': [sys.executable, '--backend', 'simulated']}}
    owner = RecoveryJobs(config)
    job = owner.new_job('b'*32, 'restore', total=8)
    path = folder / 'jobs' / job['job_id']
    with archive.private_output(path / 'upload.age', 8) as stream:
        stream.write(b'12345678')
    with archive.private_input(path / 'upload.age', 8) as (_, info):
        stamp = list(archive.observation(info))
    owner.save(dict(job, state='failed', error='validation_failed', backend={'pid': os.getpid(), 'boot': 'c'*32},
                    archive={'size': '8', 'sha256': hashlib.sha256(b'12345678').hexdigest(), 'stamp': stamp}))
    durable.publish_record(path / 'validation-result.json', {'schema': 'recovery-validation-result/1', 'job_id': job['job_id'],
                           'backend_boot': 'c'*32, 'state': 'failed', 'error': 'busy', 'preview_digest': None}, None)
    entered, release = threading.Event(), threading.Event()
    class SlowOwner:
        calls = 0
        def synchronize(self, job_id):
            saved = owner.read(job_id)
            assert saved['state'] == 'failed' and saved['error'] == 'interrupted' and saved['backend'] is None
            self.calls += 1
            if self.calls < 3:
                raise OwnerBusy('Definite directory-lock refusal')
            entered.set()
            assert release.wait(5)
    owner.state_owner = SlowOwner()
    # Archive crypto has its independent native fixture. This case stops after
    # real native staging cleanup; no backend or service command may execute.
    with mock.patch.object(archive, 'validate_archive', side_effect=archive.RecoveryError('Synthetic archive boundary')):
        try:
            result = owner.command({'action': 'restore.validate', 'job_id': job['job_id'], 'purpose': 'recover', 'passphrase': 'fixture'})
            assert result['state'] == 'validating' and entered.wait(3)
            assert owner.command({'action': 'status', 'job_id': job['job_id']})['state'] == 'validating'
            assert owner.command({'action': 'list.summary'})['jobs'][0]['state'] == 'validating'
            refuses(lambda: owner.command({'action': 'cancel', 'job_id': job['job_id']}))
            assert (path / 'validation-result.json').exists()
        finally:
            release.set()
            owner.thread.join(10)
    assert not owner.thread.is_alive() and owner.validation_reset is None and owner.registry['active'] is None
    assert owner.state_owner.calls == 3
    assert owner.read(job['job_id'])['error'] == 'archive_invalid' and not (path / 'validation-result.json').exists()


def main():
    assert sys.platform == 'win32' and len(sys.argv) == 3
    assert sys.version_info[:2] == (3, 15), 'Select the CPython 3.15 profile explicitly'
    assert ctypes.sizeof(native_files.NativeInfo) == 256
    native_files.configure(Path(sys.argv[1]))
    from application_recovery.jobs import process_alive
    assert process_alive(os.getpid()) is True
    for invalid in (None, True, 0, -1, 2**32):
        assert process_alive(invalid) is True
    from application_recovery.windows_service import ControllerService
    refuses(ControllerService)
    from application_recovery.windows_backend import backend_command
    from application_recovery.jobs import BackendUnavailable
    refuses(lambda: backend_command({}, 'observe'), (BackendUnavailable,))
    root = native_files.private_directory(Path(sys.argv[2]))
    existing_owner_lookup(root)
    retained_evidence(root)
    retry_cleanup(root)
    from application_recovery.windows_state_owner import greeting, validate_context
    from application_recovery.state_owner import SCHEMA
    actual_owner = {'sid': native_files.path_info(root).st_uid, 'home': str(root), 'root': str(root / '.wsprrypi-recovery')}
    principal = {'schema': SCHEMA, 'service_pid': os.getpid(), 'sid': actual_owner['sid'], 'mode': 'controller', 'helper_nonce': None, 'identity': 'a'*32}
    assert greeting(principal, os.getpid(), actual_owner) == principal
    for changed in (dict(principal, service_pid=os.getpid()+1), dict(principal, sid='S-1-5-18'),
                    dict(principal, mode='unknown'), dict(principal, helper_nonce='a'*32),
                    dict(principal, mode='recovery-owner', helper_nonce=None), dict(principal, extra=True)):
        refuses(lambda changed=changed: greeting(changed, os.getpid(), actual_owner))
    bootstrap = {'owner': actual_owner, 'application_root': str(root / 'application-state'), 'identity': 'a'*32,
                 'role': 'controller', 'backend_command': [str(root / 'wsprrypi.exe')], 'backend_runtime': str(root / 'Run'),
                 'mode': 'controller'}
    context = dict({key: bootstrap[key] for key in ('owner', 'application_root', 'identity', 'role', 'backend_command', 'backend_runtime')},
                   schema=SCHEMA, nonce='b'*32, job_id=None, operation='application-record', arguments={'name': 'authority.json'}, inputs=[])
    identity_directory = native_files.make_directory(root / '.wsprrypi-controller-identity')
    durable.publish_record(identity_directory / 'authority.json', {'schema': 'controller-identity/1', 'device_id': bootstrap['identity']}, None)
    assert validate_context(context, bootstrap) == context
    for changed in (dict(context, identity='c'*32), dict(context, owner=dict(actual_owner, sid='S-1-5-18')),
                    dict(context, application_root=str(root)), dict(context, backend_command=['C:/other.exe']),
                    dict(context, operation='publish', job_id='d'*32), dict(context, extra=True)):
        refuses(lambda changed=changed: validate_context(changed, bootstrap))
    installation = root / '.wsprrypi-controller-instance'
    refuses(lambda: native_files.installation_lock(installation).__enter__())
    assert not installation.exists()
    native_files.make_directory(installation)
    with native_files.private_output(installation / 'instance.lock', 0):
        pass
    with native_files.installation_lock(installation):
        refuses(lambda: native_files.installation_lock(installation).__enter__(), (native_files.NativeLockBusy,))
    with native_files.installation_lock(installation):
        pass
    folder = native_files.make_directory(root / 'binding-\u03a9')
    with native_files.generation_lock(folder):
        refuses(lambda: native_files.generation_lock(folder).__enter__(), (native_files.NativeLockBusy,))
        assert lifecycle.root_available(folder) is False
    with native_files.generation_lock(folder):
        pass
    assert lifecycle.root_available(folder) is True
    candidate = native_files.make_directory(root / 'candidate')
    published = folder / 'published'
    with durable.directory_lock(folder, generation=True) as generation:
        refuses(lambda: native_files.database_lock(candidate, generation).__enter__())
        native_files.initialize_database_snapshot(candidate)
        with native_files.database_lock(candidate, generation) as database:
            refuses(lambda: native_files.database_lock(candidate, generation).__enter__(), (native_files.NativeLockBusy,))
            database.verify(candidate)
            refuses(lambda: database.verify(folder))
            refuses(lambda: database.move(candidate, root / 'wrong'))
            native_files.make_directory(published)
            refuses(lambda: database.move(candidate, published))
            published.rmdir()
            database.move(candidate, published)
            database.verify(published)
            refuses(lambda: database.verify(candidate))
        refuses(lambda: database.verify(published))
    refuses(lambda: native_files.database_lock(published, generation).__enter__())
    removable = native_files.make_directory(folder / 'removable')
    with durable.directory_lock(removable):
        refuses(lambda: durable.directory_lock(removable).__enter__(), (durable.RecoveryLockBusy,))
        removable.rmdir()
    native_files.make_directory(removable)
    with durable.directory_lock(removable):
        pass
    witness = removable / 'state.sqlite3.owner.lock'
    with native_files.private_output(witness, 0):
        pass
    refuses(lambda: durable.directory_lock(removable).__enter__())
    witness.unlink()
    upload = archive.CiphertextUpload(folder / 'upload.age', 8)
    try:
        assert upload.append(0, b'\0\r\nx') == 4
        assert upload.append(0, b'\0\r\nx') == 4
        refuses(lambda: upload.append(0, b'changed'))
        refuses(lambda: upload.append(5, b'x'))
        refuses(upload.complete)
        assert upload.append(4, b'tail') == 8
        refuses(lambda: upload.append(8, b'x'))
        upload.complete()
    finally:
        upload.close()
    upload.close()
    refuses(lambda: upload.append(0, b'x'))
    with archive.private_input(folder / 'upload.age', 8) as (stream, _):
        assert stream.read() == b'\0\r\nxtail'
        refuses(lambda: native_files.PruneLease(folder / 'upload.age'), (native_files.NativeLockBusy,))
    retained = folder / 'retained.age'
    with native_files.private_output(retained, 4) as (stream, _):
        stream.write(b'age!')
    with native_files.PruneLease(retained) as lease:
        lease.check(retained)
        refuses(lambda: lease.check(folder / 'upload.age'))
        refuses(lambda: native_files.NativeFile(retained, 1, 4))
        refuses(lambda: retained.rename(folder / 'renamed.age'))
        lease.remove()
        refuses(lambda: lease.check(retained))
    assert not retained.exists()
    from application_recovery.jobs import RecoveryBusy, RecoveryJobs
    retention_root = native_files.make_directory(root / 'retention')
    retention = RecoveryJobs({'root': str(retention_root), 'application_root': str(root / 'unused-application'),
                             'backend_command': [sys.executable], 'backend_runtime': str(root / 'unused-runtime'),
                             'backend_owner': None, 'age': str(root / 'unused-age.exe'), 'identity': 'a'*32, 'role': 'controller',
                             'lifecycle': {'kind': 'software_fixture', 'start_command': [sys.executable, '--backend', 'simulated']}})
    jobs = []
    for index in range(4):
        job = retention.command({'action': 'restore.upload.start', 'request_id': f'{index+1:032x}', 'size': '8'})
        retention.command({'action': 'cancel', 'job_id': job['job_id']})
        jobs.append(job)
    next_upload = {'action': 'restore.upload.start', 'request_id': 'e'*32, 'size': '8'}
    oldest = retention_root / 'jobs' / jobs[0]['job_id']
    with archive.private_input(oldest / 'upload.age', 8):
        refuses(lambda: retention.command(next_upload), (RecoveryBusy,))
        retention.thread.join(10)
        assert not retention.thread.is_alive()
        assert len(retention.registry['jobs']) == 4 and oldest.exists()
    refuses(lambda: retention.command(next_upload), (RecoveryBusy,))
    retention.thread.join(10)
    assert not retention.thread.is_alive()
    admitted = retention.command(next_upload)
    assert not oldest.exists() and len(retention.registry['jobs']) == 4
    retention.command({'action': 'cancel', 'job_id': admitted['job_id']})
    from application_recovery.state_owner import completed_outputs
    relay = native_files.make_directory(root / 'relay-candidate')
    candidate = native_files.make_directory(relay / 'candidate')
    native_files.initialize_database_snapshot(candidate)
    with native_files.private_output(candidate / 'state.sqlite3', 9) as (stream, _):
        stream.write(b'synthetic')
    durable.publish_record(relay / 'candidate-built.json', {'synthetic': 'relay inventory only'}, None)
    exported = completed_outputs(relay)
    assert {item['name'] for item in exported} == {'candidate-built.json', 'candidate/state.sqlite3'}
    marker = candidate / 'state.sqlite3.owner.lock'
    with native_files.NativeFile(marker, 3, 1) as writable:
        os.write(writable.fd, b'x')
        writable.sync()
    refuses(lambda: completed_outputs(relay))
    from application_recovery.state_owner import StateOwner
    import hashlib
    import io
    incoming = native_files.make_directory(root / 'incoming-relay')
    contents = {'snapshot/application/authority.json': b'{"synthetic":true}',
                'snapshot/application/state.sqlite3': b'synthetic inactive SQL bytes'}
    files = [{'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
             for name, data in sorted(contents.items())]
    receiver = StateOwner.__new__(StateOwner)
    receiver.receive_outputs(io.BytesIO(b''.join(contents[item['name']] for item in files)), incoming, files)
    for name, data in contents.items():
        with archive.private_input(incoming / name, len(data)) as (stream, _):
            assert stream.read() == data
    assert not list(incoming.glob('.owner-reply-*'))
    refuses(lambda: native_files.configure(Path(sys.argv[1])), (RuntimeError,))
    binary = folder / 'binary'
    with native_files.private_output(binary, 4) as (stream, owner):
        stream.write(b'\0\r\nx'); stream.flush()
        assert owner.info().st_size == 4
    metadata = native_files.path_info(binary)
    assert metadata.st_size == 4 and metadata.st_uid == native_files.path_info(root).st_uid
    assert type(metadata.st_uid) is str and metadata.st_uid.startswith('S-1-')
    assert metadata.st_mode & 0o777 == 0  # Native ACLs are not fictitious POSIX bits.
    with archive.private_input(binary, 4) as (stream, before):
        assert stream.read() == b'\0\r\nx'
        after = archive.stat_descriptor(stream.fileno())
        assert archive.observation(before) == archive.observation(after)
        assert archive.observation(before)[1] == metadata.native_file_id
    refuses(lambda: native_files.NativeFile(binary, 1, 3))
    alias = folder / 'alias'; os.link(binary, alias)
    refuses(lambda: native_files.NativeFile(binary, 1, 4)); alias.unlink()
    record = folder / 'record.json'
    assert durable.read_record(record) == (None, None)
    first = durable.publish_record(record, {'value': 1}, None)
    assert type(first[0]) is bytes and type(first[1]) is str and len(first[1]) == 64
    second = durable.publish_record(record, {'value': 2}, first)
    refuses(lambda: durable.publish_record(record, {'value': 3}, first))
    assert durable.read_record(record) == ({'value': 2}, second)
    native_files.publish_record(record, b'{"same":1,"same":2}', second)
    refuses(lambda: durable.read_record(record))
    native_files.sync_directory(folder)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from native_windows_publication_test import run
    run(root)
    print(json.dumps({'result': 'pass', 'python': sys.version.split()[0],
                      'scope': 'native DLL/CPython ACL admission, full file identity, binary streams, JSON closure, and record CAS; no service or recovery qualification'}))


if __name__ == '__main__':
    main()
