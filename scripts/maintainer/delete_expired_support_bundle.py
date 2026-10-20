#!/usr/bin/env python3
"""Delete one explicitly selected, retention-due processed support bundle."""

from __future__ import annotations

import argparse
import errno
from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum
import fcntl
import importlib.util
import os
from pathlib import Path
import stat
import sys


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "support_bundle_retention_audit", HERE / "audit_support_bundle_retention.py")
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = AUDIT
SPEC.loader.exec_module(AUDIT)
PROCESSOR = AUDIT.PROCESSOR


class DeleteStatus(Enum):
    deleted = "deleted"
    resumed_deleted = "resumed_deleted"
    absent = "absent"
    confirmation_failed = "confirmation_failed"
    invalid_request = "invalid_request"
    retained = "retained"
    unsafe = "unsafe"
    collision = "collision"
    publication_failed = "publication_failed"
    committed_sync_uncertain = "committed_sync_uncertain"
    cleanup_pending = "cleanup_pending"
    deletion_failed = "deletion_failed"


@dataclass(frozen=True)
class DeleteResult:
    status: DeleteStatus
    case_id: str = ""
    artifact_id: str = ""


class DeleteError(Exception):
    def __init__(self, status: DeleteStatus):
        self.status = status


class FileOps:
    """Typed test seam; production uses concrete descriptor-relative operations."""
    def rename(self, source: str, destination: str, *, directory_fd: int) -> None:
        PROCESSOR.FileOps().rename(source, destination, src_dir_fd=directory_fd,
                                   dst_dir_fd=directory_fd)

    def unlink(self, name: str, *, directory_fd: int) -> None:
        os.unlink(name, dir_fd=directory_fd)

    def rmdir(self, name: str, *, directory_fd: int) -> None:
        os.rmdir(name, dir_fd=directory_fd)

    def fsync(self, fd: int) -> None:
        os.fsync(fd)

    def before_unlink(self, name: str, *, directory_fd: int) -> None:
        del name, directory_fd

    def before_tombstone_revalidate(self, name: str, *, directory_fd: int) -> None:
        del name, directory_fd


def _safe_request(case_id: str, artifact_id: str, confirmation: str) -> None:
    if (not isinstance(case_id, str) or not isinstance(artifact_id, str)
            or not isinstance(confirmation, str)
            or not PROCESSOR.INSPECTOR.CASE_ID.fullmatch(case_id)
            or not PROCESSOR.INSPECTOR.ARTIFACT_ID.fullmatch(artifact_id)):
        raise DeleteError(DeleteStatus.invalid_request)
    if confirmation != f"DELETE {case_id} {artifact_id}":
        raise DeleteError(DeleteStatus.confirmation_failed)


def _exists(directory_fd: int, name: str) -> bool:
    try:
        os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
        return True
    except FileNotFoundError:
        return False
    except OSError as error:
        raise DeleteError(DeleteStatus.unsafe) from error


def _open_tombstone(processed_fd: int, name: str) -> int:
    fd = -1
    try:
        fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW,
                     dir_fd=processed_fd)
        info = os.fstat(fd)
        if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid()
                or stat.S_IMODE(info.st_mode) != 0o700):
            raise OSError("unsafe")
        return fd
    except OSError as error:
        if fd >= 0:
            try:
                os.close(fd)
            except OSError:
                pass
        raise DeleteError(DeleteStatus.unsafe) from error


def _inventory(directory_fd: int) -> set[str]:
    try:
        names: set[str] = set()
        with os.scandir(directory_fd) as iterator:
            for entry in iterator:
                names.add(entry.name)
                if len(names) > len(AUDIT.EXPECTED_FILES):
                    raise DeleteError(DeleteStatus.unsafe)
        if not names.issubset(AUDIT.EXPECTED_FILES):
            raise DeleteError(DeleteStatus.unsafe)
        return names
    except DeleteError:
        raise
    except OSError as error:
        raise DeleteError(DeleteStatus.unsafe) from error


def _same_directory(processed_fd: int, tombstone: str, tombstone_fd: int) -> bool:
    try:
        opened = os.fstat(tombstone_fd)
        current = os.stat(tombstone, dir_fd=processed_fd, follow_symlinks=False)
        return (stat.S_ISDIR(opened.st_mode) and stat.S_ISDIR(current.st_mode)
                and (opened.st_dev, opened.st_ino) == (current.st_dev, current.st_ino)
                and opened.st_uid == os.geteuid() and current.st_uid == os.geteuid()
                and stat.S_IMODE(opened.st_mode) == 0o700
                and stat.S_IMODE(current.st_mode) == 0o700)
    except OSError:
        return False


def _record_due(directory_fd: int, case_id: str, artifact_id: str, now) -> dict[str, object]:
    try:
        record = PROCESSOR._read_record(directory_fd)
        if record["case_id"] != case_id or record["artifact_id"] != artifact_id:
            raise ValueError("correlation")
        processed = AUDIT._parse_utc(record["processed_at_utc"])
        review = record["retention_review_at_utc"]
        if record["retention_class"] == "active_case" or not isinstance(review, str):
            raise DeleteError(DeleteStatus.retained)
        review_at = AUDIT._parse_utc(review)
        interval = review_at - processed
        seconds = interval.total_seconds()
        if (record["retention_class"] == "uncorrelated" and seconds != 14 * 86400
                or record["retention_class"] == "resolved_case"
                and (not 30 * 86400 <= seconds <= 90 * 86400 or seconds % 86400 != 0)):
            raise ValueError("policy")
        if now < review_at:
            raise DeleteError(DeleteStatus.retained)
        return record
    except DeleteError:
        raise
    except Exception as error:
        raise DeleteError(DeleteStatus.unsafe) from error


def _validate_remaining(directory_fd: int, names: set[str], case_id: str,
                        artifact_id: str, now) -> None:
    record = _record_due(directory_fd, case_id, artifact_id, now)
    if "encrypted-support-bundle.age" in names:
        try:
            fd = AUDIT._open_regular(directory_fd, "encrypted-support-bundle.age")
        except Exception as error:
            raise DeleteError(DeleteStatus.unsafe) from error
        try:
            try:
                actual = PROCESSOR._hash_fd(fd)
            except Exception as error:
                raise DeleteError(DeleteStatus.unsafe) from error
        finally:
            os.close(fd)
        if actual != (record["encrypted_size"], record["encrypted_sha256"]):
            raise DeleteError(DeleteStatus.unsafe)
    if "support-bundle.receipt.json" in names:
        try:
            fd = AUDIT._open_regular(directory_fd, "support-bundle.receipt.json")
        except Exception as error:
            raise DeleteError(DeleteStatus.unsafe) from error
        try:
            receipt = PROCESSOR._receipt_from_fd(fd)
        except Exception as error:
            raise DeleteError(DeleteStatus.unsafe) from error
        finally:
            os.close(fd)
        correlated = {"case_id", "artifact_id", "bundle_encryption_key_id", "issue_url",
                      "encrypted_size", "encrypted_sha256"}
        if any(receipt[key] != record[key] for key in correlated):
            raise DeleteError(DeleteStatus.unsafe)


def _unlink_verified(directory_fd: int, name: str, ops: FileOps) -> None:
    try:
        fd = AUDIT._open_regular(directory_fd, name)
    except Exception as error:
        raise DeleteError(DeleteStatus.unsafe) from error
    try:
        before = os.fstat(fd)
        ops.before_unlink(name, directory_fd=directory_fd)
        current = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
        if ((before.st_dev, before.st_ino) != (current.st_dev, current.st_ino)
                or not stat.S_ISREG(current.st_mode) or current.st_uid != os.geteuid()
                or current.st_nlink != 1 or stat.S_IMODE(current.st_mode) != 0o600):
            raise DeleteError(DeleteStatus.unsafe)
        ops.unlink(name, directory_fd=directory_fd)
    except DeleteError:
        raise
    except OSError as error:
        raise DeleteError(DeleteStatus.cleanup_pending) from error
    finally:
        os.close(fd)


def _delete_tombstone(processed_fd: int, tombstone: str, case_id: str,
                      artifact_id: str, now, resumed: bool, ops: FileOps) -> DeleteResult:
    tombstone_fd = _open_tombstone(processed_fd, tombstone)
    try:
        if not _same_directory(processed_fd, tombstone, tombstone_fd):
            raise DeleteError(DeleteStatus.unsafe)
        names = _inventory(tombstone_fd)
        if names and "processing-record.json" not in names:
            raise DeleteError(DeleteStatus.unsafe)
        if names:
            _validate_remaining(tombstone_fd, names, case_id, artifact_id, now)
        for name in ("encrypted-support-bundle.age", "support-bundle.receipt.json"):
            if name in names:
                _unlink_verified(tombstone_fd, name, ops)
        ops.fsync(tombstone_fd)
        if "processing-record.json" in names:
            _unlink_verified(tombstone_fd, "processing-record.json", ops)
        ops.fsync(tombstone_fd)
        ops.before_tombstone_revalidate(tombstone, directory_fd=processed_fd)
        if not _same_directory(processed_fd, tombstone, tombstone_fd):
            raise DeleteError(DeleteStatus.cleanup_pending)
    except DeleteError:
        raise
    except OSError as error:
        raise DeleteError(DeleteStatus.cleanup_pending) from error
    finally:
        os.close(tombstone_fd)
    try:
        ops.rmdir(tombstone, directory_fd=processed_fd)
    except OSError as error:
        raise DeleteError(DeleteStatus.cleanup_pending) from error
    try:
        ops.fsync(processed_fd)
    except OSError as error:
        raise DeleteError(DeleteStatus.committed_sync_uncertain) from error
    return DeleteResult(DeleteStatus.resumed_deleted if resumed else DeleteStatus.deleted,
                        case_id, artifact_id)


def _delete_internal(*, processed: Path, case_id: str, artifact_id: str,
                     now_utc: str, confirmation: str, repository: Path,
                     ops: FileOps) -> DeleteResult:
    processed_fd = -1
    try:
        _safe_request(case_id, artifact_id, confirmation)
        try:
            now = AUDIT._parse_utc(now_utc)
        except Exception as error:
            raise DeleteError(DeleteStatus.invalid_request) from error
        try:
            processed_fd, _ = PROCESSOR._safe_directory(processed, repository)
        except Exception as error:
            raise DeleteError(DeleteStatus.unsafe) from error
        fcntl.flock(processed_fd, fcntl.LOCK_EX)
        source = f"case-{case_id}-{artifact_id}"
        tombstone = f".retiring-{case_id}-{artifact_id}"
        source_exists = _exists(processed_fd, source)
        tombstone_exists = _exists(processed_fd, tombstone)
        if source_exists and tombstone_exists:
            raise DeleteError(DeleteStatus.collision)
        if not source_exists and not tombstone_exists:
            try:
                ops.fsync(processed_fd)
            except OSError as error:
                raise DeleteError(DeleteStatus.committed_sync_uncertain) from error
            return DeleteResult(DeleteStatus.absent, case_id, artifact_id)
        if tombstone_exists:
            return _delete_tombstone(processed_fd, tombstone, case_id, artifact_id,
                                     now, True, ops)
        try:
            entry = AUDIT._audit_case(processed_fd, source, now)
        except Exception as error:
            raise DeleteError(DeleteStatus.unsafe) from error
        if entry.eligibility is not AUDIT.Eligibility.due:
            raise DeleteError(DeleteStatus.retained)
        try:
            ops.rename(source, tombstone, directory_fd=processed_fd)
        except OSError as error:
            status = (DeleteStatus.collision
                      if error.errno in {errno.EEXIST, errno.ENOTEMPTY}
                      else DeleteStatus.publication_failed)
            raise DeleteError(status) from error
        try:
            ops.fsync(processed_fd)
        except OSError as error:
            raise DeleteError(DeleteStatus.committed_sync_uncertain) from error
        return _delete_tombstone(processed_fd, tombstone, case_id, artifact_id,
                                 now, False, ops)
    except DeleteError as error:
        return DeleteResult(error.status)
    except Exception:
        return DeleteResult(DeleteStatus.deletion_failed)
    finally:
        if processed_fd >= 0:
            os.close(processed_fd)


def delete_expired_support_bundle(*, processed: Path, case_id: str, artifact_id: str,
                                  confirmation: str, repository: Path) -> DeleteResult:
    now_utc = datetime.now(timezone.utc).replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")
    return _delete_internal(processed=processed, case_id=case_id, artifact_id=artifact_id,
                            now_utc=now_utc, confirmation=confirmation,
                            repository=repository, ops=FileOps())


def delete_expired_support_bundle_for_test(**kwargs) -> DeleteResult:
    return _delete_internal(**kwargs)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Delete one retention-due support bundle")
    parser.add_argument("--processed", required=True, type=Path)
    parser.add_argument("--case-id", required=True)
    parser.add_argument("--artifact-id", required=True)
    parser.add_argument("--confirm", required=True)
    arguments = parser.parse_args(argv)
    result = delete_expired_support_bundle(
        processed=arguments.processed, case_id=arguments.case_id,
        artifact_id=arguments.artifact_id, confirmation=arguments.confirm,
        repository=Path(__file__).resolve().parents[2])
    print(f"status: {result.status.value}")
    if result.case_id:
        print(f"case ID: {result.case_id}")
        print(f"artifact ID: {result.artifact_id}")
    return 0 if result.status in {
        DeleteStatus.deleted, DeleteStatus.resumed_deleted, DeleteStatus.absent} else 1


if __name__ == "__main__":
    raise SystemExit(main())
