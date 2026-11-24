# SPDX-License-Identifier: MIT
"""Authenticated private byte stream for the existing owner protocol."""
import ctypes
import math
import time
from . import archive, native_files


class OwnerPipe:
    def __init__(self, token):
        archive.require(type(token) is int and token > 0)
        self.token = token
        self.deadline = time.monotonic() + 5

    @classmethod
    def connect(cls, service_pid):
        archive.require(type(service_pid) is int and 0 < service_pid < 2**32)
        token = ctypes.c_uint64()
        native_files._check(native_files.library().wrn_owner_connect(service_pid, ctypes.byref(token)))
        return cls(token.value)

    @classmethod
    def inherited(cls, handle, service_pid):
        archive.require(type(handle) is int and 0 < handle < 2**64-1 and type(service_pid) is int and 0 < service_pid < 2**32)
        token = ctypes.c_uint64()
        native_files._check(native_files.library().wrn_owner_attach(handle, service_pid, ctypes.byref(token)))
        return cls(token.value)

    def settimeout(self, seconds):
        archive.require(type(seconds) in (float, int) and math.isfinite(seconds) and 0 < seconds <= 2400)
        self.deadline = time.monotonic() + seconds

    def transfer(self, buffer, size, sending):
        archive.require(0 < size <= 65536)
        remaining = self.deadline - time.monotonic()
        archive.require(remaining > 0)
        native_files._check(native_files.library().wrn_owner_transfer(self.token, buffer, size, int(sending), max(1, math.ceil(remaining * 1000))))

    def sendall(self, data):
        archive.require(type(data) in (bytes, bytearray) and len(data) <= archive.MAX_MANIFEST)
        for offset in range(0, len(data), 65536):
            part = bytes(data[offset:offset + 65536])
            buffer = ctypes.create_string_buffer(part, len(part))
            self.transfer(buffer, len(part), True)

    def recv(self, size):
        archive.require(type(size) is int and 0 < size <= 65536)
        buffer = ctypes.create_string_buffer(size)
        self.transfer(buffer, size, False)
        return buffer.raw

    def lost(self):
        value = ctypes.c_uint32()
        native_files._check(native_files.library().wrn_owner_lost(self.token, ctypes.byref(value)))
        archive.require(value.value in (0, 1))
        return bool(value.value)

    def accept(self):
        native_files._check(native_files.library().wrn_owner_accept(self.token))

    def disconnect(self):
        native_files._check(native_files.library().wrn_owner_disconnect(self.token))

    def close(self):
        if self.token:
            token, self.token = self.token, 0
            native_files._check(native_files.library().wrn_owner_close(token))

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
