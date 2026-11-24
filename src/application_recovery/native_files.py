# SPDX-License-Identifier: MIT
"""Private Windows file primitives backed by the application's native policy.

The trusted broker selects and pins the matching first-party DLL before launch.
No library path comes from the environment, archive, browser, or working directory.
This module is not an alternate archive, journal, or generation engine.
"""
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path, PureWindowsPath
import stat
import sys
import threading
from types import SimpleNamespace


class NativeInfo(ctypes.Structure):
    _fields_ = [('volume', ctypes.c_uint64), ('file_id', ctypes.c_ubyte * 16),
                ('size', ctypes.c_uint64), ('links', ctypes.c_uint32), ('attributes', ctypes.c_uint32),
                ('modified_ticks', ctypes.c_int64), ('changed_ticks', ctypes.c_int64),
                ('directory', ctypes.c_uint32), ('owner_sid', ctypes.c_char * 192)]


_library = None
_kernel = None
_selection = threading.Lock()


class NativeLockBusy(OSError):
    pass


def _check(status):
    if status == 0:
        return
    if status == 2:
        raise FileNotFoundError('Native private recovery input is absent')
    if status == 4:
        raise FileExistsError('Native private recovery destination exists')
    if status == 3:
        raise ValueError('Native private recovery argument or token is invalid')
    if status == 5:
        raise NativeLockBusy('Native private state is busy')
    raise OSError('Native private recovery storage is unsafe or unavailable')


def _path(value):
    text = os.fspath(value)
    if type(text) is not str or not text or '\0' in text:
        raise ValueError('Native recovery paths must be explicit text')
    encoded = text.encode('utf-8', errors='strict')
    if len(encoded) > 32760:
        raise ValueError('Native recovery path exceeds its bound')
    return encoded


def library():
    if _library is None:
        raise OSError('Native private recovery library is not selected')
    return _library


def configure(path):
    global _library, _kernel
    with _selection:
        if _library is not None:
            raise RuntimeError('Native recovery library is already selected')
        if sys.platform != 'win32' or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise OSError('Native recovery requires its selected Windows runtime')
        encoded = _path(path)
        value = encoded.decode('utf-8'); native = PureWindowsPath(value)
        parts = value.replace('\\', '/').split('/')
        def reserved(part):
            stem = part.split('.', 1)[0].rstrip(' ').upper()
            return (stem in ('CON', 'PRN', 'AUX', 'NUL', 'CLOCK$', 'CONIN$', 'CONOUT$')
                    or len(stem) == 4 and stem[:3] in ('COM', 'LPT') and stem[3] in '0123456789\u00b9\u00b2\u00b3')
        if (not native.is_absolute() or len(native.drive) != 2 or native.drive[1] != ':' or not native.drive[0].isascii()
                or not native.drive[0].isalpha() or not native.root or len(parts) < 2
                or any(not part or part in ('.', '..') or part.endswith(('.', ' ')) or ':' in part or reserved(part)
                       or any(ord(character) < 32 or ord(character) == 127 or character in '<>"|?*' for character in part)
                       for part in parts[1:])
                or native.name.lower() != 'wsprrypi-recovery-native.dll'):
            raise ValueError('Native recovery library must be its explicit local program file')
        # The native broker validates/pins this exact code file and its parents.
        # Search dependencies only in its directory and the OS System32 folder.
        loaded = ctypes.WinDLL(encoded.decode('utf-8'), winmode=0x100 | 0x800)
        loaded.wrn_version.argtypes, loaded.wrn_version.restype = (), ctypes.c_uint32
        loaded.wrn_info_size.argtypes, loaded.wrn_info_size.restype = (), ctypes.c_uint32
        if loaded.wrn_version() != 1 or loaded.wrn_info_size() != ctypes.sizeof(NativeInfo):
            raise OSError('Native recovery library ABI is incompatible')
        pointer = ctypes.POINTER
        path_args = (ctypes.c_char_p, ctypes.c_size_t)
        signatures = {
            'wrn_check_parent': path_args,
            'wrn_path_info': (*path_args, pointer(NativeInfo), ctypes.c_size_t),
            'wrn_open': (*path_args, ctypes.c_uint32, ctypes.c_uint64, pointer(ctypes.c_uint64)),
            'wrn_duplicate': (ctypes.c_uint64, pointer(ctypes.c_uint64)),
            'wrn_file_info': (ctypes.c_uint64, pointer(NativeInfo), ctypes.c_size_t),
            'wrn_handle_info': (ctypes.c_uint64, pointer(NativeInfo), ctypes.c_size_t),
            'wrn_flush': (ctypes.c_uint64,), 'wrn_close': (ctypes.c_uint64,),
            'wrn_prune_open': (*path_args, pointer(ctypes.c_uint64)),
            'wrn_prune_check': (ctypes.c_uint64, *path_args),
            'wrn_prune_remove': (ctypes.c_uint64,), 'wrn_prune_close': (ctypes.c_uint64,),
            'wrn_directory': (*path_args, ctypes.c_uint32), 'wrn_directory_flush': path_args,
            'wrn_record_read': (*path_args, ctypes.c_void_p, ctypes.c_size_t, pointer(ctypes.c_uint64),
                                ctypes.c_void_p, ctypes.c_size_t, pointer(ctypes.c_uint32)),
            'wrn_crypto_start': (*path_args, *path_args, ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64, pointer(ctypes.c_uint64)),
            'wrn_crypto_wait': (ctypes.c_uint64, ctypes.c_uint32, pointer(ctypes.c_uint32)),
            'wrn_crypto_close': (ctypes.c_uint64,),
            'wrn_pipe': (pointer(ctypes.c_uint64), pointer(ctypes.c_uint64)),
            'wrn_generation_acquire': (*path_args, pointer(ctypes.c_uint64)),
            'wrn_generation_close': (ctypes.c_uint64,),
            'wrn_database_acquire': (ctypes.c_uint64, *path_args, pointer(ctypes.c_uint64)),
            'wrn_database_verify': (ctypes.c_uint64, *path_args),
            'wrn_database_move': (ctypes.c_uint64, *path_args, *path_args),
            'wrn_database_close': (ctypes.c_uint64,),
            'wrn_database_initialize': path_args,
            'wrn_directory_acquire': (*path_args, pointer(ctypes.c_uint64)),
            'wrn_directory_close': (ctypes.c_uint64,),
            'wrn_owner_attach': (ctypes.c_uint64, ctypes.c_uint32, pointer(ctypes.c_uint64)),
            'wrn_owner_connect': (ctypes.c_uint32, pointer(ctypes.c_uint64)),
            'wrn_owner_accept': (ctypes.c_uint64,), 'wrn_owner_disconnect': (ctypes.c_uint64,),
            'wrn_owner_transfer': (ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32),
            'wrn_owner_lost': (ctypes.c_uint64, pointer(ctypes.c_uint32)), 'wrn_owner_close': (ctypes.c_uint64,),
            'wrn_backend_start': (*path_args, *path_args, *path_args, pointer(ctypes.c_uint64)),
            'wrn_backend_poll': (ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t, pointer(ctypes.c_uint32), pointer(ctypes.c_uint32), pointer(ctypes.c_uint32)),
            'wrn_backend_close': (ctypes.c_uint64,),
            'wrn_service_open': (pointer(ctypes.c_uint64),),
            'wrn_service_query': (ctypes.c_uint64, pointer(ctypes.c_uint32), pointer(ctypes.c_uint32)),
            'wrn_service_start': (ctypes.c_uint64, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t, pointer(ctypes.c_uint32)),
            'wrn_service_stop': (ctypes.c_uint64, ctypes.c_uint32),
            'wrn_service_close': (ctypes.c_uint64,),
            'wrn_installation_acquire': (*path_args, pointer(ctypes.c_uint64)),
            'wrn_installation_close': (ctypes.c_uint64,),
            'wrn_record_publish': (*path_args, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t),
        }
        for name, arguments in signatures.items():
            function = getattr(loaded, name)
            function.argtypes, function.restype = arguments, ctypes.c_uint32
        kernel = ctypes.WinDLL('kernel32.dll', winmode=0x800, use_last_error=True)
        kernel.CloseHandle.argtypes, kernel.CloseHandle.restype = (ctypes.c_void_p,), ctypes.c_int
        kernel.OpenProcess.argtypes, kernel.OpenProcess.restype = (ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32), ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes, kernel.WaitForSingleObject.restype = (ctypes.c_void_p, ctypes.c_uint32), ctypes.c_uint32
        _kernel, _library = kernel, loaded


def process_alive(pid):
    # Windows os.kill(pid, 0) is not a POSIX existence probe. Never request
    # termination or mutation rights for this conservative retention check.
    library()
    if type(pid) is not int or not 0 < pid < 2**32:
        return True
    handle = _kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE only.
    if not handle:
        return ctypes.get_last_error() != 87  # ERROR_INVALID_PARAMETER: no PID.
    try:
        return _kernel.WaitForSingleObject(handle, 0) != 0
    finally:
        if not _kernel.CloseHandle(handle):
            raise OSError('Native process observation could not close')


def _metadata(value):
    if value.directory not in (0, 1):
        raise OSError('Native recovery metadata is invalid')
    owner = value.owner_sid.decode('ascii', errors='strict')
    if not owner.startswith('S-1-'):
        raise OSError('Native recovery owner metadata is invalid')
    identity = bytes(value.file_id)
    return SimpleNamespace(st_dev=int(value.volume), st_ino=int.from_bytes(identity, 'little'),
                           st_size=int(value.size), st_nlink=int(value.links),
                           st_mode=stat.S_IFDIR if value.directory else stat.S_IFREG,
                           st_mtime_ns=(int(value.modified_ticks) - 116444736000000000) * 100,
                           st_ctime_ns=(int(value.changed_ticks) - 116444736000000000) * 100,
                           st_uid=owner, native_file_id=identity.hex())


def path_info(path):
    encoded = _path(path); value = NativeInfo()
    _check(library().wrn_path_info(encoded, len(encoded), ctypes.byref(value), ctypes.sizeof(value)))
    return _metadata(value)


def stat_descriptor(descriptor):
    import msvcrt
    handle = msvcrt.get_osfhandle(descriptor)
    value = NativeInfo()
    _check(library().wrn_handle_info(ctypes.c_uint64(handle), ctypes.byref(value), ctypes.sizeof(value)))
    return _metadata(value)


def safe_parent(path):
    encoded = _path(path); _check(library().wrn_check_parent(encoded, len(encoded))); return Path(path)


def private_directory(path):
    encoded = _path(path); _check(library().wrn_directory(encoded, len(encoded), 0)); return Path(path)


def make_directory(path, *, exist_ok=False):
    encoded = _path(path); _check(library().wrn_directory(encoded, len(encoded), 2 if exist_ok else 1)); return Path(path)


def sync_directory(path):
    encoded = _path(path); _check(library().wrn_directory_flush(encoded, len(encoded)))


class PruneLease:
    """Retire the admitted artifact while excluding downloads and replacement."""
    def __init__(self, path):
        self.path, self.token = Path(path), 0
        encoded, token = _path(path), ctypes.c_uint64()
        _check(library().wrn_prune_open(encoded, len(encoded), ctypes.byref(token)))
        self.token = token.value

    def check(self, path):
        encoded = _path(path)
        _check(library().wrn_prune_check(self.token, encoded, len(encoded)))

    def remove(self):
        _check(library().wrn_prune_remove(self.token))

    def close(self):
        token, self.token = self.token, 0
        if token:
            _check(library().wrn_prune_close(token))

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class NativeFile:
    """Borrow fd only inside this context; its native admission stays pinned."""
    def __init__(self, path, mode, maximum):
        import msvcrt
        self.fd, self.token = None, 0
        if type(mode) is not int or mode not in (1, 2, 3) or type(maximum) is not int or not 0 <= maximum <= 2 * 1024**3:
            raise ValueError('Native private file mode or bound is invalid')
        encoded, token, raw = _path(path), ctypes.c_uint64(), ctypes.c_uint64()
        native = library()
        try:
            _check(native.wrn_open(encoded, len(encoded), mode, maximum, ctypes.byref(token)))
            _check(native.wrn_duplicate(token.value, ctypes.byref(raw)))
            flags = os.O_BINARY | (os.O_RDONLY if mode == 1 else os.O_RDWR)
            self.fd = msvcrt.open_osfhandle(ctypes.c_ssize_t(raw.value).value, flags)
            raw.value = 0
            os.set_inheritable(self.fd, False)
            self.token = token.value
        except BaseException:
            if self.fd is not None:
                os.close(self.fd); self.fd = None
            if raw.value:
                _kernel.CloseHandle(ctypes.c_void_p(raw.value))
            if token.value:
                native.wrn_close(token.value)
            raise

    def info(self):
        value = NativeInfo()
        _check(library().wrn_file_info(self.token, ctypes.byref(value), ctypes.sizeof(value)))
        return _metadata(value)

    def sync(self):
        _check(library().wrn_flush(self.token))

    def close(self):
        descriptor, token = self.fd, self.token
        self.fd, self.token = None, 0
        try:
            if descriptor is not None:
                os.close(descriptor)
        finally:
            if token:
                _check(library().wrn_close(token))

    def __enter__(self):
        return self

    def __exit__(self, *ignored):
        self.close()


@contextmanager
def private_input(path, maximum):
    with NativeFile(path, 1, maximum) as native:
        before = native.info()
        with os.fdopen(native.fd, 'rb', closefd=False) as stream:
            yield stream, before


@contextmanager
def private_output(path, maximum):
    with NativeFile(path, 2, maximum) as native:
        with os.fdopen(native.fd, 'wb', closefd=False) as stream:
            yield stream, native
        native.sync()


def read_record(path):
    encoded = _path(path); data, signature = ctypes.create_string_buffer(1048576), ctypes.create_string_buffer(65)
    length, present = ctypes.c_uint64(), ctypes.c_uint32()
    _check(library().wrn_record_read(encoded, len(encoded), data, len(data), ctypes.byref(length), signature, len(signature), ctypes.byref(present)))
    if present.value == 0:
        return None
    if present.value != 1 or length.value > len(data):
        raise OSError('Native private record metadata is invalid')
    return data.raw[:length.value], signature.value.decode('ascii', errors='strict')


def publish_record(path, data, expected):
    encoded = _path(path)
    if type(data) is not bytes or not 0 < len(data) <= 1048576:
        raise ValueError('Native private record exceeds its bound')
    before, signature = b'', b''
    if expected is not None:
        if type(expected) is not tuple or len(expected) != 2 or type(expected[0]) is not bytes or type(expected[1]) is not str:
            raise ValueError('Native record observation is invalid')
        before, signature = expected[0], expected[1].encode('ascii', errors='strict')
    _check(library().wrn_record_publish(encoded, len(encoded), int(expected is not None), before, len(before),
                                       signature, len(signature), data, len(data)))


@contextmanager
def generation_lock(path):
    encoded, token = _path(path), ctypes.c_uint64()
    _check(library().wrn_generation_acquire(encoded, len(encoded), ctypes.byref(token)))
    lease = _GenerationLease(token.value)
    try:
        yield lease
    finally:
        lease.token = 0
        _check(library().wrn_generation_close(token.value))


class _GenerationLease:
    def __init__(self, token):
        self.token = token


class DatabaseLease:
    """Stopped SQL ownership, bound to a real native generation admission."""
    def __init__(self, path, generation):
        if not isinstance(generation, _GenerationLease) or not generation.token:
            raise ValueError('Native SQL ownership requires live generation admission')
        encoded, token = _path(path), ctypes.c_uint64()
        _check(library().wrn_database_acquire(generation.token, encoded, len(encoded), ctypes.byref(token)))
        self.token = token.value

    def verify(self, path):
        encoded = _path(path)
        _check(library().wrn_database_verify(self.token, encoded, len(encoded)))

    def move(self, source, destination):
        import errno
        original, target = _path(source), _path(destination)
        result = library().wrn_database_move(self.token, original, len(original), target, len(target))
        if result == 6:
            raise OSError(errno.EXDEV, 'Native generation requires cross-volume staging')
        _check(result)

    def close(self):
        token, self.token = self.token, 0
        if token:
            _check(library().wrn_database_close(token))


@contextmanager
def database_lock(path, generation):
    lease = DatabaseLease(path, generation)
    try:
        yield lease
    finally:
        lease.close()


def initialize_database_snapshot(path):
    encoded = _path(path)
    _check(library().wrn_database_initialize(encoded, len(encoded)))


@contextmanager
def installation_lock(path):
    encoded, token = _path(path), ctypes.c_uint64()
    _check(library().wrn_installation_acquire(encoded, len(encoded), ctypes.byref(token)))
    try:
        yield
    finally:
        _check(library().wrn_installation_close(token.value))


@contextmanager
def directory_lock(path):
    encoded, token = _path(path), ctypes.c_uint64()
    _check(library().wrn_directory_acquire(encoded, len(encoded), ctypes.byref(token)))
    try:
        yield
    finally:
        _check(library().wrn_directory_close(token.value))


def pipe():
    """Transfer explicitly private native handles into binary, noninheritable CRT descriptors."""
    import msvcrt
    reader, writer = ctypes.c_uint64(), ctypes.c_uint64()
    _check(library().wrn_pipe(ctypes.byref(reader), ctypes.byref(writer)))
    descriptors = []
    try:
        for raw, mode in ((reader, os.O_RDONLY), (writer, os.O_WRONLY)):
            descriptor = msvcrt.open_osfhandle(raw.value, mode | os.O_BINARY)
            raw.value = 0  # Ownership transferred to the CRT.
            descriptors.append(descriptor)
            os.set_inheritable(descriptor, False)
        return tuple(descriptors)
    except BaseException:
        for descriptor in descriptors:
            os.close(descriptor)
        raise
    finally:
        for raw in (reader, writer):
            if raw.value:
                _kernel.CloseHandle(ctypes.c_void_p(raw.value))


class CryptProcess:
    """Fixed native helper, same-account job ownership, and private secret pipe."""
    def __init__(self, helper, action, phrase, input_fd, output_fd, working_directory):
        import msvcrt
        import struct
        if action not in ('encrypt', 'decrypt') or type(phrase) is not bytes or not 0 < len(phrase) <= 1024:
            raise ValueError('Native recovery crypto invocation is invalid')
        program, root = _path(helper), _path(working_directory)
        self.token, self.finished = 0, False
        reader, writer = pipe()
        token = ctypes.c_uint64()
        try:
            frame = struct.pack('!I', len(phrase)) + phrase
            if os.write(writer, frame) != len(frame):
                raise OSError('Native recovery secret pipe write failed')
            os.close(writer); writer = -1
            _check(library().wrn_crypto_start(program, len(program), root, len(root), 1 if action == 'encrypt' else 2,
                    ctypes.c_uint64(msvcrt.get_osfhandle(reader)), ctypes.c_uint64(msvcrt.get_osfhandle(input_fd)),
                    ctypes.c_uint64(msvcrt.get_osfhandle(output_fd)), ctypes.byref(token)))
            self.token = token.value
        except BaseException:
            if token.value:
                library().wrn_crypto_close(token.value)
            raise
        finally:
            os.close(reader)
            if writer >= 0:
                os.close(writer)

    def finish(self):
        if self.finished or not self.token:
            raise ValueError('Native recovery crypto child is already retired')
        code = ctypes.c_uint32()
        try:
            _check(library().wrn_crypto_wait(self.token, 60000, ctypes.byref(code)))
        finally:
            self.cancel()
        if code.value != 0:
            from .archive import RecoveryError
            raise RecoveryError('Recovery encryption failed')

    def cancel(self):
        token, self.token = self.token, 0
        self.finished = True
        if token:
            _check(library().wrn_crypto_close(token))
