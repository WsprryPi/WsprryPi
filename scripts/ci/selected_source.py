#!/usr/bin/env python3
"""Record and verify the single Pico commit selected for a validation run."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def git(*arguments, watchdog=30):
    started = time.monotonic()
    child = subprocess.Popen(["git", "-c", "core.fsmonitor=false", *arguments],
        text=True, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        start_new_session=True, env={key: value for key, value in os.environ.items()
                                    if not key.startswith("GIT_")})
    try:
        output, _ = child.communicate(timeout=watchdog)
        if child.returncode:
            raise ValueError("stage=source-git; git refused the selected repository/ref")
        try:
            os.killpg(child.pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise ValueError("stage=source-git; leader exited with owned descendants remaining")
        return output.strip()
    except BaseException as error:
        primary = error
        if isinstance(error, subprocess.TimeoutExpired):
            primary = ValueError("stage=source-git; expected=git completion; last_transition=running; "
                f"elapsed={time.monotonic() - started:.3f}s; hang_guard={watchdog}s; child_exit={child.poll()}")
        errors = [primary]
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except BaseException as failure:
            errors.append(failure)
        try:
            child.communicate(timeout=2)
        except BaseException as failure:
            errors.append(failure)
        for pipe in (child.stdout, child.stderr):
            try:
                pipe.close()
            except BaseException as failure:
                errors.append(failure)
        if len(errors) > 1:
            raise BaseExceptionGroup("source Git operation and cleanup failed", errors) from None
        raise primary from None


def require_sha(value):
    if not re.fullmatch(r"[0-9a-f]{40}", value) or value == "0" * 40:
        raise ValueError("stage=source-selection; expected=one immutable nonzero commit SHA")
    return value


def head(repository):
    repository = Path(repository).resolve(strict=True)
    if Path(git("-C", str(repository), "rev-parse", "--show-toplevel")).resolve() != repository:
        raise ValueError("stage=source-selection; expected=the selected repository root")
    if git("-C", str(repository), "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("stage=source-selection; selected checkout has uncommitted files")
    return require_sha(git("-C", str(repository), "rev-parse", "--verify", "HEAD^{commit}"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument("operation", choices=("selection", "record", "require", "verify"))
    parser.add_argument("value")
    parser.add_argument("repository", nargs="?")
    args = parser.parse_args()
    try:
        if args.operation != "verify" and args.repository is not None:
            raise ValueError("stage=source-selection; unexpected extra argument")
        if args.operation == "selection":
            if not args.value or args.value.startswith("-"):
                raise ValueError("stage=source-selection; missing or invalid requested ref")
            git("check-ref-format", "--allow-onelevel", args.value)
        elif args.operation == "require":
            require_sha(args.value)
        elif args.operation == "record":
            sha = head(args.value)
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
                output.write(f"pico_sha={sha}\n")
            print(f"Selected Pico commit: {sha}")
        else:
            expected = require_sha(args.value)
            if args.repository is None:
                raise ValueError("stage=source-verification; missing checkout path")
            actual = head(args.repository)
            if actual != expected:
                raise ValueError(f"stage=source-verification; expected={expected}; actual={actual}")
            print(f"Verified Pico commit: {actual}")
        return 0
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
