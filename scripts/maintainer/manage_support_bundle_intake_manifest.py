#!/usr/bin/env python3
"""Authenticate and manage local WsprryPi support-intake manifest generations."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

import prepare_support_bundle_intake_manifest as preparation


GENERATION = re.compile(r"^generation-([1-9][0-9]*)$")


class LifecycleError(RuntimeError):
    pass


class LifecycleStatus(Enum):
    inspected = "inspected"
    proposed = "proposed"
    committed = "committed"
    committed_sync_uncertain = "committed_sync_uncertain"


@dataclass(frozen=True)
class CurrentManifest:
    directory: Path
    manifest_path: Path
    signature_path: Path
    manifest_bytes: bytes
    value: dict
    signing_key_id: str
    manifest_sha256: str


@dataclass(frozen=True)
class LifecycleResult:
    status: LifecycleStatus
    operation: str
    generation: int
    intake_status: str
    published_at: str
    expires_at: str
    minimum_client_protocol: int
    minimum_upload_version: str
    signing_key_id: str
    bundle_key_id: str
    manifest_sha256: str


def require_generation_directory(path: Path) -> None:
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode)
            or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700):
        raise LifecycleError("generation directory is unsafe")
    if {entry.name for entry in path.iterdir()} != {"intake.json", "intake.json.sig"}:
        raise LifecycleError("generation directory contents are incomplete or unexpected")
    for name in ("intake.json", "intake.json.sig"):
        info = (path / name).lstat()
        if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode)
                or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o600
                or info.st_nlink != 1 or info.st_size <= 0):
            raise LifecycleError("generation file is unsafe")


def inventory(root: Path) -> list[Path]:
    root = preparation.require_staging(root)
    numbered = {}
    for entry in root.iterdir():
        match = GENERATION.fullmatch(entry.name)
        if not match:
            raise LifecycleError("staging root contains a partial or unexpected entry")
        number = int(match.group(1))
        if number in numbered:
            raise LifecycleError("staging root contains duplicate numeric generations")
        require_generation_directory(entry)
        numbered[number] = entry
    if not numbered:
        raise LifecycleError("staging root contains no complete generation")
    maximum = max(numbered)
    if set(numbered) != set(range(1, maximum + 1)):
        raise LifecycleError("staging generations are not contiguous")
    return [numbered[number] for number in range(1, maximum + 1)]


def authenticate_generation(directory: Path, openssl: Path,
                            signing_metadata_path: Path) -> CurrentManifest:
    openssl = preparation.require_openssl(openssl)
    signing_metadata_path = preparation.require_file(signing_metadata_path, "signing metadata")
    match = GENERATION.fullmatch(directory.name)
    if match is None:
        raise LifecycleError("generation directory name is invalid")
    require_generation_directory(directory)
    generation = int(match.group(1))
    manifest_path = directory / "intake.json"
    signature_path = directory / "intake.json.sig"
    signing_key_id, raw_public = preparation.signing_metadata(signing_metadata_path)
    envelope = preparation.strict_json(signature_path, preparation.MAX_ENVELOPE_BYTES)
    preparation.exact_keys(envelope, {"schema_version", "algorithm", "key_id", "signature"})
    if (type(envelope["schema_version"]) is not int or envelope["schema_version"] != 1
            or envelope["algorithm"] != "Ed25519" or envelope["key_id"] != signing_key_id):
        raise LifecycleError("signature envelope does not match selected trust")
    signature = preparation.canonical_base64url(envelope["signature"], 64)
    manifest_bytes = manifest_path.read_bytes()
    if len(manifest_bytes) > preparation.MAX_MANIFEST_BYTES:
        raise LifecycleError("manifest is oversized")
    with tempfile.NamedTemporaryFile(mode="wb") as public_key, tempfile.NamedTemporaryFile(mode="wb") as signature_file:
        public_key.write(preparation.ED25519_SPKI_PREFIX + raw_public); public_key.flush()
        signature_file.write(signature); signature_file.flush()
        verified = subprocess.run(
            [str(openssl), "pkeyutl", "-verify", "-pubin", "-keyform", "DER",
             "-inkey", public_key.name, "-rawin", "-in", str(manifest_path),
             "-sigfile", signature_file.name],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False, timeout=30)
    if verified.returncode != 0:
        raise LifecycleError("current manifest signature is invalid")

    value = preparation.strict_json(manifest_path, preparation.MAX_MANIFEST_BYTES)
    common = {"schema_version", "project_id", "generation", "published_at", "expires_at",
              "status", "minimum_client_protocol", "minimum_upload_version", "release_url",
              "user_message", "bundle_encryption_key_id"}
    if value.get("status") == "active":
        preparation.exact_keys(value, common | {"request_url"})
        request_url = value["request_url"]
    elif value.get("status") == "disabled":
        preparation.exact_keys(value, common)
        request_url = None
    else:
        raise LifecycleError("current manifest status is invalid")
    if value.get("schema_version") != 1 or type(value.get("schema_version")) is not int:
        raise LifecycleError("current manifest schema is invalid")
    if value.get("project_id") != "wsprrypi" or value.get("generation") != generation:
        raise LifecycleError("manifest does not match its generation directory")
    reconstructed = preparation.build_manifest(
        generation=value["generation"], published_at=value["published_at"],
        expires_at=value["expires_at"], status=value["status"],
        minimum_client_protocol=value["minimum_client_protocol"],
        minimum_upload_version=value["minimum_upload_version"], request_url=request_url,
        release_url=value["release_url"], user_message=value["user_message"],
        bundle_key_id=value["bundle_encryption_key_id"])
    if reconstructed != manifest_bytes:
        raise LifecycleError("current manifest is not deterministic Slice 14 bytes")
    return CurrentManifest(directory, manifest_path, signature_path, manifest_bytes, value,
                           signing_key_id, hashlib.sha256(manifest_bytes).hexdigest())


def authenticate_current(root: Path, openssl: Path, signing_metadata_path: Path) -> CurrentManifest:
    directories = inventory(root)
    return authenticate_generation(directories[-1], openssl, signing_metadata_path)


def result_from(current: CurrentManifest, operation: str, status: LifecycleStatus,
                value: dict | None = None, digest: str | None = None) -> LifecycleResult:
    selected = value or current.value
    return LifecycleResult(status, operation, selected["generation"], selected["status"],
                           selected["published_at"], selected["expires_at"],
                           selected["minimum_client_protocol"], selected["minimum_upload_version"],
                           current.signing_key_id, selected["bundle_encryption_key_id"],
                           digest or current.manifest_sha256)


def manage(*, operation: str, approve: bool, openssl: Path, staging_root: Path,
           signing_metadata_path: Path, bundle_metadata_path: Path | None = None,
           private_key: Path | None = None, published_at: str | None = None,
           expires_at: str | None = None, request_url: str | None = None,
           minimum_upload_version: str | None = None, user_message: str | None = None) -> LifecycleResult:
    root = preparation.require_staging(staging_root)
    descriptor = os.open(root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        fcntl.flock(descriptor, fcntl.LOCK_SH if operation == "inspect" else fcntl.LOCK_EX)
        current = authenticate_current(root, openssl, signing_metadata_path)
        if operation == "inspect":
            if (approve or bundle_metadata_path is not None or private_key is not None
                    or published_at is not None or expires_at is not None or request_url is not None
                    or minimum_upload_version is not None or user_message is not None):
                raise LifecycleError("inspect does not accept mutation arguments")
            return result_from(current, operation, LifecycleStatus.inspected)
        if operation not in {"rotate", "disable", "renew"}:
            raise LifecycleError("unsupported lifecycle operation")
        if published_at is None or expires_at is None:
            raise LifecycleError("mutating operations require publication and expiration timestamps")
        current_published = preparation.parse_utc(current.value["published_at"])
        current_expires = preparation.parse_utc(current.value["expires_at"])
        next_published = preparation.parse_utc(published_at)
        next_expires = preparation.parse_utc(expires_at)
        if next_published <= current_published or next_expires <= current_expires:
            raise LifecycleError("successor timestamps must move forward")
        value = dict(current.value)
        value["generation"] += 1
        value["published_at"] = published_at
        value["expires_at"] = expires_at
        if operation == "rotate":
            if request_url is None:
                raise LifecycleError("rotate requires a replacement URL")
            value["status"] = "active"
            value["request_url"] = request_url
            if minimum_upload_version is not None:
                value["minimum_upload_version"] = minimum_upload_version
            if user_message is not None:
                value["user_message"] = user_message
        elif operation == "disable":
            if request_url is not None or minimum_upload_version is not None:
                raise LifecycleError("disable does not accept rotation policy changes")
            if not user_message:
                raise LifecycleError("disable requires a nonempty user message")
            value["status"] = "disabled"
            value.pop("request_url", None)
            value["user_message"] = user_message
        else:
            if request_url is not None or minimum_upload_version is not None or user_message is not None:
                raise LifecycleError("renew does not accept policy changes")
        candidate = preparation.build_manifest(
            generation=value["generation"], published_at=value["published_at"],
            expires_at=value["expires_at"], status=value["status"],
            minimum_client_protocol=value["minimum_client_protocol"],
            minimum_upload_version=value["minimum_upload_version"],
            request_url=value.get("request_url"), release_url=value["release_url"],
            user_message=value["user_message"], bundle_key_id=value["bundle_encryption_key_id"])
        if not approve:
            return result_from(current, operation, LifecycleStatus.proposed, value,
                               hashlib.sha256(candidate).hexdigest())
        if private_key is None or bundle_metadata_path is None:
            raise LifecycleError("approved operation requires private key and bundle metadata")
        bundle_key_id, _ = preparation.bundle_metadata(bundle_metadata_path)
        if bundle_key_id != current.value["bundle_encryption_key_id"]:
            raise LifecycleError("bundle metadata does not match current manifest")
        prepared = preparation.prepare(
            openssl=openssl, private_key=private_key,
            signing_metadata_path=signing_metadata_path,
            bundle_metadata_path=bundle_metadata_path, staging_directory=root,
            generation=value["generation"], published_at=value["published_at"],
            expires_at=value["expires_at"], status=value["status"],
            minimum_client_protocol=value["minimum_client_protocol"],
            minimum_upload_version=value["minimum_upload_version"],
            request_url=value.get("request_url"), release_url=value["release_url"],
            user_message=value["user_message"])
        lifecycle_status = (LifecycleStatus.committed if prepared.status is preparation.PreparationStatus.committed
                            else LifecycleStatus.committed_sync_uncertain)
        return result_from(current, operation, lifecycle_status, value, prepared.manifest_sha256)
    finally:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally:
            os.close(descriptor)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=("inspect", "rotate", "disable", "renew"))
    parser.add_argument("--approve", action="store_true")
    parser.add_argument("--openssl", required=True, type=Path)
    parser.add_argument("--staging-root", required=True, type=Path)
    parser.add_argument("--signing-metadata", required=True, type=Path)
    parser.add_argument("--bundle-metadata", type=Path)
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--published-at")
    parser.add_argument("--expires-at")
    parser.add_argument("--request-url")
    parser.add_argument("--minimum-upload-version")
    parser.add_argument("--user-message")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    try:
        result = manage(operation=arguments.operation, approve=arguments.approve,
                        openssl=arguments.openssl, staging_root=arguments.staging_root,
                        signing_metadata_path=arguments.signing_metadata,
                        bundle_metadata_path=arguments.bundle_metadata,
                        private_key=arguments.private_key, published_at=arguments.published_at,
                        expires_at=arguments.expires_at, request_url=arguments.request_url,
                        minimum_upload_version=arguments.minimum_upload_version,
                        user_message=arguments.user_message)
    except (OSError, subprocess.SubprocessError, preparation.PreparationError, LifecycleError) as error:
        print(f"manifest lifecycle failed: {error}", file=sys.stderr)
        return 1
    print(f"status: {result.status.value}")
    print(f"operation: {result.operation}")
    print(f"generation: {result.generation}")
    print(f"intake status: {result.intake_status}")
    print(f"published at: {result.published_at}")
    print(f"expires at: {result.expires_at}")
    print(f"minimum client protocol: {result.minimum_client_protocol}")
    print(f"minimum upload version: {result.minimum_upload_version}")
    print(f"signing key ID: {result.signing_key_id}")
    print(f"bundle key ID: {result.bundle_key_id}")
    print(f"manifest SHA-256: {result.manifest_sha256}")
    return 0 if result.status is not LifecycleStatus.committed_sync_uncertain else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
