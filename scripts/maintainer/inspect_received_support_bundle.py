#!/usr/bin/env python3
"""Fail-closed offline inspection of a received WsprryPi support bundle."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import signal
import stat
import subprocess
import sys
import tarfile
import tempfile
import time


CASE_ID = re.compile(r"^[0-9ABCDEFGHJKMNPQRSTVWXYZ]{4}-[0-9ABCDEFGHJKMNPQRSTVWXYZ]{4}-[0-9ABCDEFGHJKMNPQRSTVWXYZ]{4}$")
ARTIFACT_ID = re.compile(r"^[0-9a-f]{32}$")
KEY_ID = re.compile(r"^wsprrypi-bundle-[0-9]{4}-[0-9]{2}$")
DIGEST = re.compile(r"^[0-9a-f]{64}$")
ISSUE_URL = re.compile(r"^https://github\.com/WsprryPi/WsprryPi/issues/([1-9][0-9]{0,9})$")
TIMESTAMP = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")
MAX_RECEIPT = 16 * 1024
MAX_MANIFEST = 256 * 1024
MAX_ARCHIVE = 64 * 1024 * 1024
MAX_FILES = 2048
MAX_MEMBERS = 4096
MAX_FILE = 64 * 1024 * 1024
MAX_EXPANDED = 256 * 1024 * 1024
RECEIPT_KEYS = {
    "schema_version", "project_id", "case_id", "artifact_id", "created_at_utc",
    "archive_filename", "archive_size", "archive_sha256", "encrypted_filename",
    "encrypted_size", "encrypted_sha256", "bundle_encryption_key_id", "issue_url",
    "upload_state",
}
MANIFEST_KEYS = {
    "schema_version", "contract_version", "project_id", "project_version", "case_id",
    "created_at_utc", "collection_options", "privacy_categories", "support_context",
    "collection_warnings", "files",
}


class InspectionStatus(Enum):
    inspected = "inspected"
    invalid_receipt = "invalid_receipt"
    unsafe_input = "unsafe_input"
    key_mismatch = "key_mismatch"
    ciphertext_mismatch = "ciphertext_mismatch"
    decrypt_failed = "decrypt_failed"
    archive_mismatch = "archive_mismatch"
    unsafe_archive = "unsafe_archive"
    invalid_manifest = "invalid_manifest"
    inspection_failed = "inspection_failed"


@dataclass(frozen=True)
class InspectionResult:
    status: InspectionStatus
    case_id: str = ""
    artifact_id: str = ""
    key_id: str = ""
    issue_url: str = ""


class InspectionError(Exception):
    def __init__(self, status: InspectionStatus):
        self.status = status


def strict_json(raw: bytes) -> object:
    def pairs(values: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in values:
            if key in result:
                raise ValueError("duplicate")
            result[key] = value
        return result
    return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs)


def safe_basename(value: object, suffix: str) -> bool:
    return isinstance(value, str) and 1 <= len(value) <= 255 and value.endswith(suffix) \
        and value not in {".", ".."} and "/" not in value and "\\" not in value \
        and all(32 <= ord(char) < 127 for char in value)


def positive_int(value: object, maximum: int) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and 0 < value <= maximum


def parse_receipt(raw: bytes) -> dict[str, object]:
    try:
        if len(raw) > MAX_RECEIPT:
            raise InspectionError(InspectionStatus.invalid_receipt)
        value = strict_json(raw)
    except InspectionError:
        raise
    except Exception as error:
        raise InspectionError(InspectionStatus.invalid_receipt) from error
    if not isinstance(value, dict) or set(value) != RECEIPT_KEYS:
        raise InspectionError(InspectionStatus.invalid_receipt)
    case_id, artifact_id, key_id = value["case_id"], value["artifact_id"], value["bundle_encryption_key_id"]
    issue = value["issue_url"]
    valid = (
        type(value["schema_version"]) is int and value["schema_version"] == 1
        and value["project_id"] == "wsprrypi"
        and isinstance(case_id, str) and CASE_ID.fullmatch(case_id)
        and isinstance(artifact_id, str) and ARTIFACT_ID.fullmatch(artifact_id)
        and isinstance(key_id, str) and KEY_ID.fullmatch(key_id)
        and isinstance(value["created_at_utc"], str) and TIMESTAMP.fullmatch(value["created_at_utc"])
        and safe_basename(value["archive_filename"], ".tar.gz")
        and safe_basename(value["encrypted_filename"], ".tar.gz.age")
        and positive_int(value["archive_size"], MAX_ARCHIVE)
        and positive_int(value["encrypted_size"], MAX_ARCHIVE * 2)
        and isinstance(value["archive_sha256"], str) and DIGEST.fullmatch(value["archive_sha256"])
        and isinstance(value["encrypted_sha256"], str) and DIGEST.fullmatch(value["encrypted_sha256"])
        and (issue is None or isinstance(issue, str) and ISSUE_URL.fullmatch(issue))
        and value["upload_state"] == "encrypted_artifact_downloaded"
        and value["encrypted_filename"] == f"wsprrypi-support-{case_id}-{artifact_id}.tar.gz.age"
    )
    if not valid:
        raise InspectionError(InspectionStatus.invalid_receipt)
    return value


def load_receipt(path: Path) -> dict[str, object]:
    descriptor = -1
    try:
        descriptor = open_safe_file(path)
        raw = bytearray()
        while block := os.read(descriptor, 4096):
            raw.extend(block)
            if len(raw) > MAX_RECEIPT:
                raise InspectionError(InspectionStatus.invalid_receipt)
        return parse_receipt(bytes(raw))
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def open_safe_file(path: Path, *, identity: bool = False) -> int:
    if not path.is_absolute():
        raise InspectionError(InspectionStatus.unsafe_input)
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        info = os.fstat(descriptor)
    except OSError as error:
        raise InspectionError(InspectionStatus.unsafe_input) from error
    identity_mode = stat.S_IMODE(info.st_mode)
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_nlink != 1
            or identity and identity_mode not in (0o400, 0o600)):
        os.close(descriptor)
        raise InspectionError(InspectionStatus.unsafe_input)
    return descriptor


def safe_executable(path: Path) -> None:
    try:
        info = path.lstat()
    except OSError as error:
        raise InspectionError(InspectionStatus.unsafe_input) from error
    if (not path.is_absolute() or not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)
            or info.st_uid not in (0, os.geteuid())
            or not info.st_mode & stat.S_IXUSR or info.st_mode & 0o022):
        raise InspectionError(InspectionStatus.unsafe_input)


def hash_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while block := source.read(64 * 1024):
            size += len(block)
            digest.update(block)
    return size, digest.hexdigest()


def hash_descriptor(descriptor: int) -> tuple[int, str]:
    os.lseek(descriptor, 0, os.SEEK_SET)
    digest = hashlib.sha256()
    size = 0
    while block := os.read(descriptor, 64 * 1024):
        size += len(block)
        digest.update(block)
    os.lseek(descriptor, 0, os.SEEK_SET)
    return size, digest.hexdigest()


def descriptor_path(descriptor: int) -> str:
    proc = Path("/proc/self/fd")
    return str(proc / str(descriptor)) if proc.is_dir() else f"/dev/fd/{descriptor}"


def stage_descriptor(descriptor: int, destination: Path, maximum: int) -> None:
    output = -1
    copied = 0
    try:
        output = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL
                         | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
        while block := os.pread(descriptor, min(64 * 1024, maximum + 1 - copied), copied):
            copied += len(block)
            if copied > maximum:
                raise InspectionError(InspectionStatus.decrypt_failed)
            offset = 0
            while offset < len(block):
                count = os.write(output, block[offset:])
                if count <= 0:
                    raise InspectionError(InspectionStatus.decrypt_failed)
                offset += count
        if copied != os.fstat(descriptor).st_size:
            raise InspectionError(InspectionStatus.decrypt_failed)
        os.fsync(output)
    except (InspectionError, OSError) as error:
        if isinstance(error, InspectionError):
            raise
        raise InspectionError(InspectionStatus.decrypt_failed) from error
    finally:
        if output >= 0:
            os.close(output)


def decrypt_bounded(age: Path, identity_descriptor: int, ciphertext_descriptor: int, output: Path,
                    expected_size: int, timeout: float) -> None:
    staged: list[Path] = []
    proc = Path("/proc/self/fd")
    if proc.is_dir():
        identity_input = descriptor_path(identity_descriptor)
        ciphertext_input = descriptor_path(ciphertext_descriptor)
        inherited = (identity_descriptor, ciphertext_descriptor)
        identity_stdin = subprocess.DEVNULL
    else:
        identity_input = "-"
        ciphertext_input = str(output.parent / ".ciphertext")
        staged = [Path(ciphertext_input)]
        try:
            stage_descriptor(ciphertext_descriptor, staged[0], os.fstat(ciphertext_descriptor).st_size)
        except InspectionError:
            for candidate in staged:
                try:
                    candidate.unlink()
                except FileNotFoundError:
                    pass
            raise
        inherited = ()
        identity_stdin = identity_descriptor
    try:
        process = subprocess.Popen(
            [str(age), "--decrypt", "--identity", identity_input, ciphertext_input],
            stdin=identity_stdin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            env={"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"},
            start_new_session=True,
            pass_fds=inherited,
        )
    except (OSError, subprocess.SubprocessError) as error:
        for candidate in staged:
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass
        raise InspectionError(InspectionStatus.decrypt_failed) from error
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    written = 0

    def terminate() -> None:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except OSError:
                process.kill()
        try:
            process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

    try:
        with output.open("xb") as destination:
            os.chmod(output, 0o600)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise InspectionError(InspectionStatus.decrypt_failed)
                events = selector.select(min(remaining, 0.25))
                if not events:
                    if process.poll() is not None:
                        break
                    continue
                block = os.read(process.stdout.fileno(), 64 * 1024)
                if not block:
                    break
                written += len(block)
                if written > expected_size:
                    raise InspectionError(InspectionStatus.decrypt_failed)
                destination.write(block)
            if process.wait(timeout=max(0.01, deadline - time.monotonic())) != 0 or written != expected_size:
                raise InspectionError(InspectionStatus.decrypt_failed)
            destination.flush()
            os.fsync(destination.fileno())
    except (InspectionError, OSError, subprocess.SubprocessError) as error:
        terminate()
        if isinstance(error, InspectionError):
            raise
        raise InspectionError(InspectionStatus.decrypt_failed) from error
    finally:
        selector.close()
        process.stdout.close()
        terminate()
        for candidate in staged:
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass


def safe_member_name(name: str) -> tuple[str, str] | None:
    candidate = name[:-1] if name.endswith("/") else name
    if (not candidate or "\\" in candidate or candidate.startswith("/")
            or any(ord(char) < 32 or ord(char) == 127 for char in candidate)):
        return None
    parts = candidate.split("/")
    if len(parts) < 2 or parts[0] != "bundle" or any(part in {"", ".", ".."} for part in parts):
        return None
    return candidate, "/".join(parts[1:])


def validate_manifest(value: object, receipt: dict[str, object], files: dict[str, tuple[int, str]]) -> str:
    if not isinstance(value, dict) or set(value) != MANIFEST_KEYS:
        raise InspectionError(InspectionStatus.invalid_manifest)
    context = value["support_context"]
    listed = value["files"]
    if not (
        value["schema_version"] == 1 and value["contract_version"] == 1
        and value["project_id"] == "wsprrypi" and value["case_id"] == receipt["case_id"]
        and isinstance(value["project_version"], str) and 0 < len(value["project_version"]) <= 128
        and isinstance(value["created_at_utc"], str) and TIMESTAMP.fullmatch(value["created_at_utc"])
        and isinstance(value["collection_options"], dict)
        and set(value["collection_options"]) == {"configuration_files_included", "full_logs_included", "i2c_probe_requested"}
        and all(isinstance(item, bool) for item in value["collection_options"].values())
        and isinstance(value["privacy_categories"], list)
        and all(isinstance(item, str) for item in value["privacy_categories"])
        and isinstance(value["collection_warnings"], list)
        and all(isinstance(item, str) for item in value["collection_warnings"])
        and isinstance(context, dict) and set(context) == {"kind", "issue_url", "problem_description", "contact"}
        and isinstance(listed, list) and len(listed) <= MAX_FILES
    ):
        raise InspectionError(InspectionStatus.invalid_manifest)
    kind = context["kind"]
    if kind == "existing_github_issue":
        if not isinstance(context["issue_url"], str) or not ISSUE_URL.fullmatch(context["issue_url"]) \
                or context["problem_description"] is not None or context["contact"] is not None:
            raise InspectionError(InspectionStatus.invalid_manifest)
        issue_url = context["issue_url"]
    elif kind in {"new_github_issue", "no_github"}:
        if context["issue_url"] is not None or not all(
            isinstance(context[key], str) and 0 < len(context[key]) <= maximum
            for key, maximum in (("problem_description", 4096), ("contact", 512))
        ):
            raise InspectionError(InspectionStatus.invalid_manifest)
        issue_url = ""
    else:
        raise InspectionError(InspectionStatus.invalid_manifest)
    declared: dict[str, tuple[int, str]] = {}
    for entry in listed:
        if not isinstance(entry, dict) or set(entry) != {"path", "size", "sha256"}:
            raise InspectionError(InspectionStatus.invalid_manifest)
        path, size, digest = entry["path"], entry["size"], entry["sha256"]
        if (not isinstance(path, str) or safe_member_name("bundle/" + path) is None
                or not isinstance(size, int) or isinstance(size, bool) or size < 0 or size > MAX_FILE
                or not isinstance(digest, str) or not DIGEST.fullmatch(digest) or path in declared):
            raise InspectionError(InspectionStatus.invalid_manifest)
        declared[path] = (size, digest)
    if declared != files:
        raise InspectionError(InspectionStatus.invalid_manifest)
    receipt_issue = receipt["issue_url"] or ""
    if receipt_issue and receipt_issue != issue_url:
        raise InspectionError(InspectionStatus.invalid_manifest)
    return issue_url


def inspect_archive(path: Path, receipt: dict[str, object]) -> str:
    files: dict[str, tuple[int, str]] = {}
    names: set[str] = set()
    manifest_bytes: bytes | None = None
    expanded = 0
    count = 0
    member_count = 0
    try:
        with tarfile.open(path, "r:gz") as archive:
            for member in archive:
                member_count += 1
                if member_count > MAX_MEMBERS:
                    raise InspectionError(InspectionStatus.unsafe_archive)
                if member.isdir() and member.name.rstrip("/") == "bundle":
                    if "bundle" in names or member.mode & 0o7000:
                        raise InspectionError(InspectionStatus.unsafe_archive)
                    names.add("bundle")
                    continue
                parsed = safe_member_name(member.name)
                if parsed is None or parsed[0] in names or member.mode & 0o7000:
                    raise InspectionError(InspectionStatus.unsafe_archive)
                names.add(parsed[0])
                if member.isdir():
                    continue
                if not member.isreg() or member.size < 0 or member.size > MAX_FILE:
                    raise InspectionError(InspectionStatus.unsafe_archive)
                count += 1
                expanded += member.size
                if count > MAX_FILES or expanded > MAX_EXPANDED:
                    raise InspectionError(InspectionStatus.unsafe_archive)
                source = archive.extractfile(member)
                if source is None:
                    raise InspectionError(InspectionStatus.unsafe_archive)
                digest = hashlib.sha256()
                contents = bytearray() if parsed[1] == "manifest.json" else None
                read = 0
                while block := source.read(64 * 1024):
                    read += len(block)
                    digest.update(block)
                    if contents is not None:
                        if read > MAX_MANIFEST:
                            raise InspectionError(InspectionStatus.invalid_manifest)
                        contents.extend(block)
                if read != member.size:
                    raise InspectionError(InspectionStatus.unsafe_archive)
                if parsed[1] == "manifest.json":
                    if manifest_bytes is not None:
                        raise InspectionError(InspectionStatus.invalid_manifest)
                    manifest_bytes = bytes(contents or b"")
                else:
                    files[parsed[1]] = (member.size, digest.hexdigest())
    except InspectionError:
        raise
    except (OSError, tarfile.TarError, EOFError) as error:
        raise InspectionError(InspectionStatus.unsafe_archive) from error
    if manifest_bytes is None:
        raise InspectionError(InspectionStatus.invalid_manifest)
    try:
        manifest = strict_json(manifest_bytes)
    except Exception as error:
        raise InspectionError(InspectionStatus.invalid_manifest) from error
    return validate_manifest(manifest, receipt, files)


def inspect(*, age: Path, ciphertext: Path, receipt_path: Path, identity: Path,
            work_directory: Path, timeout: float = 30.0) -> InspectionResult:
    ciphertext_descriptor = -1
    identity_descriptor = -1
    try:
        receipt = load_receipt(receipt_path)
        ciphertext_descriptor = open_safe_file(ciphertext)
        identity_descriptor = open_safe_file(identity, identity=True)
        safe_executable(age)
        root_info = work_directory.lstat()
        if (not work_directory.is_absolute() or not stat.S_ISDIR(root_info.st_mode)
                or stat.S_ISLNK(root_info.st_mode) or root_info.st_uid != os.geteuid()
                or stat.S_IMODE(root_info.st_mode) != 0o700):
            raise InspectionError(InspectionStatus.unsafe_input)
        key_id = str(receipt["bundle_encryption_key_id"])
        if identity.name != f"{key_id}.age-identity.txt":
            raise InspectionError(InspectionStatus.key_mismatch)
        size, digest = hash_descriptor(ciphertext_descriptor)
        if size != receipt["encrypted_size"] or digest != receipt["encrypted_sha256"]:
            raise InspectionError(InspectionStatus.ciphertext_mismatch)
        with tempfile.TemporaryDirectory(prefix=".wsprrypi-intake-", dir=work_directory) as temporary:
            os.chmod(temporary, 0o700)
            archive = Path(temporary) / "decrypted.tar.gz"
            decrypt_bounded(age, identity_descriptor, ciphertext_descriptor, archive,
                            int(receipt["archive_size"]), timeout)
            archive_size, archive_digest = hash_file(archive)
            if archive_size != receipt["archive_size"] or archive_digest != receipt["archive_sha256"]:
                raise InspectionError(InspectionStatus.archive_mismatch)
            issue_url = inspect_archive(archive, receipt)
        return InspectionResult(InspectionStatus.inspected, str(receipt["case_id"]),
                                str(receipt["artifact_id"]), key_id, issue_url)
    except InspectionError as error:
        return InspectionResult(error.status)
    except Exception:
        return InspectionResult(InspectionStatus.inspection_failed)
    finally:
        if ciphertext_descriptor >= 0:
            os.close(ciphertext_descriptor)
        if identity_descriptor >= 0:
            os.close(identity_descriptor)


def inspect_production(*, ciphertext: Path, receipt_path: Path, identity: Path,
                       work_directory: Path, age: Path = Path("/usr/bin/age")) -> InspectionResult:
    return inspect(age=age, ciphertext=ciphertext, receipt_path=receipt_path,
                   identity=identity, work_directory=work_directory)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ciphertext", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    parser.add_argument("--identity", required=True, type=Path)
    parser.add_argument("--work-directory", required=True, type=Path)
    parser.add_argument("--age", type=Path, default=Path("/usr/bin/age"))
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    result = inspect_production(ciphertext=arguments.ciphertext,
                                receipt_path=arguments.receipt,
                                identity=arguments.identity,
                                work_directory=arguments.work_directory,
                                age=arguments.age)
    print(f"status: {result.status.value}")
    if result.status is InspectionStatus.inspected:
        print(f"case ID: {result.case_id}")
        print(f"artifact ID: {result.artifact_id}")
        print(f"bundle key ID: {result.key_id}")
        print(f"issue: {result.issue_url or 'private-context-only'}")
    return 0 if result.status is InspectionStatus.inspected else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
