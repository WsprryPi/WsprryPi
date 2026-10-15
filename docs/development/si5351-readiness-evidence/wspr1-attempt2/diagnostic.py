"""Bounded wspr1 bus-only diagnostic. Only all-output inhibition writes.

Requires exclusive device access. Only register 3 = 0xff inhibition and read
pointer writes are permitted; no clock/reference/drive programming occurs. SYS_INIT or any transport failure ends sampling;
PLLA/PLLB status is retained without assuming an already programmed clock chain.
Run only under explicit hardware authorization. Kernel I2C stalls are not bounded
by this script's userspace deadline; supervise the process separately.
"""
import collections
import ctypes
import fcntl
import json
import os
import signal
import socket
import time


class Msg(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_uint16), ("flags", ctypes.c_uint16),
                ("length", ctypes.c_uint16), ("buf", ctypes.POINTER(ctypes.c_uint8))]


class Transfer(ctypes.Structure):
    _fields_ = [("msgs", ctypes.POINTER(Msg)), ("count", ctypes.c_uint32)]


def emit(kind, **fields):
    print(json.dumps({"kind": kind, **fields}, sort_keys=True), flush=True)


def main():
    if socket.gethostname() != "wspr1":
        raise RuntimeError("This reviewed run card is bound to wspr1 only")
    started = time.monotonic()
    fd = None
    slave_selected = False
    primary = None
    cleanup_error = None
    phase = "open"
    counts = {"split": collections.Counter(), "combined": collections.Counter()}
    libc = ctypes.CDLL(None, use_errno=True)

    def interrupted(signum, _frame):
        raise InterruptedError(f"diagnostic interrupted by signal {signum}")

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)

    def split_read(register):
        if os.write(fd, bytes([register])) != 1:
            raise OSError("short register-selection write")
        data = os.read(fd, 1)
        if len(data) != 1:
            raise OSError("short register read")
        return data[0]

    def combined_read():
        pointer = (ctypes.c_uint8 * 1)(0)
        data = (ctypes.c_uint8 * 1)()
        messages = (Msg * 2)(Msg(0x60, 0, 1, pointer), Msg(0x60, 1, 1, data))
        transfer = Transfer(messages, 2)
        result = libc.ioctl(fd, 0x0707, ctypes.byref(transfer))
        if result != 2:
            error = ctypes.get_errno() if result < 0 else 0
            raise OSError(error, f"I2C_RDWR returned {result}, expected 2 messages")
        return data[0]

    emit("start", host=socket.gethostname(), device="/dev/i2c-1", address=0x60,
         maximum_pairs=500, requested_pair_wait_ms=2, maximum_requested_rf_seconds=0)
    try:
        fd = os.open("/dev/i2c-1", os.O_RDWR | os.O_CLOEXEC)
        phase = "select_slave"
        fcntl.ioctl(fd, 0x0703, 0x60)
        slave_selected = True
        phase = "inhibit_outputs"
        if os.write(fd, bytes([3, 255])) != 2:
            raise OSError("short all-output inhibition write")
        phase = "verify_inhibited_before"
        before = split_read(3)
        emit(phase, register3=before)
        if before != 0xff:
            raise RuntimeError("register 3 is not 0xff; refusing sampling without changing outputs")
        for index in range(500):
            for method, read in (("split", lambda: split_read(0)), ("combined", combined_read)):
                phase = method
                if time.monotonic() - started > 20:
                    raise TimeoutError("userspace diagnostic elapsed limit exceeded")
                call_start = time.monotonic()
                value = read()
                counts[method][value] += 1
                emit("sample", index=index, method=method, register0=value,
                     elapsed_s=time.monotonic() - started,
                     transaction_s=time.monotonic() - call_start)
                if value & 0x80:
                    raise RuntimeError(f"valid SYS_INIT indication: register0=0x{value:02x}")
            time.sleep(0.002)
    except Exception as error:
        primary = {"phase": phase, "error": str(error), "errno": getattr(error, "errno", None)}
        emit("first_fault", **primary)
    finally:
        if fd is not None and slave_selected:
            try:
                if os.write(fd, bytes([3, 255])) != 2:
                    raise OSError("short cleanup inhibition write")
                after = split_read(3)
                emit("verify_inhibited_after", register3=after)
                if after != 0xff:
                    raise RuntimeError("output state unknown: register 3 is not 0xff")
            except Exception as error:
                cleanup_error = str(error)
                emit("cleanup_failure", error=cleanup_error)
        if fd is not None:
            try:
                os.close(fd)
            except OSError as error:
                cleanup_error = (cleanup_error + "; " if cleanup_error else "") + str(error)
                emit("close_failure", error=str(error))
    emit("result", ok=primary is None and cleanup_error is None,
         first_fault=primary, cleanup_error=cleanup_error,
         counts={method: dict(values) for method, values in counts.items()},
         elapsed_s=time.monotonic() - started)
    return 0 if primary is None and cleanup_error is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
