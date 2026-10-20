#!/usr/bin/env python3
"""Provision a WsprryPi Ed25519 intake-signing key and public metadata."""

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
import subprocess
import sys
import tempfile


KEY_ID = re.compile(r"^wsprrypi-intake-[0-9]{4}-[0-9]{2}$")
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
MAX_METADATA_BYTES = 4096
MAX_PUBLIC_DER_BYTES = 256
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class ProvisioningError(RuntimeError):
    pass


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
        raise ProvisioningError("OpenSSL executable must be absolute")
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ProvisioningError("OpenSSL executable must be a regular non-symlink file")
    if info.st_mode & 0o022 or not os.access(path, os.X_OK):
        raise ProvisioningError("OpenSSL executable is unsafe or unavailable")
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


def raw_ed25519_public_key(der: bytes) -> bytes:
    if len(der) != len(ED25519_SPKI_PREFIX) + 32 or not der.startswith(ED25519_SPKI_PREFIX):
        raise ProvisioningError("OpenSSL returned a non-canonical Ed25519 public key")
    return der[len(ED25519_SPKI_PREFIX) :]


def provision(
    executable: Path,
    private_directory: Path,
    public_output: Path,
    key_id: str,
    timestamp: str | None,
) -> tuple[Path, Path]:
    if not KEY_ID.fullmatch(key_id):
        raise ProvisioningError("invalid WsprryPi intake signing key ID")
    executable = require_executable(executable)
    private_directory = require_private_directory(private_directory)
    if not public_output.is_absolute():
        raise ProvisioningError("public metadata output must be absolute")
    public_parent = public_output.parent.resolve(strict=True)
    if public_output.exists() or public_output.is_symlink():
        raise ProvisioningError("public metadata output already exists")

    identity = private_directory / f"{key_id}.ed25519-private.pem"
    identity_partial = private_directory / f".{key_id}.ed25519-private.pem.partial"
    public_partial = public_parent / f".{public_output.name}.partial"
    identity_published = False
    public_published = False
    for candidate in (identity, identity_partial, public_partial):
        if candidate.exists() or candidate.is_symlink():
            raise ProvisioningError("provisioning output collision")

    try:
        prior_umask = os.umask(0o077)
        try:
            generated = subprocess.run(
                [str(executable), "genpkey", "-algorithm", "ED25519", "-out", str(identity_partial)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
                timeout=30,
            )
        finally:
            os.umask(prior_umask)
        if generated.returncode != 0:
            raise ProvisioningError("OpenSSL Ed25519 key generation failed")
        info = identity_partial.lstat()
        if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode)
                or info.st_uid != os.geteuid() or info.st_nlink != 1 or info.st_size <= 0):
            raise ProvisioningError("OpenSSL produced an unsafe private key")
        os.chmod(identity_partial, 0o400)

        with tempfile.TemporaryFile() as public_der_output:
            derived = subprocess.run(
                [str(executable), "pkey", "-in", str(identity_partial), "-pubout", "-outform", "DER"],
                stdin=subprocess.DEVNULL,
                stdout=public_der_output,
                stderr=subprocess.DEVNULL,
                check=False,
                timeout=30,
            )
            public_der_output.seek(0)
            public_der = public_der_output.read(MAX_PUBLIC_DER_BYTES + 1)
        if derived.returncode != 0 or len(public_der) > MAX_PUBLIC_DER_BYTES:
            raise ProvisioningError("OpenSSL public-key derivation failed")
        raw_public = raw_ed25519_public_key(public_der)
        encoded_public = base64.urlsafe_b64encode(raw_public).decode("ascii").rstrip("=")
        if len(encoded_public) != 43 or "=" in encoded_public:
            raise ProvisioningError("public key encoding was not canonical")
        metadata = {
            "schema_version": 1,
            "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing",
            "algorithm": "Ed25519",
            "key_id": key_id,
            "public_key": {"encoding": "base64url", "value": encoded_public},
            "created_at_utc": created_at(timestamp),
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(raw_public).hexdigest()},
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
        for candidate in (identity_partial, public_partial,
                          public_output if public_published else None,
                          identity if identity_published else None):
            if candidate is None:
                continue
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass
            except OSError:
                pass
        raise


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--openssl", required=True, type=Path)
    parser.add_argument("--private-directory", required=True, type=Path)
    parser.add_argument("--public-output", required=True, type=Path)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--created-at")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    try:
        identity, metadata = provision(arguments.openssl, arguments.private_directory,
                                       arguments.public_output, arguments.key_id,
                                       arguments.created_at)
    except (OSError, subprocess.SubprocessError, ProvisioningError) as error:
        print(f"provisioning failed: {error}", file=sys.stderr)
        return 1
    print(f"private signing key created: {identity}")
    print(f"public metadata created: {metadata}")
    print("Back up the private key in the approved password vault before publishing metadata.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
