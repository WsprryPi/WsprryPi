#!/usr/bin/env python3
"""Read-only retention eligibility audit for processed support bundles."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from enum import Enum
import fcntl
import importlib.util
import os
from pathlib import Path
import re
import stat
import sys
from typing import Callable


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "support_bundle_processor", HERE / "process_received_support_bundle.py")
assert SPEC and SPEC.loader
PROCESSOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PROCESSOR
SPEC.loader.exec_module(PROCESSOR)

CASE_DIRECTORY = re.compile(
    r"^case-([A-Z0-9]{4}(?:-[A-Z0-9]{4}){2})-([a-f0-9]{32})$")
EXPECTED_FILES = {
    "encrypted-support-bundle.age",
    "support-bundle.receipt.json",
    "processing-record.json",
}
MAXIMUM_CASES = 4096


class AuditStatus(Enum):
    complete = "complete"
    unsafe_processed_directory = "unsafe_processed_directory"
    unsafe_case = "unsafe_case"
    invalid_time = "invalid_time"
    audit_failed = "audit_failed"


class Eligibility(Enum):
    retained = "retained"
    due = "due"


@dataclass(frozen=True)
class AuditEntry:
    case_id: str
    artifact_id: str
    eligibility: Eligibility


@dataclass(frozen=True)
class AuditResult:
    status: AuditStatus
    entries: tuple[AuditEntry, ...] = ()


class AuditError(Exception):
    def __init__(self, status: AuditStatus):
        self.status = status


def _parse_utc(value: str) -> datetime:
    if not PROCESSOR.UTC.fullmatch(value):
        raise AuditError(AuditStatus.invalid_time)
    try:
        parsed = datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    except ValueError as error:
        raise AuditError(AuditStatus.invalid_time) from error
    if parsed.strftime("%Y-%m-%dT%H:%M:%SZ") != value:
        raise AuditError(AuditStatus.invalid_time)
    return parsed


def _safe_regular(case_fd: int, name: str) -> None:
    try:
        info = os.stat(name, dir_fd=case_fd, follow_symlinks=False)
    except OSError as error:
        raise AuditError(AuditStatus.unsafe_case) from error
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
            or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600):
        raise AuditError(AuditStatus.unsafe_case)


def _open_regular(case_fd: int, name: str) -> int:
    fd = -1
    try:
        fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=case_fd)
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600):
            raise OSError("unsafe")
        return fd
    except OSError as error:
        if fd >= 0:
            try:
                os.close(fd)
            except OSError:
                pass
        raise AuditError(AuditStatus.unsafe_case) from error


def _audit_case(processed_fd: int, name: str, now: datetime) -> AuditEntry:
    match = CASE_DIRECTORY.fullmatch(name)
    if not match:
        raise AuditError(AuditStatus.unsafe_case)
    case_id, artifact_id = match.groups()
    try:
        case_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW,
                          dir_fd=processed_fd)
    except OSError as error:
        raise AuditError(AuditStatus.unsafe_case) from error
    try:
        info = os.fstat(case_fd)
        if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid()
                or stat.S_IMODE(info.st_mode) != 0o700):
            raise AuditError(AuditStatus.unsafe_case)
        try:
            inventory: set[str] = set()
            with os.scandir(case_fd) as iterator:
                for entry in iterator:
                    inventory.add(entry.name)
                    if len(inventory) > len(EXPECTED_FILES):
                        raise AuditError(AuditStatus.unsafe_case)
        except AuditError:
            raise
        except OSError as error:
            raise AuditError(AuditStatus.unsafe_case) from error
        if inventory != EXPECTED_FILES:
            raise AuditError(AuditStatus.unsafe_case)
        for filename in EXPECTED_FILES:
            _safe_regular(case_fd, filename)
        try:
            record = PROCESSOR._read_record(case_fd)
        except Exception as error:
            raise AuditError(AuditStatus.unsafe_case) from error
        if record["case_id"] != case_id or record["artifact_id"] != artifact_id:
            raise AuditError(AuditStatus.unsafe_case)
        ciphertext_fd = _open_regular(case_fd, "encrypted-support-bundle.age")
        receipt_fd = _open_regular(case_fd, "support-bundle.receipt.json")
        try:
            encrypted_size, encrypted_sha256 = PROCESSOR._hash_fd(ciphertext_fd)
            receipt = PROCESSOR._receipt_from_fd(receipt_fd)
        except Exception as error:
            raise AuditError(AuditStatus.unsafe_case) from error
        finally:
            os.close(ciphertext_fd)
            os.close(receipt_fd)
        if (record["encrypted_size"], record["encrypted_sha256"]) != (
                encrypted_size, encrypted_sha256):
            raise AuditError(AuditStatus.unsafe_case)
        correlated = {
            "case_id", "artifact_id", "bundle_encryption_key_id", "issue_url",
            "encrypted_size", "encrypted_sha256",
        }
        if any(receipt[key] != record[key] for key in correlated):
            raise AuditError(AuditStatus.unsafe_case)
        review = record["retention_review_at_utc"]
        try:
            processed_at = _parse_utc(record["processed_at_utc"])
        except AuditError as error:
            raise AuditError(AuditStatus.unsafe_case) from error
        if record["retention_class"] == "active_case":
            if review is not None:
                raise AuditError(AuditStatus.unsafe_case)
            eligibility = Eligibility.retained
        else:
            if not isinstance(review, str):
                raise AuditError(AuditStatus.unsafe_case)
            try:
                review_at = _parse_utc(review)
            except AuditError as error:
                raise AuditError(AuditStatus.unsafe_case) from error
            interval = review_at - processed_at
            if (record["retention_class"] == "uncorrelated" and interval != timedelta(days=14)
                    or record["retention_class"] == "resolved_case"
                    and (not timedelta(days=30) <= interval <= timedelta(days=90)
                         or interval.total_seconds() % 86400 != 0)):
                raise AuditError(AuditStatus.unsafe_case)
            eligibility = Eligibility.due if now >= review_at else Eligibility.retained
        return AuditEntry(case_id, artifact_id, eligibility)
    finally:
        os.close(case_fd)


def _audit_retention(processed: Path, now_utc: str, repository: Path,
                     after_lock: Callable[[int], None] | None) -> AuditResult:
    processed_fd = -1
    try:
        now = _parse_utc(now_utc)
        try:
            processed_fd, _ = PROCESSOR._safe_directory(processed, repository)
        except Exception as error:
            raise AuditError(AuditStatus.unsafe_processed_directory) from error
        fcntl.flock(processed_fd, fcntl.LOCK_SH)
        if after_lock is not None:
            after_lock(processed_fd)
        try:
            names: list[str] = []
            with os.scandir(processed_fd) as iterator:
                for entry in iterator:
                    names.append(entry.name)
                    if len(names) > MAXIMUM_CASES:
                        raise AuditError(AuditStatus.unsafe_case)
            names.sort()
        except AuditError:
            raise
        except OSError as error:
            raise AuditError(AuditStatus.unsafe_processed_directory) from error
        entries = tuple(_audit_case(processed_fd, name, now) for name in names)
        return AuditResult(AuditStatus.complete, entries)
    except AuditError as error:
        return AuditResult(error.status)
    except Exception:
        return AuditResult(AuditStatus.audit_failed)
    finally:
        if processed_fd >= 0:
            os.close(processed_fd)


def audit_retention(processed: Path, now_utc: str, repository: Path) -> AuditResult:
    return _audit_retention(processed, now_utc, repository, None)


def audit_retention_for_test(processed: Path, now_utc: str, repository: Path,
                             after_lock: Callable[[int], None]) -> AuditResult:
    """Typed in-process observation seam; production never accepts callbacks."""
    return _audit_retention(processed, now_utc, repository, after_lock)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Audit processed support-bundle retention")
    parser.add_argument("--processed", required=True, type=Path)
    parser.add_argument("--now-utc", required=True)
    arguments = parser.parse_args(argv)
    result = audit_retention(arguments.processed, arguments.now_utc,
                             Path(__file__).resolve().parents[2])
    print(f"status: {result.status.value}")
    if result.status is AuditStatus.complete:
        due = sum(entry.eligibility is Eligibility.due for entry in result.entries)
        print(f"cases: {len(result.entries)}")
        print(f"due: {due}")
        print(f"retained: {len(result.entries) - due}")
        for entry in result.entries:
            print(f"{entry.eligibility.value}: {entry.case_id} {entry.artifact_id}")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
