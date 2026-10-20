#!/usr/bin/env python3
"""Compile reviewed public support-intake metadata into deterministic C++."""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

import provision_support_bundle_age_key as age_provisioning


MAX_METADATA_BYTES = 4096
SIGNING_ID = re.compile(r"^wsprrypi-intake-[0-9]{4}-[0-9]{2}$")
BUNDLE_ID = re.compile(r"^wsprrypi-bundle-[0-9]{4}-[0-9]{2}$")
TIMESTAMP = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")


class CompilationError(RuntimeError):
    pass


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise CompilationError("metadata contains a duplicate JSON key")
        result[key] = value
    return result


def exact_keys(value: object, expected: set[str]) -> dict:
    if type(value) is not dict or set(value) != expected:
        raise CompilationError("metadata fields are invalid")
    return value


def read_metadata(path: Path) -> dict:
    if not path.is_absolute():
        raise CompilationError("metadata path must be absolute")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        info = os.fstat(descriptor)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                or info.st_nlink != 1 or info.st_size <= 0
                or info.st_size > MAX_METADATA_BYTES
                or stat.S_IMODE(info.st_mode) & 0o022):
            raise CompilationError("metadata file is unsafe")
        with os.fdopen(os.dup(descriptor), "rb") as stream:
            encoded = stream.read(MAX_METADATA_BYTES + 1)
        after = os.fstat(descriptor)
        if ((info.st_dev, info.st_ino, info.st_size) !=
                (after.st_dev, after.st_ino, after.st_size) or len(encoded) != info.st_size):
            raise CompilationError("metadata file changed while reading")
    finally:
        os.close(descriptor)
    try:
        value = json.loads(encoded.decode("utf-8"), object_pairs_hook=unique_object,
                           parse_constant=lambda _: (_ for _ in ()).throw(
                               CompilationError("invalid JSON number")))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CompilationError("metadata is not strict UTF-8 JSON") from error
    return exact_keys(value, {"schema_version", "project_id", "purpose", "algorithm",
                              "key_id", "created_at_utc", "fingerprint"} |
                             ({"public_key"} if value.get("purpose") ==
                               "support_intake_manifest_signing" else {"recipient"}))


def common(value: dict, purpose: str, algorithm: str, pattern: re.Pattern[str]) -> str:
    key_id = value.get("key_id")
    if (type(value.get("schema_version")) is not int or value["schema_version"] != 1
            or value.get("project_id") != "wsprrypi" or value.get("purpose") != purpose
            or value.get("algorithm") != algorithm or type(key_id) is not str
            or not pattern.fullmatch(key_id) or type(value.get("created_at_utc")) is not str
            or not TIMESTAMP.fullmatch(value["created_at_utc"])):
        raise CompilationError("metadata policy is invalid")
    try:
        dt.datetime.strptime(value["created_at_utc"], "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        raise CompilationError("metadata timestamp is invalid") from error
    fingerprint = exact_keys(value.get("fingerprint"), {"algorithm", "value"})
    if (fingerprint.get("algorithm") != "sha256" or type(fingerprint.get("value")) is not str
            or not re.fullmatch(r"[0-9a-f]{64}", fingerprint["value"])):
        raise CompilationError("metadata fingerprint is invalid")
    return key_id


def signing_metadata(path: Path) -> tuple[str, bytes]:
    value = read_metadata(path)
    key_id = common(value, "support_intake_manifest_signing", "Ed25519", SIGNING_ID)
    public = exact_keys(value.get("public_key"), {"encoding", "value"})
    encoded = public.get("value")
    if public.get("encoding") != "base64url" or type(encoded) is not str or len(encoded) != 43:
        raise CompilationError("signing public key encoding is invalid")
    try:
        raw = base64.b64decode(encoded + "=", altchars=b"-_", validate=True)
    except (ValueError, TypeError) as error:
        raise CompilationError("signing public key encoding is invalid") from error
    if (len(raw) != 32 or base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=") != encoded
            or not any(raw)):
        raise CompilationError("signing public key is invalid")
    if hashlib.sha256(raw).hexdigest() != value["fingerprint"]["value"]:
        raise CompilationError("signing public key fingerprint does not match")
    return key_id, raw


def bundle_metadata(path: Path) -> str:
    value = read_metadata(path)
    key_id = common(value, "support_bundle_encryption", "age-x25519", BUNDLE_ID)
    recipient = value.get("recipient")
    if type(recipient) is not str or not age_provisioning.valid_age_x25519_recipient(recipient):
        raise CompilationError("bundle recipient is invalid")
    if hashlib.sha256(recipient.encode("ascii")).hexdigest() != value["fingerprint"]["value"]:
        raise CompilationError("bundle recipient fingerprint does not match")
    return key_id


def render(signing: list[tuple[str, bytes]], bundles: list[str]) -> bytes:
    lines = ["#pragma once", "", '#include "support_bundle_intake_runtime.hpp"', "",
             "inline SupportBundleIntakeRuntimeTrust support_bundle_intake_compiled_trust() {",
             "    return {"]
    lines.append("        {")
    for key_id, raw in signing:
        octets = ", ".join(f"0x{byte:02x}" for byte in raw)
        lines.append(f'            {{"{key_id}", {{{octets}}}}},')
    lines.extend(["        },", "        {"])
    for key_id in bundles:
        lines.append(f'            "{key_id}",')
    lines.extend(["        },", "    };", "}", ""])
    return "\n".join(lines).encode("ascii")


def compile_trust(signing_paths: list[Path], bundle_paths: list[Path], output: Path) -> None:
    if (not signing_paths or len(signing_paths) > 16 or not bundle_paths
            or len(bundle_paths) > 16 or not output.is_absolute()):
        raise CompilationError("signing metadata, bundle metadata, and absolute output are required")
    signing = sorted((signing_metadata(path) for path in signing_paths), key=lambda item: item[0])
    bundles = sorted(bundle_metadata(path) for path in bundle_paths)
    if len({item[0] for item in signing}) != len(signing) or len(set(bundles)) != len(bundles):
        raise CompilationError("metadata contains a duplicate key ID")
    encoded = render(signing, bundles)
    parent_info = output.parent.lstat()
    if (stat.S_ISLNK(parent_info.st_mode) or not stat.S_ISDIR(parent_info.st_mode)
            or parent_info.st_uid != os.geteuid() or stat.S_IMODE(parent_info.st_mode) & 0o022):
        raise CompilationError("trust output directory is unsafe")
    parent = output.parent.resolve(strict=True)
    partial = parent / f".{output.name}.partial"
    if partial.exists() or partial.is_symlink():
        raise CompilationError("trust output partial already exists")
    descriptor = os.open(partial, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(partial, output)
        directory = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except Exception:
        try:
            partial.unlink()
        except FileNotFoundError:
            pass
        raise


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--signing-metadata", action="append", required=True, type=Path)
    parser.add_argument("--bundle-metadata", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        compile_trust(arguments.signing_metadata, arguments.bundle_metadata, arguments.output)
    except (OSError, CompilationError) as error:
        print(f"trust compilation failed: {error}", file=sys.stderr)
        return 1
    print(f"compiled public trust: {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
