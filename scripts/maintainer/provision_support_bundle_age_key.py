#!/usr/bin/env python3
"""Provision a WsprryPi support-bundle age identity and public metadata."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys


KEY_ID = re.compile(r"^wsprrypi-bundle-[0-9]{4}-[0-9]{2}$")
BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
MAX_METADATA_BYTES = 4096
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class ProvisioningError(RuntimeError):
    pass


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


def valid_age_x25519_recipient(recipient: str) -> bool:
    if recipient != recipient.lower() or not recipient.startswith("age1"):
        return False
    separator = recipient.rfind("1")
    encoded = recipient[separator + 1 :]
    if len(encoded) < 7 or any(character not in BECH32_CHARSET for character in encoded):
        return False
    hrp_values = [ord(character) >> 5 for character in "age"]
    hrp_values += [0]
    hrp_values += [ord(character) & 31 for character in "age"]
    values = [BECH32_CHARSET.index(character) for character in encoded]
    if bech32_polymod(hrp_values + values) != 1:
        return False

    accumulator = 0
    bits = 0
    decoded = bytearray()
    for value in values[:-6]:
        accumulator = (accumulator << 5) | value
        bits += 5
        while bits >= 8:
            bits -= 8
            decoded.append((accumulator >> bits) & 0xFF)
    if bits >= 5 or ((accumulator << (8 - bits)) & 0xFF):
        return False
    return len(decoded) == 32


def require_private_directory(path: Path) -> Path:
    if not path.is_absolute():
        raise ProvisioningError("private directory must be absolute")
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise ProvisioningError("private directory must be a real directory")
    if info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700:
        raise ProvisioningError("private directory must be owned by this user with mode 0700")
    resolved = path.resolve(strict=True)
    if resolved == REPOSITORY_ROOT or resolved.is_relative_to(REPOSITORY_ROOT):
        raise ProvisioningError("private directory must be outside the repository")
    return resolved


def require_executable(path: Path) -> Path:
    if not path.is_absolute():
        raise ProvisioningError("age-keygen executable must be absolute")
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ProvisioningError("age-keygen executable must be a regular non-symlink file")
    if info.st_mode & 0o022 or not os.access(path, os.X_OK):
        raise ProvisioningError("age-keygen executable is unsafe or unavailable")
    return path.resolve(strict=True)


def created_at(value: str | None) -> str:
    if value is None:
        return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
            "+00:00", "Z"
        )
    if not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", value):
        raise ProvisioningError("created-at must be UTC YYYY-MM-DDTHH:MM:SSZ")
    try:
        dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        raise ProvisioningError("created-at is not a valid UTC timestamp") from error
    return value


def provision(
    executable: Path,
    private_directory: Path,
    public_output: Path,
    key_id: str,
    timestamp: str | None,
) -> tuple[Path, Path]:
    if not KEY_ID.fullmatch(key_id):
        raise ProvisioningError("invalid WsprryPi bundle key ID")
    executable = require_executable(executable)
    private_directory = require_private_directory(private_directory)
    if not public_output.is_absolute():
        raise ProvisioningError("public metadata output must be absolute")
    public_parent = public_output.parent.resolve(strict=True)
    if public_output.exists() or public_output.is_symlink():
        raise ProvisioningError("public metadata output already exists")

    identity = private_directory / f"{key_id}.age-identity.txt"
    identity_partial = private_directory / f".{key_id}.age-identity.txt.partial"
    public_partial = public_parent / f".{public_output.name}.partial"
    identity_published = False
    public_published = False
    for candidate in (identity, identity_partial, public_partial):
        if candidate.exists() or candidate.is_symlink():
            raise ProvisioningError("provisioning output collision")

    try:
        generated = subprocess.run(
            [str(executable), "-o", str(identity_partial)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=30,
        )
        if generated.returncode != 0:
            raise ProvisioningError("age-keygen failed")
        info = identity_partial.lstat()
        if (
            stat.S_ISLNK(info.st_mode)
            or not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.geteuid()
            or info.st_size <= 0
        ):
            raise ProvisioningError("age-keygen produced an unsafe identity")
        os.chmod(identity_partial, 0o400)

        derived = subprocess.run(
            [str(executable), "-y", str(identity_partial)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=30,
            text=True,
        )
        recipient = derived.stdout.removesuffix("\n")
        if (
            derived.returncode != 0
            or derived.stdout != recipient + "\n"
            or not valid_age_x25519_recipient(recipient)
        ):
            raise ProvisioningError("age-keygen returned an invalid X25519 recipient")

        fingerprint = hashlib.sha256(recipient.encode("ascii")).hexdigest()
        metadata = {
            "schema_version": 1,
            "project_id": "wsprrypi",
            "purpose": "support_bundle_encryption",
            "algorithm": "age-x25519",
            "key_id": key_id,
            "recipient": recipient,
            "created_at_utc": created_at(timestamp),
            "fingerprint": {"algorithm": "sha256", "value": fingerprint},
        }
        encoded = (json.dumps(metadata, indent=2, separators=(",", ": ")) + "\n").encode("utf-8")
        if len(encoded) > MAX_METADATA_BYTES:
            raise ProvisioningError("public metadata is oversized")
        descriptor = os.open(public_partial, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(descriptor, "wb", closefd=True) as output:
                output.write(encoded)
                output.flush()
                os.fsync(output.fileno())
        except Exception:
            try:
                os.close(descriptor)
            except OSError:
                pass
            raise
        os.link(identity_partial, identity)
        identity_published = True
        identity_partial.unlink()
        os.link(public_partial, public_output)
        public_published = True
        public_partial.unlink()
        return identity, public_output
    except Exception:
        for candidate in (
            identity_partial,
            public_partial,
            public_output if public_published else None,
            identity if identity_published else None,
        ):
            if candidate is None:
                continue
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass
            except OSError:
                # Continue best-effort rollback so one filesystem error does not
                # prevent removal of the other sensitive or public output.
                pass
        raise


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--age-keygen", required=True, type=Path)
    parser.add_argument("--private-directory", required=True, type=Path)
    parser.add_argument("--public-output", required=True, type=Path)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--created-at")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    try:
        identity, metadata = provision(
            arguments.age_keygen,
            arguments.private_directory,
            arguments.public_output,
            arguments.key_id,
            arguments.created_at,
        )
    except (OSError, subprocess.SubprocessError, ProvisioningError) as error:
        print(f"provisioning failed: {error}", file=sys.stderr)
        return 1
    print(f"private identity created: {identity}")
    print(f"public metadata created: {metadata}")
    print("Back up the private identity in the approved password vault before publishing metadata.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
