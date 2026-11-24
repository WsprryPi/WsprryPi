# SPDX-License-Identifier: MIT
"""Fixed SCM boundary for the installed unprivileged controller service.

No command, executable, service, account, or startup policy comes from a request.
The native adapter checks the maintenance SID and exact installed definition.
"""
import ctypes
from . import archive, native_files


class ControllerService:
    STOPPED = 1
    RUNNING = 4

    def __init__(self):
        self.token = 0
        value = ctypes.c_uint64()
        native_files._check(native_files.library().wrn_service_open(ctypes.byref(value)))
        self.token = value.value
        self.helper_nonce = None
        self.helper_pid = self.helper_sid = self.helper_identity = None

    def close(self):
        if self.token:
            token, self.token = self.token, 0
            native_files._check(native_files.library().wrn_service_close(token))

    def query(self):
        state, pid = ctypes.c_uint32(), ctypes.c_uint32()
        native_files._check(native_files.library().wrn_service_query(self.token, ctypes.byref(state), ctypes.byref(pid)))
        archive.require(state.value in (1, 2, 3, 4) and (state.value != self.RUNNING or pid.value > 0))
        return state.value, pid.value

    def start(self, nonce=None):
        archive.require(nonce is None or type(nonce) is str and archive.ID.fullmatch(nonce))
        encoded = nonce.encode('ascii') if nonce is not None else None
        pid = ctypes.c_uint32()
        native_files._check(native_files.library().wrn_service_start(self.token, 2 if encoded is not None else 1,
                            encoded, len(encoded) if encoded is not None else 0, ctypes.byref(pid)))
        archive.require(pid.value > 0)
        self.helper_nonce = nonce
        self.helper_pid = pid.value if nonce is not None else None
        self.helper_sid = self.helper_identity = None
        return pid.value

    def bind_owner(self, principal):
        self.helper_nonce = principal['helper_nonce']
        self.helper_pid = principal['service_pid'] if self.helper_nonce is not None else None
        self.helper_sid = principal['sid'] if self.helper_nonce is not None else None
        self.helper_identity = principal['identity'] if self.helper_nonce is not None else None

    def verify_helper(self, pid):
        from .state_owner import SCHEMA, Wire
        from .windows_owner_pipe import OwnerPipe
        archive.require(self.helper_nonce is not None and self.helper_pid == pid
                        and self.helper_sid is not None and self.helper_identity is not None)
        # Reauthenticate the current mode/nonce, even when an old PID is reused.
        # A cached greeting cannot authorize stopping a replacement controller.
        with OwnerPipe.connect(pid) as channel:
            principal = Wire(channel, 5).receive()
            archive.closed(principal, ('schema', 'service_pid', 'sid', 'mode', 'helper_nonce', 'identity'))
            archive.require(principal == {'schema': SCHEMA, 'service_pid': pid, 'sid': self.helper_sid,
                                         'mode': 'recovery-owner', 'helper_nonce': self.helper_nonce,
                                         'identity': self.helper_identity})

    def stop(self, observed_pid):
        archive.require(type(observed_pid) is int and 0 < observed_pid < 2**32)
        native_files._check(native_files.library().wrn_service_stop(self.token, observed_pid))
        self.helper_nonce = None
        self.helper_pid = self.helper_sid = self.helper_identity = None
