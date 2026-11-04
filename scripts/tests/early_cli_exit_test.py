#!/usr/bin/env python3
"""Check completed CLI results; a watchdog bounds hangs, not acceptable latency."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


# A failed child stops this eighteen-case cohort within one minute, leaving the
# twenty-minute native job budget for diagnostics/cleanup. This is a resource
# fuse, not a three-second CLI responsiveness requirement. Race sensitivity lives
# in logger_shutdown_test.py's entered/released predicate-mutex gate.
HANG_WATCHDOG_SECONDS = 60


def run_cli(command, watchdog=HANG_WATCHDOG_SECONDS):
    started = time.monotonic()
    child = subprocess.Popen(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             env=dict(os.environ, WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
    try:
        out, err = child.communicate(timeout=watchdog)
    except subprocess.TimeoutExpired as failure:
        elapsed = time.monotonic() - started
        observed = (failure.stdout or b'').decode(errors='replace') if isinstance(failure.stdout, bytes) else (failure.stdout or '')
        phase = 'waiting for exit after stdout' if observed else 'waiting for first stdout or exit'
        exit_at_deadline = child.poll()
        diagnostic = ''
        cleanup = ''
        try:
            # Inspect the still-live process without extending the assertion budget.
            if sys.platform == 'darwin' and exit_at_deadline is None:
                try:
                    with tempfile.TemporaryDirectory(prefix='early-cli-stack-') as raw:
                        stack = Path(raw) / 'sample.txt'
                        sampled = subprocess.run(['/usr/bin/sample', str(child.pid), '1', '1', '-file', str(stack)],
                                       capture_output=True, timeout=3, check=False)
                        diagnostic = stack.read_text(errors='replace')[:12000] if stack.is_file() else f'process sampling exit={sampled.returncode}; no stack produced'
                except (OSError, subprocess.TimeoutExpired) as error:
                    diagnostic = f'process sampling unavailable: {type(error).__name__}'
        finally:
            try:
                child.kill()
                out, err = child.communicate(timeout=2)
            except (OSError, subprocess.TimeoutExpired) as error:
                out, err = observed, ''
                cleanup = f'; cleanup={type(error).__name__}, child_exit={child.poll()}'
            finally:
                child.stdout.close(); child.stderr.close()
        raise AssertionError(f'CLI phase={phase}; command={command!r}; elapsed={elapsed:.3f}s; '
                             f'hang_watchdog={watchdog}s; child_exit_at_deadline={exit_at_deadline}; child_exit_after_cleanup={child.poll()}{cleanup}; '
                             f'stdout={out[-2000:]!r}; stderr={err[-2000:]!r}\n{diagnostic}') from failure
    except BaseException as failure:
        try:
            child.kill(); child.communicate(timeout=2)
        except BaseException as cleanup:
            raise BaseExceptionGroup('CLI failure and cleanup failure', [failure, cleanup]) from None
        finally:
            child.stdout.close(); child.stderr.close()
        raise
    return subprocess.CompletedProcess(command, child.returncode, out, err), time.monotonic() - started


def main():
    binary = str(Path(sys.argv[1]).resolve())
    expected_transport = sys.argv[2] if len(sys.argv) > 2 else 'linux-i2c'
    durations = []
    for option, required in [('--version', 'Compiled backends:'), ('-v', 'Ancillary GPIO:'),
                             ('--help', 'Usage:'), ('-h', 'Usage:'), ('--list-backends', 'wtp'),
                             ('--list-i2c-adapters', expected_transport)]:
        for logging in ([], ['--journald'], ['-J']):
            result, elapsed = run_cli([binary, *logging, option])
            assert result.returncode == 0, (option, result.returncode, result.stderr)
            assert required.lower() in (result.stdout + result.stderr).lower(), option
            if option == '--list-i2c-adapters':
                body = json.loads(result.stdout)
                assert set(body) == {'Transport', 'I2C Adapters', 'Discovery Error'}
                assert body['Transport'] == expected_transport and isinstance(body['I2C Adapters'], list)
                assert isinstance(body['Discovery Error'], str)
            durations.append(elapsed)
    print(f'{len(durations)} informational CLI exit checks passed; observed maximum={max(durations):.3f}s; hang watchdog={HANG_WATCHDOG_SECONDS}s (not a latency assertion)')


if __name__ == '__main__':
    main()
