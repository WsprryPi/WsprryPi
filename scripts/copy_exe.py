#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Install the locally built WsprryPi executable."""

from pathlib import Path
import os
import subprocess
import sys


SERVICE_NAME = "wsprrypi.service"
INSTALL_PATH = Path("/usr/local/bin/wsprrypi")


def run(command, *, check=True, **kwargs):
    """Run a command and report it if it fails."""
    try:
        return subprocess.run(command, check=check, **kwargs)
    except FileNotFoundError as exc:
        raise RuntimeError(f"Required command not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(
            f"Command failed with exit status {exc.returncode}: {' '.join(command)}"
        ) from exc


def get_git_root():
    """Determine the repository root using this script's location."""
    script_dir = Path(__file__).resolve().parent
    result = run(
        ["git", "-C", str(script_dir), "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip())


def service_exists(service_name):
    """Return whether systemd knows about the service."""
    result = run(
        ["systemctl", "show", service_name, "--property=LoadState", "--value"],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0 and result.stdout.strip() == "loaded"


def service_is_active(service_name):
    """Return whether the service is currently active."""
    result = run(
        ["systemctl", "is-active", "--quiet", service_name],
        check=False,
    )
    return result.returncode == 0


def install_file(source, destination):
    """Stage and atomically replace an installed executable."""
    staged = destination.with_name(f".{destination.name}.install-{os.getpid()}")
    try:
        run(
            [
                "sudo",
                "install",
                "-o",
                "root",
                "-g",
                "root",
                "-m",
                "0755",
                str(source),
                str(staged),
            ]
        )
        run(["sudo", "mv", "-f", str(staged), str(destination)])
    except RuntimeError:
        run(["sudo", "rm", "-f", str(staged)], check=False)
        raise


def main():
    """Install the build while preserving the service's current state."""
    git_root = get_git_root()
    source = git_root / "src" / "build" / "bin" / "wsprrypi"

    if not source.is_file():
        raise RuntimeError(
            f"Built executable not found: {source}\n"
            "Run 'make -C src release' first."
        )
    if not os.access(source, os.X_OK):
        raise RuntimeError(f"Built executable is not executable: {source}")

    exists = service_exists(SERVICE_NAME)
    was_active = exists and service_is_active(SERVICE_NAME)

    if was_active:
        run(["sudo", "systemctl", "stop", SERVICE_NAME])

    try:
        install_file(source, INSTALL_PATH)
    finally:
        if was_active:
            run(["sudo", "systemctl", "start", SERVICE_NAME])

    print(f"Installed {source} to {INSTALL_PATH}.")
    if not exists:
        print(f"Note: {SERVICE_NAME} is not installed; no service state was changed.")
    elif was_active:
        print(f"Restarted {SERVICE_NAME} because it was active before installation.")
    else:
        print(f"Left inactive {SERVICE_NAME} stopped.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
