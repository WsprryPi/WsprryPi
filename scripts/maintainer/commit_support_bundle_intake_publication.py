#!/usr/bin/env python3
"""Commit an authenticated intake pair to a dedicated bare publication repo."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
import fcntl
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
from typing import Callable

import manage_support_bundle_intake_manifest as lifecycle
import prepare_support_bundle_intake_manifest as preparation


EXPECTED_REMOTE = "https://github.com/WsprryPi/support-intake.git"
TARGETS = ("wsprrypi/intake.json", "wsprrypi/intake.json.sig")
OBJECT_ID = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")


class PublicationError(RuntimeError):
    pass


class PublicationRollbackError(PublicationError):
    pass


class PublicationStatus(Enum):
    proposed = "proposed"
    committed = "committed"


@dataclass(frozen=True)
class PublicationResult:
    status: PublicationStatus
    generation: int
    intake_status: str
    signing_key_id: str
    bundle_key_id: str
    manifest_sha256: str
    previous_commit: str
    publication_commit: str | None


def require_git(path: Path) -> Path:
    if path != Path("/usr/bin/git") or not path.is_absolute():
        raise PublicationError("Git executable must be exact /usr/bin/git")
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode) or info.st_uid != 0
            or info.st_mode & 0o022 or not os.access(path, os.X_OK)):
        raise PublicationError("Git executable is unsafe or unavailable")
    return path.resolve(strict=True)


def require_bare_repository(path: Path) -> Path:
    if not path.is_absolute():
        raise PublicationError("publication repository must be absolute")
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode)
            or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700):
        raise PublicationError("publication repository must be owner-controlled mode 0700")
    return path.resolve(strict=True)


def environment(repository: Path, temporary_index: Path | None = None) -> dict[str, str]:
    allowed = {key: value for key, value in os.environ.items()
               if key in {"PATH", "LANG", "LC_ALL", "TZ", "TMPDIR"}}
    allowed.update({"GIT_DIR": str(repository), "GIT_CONFIG_NOSYSTEM": "1",
                    "GIT_CONFIG_SYSTEM": "/dev/null", "GIT_CONFIG_GLOBAL": "/dev/null",
                    "GIT_ATTR_NOSYSTEM": "1", "GIT_TERMINAL_PROMPT": "0",
                    "GIT_NO_REPLACE_OBJECTS": "1",
                    "GIT_AUTHOR_NAME": "WsprryPi Support Intake",
                    "GIT_AUTHOR_EMAIL": "support-intake@invalid",
                    "GIT_COMMITTER_NAME": "WsprryPi Support Intake",
                    "GIT_COMMITTER_EMAIL": "support-intake@invalid"})
    if temporary_index is not None:
        allowed["GIT_INDEX_FILE"] = str(temporary_index)
    return allowed


def git(git_path: Path, repository: Path, arguments: list[str], *, input_bytes: bytes | None = None,
        temporary_index: Path | None = None, maximum: int = 8192) -> bytes:
    completed = subprocess.run(
        [str(git_path), "-c", "core.hooksPath=/dev/null", *arguments],
        stdin=None if input_bytes is not None else subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, input=input_bytes,
        env=environment(repository, temporary_index), check=False, timeout=30)
    if completed.returncode != 0 or len(completed.stdout) > maximum:
        raise PublicationError("controlled Git operation failed")
    return completed.stdout


def single_line(value: bytes, description: str) -> str:
    try:
        text = value.decode("ascii")
    except UnicodeError as error:
        raise PublicationError(f"{description} was not ASCII") from error
    if not text.endswith("\n") or "\n" in text[:-1]:
        raise PublicationError(f"{description} was not one line")
    return text[:-1]


def validate_repository(git_path: Path, repository: Path) -> str:
    if single_line(git(git_path, repository, ["rev-parse", "--is-bare-repository"]), "bare status") != "true":
        raise PublicationError("publication repository is not bare")
    if single_line(git(git_path, repository, ["symbolic-ref", "HEAD"]), "HEAD") != "refs/heads/main":
        raise PublicationError("publication repository HEAD is not main")
    remotes = git(git_path, repository, ["remote"]).decode("ascii").splitlines()
    if remotes != ["origin"]:
        raise PublicationError("publication repository must contain only origin")
    remote_urls = git(git_path, repository, ["remote", "get-url", "--all", "origin"]).decode("ascii").splitlines()
    if remote_urls != [EXPECTED_REMOTE]:
        raise PublicationError("publication repository origin is unexpected")
    push_urls = git(git_path, repository,
                    ["remote", "get-url", "--all", "--push", "origin"]).decode("ascii").splitlines()
    if push_urls != [EXPECTED_REMOTE]:
        raise PublicationError("publication repository push destination is unexpected")
    refs = git(git_path, repository, ["for-each-ref", "--format=%(refname)"]).decode("ascii").splitlines()
    if refs != ["refs/heads/main"]:
        raise PublicationError("publication repository contains unexpected refs")
    for unsafe in (repository / "objects/info/alternates", repository / "shallow",
                   repository / "info/grafts", repository / "refs/replace"):
        if unsafe.exists() or unsafe.is_symlink():
            raise PublicationError("publication repository contains unsafe object indirection")
    current = single_line(git(git_path, repository, ["rev-parse", "refs/heads/main^{commit}"]), "main commit")
    if not OBJECT_ID.fullmatch(current):
        raise PublicationError("publication repository main is invalid")
    return current


def object_id(output: bytes) -> str:
    value = single_line(output, "object ID")
    if not OBJECT_ID.fullmatch(value):
        raise PublicationError("Git returned an invalid object ID")
    return value


def verify_commit(git_path: Path, repository: Path, old: str, new: str,
                  manifest: bytes, signature: bytes) -> None:
    parent_line = single_line(git(git_path, repository, ["rev-list", "--parents", "-n", "1", new]), "commit parents")
    if parent_line.split() != [new, old]:
        raise PublicationError("publication commit parent is invalid")
    changed = git(git_path, repository,
                  ["diff-tree", "--no-commit-id", "--name-only", "-r", old, new]).decode("utf-8").splitlines()
    if changed != list(TARGETS):
        raise PublicationError("publication commit changed unexpected paths")
    committed_manifest = git(git_path, repository, ["show", f"{new}:{TARGETS[0]}"],
                             maximum=preparation.MAX_MANIFEST_BYTES)
    committed_signature = git(git_path, repository, ["show", f"{new}:{TARGETS[1]}"],
                              maximum=preparation.MAX_ENVELOPE_BYTES)
    if committed_manifest != manifest or committed_signature != signature:
        raise PublicationError("publication commit bytes do not match authenticated source")
    current = single_line(git(git_path, repository,
                              ["rev-parse", "refs/heads/main^{commit}"]), "verified main")
    if current != new:
        raise PublicationError("publication branch advanced during verification")


def commit_publication(*, approve: bool, git_path: Path, repository: Path, openssl: Path,
                       staging_root: Path, signing_metadata_path: Path,
                       _before_update: Callable[[], None] | None = None) -> PublicationResult:
    git_path = require_git(git_path)
    repository = require_bare_repository(repository)
    openssl = preparation.require_openssl(openssl)
    staging_root = preparation.require_staging(staging_root)
    descriptor = os.open(staging_root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        fcntl.flock(descriptor, fcntl.LOCK_SH)
        return _commit_publication_locked(approve=approve, git_path=git_path,
            repository=repository, openssl=openssl, staging_root=staging_root,
            signing_metadata_path=signing_metadata_path, _before_update=_before_update)
    finally:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally:
            os.close(descriptor)


def _commit_publication_locked(*, approve: bool, git_path: Path, repository: Path,
                               openssl: Path, staging_root: Path,
                               signing_metadata_path: Path,
                               _before_update: Callable[[], None] | None) -> PublicationResult:
    current = lifecycle.authenticate_current(staging_root, openssl, signing_metadata_path)
    previous = validate_repository(git_path, repository)
    result = PublicationResult(PublicationStatus.proposed, current.value["generation"],
        current.value["status"], current.signing_key_id,
        current.value["bundle_encryption_key_id"], current.manifest_sha256, previous, None)
    if not approve:
        return result
    signature_bytes = current.signature_path.read_bytes()
    with tempfile.TemporaryDirectory(prefix="wsprrypi-publication-index-") as temporary:
        temporary_index = Path(temporary) / "index"
        git(git_path, repository, ["read-tree", previous], temporary_index=temporary_index)
        manifest_blob = object_id(git(git_path, repository, ["hash-object", "-w", "--stdin"],
                                      input_bytes=current.manifest_bytes))
        signature_blob = object_id(git(git_path, repository, ["hash-object", "-w", "--stdin"],
                                       input_bytes=signature_bytes))
        for target, blob in zip(TARGETS, (manifest_blob, signature_blob)):
            git(git_path, repository, ["update-index", "--add", "--cacheinfo", f"100644,{blob},{target}"],
                temporary_index=temporary_index)
        tree = object_id(git(git_path, repository, ["write-tree"], temporary_index=temporary_index))
        message = (f"Publish WsprryPi intake generation {current.value['generation']}\n\n"
                   f"Status: {current.value['status']}\n"
                   f"Signing key: {current.signing_key_id}\n"
                   f"Bundle key: {current.value['bundle_encryption_key_id']}\n"
                   f"Manifest SHA-256: {current.manifest_sha256}\n").encode("ascii")
        new = object_id(git(git_path, repository, ["commit-tree", tree, "-p", previous], input_bytes=message))
        if _before_update is not None:
            _before_update()
        git(git_path, repository, ["update-ref", "refs/heads/main", new, previous])
        try:
            verify_commit(git_path, repository, previous, new, current.manifest_bytes, signature_bytes)
        except Exception as verification_error:
            try:
                git(git_path, repository, ["update-ref", "refs/heads/main", previous, new])
            except Exception as rollback_error:
                raise PublicationRollbackError("publication verification and ref rollback failed") from rollback_error
            raise PublicationError("publication verification failed and ref was rolled back") from verification_error
    return PublicationResult(PublicationStatus.committed, current.value["generation"],
        current.value["status"], current.signing_key_id,
        current.value["bundle_encryption_key_id"], current.manifest_sha256, previous, new)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--approve", action="store_true")
    parser.add_argument("--git", required=True, type=Path)
    parser.add_argument("--repository", required=True, type=Path)
    parser.add_argument("--openssl", required=True, type=Path)
    parser.add_argument("--staging-root", required=True, type=Path)
    parser.add_argument("--signing-metadata", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    try:
        result = commit_publication(approve=arguments.approve, git_path=arguments.git,
            repository=arguments.repository, openssl=arguments.openssl,
            staging_root=arguments.staging_root, signing_metadata_path=arguments.signing_metadata)
    except (OSError, subprocess.SubprocessError, preparation.PreparationError,
            lifecycle.LifecycleError, PublicationError) as error:
        print(f"publication commit failed: {error}", file=sys.stderr)
        return 1
    print(f"status: {result.status.value}")
    print(f"generation: {result.generation}")
    print(f"intake status: {result.intake_status}")
    print(f"signing key ID: {result.signing_key_id}")
    print(f"bundle key ID: {result.bundle_key_id}")
    print(f"manifest SHA-256: {result.manifest_sha256}")
    print(f"previous commit: {result.previous_commit}")
    if result.publication_commit is not None:
        print(f"publication commit: {result.publication_commit}")
    print(f"target paths: {TARGETS[0]}, {TARGETS[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
