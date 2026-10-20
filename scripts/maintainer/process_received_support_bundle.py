#!/usr/bin/env python3
"""Promote an inspected support bundle into private maintainer storage."""

from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from enum import Enum
import errno
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import sys
import uuid


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "support_bundle_intake_inspector", HERE / "inspect_received_support_bundle.py")
assert SPEC and SPEC.loader
INSPECTOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = INSPECTOR
SPEC.loader.exec_module(INSPECTOR)

RECORD_KEYS = {
    "schema_version", "project_id", "case_id", "artifact_id", "bundle_encryption_key_id",
    "issue_url", "encrypted_size", "encrypted_sha256", "received_filename",
    "processed_at_utc", "lifecycle_state", "retention_class", "retention_review_at_utc",
}
RETENTION = {"uncorrelated", "active_case", "resolved_case"}
UTC = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")


class ProcessingStatus(Enum):
    processed = "processed"
    unchanged = "unchanged"
    processed_cleanup_pending = "processed_cleanup_pending"
    inspection_failed = "inspection_failed"
    unsafe_directory = "unsafe_directory"
    unsafe_input = "unsafe_input"
    output_collision = "output_collision"
    processing_record_invalid = "processing_record_invalid"
    copy_failed = "copy_failed"
    copy_mismatch = "copy_mismatch"
    publication_failed = "publication_failed"
    committed_sync_uncertain = "committed_sync_uncertain"
    incoming_cleanup_failed = "incoming_cleanup_failed"
    processing_failed = "processing_failed"


@dataclass(frozen=True)
class ProcessingResult:
    status: ProcessingStatus
    case_id: str = ""
    artifact_id: str = ""


class ProcessingError(Exception):
    def __init__(self, status: ProcessingStatus):
        self.status = status


class FileOps:
    """Typed test seam; production uses this concrete implementation."""
    def write(self, fd: int, data: bytes) -> int:
        return os.write(fd, data)

    def fsync(self, fd: int) -> None:
        os.fsync(fd)

    def close(self, fd: int) -> None:
        os.close(fd)

    def rename(self, source: str, destination: str, *, src_dir_fd: int, dst_dir_fd: int) -> None:
        libc = ctypes.CDLL(None, use_errno=True)
        encoded_source, encoded_destination = os.fsencode(source), os.fsencode(destination)
        if sys.platform == "darwin" and hasattr(libc, "renameatx_np"):
            result = libc.renameatx_np(src_dir_fd, encoded_source, dst_dir_fd,
                                       encoded_destination, 0x00000004)  # RENAME_EXCL
        elif sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
            result = libc.renameat2(src_dir_fd, encoded_source, dst_dir_fd,
                                    encoded_destination, 0x00000001)  # RENAME_NOREPLACE
        else:
            raise OSError(errno.ENOTSUP, "exclusive rename unavailable")
        if result != 0:
            number = ctypes.get_errno()
            raise OSError(number, os.strerror(number))

    def link(self, source: str, destination: str, *, dir_fd: int) -> None:
        os.link(source, destination, src_dir_fd=dir_fd, dst_dir_fd=dir_fd,
                follow_symlinks=False)

    def unlink(self, name: str, *, dir_fd: int) -> None:
        os.unlink(name, dir_fd=dir_fd)

    def before_partial_revalidate(self, name: str, *, dir_fd: int) -> None:
        del name, dir_fd


def _strict_json(raw: bytes) -> object:
    def pairs(items: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate")
            result[key] = value
        return result
    return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs)


def _timestamp(now: datetime) -> str:
    if now.tzinfo is None or now.utcoffset() != timedelta(0) or now.microsecond:
        raise ProcessingError(ProcessingStatus.processing_record_invalid)
    return now.strftime("%Y-%m-%dT%H:%M:%SZ")


def _retention(kind: str, now: datetime, resolved_days: int) -> str | None:
    if kind not in RETENTION or not 30 <= resolved_days <= 90:
        raise ProcessingError(ProcessingStatus.processing_record_invalid)
    if kind == "active_case":
        return None
    days = 14 if kind == "uncorrelated" else resolved_days
    return _timestamp(now + timedelta(days=days))


def _safe_directory(path: Path, repository: Path) -> tuple[int, os.stat_result]:
    if not path.is_absolute():
        raise ProcessingError(ProcessingStatus.unsafe_directory)
    try:
        info = path.lstat()
        resolved = path.resolve(strict=True)
        repo = repository.resolve(strict=True)
        if (stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode)
                or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700
                or resolved == repo or repo in resolved.parents):
            raise ProcessingError(ProcessingStatus.unsafe_directory)
        fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW)
        current = os.fstat(fd)
        if (current.st_dev, current.st_ino) != (info.st_dev, info.st_ino):
            os.close(fd)
            raise ProcessingError(ProcessingStatus.unsafe_directory)
        return fd, current
    except ProcessingError:
        raise
    except OSError as error:
        raise ProcessingError(ProcessingStatus.unsafe_directory) from error


def _open_incoming(path: Path, incoming: Path, incoming_fd: int) -> tuple[int, os.stat_result]:
    if not path.is_absolute() or path.parent.resolve(strict=True) != incoming.resolve(strict=True):
        raise ProcessingError(ProcessingStatus.unsafe_input)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=incoming_fd)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_nlink != 1:
            os.close(fd)
            raise ProcessingError(ProcessingStatus.unsafe_input)
        return fd, info
    except ProcessingError:
        raise
    except OSError as error:
        raise ProcessingError(ProcessingStatus.unsafe_input) from error


def _hash_fd(fd: int) -> tuple[int, str]:
    os.lseek(fd, 0, os.SEEK_SET)
    digest, size = hashlib.sha256(), 0
    while block := os.read(fd, 65536):
        size += len(block)
        digest.update(block)
    os.lseek(fd, 0, os.SEEK_SET)
    return size, digest.hexdigest()


def _receipt_from_fd(fd: int) -> dict[str, object]:
    os.lseek(fd, 0, os.SEEK_SET)
    raw = bytearray()
    while block := os.read(fd, 4096):
        raw.extend(block)
        if len(raw) > INSPECTOR.MAX_RECEIPT:
            raise ProcessingError(ProcessingStatus.processing_record_invalid)
    os.lseek(fd, 0, os.SEEK_SET)
    try:
        return INSPECTOR.parse_receipt(bytes(raw))
    except INSPECTOR.InspectionError as error:
        raise ProcessingError(ProcessingStatus.processing_record_invalid) from error


def _record_valid(value: object) -> bool:
    return (isinstance(value, dict) and set(value) == RECORD_KEYS
            and value["schema_version"] == 1 and value["project_id"] == "wsprrypi"
            and isinstance(value["case_id"], str) and INSPECTOR.CASE_ID.fullmatch(value["case_id"])
            and isinstance(value["artifact_id"], str) and INSPECTOR.ARTIFACT_ID.fullmatch(value["artifact_id"])
            and isinstance(value["bundle_encryption_key_id"], str)
            and INSPECTOR.KEY_ID.fullmatch(value["bundle_encryption_key_id"])
            and (value["issue_url"] is None or isinstance(value["issue_url"], str)
                 and INSPECTOR.ISSUE_URL.fullmatch(value["issue_url"]))
            and type(value["encrypted_size"]) is int and value["encrypted_size"] > 0
            and isinstance(value["encrypted_sha256"], str)
            and INSPECTOR.DIGEST.fullmatch(value["encrypted_sha256"])
            and (value["received_filename"] is None or INSPECTOR.safe_basename(value["received_filename"], ".age"))
            and isinstance(value["processed_at_utc"], str) and UTC.fullmatch(value["processed_at_utc"])
            and value["lifecycle_state"] == "processed"
            and value["retention_class"] in RETENTION
            and (value["retention_review_at_utc"] is None or isinstance(value["retention_review_at_utc"], str)
                 and UTC.fullmatch(value["retention_review_at_utc"])))


def _write_all(fd: int, data: bytes, ops: FileOps) -> None:
    offset = 0
    while offset < len(data):
        count = ops.write(fd, data[offset:])
        if count <= 0:
            raise OSError("write")
        offset += count


def _copy_fd(source: int, directory_fd: int, name: str, expected: tuple[int, str], ops: FileOps) -> None:
    fd = -1
    temporary = f".{name}.partial-{uuid.uuid4().hex}"
    published = False
    digest, size = hashlib.sha256(), 0
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                     0o600, dir_fd=directory_fd)
        os.lseek(source, 0, os.SEEK_SET)
        while block := os.read(source, 65536):
            _write_all(fd, block, ops)
            size += len(block)
            digest.update(block)
        ops.fsync(fd)
        before = os.fstat(fd)
        path_info = os.stat(temporary, dir_fd=directory_fd, follow_symlinks=False)
        if ((before.st_dev, before.st_ino) != (path_info.st_dev, path_info.st_ino)
                or before.st_nlink != 1 or not stat.S_ISREG(before.st_mode)
                or stat.S_IMODE(before.st_mode) != 0o600 or (size, digest.hexdigest()) != expected):
            raise ProcessingError(ProcessingStatus.copy_mismatch)
        ops.close(fd)
        fd = -1
        try:
            ops.link(temporary, name, dir_fd=directory_fd)
            ops.unlink(temporary, dir_fd=directory_fd)
            published = True
        except OSError as error:
            raise ProcessingError(ProcessingStatus.publication_failed) from error
    except ProcessingError:
        raise
    except OSError as error:
        raise ProcessingError(ProcessingStatus.copy_failed) from error
    finally:
        close_error: OSError | None = None
        if fd >= 0:
            try:
                ops.close(fd)
            except OSError as error:
                close_error = error
        if not published:
            try:
                os.unlink(temporary, dir_fd=directory_fd)
            except OSError:
                pass
        if close_error is not None:
            raise ProcessingError(ProcessingStatus.copy_failed) from close_error


def _read_record(case_fd: int) -> dict[str, object]:
    try:
        fd = os.open("processing-record.json", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW,
                     dir_fd=case_fd)
    except FileNotFoundError:
        raise
    except OSError as error:
        raise ProcessingError(ProcessingStatus.processing_record_invalid) from error
    try:
        try:
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                    or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600
                    or info.st_size > 16384):
                raise ValueError("unsafe record")
            raw = bytearray()
            while block := os.read(fd, 4096):
                raw.extend(block)
                if len(raw) > 16384:
                    raise ValueError("large")
        finally:
            os.close(fd)
        value = _strict_json(bytes(raw))
        if not _record_valid(value):
            raise ValueError("record")
        return value
    except Exception as error:
        raise ProcessingError(ProcessingStatus.processing_record_invalid) from error


def _existing_matches(processed_fd: int, case_name: str, record: dict[str, object],
                      cipher_expected: tuple[int, str], receipt_expected: tuple[int, str]) -> bool:
    try:
        case_fd = os.open(case_name, os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW,
                          dir_fd=processed_fd)
    except OSError:
        return False
    try:
        case_info = os.fstat(case_fd)
        if (not stat.S_ISDIR(case_info.st_mode) or case_info.st_uid != os.geteuid()
                or stat.S_IMODE(case_info.st_mode) != 0o700):
            return False
        existing = _read_record(case_fd)
        stable = RECORD_KEYS - {"processed_at_utc", "retention_review_at_utc"}
        if any(existing[key] != record[key] for key in stable):
            return False
        for name, expected in (("encrypted-support-bundle.age", cipher_expected),
                               ("support-bundle.receipt.json", receipt_expected)):
            fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=case_fd)
            try:
                info = os.fstat(fd)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                        or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600
                        or _hash_fd(fd) != expected):
                    return False
            finally:
                os.close(fd)
        return True
    except ProcessingError:
        raise
    except OSError:
        return False
    finally:
        os.close(case_fd)


def _cleanup_inputs(incoming_fd: int, inputs: list[tuple[str, os.stat_result]], ops: FileOps) -> bool:
    ok = True
    for name, expected in inputs:
        try:
            current = os.stat(name, dir_fd=incoming_fd, follow_symlinks=False)
            if (current.st_dev, current.st_ino) != (expected.st_dev, expected.st_ino):
                ok = False
                continue
            ops.unlink(name, dir_fd=incoming_fd)
        except OSError:
            ok = False
    try:
        ops.fsync(incoming_fd)
    except OSError:
        ok = False
    return ok


def _remove_partial_by_identity(processed_fd: int, identity: tuple[int, int]) -> None:
    """Best-effort cleanup of only the partial directory inode we created."""
    try:
        names = os.listdir(processed_fd)
    except OSError:
        return
    for name in names:
        try:
            info = os.stat(name, dir_fd=processed_fd, follow_symlinks=False)
            if (info.st_dev, info.st_ino) != identity or not stat.S_ISDIR(info.st_mode):
                continue
            directory_fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                   dir_fd=processed_fd)
            try:
                for child in ("encrypted-support-bundle.age", "support-bundle.receipt.json",
                              "processing-record.json", ".record-source"):
                    try:
                        os.unlink(child, dir_fd=directory_fd)
                    except OSError:
                        pass
            finally:
                os.close(directory_fd)
            try:
                os.rmdir(name, dir_fd=processed_fd)
            except OSError:
                pass
            return
        except OSError:
            continue


def process_with_test_seam(*, incoming: Path, processed: Path, ciphertext: Path,
                           receipt_path: Path, identity: Path, work_directory: Path,
                           retention_class: str, resolved_retention_days: int,
                           repository: Path, now_provider, inspector, ops: FileOps) -> ProcessingResult:
    incoming_fd = processed_fd = cipher_fd = receipt_fd = partial_fd = -1
    partial_name = ""
    partial_identity: tuple[int, int] | None = None
    committed = False
    safe = ProcessingResult(ProcessingStatus.processing_failed)
    try:
        incoming_fd, incoming_info = _safe_directory(incoming, repository)
        processed_fd, processed_info = _safe_directory(processed, repository)
        if (incoming_info.st_dev, incoming_info.st_ino) == (processed_info.st_dev, processed_info.st_ino):
            raise ProcessingError(ProcessingStatus.unsafe_directory)
        cipher_fd, cipher_info = _open_incoming(ciphertext, incoming, incoming_fd)
        receipt_fd, receipt_info = _open_incoming(receipt_path, incoming, incoming_fd)
        receipt = _receipt_from_fd(receipt_fd)
        cipher_expected = _hash_fd(cipher_fd)
        receipt_expected = _hash_fd(receipt_fd)
        if cipher_expected != (receipt["encrypted_size"], receipt["encrypted_sha256"]):
            raise ProcessingError(ProcessingStatus.copy_mismatch)
        now = now_provider()
        processed_at = _timestamp(now)
        record = {
            "schema_version": 1, "project_id": "wsprrypi", "case_id": receipt["case_id"],
            "artifact_id": receipt["artifact_id"],
            "bundle_encryption_key_id": receipt["bundle_encryption_key_id"],
            "issue_url": receipt["issue_url"], "encrypted_size": cipher_expected[0],
            "encrypted_sha256": cipher_expected[1],
            # Dropbox may place uploader identity in its stored name. Without a
            # provenance signal it cannot be classified safe, so omit it.
            "received_filename": None,
            "processed_at_utc": processed_at, "lifecycle_state": "processed",
            "retention_class": retention_class,
            "retention_review_at_utc": _retention(retention_class, now, resolved_retention_days),
        }
        if not _record_valid(record):
            raise ProcessingError(ProcessingStatus.processing_record_invalid)
        case_name = f"case-{receipt['case_id']}-{receipt['artifact_id']}"
        safe = ProcessingResult(ProcessingStatus.processing_failed,
                                str(receipt["case_id"]), str(receipt["artifact_id"]))
        fcntl.flock(processed_fd, fcntl.LOCK_EX)
        if _existing_matches(processed_fd, case_name, record, cipher_expected, receipt_expected):
            cleaned = _cleanup_inputs(incoming_fd, [(ciphertext.name, cipher_info),
                                                     (receipt_path.name, receipt_info)], ops)
            return ProcessingResult(ProcessingStatus.unchanged if cleaned else ProcessingStatus.processed_cleanup_pending,
                                    str(receipt["case_id"]), str(receipt["artifact_id"]))
        try:
            os.stat(case_name, dir_fd=processed_fd, follow_symlinks=False)
            raise ProcessingError(ProcessingStatus.output_collision)
        except FileNotFoundError:
            pass
        inspected = inspector(ciphertext=ciphertext, receipt_path=receipt_path, identity=identity,
                              work_directory=work_directory)
        if inspected.status is not INSPECTOR.InspectionStatus.inspected:
            raise ProcessingError(ProcessingStatus.inspection_failed)
        if (inspected.case_id, inspected.artifact_id, inspected.key_id, inspected.issue_url or None) != (
                receipt["case_id"], receipt["artifact_id"], receipt["bundle_encryption_key_id"],
                receipt["issue_url"]):
            raise ProcessingError(ProcessingStatus.inspection_failed)
        # Rebind after inspection and require the same objects, closing the pathname race.
        for old_info, path in ((cipher_info, ciphertext), (receipt_info, receipt_path)):
            new_fd, new_info = _open_incoming(path, incoming, incoming_fd)
            os.close(new_fd)
            if (old_info.st_dev, old_info.st_ino) != (new_info.st_dev, new_info.st_ino):
                raise ProcessingError(ProcessingStatus.unsafe_input)
        partial_name = f".{case_name}.partial-{uuid.uuid4().hex}"
        os.mkdir(partial_name, 0o700, dir_fd=processed_fd)
        partial_fd = os.open(partial_name, os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW,
                             dir_fd=processed_fd)
        opened_partial = os.fstat(partial_fd)
        partial_identity = (opened_partial.st_dev, opened_partial.st_ino)
        _copy_fd(cipher_fd, partial_fd, "encrypted-support-bundle.age", cipher_expected, ops)
        _copy_fd(receipt_fd, partial_fd, "support-bundle.receipt.json", receipt_expected, ops)
        record_bytes = (json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n").encode()
        record_expected = (len(record_bytes), hashlib.sha256(record_bytes).hexdigest())
        record_source = os.memfd_create("record") if hasattr(os, "memfd_create") else -1
        if record_source >= 0:
            os.write(record_source, record_bytes)
        else:
            record_source = os.open(".record-source", os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600,
                                    dir_fd=partial_fd)
            os.write(record_source, record_bytes)
            os.unlink(".record-source", dir_fd=partial_fd)
        try:
            _copy_fd(record_source, partial_fd, "processing-record.json", record_expected, ops)
        finally:
            os.close(record_source)
        ops.fsync(partial_fd)
        ops.before_partial_revalidate(partial_name, dir_fd=processed_fd)
        partial_info = os.fstat(partial_fd)
        named_info = os.stat(partial_name, dir_fd=processed_fd, follow_symlinks=False)
        if ((partial_info.st_dev, partial_info.st_ino) != (named_info.st_dev, named_info.st_ino)
                or not stat.S_ISDIR(named_info.st_mode) or stat.S_IMODE(named_info.st_mode) != 0o700):
            raise ProcessingError(ProcessingStatus.publication_failed)
        try:
            ops.rename(partial_name, case_name, src_dir_fd=processed_fd, dst_dir_fd=processed_fd)
        except OSError as error:
            raise ProcessingError(ProcessingStatus.publication_failed) from error
        committed = True
        partial_name = ""
        try:
            ops.fsync(processed_fd)
        except OSError:
            return ProcessingResult(ProcessingStatus.committed_sync_uncertain,
                                    str(receipt["case_id"]), str(receipt["artifact_id"]))
        cleaned = _cleanup_inputs(incoming_fd, [(ciphertext.name, cipher_info),
                                                 (receipt_path.name, receipt_info)], ops)
        return ProcessingResult(ProcessingStatus.processed if cleaned else ProcessingStatus.processed_cleanup_pending,
                                str(receipt["case_id"]), str(receipt["artifact_id"]))
    except ProcessingError as error:
        return ProcessingResult(error.status, safe.case_id, safe.artifact_id)
    except Exception:
        return ProcessingResult(ProcessingStatus.processing_failed, safe.case_id, safe.artifact_id)
    finally:
        if partial_fd >= 0:
            try:
                os.close(partial_fd)
            except OSError:
                pass
        if partial_identity is not None and processed_fd >= 0 and not committed:
            _remove_partial_by_identity(processed_fd, partial_identity)
        for fd in (cipher_fd, receipt_fd, incoming_fd, processed_fd):
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass


def process_production(*, age: Path = Path("/usr/bin/age"), **kwargs) -> ProcessingResult:
    def production_inspector(**inspection_kwargs):
        return INSPECTOR.inspect_production(age=age, **inspection_kwargs)

    return process_with_test_seam(repository=HERE.parents[1], now_provider=lambda: datetime.now(timezone.utc).replace(microsecond=0),
                                  inspector=production_inspector, ops=FileOps(), **kwargs)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    for flag in ("incoming", "processed", "ciphertext", "receipt", "identity", "work-directory"):
        parser.add_argument("--" + flag, required=True, type=Path)
    parser.add_argument("--retention-class", required=True, choices=sorted(RETENTION))
    parser.add_argument("--resolved-retention-days", type=int, default=60)
    parser.add_argument("--age", type=Path, default=Path("/usr/bin/age"))
    args = parser.parse_args(argv)
    result = process_production(incoming=args.incoming, processed=args.processed,
                                ciphertext=args.ciphertext, receipt_path=args.receipt,
                                identity=args.identity, work_directory=args.work_directory,
                                retention_class=args.retention_class,
                                resolved_retention_days=args.resolved_retention_days,
                                age=args.age)
    print(f"status: {result.status.value}")
    if result.case_id:
        print(f"case ID: {result.case_id}")
        print(f"artifact ID: {result.artifact_id}")
    return 0 if result.status in {ProcessingStatus.processed, ProcessingStatus.unchanged} else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
