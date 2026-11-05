"""Offline command expectations. No command can fall through to a real runner."""
from collections import deque
from dataclasses import dataclass
import os
import signal
import subprocess
import time


@dataclass(frozen=True)
class Command:
    argv: tuple
    cwd: object = None
    passthrough: bool = False
    stage: str = "mutation"


class StrictCommands:
    def __init__(self, reads=None, *, steps=(), responses=None, result=None, error=RuntimeError):
        self.responses = {}
        self.reads = set()
        self.pending = deque(steps)
        self.required = frozenset(step.argv for step in self.pending)
        for command, value in (reads or {}).items():
            self.allow_read(command, value)
        if any(not isinstance(key, tuple) for key in (responses or {})):
            raise TypeError("response keys must be argv tuples; declare context in steps")
        self.responses.update(responses or {})
        self.result = result or (lambda: subprocess.CompletedProcess([], 0, "", ""))
        self.error = error
        self.calls = []
        self.contexts = []
        self.passthrough_calls = []
        self.violations = []

    def allow_read(self, command, value):
        call = command if isinstance(command, Command) else Command(tuple(command))
        if call.argv in self.required:
            raise ValueError("a required operation cannot also be a repeatable inspection")
        self.reads.add(Command(call.argv, call.cwd, call.passthrough))
        self.responses[call.argv] = value

    def run(self, argv, *, check=True, cwd=None, passthrough=False):
        key = tuple(str(item) for item in argv)
        context = Command(key, None if cwd is None else str(cwd), passthrough)
        self.calls.append(key)
        if passthrough:
            self.passthrough_calls.append(key)
        if self.pending and context == Command(self.pending[0].argv, self.pending[0].cwd,
                                               self.pending[0].passthrough):
            expected = self.pending.popleft()
            self.contexts.append((expected.stage, context))
        elif context in self.reads:
            self.contexts.append(("inspection", context))
        else:
            # Persist refusals even when production catches the raised exception.
            self.violations.append(context)
            raise AssertionError(f"unexpected fixture command/context: {context}; next={self.pending[0] if self.pending else None}")
        value = self.responses.get(key)
        if value is None:
            value = self.result()
        if isinstance(value, BaseException):
            raise value
        if check and value.returncode:
            raise self.error(value.stderr or "fixture command failed")
        return value

    def assert_complete(self):
        assert not self.violations, f"unexpected commands were caught by production: {self.violations}"
        assert not self.pending, f"required fixture commands not consumed: {list(self.pending)}"


def _owned_state(owner):
    """Bounded Linux metadata for only the timed-out process group; no payloads."""
    pending,seen,rows = [owner],set(),[]
    def read(path):
        descriptor = os.open(path,os.O_RDONLY|os.O_NONBLOCK)
        try: return os.read(descriptor,1024).decode(errors='replace')
        finally: os.close(descriptor)
    while pending and len(seen)<8:
        pid = pending.pop(0)
        if pid in seen: continue
        seen.add(pid)
        root = f'/proc/{pid}'
        row = {'pid':pid}
        try:
            if os.getpgid(pid)!=owner: continue
            for field in ('comm','wchan','syscall','stat'):
                try: row[field] = read(root+'/'+field).strip()
                except OSError as error: row[field] = type(error).__name__
            children = read(root+f'/task/{pid}/children').split()
            pending.extend(int(value) for value in children if value.isdigit())
            row['fds'] = []
            with os.scandir(root+'/fd') as descriptors:
                for index,entry in enumerate(descriptors):
                    if index>=8: break
                    row['fds'].append((entry.name,os.readlink(entry.path)[:160]))
        except OSError as error: row['state'] = type(error).__name__
        rows.append(row)
    return repr(rows)[:8192]


def run_shell(source, *, env, cwd=None, stage, stdin=False, watchdog=30):
    """Wait for actual shell completion; watchdogs bound hangs and owned cleanup."""
    started = time.monotonic()
    command = ["bash"] if stdin else ["bash", "-c", source]
    child = subprocess.Popen(command, stdin=subprocess.PIPE if stdin else subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                             env=env, cwd=cwd, start_new_session=True)
    try:
        out, err = child.communicate(source if stdin else None, timeout=watchdog)
        try:
            os.killpg(child.pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(f"fixture stage={stage}; shell exited but owned processes remain")
    except BaseException as failure:
        primary = failure
        if isinstance(failure, subprocess.TimeoutExpired):
            primary = TimeoutError(f"fixture stage={stage}; expected=shell exit; last_transition=running; "
                f"elapsed={time.monotonic() - started:.3f}s; hang_watchdog={watchdog}s; "
                f"child_exit={child.poll()}; stdout_bytes={len(failure.stdout or b'')}; "
                f"stderr_bytes={len(failure.stderr or b'')}")
        errors = [primary]
        if isinstance(failure,subprocess.TimeoutExpired):
            try: primary.args = (str(primary)+'; owned_state='+_owned_state(child.pid),)
            except BaseException as diagnostic: errors.append(diagnostic)
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except BaseException as cleanup:
            errors.append(cleanup)
        try:
            child.communicate(timeout=2)
        except BaseException as cleanup:
            errors.append(cleanup)
        for pipe in (child.stdout, child.stderr, child.stdin):
            try:
                if pipe: pipe.close()
            except BaseException as cleanup:
                errors.append(cleanup)
        if len(errors) > 1:
            raise BaseExceptionGroup("fixture operation and cleanup failed", errors) from None
        raise primary from None
    return subprocess.CompletedProcess(command, child.returncode, out, err)
