#!/usr/bin/env python3
"""Push one verified support-intake publication candidate with an exact lease."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
import fcntl
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Protocol

import commit_support_bundle_intake_publication as publication
import manage_support_bundle_intake_manifest as lifecycle
import prepare_support_bundle_intake_manifest as preparation


HELPERS = {"osxkeychain", "libsecret", "manager-core"}


class PushStatus(Enum):
    proposed = "proposed"
    published = "published"
    already_published = "already_published"
    remote_conflict = "remote_conflict"
    remote_invalid = "remote_invalid"
    query_failed = "query_failed"
    push_rejected = "push_rejected"
    pushed_confirmation_uncertain = "pushed_confirmation_uncertain"
    failed = "failed"


@dataclass(frozen=True)
class PushResult:
    status: PushStatus
    generation: int = 0
    intake_status: str = ""
    signing_key_id: str = ""
    bundle_key_id: str = ""
    manifest_sha256: str = ""
    parent_commit: str = ""
    candidate_commit: str = ""


class RemoteTransport(Protocol):
    def query_main(self) -> bytes: ...
    def push(self, parent: str, candidate: str) -> bool: ...


class GitRemoteTransport:
    def __init__(self, git_path: Path, repository: Path, credential_helper: str):
        if credential_helper not in HELPERS:
            raise ValueError("unsupported credential helper")
        self.git_path = git_path; self.repository = repository; self.helper = credential_helper

    def _run(self, arguments: list[str]) -> tuple[int, bytes]:
        command = [str(self.git_path), "-c", "core.hooksPath=/dev/null",
                   "-c", "credential.helper=", "-c", f"credential.helper={self.helper}",
                   "-c", "http.proxy=", "-c", "https.proxy=", *arguments]
        completed = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=publication.environment(self.repository),
            check=False, timeout=60)
        return completed.returncode, completed.stdout[:8193]

    def query_main(self) -> bytes:
        code, output = self._run(["ls-remote", "--refs", "origin", "refs/heads/main"])
        if code != 0 or len(output) > 8192: raise RuntimeError("remote query failed")
        return output

    def push(self, parent: str, candidate: str) -> bool:
        code, output = self._run(["push", "--porcelain",
            f"--force-with-lease=refs/heads/main:{parent}", "origin",
            f"{candidate}:refs/heads/main"])
        return code == 0 and len(output) <= 8192


def parse_remote_main(output: bytes) -> str:
    try: text = output.decode("ascii")
    except UnicodeError as error: raise ValueError("remote response was not ASCII") from error
    lines = text.splitlines()
    if len(lines) != 1: raise ValueError("remote response was not one ref")
    fields = lines[0].split("\t")
    if len(fields) != 2 or fields[1] != "refs/heads/main" or not publication.OBJECT_ID.fullmatch(fields[0]):
        raise ValueError("remote response was malformed")
    return fields[0]


def local_policy(git_path: Path, repository: Path) -> None:
    names = publication.git(git_path, repository,
        ["config", "--local", "--name-only", "--list", "-z"]).decode("utf-8").split("\0")
    for name in names:
        lowered = name.lower()
        if ((lowered.startswith("url.") and lowered.endswith(".insteadof"))
                or lowered in {"http.proxy", "https.proxy", "core.sshcommand"}):
            raise ValueError("repository contains unsafe transport configuration")


def resolve_for_test(*, approve: bool, git_path: Path, repository: Path, openssl: Path,
                     staging_root: Path, signing_metadata_path: Path,
                     credential_helper: str, transport: RemoteTransport) -> PushResult:
    if credential_helper not in HELPERS: return PushResult(PushStatus.failed)
    git_path = publication.require_git(git_path); repository = publication.require_bare_repository(repository)
    openssl = preparation.require_openssl(openssl); staging_root = preparation.require_staging(staging_root)
    descriptor = os.open(staging_root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        fcntl.flock(descriptor, fcntl.LOCK_SH)
        source = lifecycle.authenticate_current(staging_root, openssl, signing_metadata_path)
        candidate = publication.validate_repository(git_path, repository)
        parents = publication.single_line(publication.git(git_path, repository,
            ["rev-list", "--parents", "-n", "1", candidate]), "candidate parents").split()
        if len(parents) != 2 or parents[0] != candidate: return PushResult(PushStatus.failed)
        parent = parents[1]
        publication.verify_commit(git_path, repository, parent, candidate,
                                  source.manifest_bytes, source.signature_path.read_bytes())
        local_policy(git_path, repository)
        base = dict(generation=source.value["generation"], intake_status=source.value["status"],
            signing_key_id=source.signing_key_id, bundle_key_id=source.value["bundle_encryption_key_id"],
            manifest_sha256=source.manifest_sha256, parent_commit=parent, candidate_commit=candidate)
        try: remote_output = transport.query_main()
        except Exception: return PushResult(PushStatus.query_failed, **base)
        try: remote = parse_remote_main(remote_output)
        except Exception: return PushResult(PushStatus.remote_invalid, **base)
        if remote == candidate: return PushResult(PushStatus.already_published, **base)
        if remote != parent: return PushResult(PushStatus.remote_conflict, **base)
        if not approve: return PushResult(PushStatus.proposed, **base)
        try: pushed = transport.push(parent, candidate)
        except Exception: return PushResult(PushStatus.push_rejected, **base)
        if not pushed: return PushResult(PushStatus.push_rejected, **base)
        try: confirmed = parse_remote_main(transport.query_main())
        except Exception: return PushResult(PushStatus.pushed_confirmation_uncertain, **base)
        if confirmed != candidate: return PushResult(PushStatus.pushed_confirmation_uncertain, **base)
        return PushResult(PushStatus.published, **base)
    except Exception:
        return PushResult(PushStatus.failed)
    finally:
        try: fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally: os.close(descriptor)


def resolve(*, approve: bool, git_path: Path, repository: Path, openssl: Path,
            staging_root: Path, signing_metadata_path: Path, credential_helper: str) -> PushResult:
    try:
        transport = GitRemoteTransport(git_path, repository, credential_helper)
    except Exception:
        return PushResult(PushStatus.failed)
    return resolve_for_test(approve=approve, git_path=git_path, repository=repository,
        openssl=openssl, staging_root=staging_root, signing_metadata_path=signing_metadata_path,
        credential_helper=credential_helper, transport=transport)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--approve", action="store_true")
    parser.add_argument("--git", required=True, type=Path); parser.add_argument("--repository", required=True, type=Path)
    parser.add_argument("--openssl", required=True, type=Path); parser.add_argument("--staging-root", required=True, type=Path)
    parser.add_argument("--signing-metadata", required=True, type=Path)
    parser.add_argument("--credential-helper", required=True, choices=sorted(HELPERS)); args = parser.parse_args(argv)
    result = resolve(approve=args.approve, git_path=args.git, repository=args.repository,
        openssl=args.openssl, staging_root=args.staging_root,
        signing_metadata_path=args.signing_metadata, credential_helper=args.credential_helper)
    print(f"status: {result.status.value}")
    if result.generation:
        print(f"generation: {result.generation}"); print(f"intake status: {result.intake_status}")
        print(f"signing key ID: {result.signing_key_id}"); print(f"bundle key ID: {result.bundle_key_id}")
        print(f"manifest SHA-256: {result.manifest_sha256}"); print(f"parent commit: {result.parent_commit}")
        print(f"candidate commit: {result.candidate_commit}")
    return 0 if result.status in {PushStatus.proposed, PushStatus.published, PushStatus.already_published} else 1


if __name__ == "__main__": raise SystemExit(main(sys.argv[1:]))
