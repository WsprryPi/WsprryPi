#!/usr/bin/env python3
"""Render the static Apache client-identity policy without changing Apache."""

import argparse
import configparser
import os
import tempfile
from pathlib import Path


TRUSTED_IDENTITY_HEADER = "X-WsprryPi-Client-Address"


def configured_mode(ini_path: Path) -> str:
    parser = configparser.ConfigParser(interpolation=None)
    try:
        with ini_path.open(encoding="utf-8") as source:
            parser.read_file(source)
        value = parser.get("Security", "Privileged Network Safety").strip()
    except (OSError, configparser.Error, KeyError):
        return "enforced"
    return value if value in {"enforced", "insecure-disabled"} else "enforced"


def render(mode: str) -> str:
    status = (
        "# NETWORK SAFETY OFF\n"
        if mode == "insecure-disabled"
        else "# Current-network authorization is enforced by WsprryPi for each request.\n"
    )
    return (
        "# Managed by WsprryPi. Manual edits will be replaced.\n"
        "# Apache overwrites the dedicated backend identity with its actual connection peer.\n"
        f"{status}"
        '<LocationMatch "^/wsprrypi/(?:config(?:/|$)|control(?:/|$)|version$|api(?:/|$)|socket(?:/|$))">\n'
        f"    RequestHeader unset {TRUSTED_IDENTITY_HEADER}\n"
        f'    RequestHeader set {TRUSTED_IDENTITY_HEADER} "expr=%{{CONN_REMOTE_ADDR}}"\n'
        "</LocationMatch>\n"
    )


def atomic_write(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    arguments = argparse.ArgumentParser()
    arguments.add_argument("--ini", required=True, type=Path)
    arguments.add_argument("--output", required=True, type=Path)
    args = arguments.parse_args()

    mode = configured_mode(args.ini)
    atomic_write(args.output, render(mode))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
