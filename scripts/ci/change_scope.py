#!/usr/bin/env python3
"""Choose full CI unless a complete Git diff proves only documentation changed."""

from __future__ import annotations

import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import NamedTuple


ROOT = Path(__file__).resolve().parents[2]
PROSE_SUFFIXES = {".md", ".rst"}
ILLUSTRATION_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".pdf"}
# These documentation paths are regression inputs, fixtures or distributed notices.
CHECKED_DOCUMENTS = {"docs/support-bundle-private-intake.md"}
INPUT_DIRECTORIES = (
    "docs/licenses/",
    "docs/development/wtp-reply-routing-results/",
    "release_tools/rp1-qualification/",
)


class Change(NamedTuple):
    old_mode: str
    new_mode: str
    status: str
    path: str


def documentation_path(path: str) -> bool:
    name = PurePosixPath(path)
    if (not path or name.is_absolute() or name.as_posix() != path
            or ".." in name.parts or name.name == "AGENTS.md"
            or name.name.upper().startswith(("LICENSE", "COPYING", "NOTICE", "PROVENANCE"))):
        return False
    if path in CHECKED_DOCUMENTS or path.startswith(INPUT_DIRECTORIES):
        return False
    if path == "README.md":
        return True
    if path.startswith("docs/"):
        return name.suffix in PROSE_SUFFIXES | ILLUSTRATION_SUFFIXES
    return path.startswith("release_tools/") and name.suffix in PROSE_SUFFIXES


def parse_changes(raw: bytes) -> list[Change]:
    """Read --raw -z --no-renames output without quoting or filename splitting."""
    if not raw:
        return []
    fields = raw.split(b"\0")
    if fields.pop() != b"" or len(fields) % 2:
        raise ValueError("incomplete Git diff")
    changes = []
    for index in range(0, len(fields), 2):
        header = fields[index].decode("ascii").split()
        if (len(header) != 5 or not re.fullmatch(r":[0-7]{6}", header[0])
                or not re.fullmatch(r"[0-7]{6}", header[1])
                or any(not re.fullmatch(r"[0-9a-f]{40}", sha) for sha in header[2:4])
                or header[4] not in {"A", "D", "M", "T"}):
            raise ValueError("unsupported Git diff record")
        path = fields[index + 1].decode("utf-8")
        if not path:
            raise ValueError("missing Git path")
        changes.append(Change(header[0][1:], header[1], header[4], path))
    return changes


def documentation_change(change: Change) -> bool:
    # Executable bits, symlinks, submodules and type changes always need full CI.
    regular_modes = {
        "A": ("000000", "100644"),
        "D": ("100644", "000000"),
        "M": ("100644", "100644"),
    }
    return (regular_modes.get(change.status) == (change.old_mode, change.new_mode)
            and documentation_path(change.path))


def comparison(event_name: str, event: dict) -> str:
    if not isinstance(event, dict):
        raise ValueError("unrecognized event")
    if event_name == "push":
        if event.get("forced") or event.get("deleted"):
            raise ValueError("forced or deleted branch")
        base, head, separator = event["before"], event["after"], ".."
    elif event_name == "pull_request":
        pull = event["pull_request"]
        base, head, separator = pull["base"]["sha"], pull["head"]["sha"], "..."
    else:
        raise ValueError("unsupported event")
    for sha in (base, head):
        if (not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{40}", sha)
                or sha == "0" * 40):
            raise ValueError("missing comparison commit")
    return base + separator + head


def full_ci_required(repo: Path, event_name: str, event: dict) -> tuple[bool, str]:
    try:
        revision = comparison(event_name, event)
        raw = subprocess.check_output(
            ["git", "-C", str(repo), "diff", "--raw", "--no-abbrev", "-z",
             "--no-renames", "--no-ext-diff", "--no-textconv",
             "--ignore-submodules=none", revision, "--"],
            stderr=subprocess.PIPE, timeout=60,
        )
        changes = parse_changes(raw)
        if not changes:
            return True, "empty comparison"
        if all(documentation_change(change) for change in changes):
            return False, "only ordinary documentation changed"
        return True, "source, checked input, unknown path or file mode changed"
    except (KeyError, TypeError, ValueError, UnicodeError, OSError,
            subprocess.SubprocessError):
        return True, "comparison unavailable or unrecognized"


def main() -> int:
    try:
        event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
        required, reason = full_ci_required(ROOT, os.environ.get("GITHUB_EVENT_NAME", ""), event)
    except (KeyError, TypeError, ValueError, OSError, UnicodeError):
        required, reason = True, "event unavailable or unrecognized"
    # Only fixed boolean values reach the Actions output file; paths are never logged.
    print(f"Full CI required: {str(required).lower()} ({reason})")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"run_full_ci={str(required).lower()}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
