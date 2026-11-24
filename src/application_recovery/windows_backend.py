# SPDX-License-Identifier: MIT
"""Bounded fixed backend commands with native owned-child cleanup."""
import ctypes
import time
from . import archive, native_files
from .durable import canonical


def backend_command(config, action, job_id=None, payload=None, *, cancelled=None):
    from .jobs import BackendUnavailable, BackendRefused, RecoveryBusy
    # This adapter always uses the fixed installed image and runtime. A typed
    # software fixture is not silently converted into installed-service work.
    archive.require(config.get('lifecycle', {}).get('kind') != 'software_fixture')
    archive.require(type(action) is str and action in ('observe', 'capture', 'validate', 'prepare', 'review', 'build-candidate', 'seal-candidate'))
    archive.require(job_id is None or type(job_id) is str and archive.ID.fullmatch(job_id))
    action_bytes = action.encode('ascii')
    job = job_id.encode('ascii') if job_id is not None else None
    body = canonical(payload) if payload is not None else None
    archive.require(body is None or len(body) <= 8192)
    token = ctypes.c_uint64()
    api = native_files.library()
    output = bytearray()
    try:
        native_files._check(api.wrn_backend_start(action_bytes, len(action_bytes), job, len(job) if job else 0,
                                                body, len(body) if body else 0, ctypes.byref(token)))
        buffer = ctypes.create_string_buffer(8192)
        while True:
            if cancelled is not None and cancelled():
                raise BackendUnavailable('Backend owner connection lost')
            size, done, code = ctypes.c_uint32(), ctypes.c_uint32(), ctypes.c_uint32()
            native_files._check(api.wrn_backend_poll(token.value, buffer, len(buffer), ctypes.byref(size), ctypes.byref(done), ctypes.byref(code)))
            archive.require(size.value <= 8192 and done.value in (0, 1))
            output.extend(buffer.raw[:size.value])
            archive.require(len(output) <= 32768)
            if done.value:
                break
            if not size.value:
                time.sleep(.05)
        if code.value:
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
    except (BackendUnavailable, BackendRefused, RecoveryBusy):
        raise
    except (OSError, ValueError, archive.RecoveryError) as error:
        # A failed fixed command or missing/invalid reply says nothing about
        # the archive. Callers must inspect the existing bound owner receipt,
        # without sending the command again or treating an unknown as success.
        raise BackendUnavailable('Backend response unavailable') from error
    finally:
        if token.value:
            try:
                native_files._check(api.wrn_backend_close(token.value))
            except (OSError, ValueError) as error:
                raise BackendUnavailable('Backend cleanup outcome unavailable') from error
