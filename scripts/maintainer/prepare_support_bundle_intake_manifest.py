#!/usr/bin/env python3
"""Prepare and exact-byte sign a WsprryPi support-intake manifest."""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import datetime as dt
from enum import Enum
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
from typing import Callable
from urllib.parse import unquote, urlsplit


INTAKE_ID = re.compile(r"^wsprrypi-intake-[0-9]{4}-[0-9]{2}$")
BUNDLE_ID = re.compile(r"^wsprrypi-bundle-[0-9]{4}-[0-9]{2}$")
SEMVER = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\."
    r"(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
MAX_METADATA_BYTES = 4096
MAX_MANIFEST_BYTES = 16 * 1024
MAX_ENVELOPE_BYTES = 2048
MAX_MESSAGE_BYTES = 1024
MAX_TOOL_OUTPUT_BYTES = 256


class PreparationError(RuntimeError):
    pass


class PreparationStatus(Enum):
    committed = "committed"
    committed_sync_uncertain = "committed_sync_uncertain"


@dataclass(frozen=True)
class PreparationResult:
    status: PreparationStatus
    manifest_path: Path
    signature_path: Path
    generation: int
    signing_key_id: str
    bundle_key_id: str
    manifest_sha256: str


def strict_json(path: Path, maximum: int) -> dict:
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode)
            or info.st_nlink != 1 or info.st_size <= 0 or info.st_size > maximum):
        raise PreparationError("metadata input is unsafe or oversized")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise PreparationError("metadata contains a duplicate key")
            result[key] = value
        return result
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise PreparationError("metadata is not strict UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise PreparationError("metadata must be a JSON object")
    return value


def exact_keys(value: dict, expected: set[str]) -> None:
    if set(value) != expected:
        raise PreparationError("metadata fields do not match the required schema")


def canonical_base64url(value: str, decoded_size: int) -> bytes:
    if not isinstance(value, str) or "=" in value or not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise PreparationError("public value is not unpadded base64url")
    try:
        decoded = base64.urlsafe_b64decode(value + "=" * ((4 - len(value) % 4) % 4))
    except ValueError as error:
        raise PreparationError("public value is not valid base64url") from error
    if len(decoded) != decoded_size or base64.urlsafe_b64encode(decoded).decode("ascii").rstrip("=") != value:
        raise PreparationError("public value is not canonical base64url")
    return decoded


def bech32_polymod(values: list[int]) -> int:
    checksum = 1
    generators = (0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3)
    for value in values:
        top = checksum >> 25
        checksum = ((checksum & 0x1FFFFFF) << 5) ^ value
        for bit, generator in enumerate(generators):
            if (top >> bit) & 1:
                checksum ^= generator
    return checksum


def valid_age_recipient(recipient: str) -> bool:
    if not isinstance(recipient, str) or recipient != recipient.lower() or not recipient.startswith("age1"):
        return False
    encoded = recipient[recipient.rfind("1") + 1:]
    if len(encoded) < 7 or any(character not in BECH32_CHARSET for character in encoded):
        return False
    expanded = [ord(c) >> 5 for c in "age"] + [0] + [ord(c) & 31 for c in "age"]
    values = [BECH32_CHARSET.index(c) for c in encoded]
    if bech32_polymod(expanded + values) != 1:
        return False
    accumulator = bits = 0
    decoded = bytearray()
    for value in values[:-6]:
        accumulator = (accumulator << 5) | value
        bits += 5
        while bits >= 8:
            bits -= 8
            decoded.append((accumulator >> bits) & 0xFF)
    return bits < 5 and not ((accumulator << (8 - bits)) & 0xFF) and len(decoded) == 32


def fingerprint(value: dict, material: bytes) -> None:
    exact_keys(value, {"algorithm", "value"})
    if value["algorithm"] != "sha256" or value["value"] != hashlib.sha256(material).hexdigest():
        raise PreparationError("public metadata fingerprint does not match")


def signing_metadata(path: Path) -> tuple[str, bytes]:
    value = strict_json(path, MAX_METADATA_BYTES)
    exact_keys(value, {"schema_version", "project_id", "purpose", "algorithm", "key_id",
                       "public_key", "created_at_utc", "fingerprint"})
    if (type(value["schema_version"]) is not int or value["schema_version"] != 1
            or value["project_id"] != "wsprrypi"
            or value["purpose"] != "support_intake_manifest_signing"
            or value["algorithm"] != "Ed25519"
            or not isinstance(value["key_id"], str) or not INTAKE_ID.fullmatch(value["key_id"])):
        raise PreparationError("invalid signing public metadata")
    if not isinstance(value["public_key"], dict):
        raise PreparationError("invalid signing public key")
    exact_keys(value["public_key"], {"encoding", "value"})
    if value["public_key"]["encoding"] != "base64url":
        raise PreparationError("unsupported signing public key encoding")
    raw = canonical_base64url(value["public_key"]["value"], 32)
    fingerprint(value["fingerprint"], raw)
    parse_utc(value["created_at_utc"])
    return value["key_id"], raw


def bundle_metadata(path: Path) -> tuple[str, str]:
    value = strict_json(path, MAX_METADATA_BYTES)
    exact_keys(value, {"schema_version", "project_id", "purpose", "algorithm", "key_id",
                       "recipient", "created_at_utc", "fingerprint"})
    if (type(value["schema_version"]) is not int or value["schema_version"] != 1
            or value["project_id"] != "wsprrypi"
            or value["purpose"] != "support_bundle_encryption"
            or value["algorithm"] != "age-x25519"
            or not isinstance(value["key_id"], str) or not BUNDLE_ID.fullmatch(value["key_id"])
            or not valid_age_recipient(value["recipient"])):
        raise PreparationError("invalid bundle public metadata")
    recipient = value["recipient"]
    fingerprint(value["fingerprint"], recipient.encode("ascii"))
    parse_utc(value["created_at_utc"])
    return value["key_id"], recipient


def parse_utc(value: str) -> dt.datetime:
    if not isinstance(value, str) or not re.fullmatch(
            r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", value):
        raise PreparationError("timestamp must be UTC YYYY-MM-DDTHH:MM:SSZ")
    try:
        parsed = dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)
        if parsed.year < 1970:
            raise ValueError("year precedes supported epoch")
        return parsed
    except ValueError as error:
        raise PreparationError("timestamp is not a real UTC time") from error


def require_file(path: Path, description: str, mode: int | None = None) -> Path:
    if not path.is_absolute():
        raise PreparationError(f"{description} must be absolute")
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode) or info.st_nlink != 1
            or info.st_size <= 0
            or (mode is not None and (info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != mode))):
        raise PreparationError(f"{description} is unsafe")
    return path.resolve(strict=True)


def require_openssl(path: Path) -> Path:
    path = require_file(path, "OpenSSL executable")
    info = path.stat()
    if info.st_mode & 0o022 or not os.access(path, os.X_OK):
        raise PreparationError("OpenSSL executable is unsafe or unavailable")
    return path


def require_staging(path: Path) -> Path:
    if not path.is_absolute():
        raise PreparationError("staging directory must be absolute")
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode)
            or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700):
        raise PreparationError("staging directory must be owner-controlled mode 0700")
    return path.resolve(strict=True)


def valid_release_url(value: str) -> bool:
    try:
        parsed = urlsplit(value)
        decoded_path = unquote(parsed.path)
        return (parsed.scheme == "https" and parsed.netloc == "github.com"
                and not parsed.query and not parsed.fragment
                and re.fullmatch(r"/WsprryPi/WsprryPi/releases/[A-Za-z0-9._~!$&'()*+,;=:@%/-]+",
                                 parsed.path) is not None
                and all(segment not in {".", ".."} for segment in decoded_path.split("/")))
    except ValueError:
        return False


def valid_request_url(value: str) -> bool:
    try:
        parsed = urlsplit(value)
        return (parsed.scheme == "https" and parsed.netloc == "www.dropbox.com"
                and not parsed.query and not parsed.fragment
                and re.fullmatch(r"/request/[A-Za-z0-9_-]+", parsed.path) is not None)
    except ValueError:
        return False


def build_manifest(*, generation: int, published_at: str, expires_at: str, status: str,
                   minimum_client_protocol: int, minimum_upload_version: str,
                   request_url: str | None, release_url: str, user_message: str | None,
                   bundle_key_id: str) -> bytes:
    if not isinstance(generation, int) or isinstance(generation, bool) or generation <= 0:
        raise PreparationError("generation must be a positive integer")
    if (not isinstance(minimum_client_protocol, int) or isinstance(minimum_client_protocol, bool)
            or minimum_client_protocol <= 0):
        raise PreparationError("minimum client protocol must be a positive integer")
    if (not isinstance(minimum_upload_version, str) or len(minimum_upload_version) > 128
            or not SEMVER.fullmatch(minimum_upload_version)):
        raise PreparationError("minimum upload version must be strict SemVer")
    published = parse_utc(published_at)
    expires = parse_utc(expires_at)
    if published >= expires:
        raise PreparationError("manifest expiration must follow publication")
    if status not in {"active", "disabled"}:
        raise PreparationError("status must be active or disabled")
    if status == "active" and (not isinstance(request_url, str) or not valid_request_url(request_url)):
        raise PreparationError("active manifest requires a valid Dropbox request URL")
    if status == "disabled" and request_url is not None:
        raise PreparationError("disabled manifest must omit the request URL")
    if not valid_release_url(release_url):
        raise PreparationError("release URL is outside WsprryPi release policy")
    if user_message is not None and (not isinstance(user_message, str) or not user_message
                                     or len(user_message.encode("utf-8")) > MAX_MESSAGE_BYTES
                                     or any(ord(character) < 0x20 or ord(character) == 0x7f
                                            for character in user_message)):
        raise PreparationError("user message is invalid or oversized")
    value = {
        "schema_version": 1, "project_id": "wsprrypi", "generation": generation,
        "published_at": published_at, "expires_at": expires_at, "status": status,
        "minimum_client_protocol": minimum_client_protocol,
        "minimum_upload_version": minimum_upload_version,
    }
    if request_url is not None:
        value["request_url"] = request_url
    value.update({"release_url": release_url, "user_message": user_message,
                  "bundle_encryption_key_id": bundle_key_id})
    encoded = (json.dumps(value, indent=2, separators=(",", ": ")) + "\n").encode("utf-8")
    if len(encoded) > MAX_MANIFEST_BYTES:
        raise PreparationError("manifest is oversized")
    return encoded


def bounded_tool_output(command: list[str], maximum: int) -> tuple[int, bytes]:
    with tempfile.TemporaryFile() as output:
        completed = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=output,
                                   stderr=subprocess.DEVNULL, check=False, timeout=30)
        output.seek(0)
        return completed.returncode, output.read(maximum + 1)


def sync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def prepare(*, openssl: Path, private_key: Path, signing_metadata_path: Path,
            bundle_metadata_path: Path, staging_directory: Path, generation: int,
            published_at: str, expires_at: str, status: str,
            minimum_client_protocol: int, minimum_upload_version: str,
            request_url: str | None, release_url: str, user_message: str | None,
            _root_sync: Callable[[Path], None] = sync_directory) -> PreparationResult:
    openssl = require_openssl(openssl)
    private_key = require_file(private_key, "private signing key", 0o400)
    signing_metadata_path = require_file(signing_metadata_path, "signing metadata")
    bundle_metadata_path = require_file(bundle_metadata_path, "bundle metadata")
    staging_directory = require_staging(staging_directory)
    signing_key_id, expected_public = signing_metadata(signing_metadata_path)
    bundle_key_id, _ = bundle_metadata(bundle_metadata_path)

    code, public_der = bounded_tool_output(
        [str(openssl), "pkey", "-in", str(private_key), "-pubout", "-outform", "DER"],
        MAX_TOOL_OUTPUT_BYTES)
    if (code != 0 or public_der != ED25519_SPKI_PREFIX + expected_public):
        raise PreparationError("private key does not match signing public metadata")
    manifest = build_manifest(generation=generation, published_at=published_at,
                              expires_at=expires_at, status=status,
                              minimum_client_protocol=minimum_client_protocol,
                              minimum_upload_version=minimum_upload_version,
                              request_url=request_url, release_url=release_url,
                              user_message=user_message, bundle_key_id=bundle_key_id)
    final_directory = staging_directory / f"generation-{generation}"
    partial_directory = staging_directory / f".generation-{generation}.partial"
    for candidate in (final_directory, partial_directory):
        if candidate.exists() or candidate.is_symlink():
            raise PreparationError("staging output collision")
    os.mkdir(partial_directory, 0o700)
    partial_info = partial_directory.lstat()
    if (not stat.S_ISDIR(partial_info.st_mode) or partial_info.st_uid != os.geteuid()
            or stat.S_IMODE(partial_info.st_mode) != 0o700):
        partial_directory.rmdir()
        raise PreparationError("partial generation directory is unsafe")
    manifest_partial = partial_directory / "intake.json"
    envelope_partial = partial_directory / "intake.json.sig"
    try:
        for path, contents in ((manifest_partial, manifest),):
            descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "wb") as output:
                output.write(contents); output.flush(); os.fsync(output.fileno())
        code, signature = bounded_tool_output(
            [str(openssl), "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
             "-in", str(manifest_partial)], 64)
        if code != 0 or len(signature) != 64:
            raise PreparationError("OpenSSL did not produce an Ed25519 signature")
        with tempfile.NamedTemporaryFile(dir=staging_directory, prefix=".verify-", mode="wb") as signature_file:
            signature_file.write(signature); signature_file.flush(); os.fsync(signature_file.fileno())
            verified = subprocess.run(
                [str(openssl), "pkeyutl", "-verify", "-rawin", "-inkey", str(private_key),
                 "-in", str(manifest_partial), "-sigfile", signature_file.name],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                check=False, timeout=30)
        if verified.returncode != 0:
            raise PreparationError("OpenSSL self-verification failed")
        signature_text = base64.urlsafe_b64encode(signature).decode("ascii").rstrip("=")
        if len(signature_text) != 86 or "=" in signature_text:
            raise PreparationError("signature encoding was not canonical")
        envelope = (json.dumps({"schema_version": 1, "algorithm": "Ed25519",
                                "key_id": signing_key_id, "signature": signature_text},
                               indent=2, separators=(",", ": ")) + "\n").encode("utf-8")
        if len(envelope) > MAX_ENVELOPE_BYTES:
            raise PreparationError("signature envelope is oversized")
        descriptor = os.open(envelope_partial, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(envelope); output.flush(); os.fsync(output.fileno())
        directory = os.open(partial_directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        os.rename(partial_directory, final_directory)
    except Exception:
        for candidate in (manifest_partial, envelope_partial):
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass
            except OSError:
                pass
        try:
            partial_directory.rmdir()
        except OSError:
            pass
        raise
    manifest_final = final_directory / "intake.json"
    envelope_final = final_directory / "intake.json.sig"
    sync_uncertain = False
    try:
        _root_sync(staging_directory)
    except OSError:
        sync_uncertain = True
    status_result = (PreparationStatus.committed_sync_uncertain if sync_uncertain
                     else PreparationStatus.committed)
    return PreparationResult(status_result, manifest_final, envelope_final,
                             generation, signing_key_id, bundle_key_id,
                             hashlib.sha256(manifest).hexdigest())


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--openssl", required=True, type=Path)
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--signing-metadata", required=True, type=Path)
    parser.add_argument("--bundle-metadata", required=True, type=Path)
    parser.add_argument("--staging-directory", required=True, type=Path)
    parser.add_argument("--generation", required=True, type=int)
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--expires-at", required=True)
    parser.add_argument("--status", required=True, choices=("active", "disabled"))
    parser.add_argument("--minimum-client-protocol", type=int, default=1)
    parser.add_argument("--minimum-upload-version", required=True)
    parser.add_argument("--request-url")
    parser.add_argument("--release-url", required=True)
    parser.add_argument("--user-message")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    try:
        result = prepare(openssl=arguments.openssl, private_key=arguments.private_key,
                         signing_metadata_path=arguments.signing_metadata,
                         bundle_metadata_path=arguments.bundle_metadata,
                         staging_directory=arguments.staging_directory,
                         generation=arguments.generation, published_at=arguments.published_at,
                         expires_at=arguments.expires_at, status=arguments.status,
                         minimum_client_protocol=arguments.minimum_client_protocol,
                         minimum_upload_version=arguments.minimum_upload_version,
                         request_url=arguments.request_url, release_url=arguments.release_url,
                         user_message=arguments.user_message)
    except (OSError, subprocess.SubprocessError, PreparationError) as error:
        print(f"manifest preparation failed: {error}", file=sys.stderr)
        return 1
    print(f"status: {result.status.value}")
    print(f"manifest: {result.manifest_path}")
    print(f"signature envelope: {result.signature_path}")
    print(f"generation: {result.generation}")
    print(f"signing key ID: {result.signing_key_id}")
    print(f"bundle key ID: {result.bundle_key_id}")
    print(f"manifest SHA-256: {result.manifest_sha256}")
    return 0 if result.status is PreparationStatus.committed else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
