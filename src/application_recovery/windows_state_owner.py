# SPDX-License-Identifier: MIT
"""Windows owner admission for the shared bounded recovery exchange."""
from pathlib import Path
import secrets
from . import archive, native_files
from .windows_service import ControllerService
from .windows_owner_pipe import OwnerPipe


def greeting(value, pid, owner):
    from .state_owner import SCHEMA
    archive.closed(value, ('schema', 'service_pid', 'sid', 'mode', 'helper_nonce', 'identity'))
    archive.require(value['schema'] == SCHEMA and value['service_pid'] == pid and value['sid'] == owner['sid'])
    archive.require(type(value['identity']) is str and archive.ID.fullmatch(value['identity']))
    archive.require(value['mode'] in ('controller', 'recovery-owner'))
    archive.require(value['helper_nonce'] is None if value['mode'] == 'controller'
                    else type(value['helper_nonce']) is str and archive.ID.fullmatch(value['helper_nonce']))
    return value


class WindowsStateOwner:
    def __init__(self, config, parent):
        self.config, self.parent = config, parent
        owner = config['backend_owner']
        archive.closed(owner, ('sid', 'home', 'root', 'worker'))
        archive.require(config['role'] == 'controller' and type(owner['sid']) is str and owner['sid'].startswith('S-1-5-80-'))
        for key in ('home', 'root', 'worker'):
            native_files._path(owner[key])
            archive.require(Path(owner[key]).is_absolute())
        archive.require(Path(owner['root']) == Path(owner['home']) / '.wsprrypi-recovery')
        self.service = ControllerService()

    def call(self, operation, job_id, arguments, inputs):
        from .state_owner import SCHEMA, Wire, OPERATIONS, operation_timeout, validate_manifest, send_files, OwnerBusy, OwnerUnavailable
        archive.require(operation in OPERATIONS)
        arguments, inputs = arguments or {}, inputs or []
        validate_manifest(inputs)
        state, pid = self.service.query()
        if state == self.service.STOPPED:
            pid = self.service.start(secrets.token_hex(16))
        else:
            archive.require(state == self.service.RUNNING)
        nonce = secrets.token_hex(16)
        owner = {key: self.config['backend_owner'][key] for key in ('sid', 'home', 'root')}
        context = {'schema': SCHEMA, 'nonce': nonce, 'owner': owner, 'application_root': self.config['application_root'],
                   'identity': self.config['identity'], 'role': self.config['role'], 'job_id': job_id, 'operation': operation,
                   'arguments': arguments, 'inputs': inputs, 'backend_command': self.config['backend_command'],
                   'backend_runtime': self.config['backend_runtime']}
        replied = False
        try:
            with OwnerPipe.connect(pid) as channel:
                wire = Wire(channel, operation_timeout(operation))
                principal = greeting(wire.receive(), pid, owner)
                archive.require(principal['identity'] == self.config['identity'])
                self.service.bind_owner(principal)
                wire.frame(context)
                ready = wire.receive()
                archive.closed(ready, ('schema', 'nonce', 'ready', 'sid', 'service_pid', 'mode', 'helper_nonce', 'identity'))
                expected = dict(principal, nonce=nonce, ready=True)
                archive.require(ready == expected)
                if operation == 'import':
                    send_files(wire, self.parent.job(job_id), inputs)
                    wire.frame({'nonce': nonce, 'input_complete': True})
                response = wire.receive()
                archive.require(type(response) is dict)
                if response.get('ok') is False:
                    archive.closed(response, ('schema', 'nonce', 'ok', 'error'))
                    archive.require(response['schema'] == SCHEMA and response['nonce'] == nonce and response['error'] in ('busy', 'unavailable'))
                    replied = True
                    if response['error'] == 'busy':
                        raise OwnerBusy('Native recovery owner is busy')
                    raise OwnerUnavailable('Native recovery owner is unavailable')
                archive.closed(response, ('schema', 'nonce', 'ok', 'value', 'files'))
                archive.require(response['schema'] == SCHEMA and response['nonce'] == nonce and response['ok'] is True)
                if response['files']:
                    self.parent.receive_outputs(wire, self.parent.job(job_id), response['files'])
                else:
                    archive.require(response['files'] == [])
                replied = True
                return response['value']
        finally:
            if not replied and self.parent.process_failure is not None:
                self.parent.process_failure()


def validate_context(value, bootstrap):
    from .state_owner import SCHEMA, OPERATIONS, validate_manifest
    archive.closed(value, ('schema', 'nonce', 'owner', 'application_root', 'identity', 'role', 'job_id',
                           'operation', 'arguments', 'inputs', 'backend_command', 'backend_runtime'))
    archive.require(value['schema'] == SCHEMA and type(value['nonce']) is str and archive.ID.fullmatch(value['nonce']))
    archive.closed(value['owner'], ('sid', 'home', 'root'))
    archive.require(value['owner'] == bootstrap['owner'])
    from .durable import read_record
    established, _ = read_record(Path(bootstrap['owner']['home']) / '.wsprrypi-controller-identity' / 'authority.json')
    archive.require(established == {'schema': 'controller-identity/1', 'device_id': bootstrap['identity']})
    for field in ('application_root', 'identity', 'role', 'backend_command', 'backend_runtime'):
        archive.require(value[field] == bootstrap[field])
    archive.require(type(value['operation']) is str and value['operation'] in OPERATIONS and type(value['arguments']) is dict)
    if value['operation'] in ('root-available', 'application-record'):
        archive.require(value['job_id'] is None)
    else:
        archive.require(type(value['job_id']) is str and archive.ID.fullmatch(value['job_id']))
    validate_manifest(value['inputs'])
    archive.require(value['operation'] == 'import' or not value['inputs'])
    if value['operation'] not in ('import', 'export', 'prune-job', 'application-record', 'root-available'):
        archive.require(bootstrap['mode'] == 'recovery-owner')
    return value


def serve(bootstrap, handle):
    from .state_owner import SCHEMA, Wire, serve_exchange
    archive.closed(bootstrap, ('native_library', 'owner', 'application_root', 'identity', 'role', 'backend_command',
                               'backend_runtime', 'service_pid', 'mode', 'helper_nonce'))
    native_files.configure(bootstrap['native_library'])
    owner = bootstrap['owner']
    archive.closed(owner, ('sid', 'home', 'root'))
    archive.require(native_files.path_info(owner['home']).st_uid == owner['sid'])
    archive.require(type(bootstrap['identity']) is str and archive.ID.fullmatch(bootstrap['identity']) and bootstrap['role'] == 'controller')
    principal = greeting({'schema': SCHEMA, 'service_pid': bootstrap['service_pid'], 'sid': owner['sid'],
                          'mode': bootstrap['mode'], 'helper_nonce': bootstrap['helper_nonce'], 'identity': bootstrap['identity']}, bootstrap['service_pid'], owner)
    with OwnerPipe.inherited(handle, bootstrap['service_pid']) as channel:
        import sys
        import msvcrt
        import os
        msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)
        sys.stdout.buffer.write(b'ready\n')
        sys.stdout.buffer.flush()
        while True:
            try:
                channel.accept()
                wire = Wire(channel)
                wire.frame(principal)
                serve_exchange(wire, channel, lambda value: validate_context(value, bootstrap),
                               lambda: {key: value for key, value in principal.items() if key != 'schema'})
            except (OSError, ValueError, archive.RecoveryError):
                pass
            finally:
                channel.disconnect()
