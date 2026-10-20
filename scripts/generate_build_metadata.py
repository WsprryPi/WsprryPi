#!/usr/bin/env python3
"""Generate the build metadata header consumed by src/version.cpp.

The output is replaced atomically only when its content changes.  This makes
the header a stable Make prerequisite while still refreshing Git-derived
metadata after a checkout moves to a different commit.
"""

from __future__ import annotations

import argparse
import fcntl
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


class MetadataError(RuntimeError):
    """Raised when required Git metadata cannot be established."""


def git(repo_root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no detail"
        raise MetadataError(f"git {' '.join(args)} failed: {detail}")
    return result.stdout.strip()


def sanitized_branch(raw_branch: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z]+", "-", raw_branch).rstrip("-")
    return sanitized or "unknown"


def c_string(value: str) -> str:
    escaped: list[str] = []
    for character in value:
        escapes = {
            "\\": "\\\\",
            '"': '\\"',
            "\a": "\\a",
            "\b": "\\b",
            "\f": "\\f",
            "\n": "\\n",
            "\r": "\\r",
            "\t": "\\t",
            "\v": "\\v",
        }
        if character in escapes:
            escaped.append(escapes[character])
        elif ord(character) < 32 or ord(character) == 127:
            escaped.append(f"\\{ord(character):03o}")
        else:
            escaped.append(character)
    return "".join(escaped)


def release_version(value: str) -> str:
    """Accept a SemVer release identity without tags or build metadata."""
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?", value):
        raise MetadataError("release version must be SemVer without v prefix or build metadata")
    prerelease = value.partition("-")[2]
    if any(part.isdigit() and len(part) > 1 and part.startswith("0") for part in prerelease.split(".")):
        raise MetadataError("numeric prerelease identifiers cannot have leading zeros")
    return value


def metadata(repo_root: Path, project: str, executable: str, version_override: str | None = None) -> str:
    if version_override is not None:
        release_version(version_override)
    git(repo_root, "rev-parse", "--is-inside-work-tree")
    full_commit = git(repo_root, "rev-parse", "HEAD")
    short_commit = git(repo_root, "rev-parse", "--short", "HEAD")

    symbolic_branch = subprocess.run(
        ["git", "-C", str(repo_root), "symbolic-ref", "--quiet", "--short", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    if symbolic_branch.returncode == 0:
        raw_branch = symbolic_branch.stdout.strip()
        branch_state = "branch"
    else:
        raw_branch = "HEAD"
        branch_state = "detached"
    branch = sanitized_branch(raw_branch)

    tag_result = subprocess.run(
        [
            "git", "-C", str(repo_root), "describe", "--tags",
            "--match", "v[0-9]*", "--match", "[0-9]*", "--abbrev=0",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    raw_tag = tag_result.stdout.strip() if tag_result.returncode == 0 else ""
    base_version = raw_tag.removeprefix("v") if raw_tag else "0.0.0"
    prerelease = "" if branch in {"main", "master"} else f"-{branch}"
    exact_tag = subprocess.run(
        ["git", "-C", str(repo_root), "describe", "--tags", "--exact-match"],
        check=False,
        capture_output=True,
        text=True,
    ).returncode == 0
    version = version_override if version_override is not None else f"{base_version}{prerelease}"
    if not exact_tag or version_override is not None:
        version += f"+{short_commit}"

    dirty = "true" if git(
        repo_root, "status", "--porcelain", "--untracked-files=no"
    ) else "false"
    values = {
        "MAKE_TAG": version,
        "MAKE_BRH": branch,
        "MAKE_RAW_BRH": raw_branch,
        "MAKE_BRANCH_STATE": branch_state,
        "MAKE_COMMIT": full_commit,
        "MAKE_DIRTY": dirty,
        "MAKE_EXE": executable,
        "MAKE_PRJ": project,
    }
    lines = [
        "#pragma once",
        "",
    ]
    lines.extend(f'#define {name} "{c_string(value)}"' for name, value in values.items())
    return "\n".join(lines) + "\n"


def replace_if_changed(destination: Path, content: str) -> bool:
    destination.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    lock_path = destination.with_name(f".{destination.name}.lock")
    with lock_path.open("a+") as lock_file:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        if destination.exists() and destination.read_bytes() == encoded:
            return False
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{destination.name}.", dir=destination.parent
        )
        try:
            with os.fdopen(descriptor, "wb") as temporary_file:
                temporary_file.write(encoded)
                temporary_file.flush()
                os.fsync(temporary_file.fileno())
            os.replace(temporary_name, destination)
        except BaseException:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass
            raise
    return True


def matches_existing(destination: Path, content: str) -> bool:
    """Return whether *destination* already contains exactly *content*."""
    try:
        return destination.read_bytes() == content.encode("utf-8")
    except FileNotFoundError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--project", required=True)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--release-version", help="explicit candidate SemVer; always retains commit attribution")
    parser.add_argument(
        "--check", action="store_true",
        help="check generated content without writing (0=current, 3=stale)",
    )
    args = parser.parse_args()
    try:
        content = metadata(args.repo_root.resolve(), args.project, args.executable, args.release_version)
        if args.check:
            current = matches_existing(args.output, content)
            print(f"build metadata {'current' if current else 'stale'}: {args.output}")
            return 0 if current else 3
        changed = replace_if_changed(args.output, content)
    except (MetadataError, OSError) as error:
        print(f"build metadata generation failed: {error}", file=sys.stderr)
        return 1
    print(f"build metadata {'updated' if changed else 'unchanged'}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
