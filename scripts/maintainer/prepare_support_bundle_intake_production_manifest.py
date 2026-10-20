#!/usr/bin/env python3
"""Prepare production generation 1 using a Keychain-held request capability."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
import hashlib
import os
from pathlib import Path
import pwd
import selectors
import stat
import subprocess
import sys
import time
from typing import Callable

import prepare_support_bundle_intake_manifest as preparation


KEYCHAIN_SERVICE = "org.wsprrypi.support-intake"
KEYCHAIN_ACCOUNT = "wsprrypi-file-request"
MAX_REQUEST_URL_BYTES = 512


class ProductionPreparationError(RuntimeError):
    pass


class ProductionPreparationStatus(Enum):
    proposed = "proposed"
    committed = "committed"
    committed_sync_uncertain = "committed_sync_uncertain"


@dataclass(frozen=True)
class ProductionPreparationResult:
    status: ProductionPreparationStatus
    generation: int
    signing_key_id: str
    bundle_key_id: str
    manifest_sha256: str


@dataclass(frozen=True)
class ProductionPaths:
    openssl: Path
    security: Path
    private_key: Path
    signing_metadata: Path
    bundle_metadata: Path
    staging_root: Path


def production_paths() -> ProductionPaths:
    base = (Path(pwd.getpwuid(os.geteuid()).pw_dir)
            / "Library/Application Support/WsprryPi Support Intake")
    return ProductionPaths(
        Path("/opt/homebrew/opt/openssl@3/bin/openssl"), Path("/usr/bin/security"),
        base / "keys/signing/wsprrypi-intake-2026-01.ed25519-private.pem",
        base / "public-staging/wsprrypi-intake-2026-01.public.json",
        base / "public-staging/wsprrypi-bundle-2026-01.public.json",
        base / "manifests",
    )


def require_security(path: Path) -> Path:
    if path != Path("/usr/bin/security") or not path.is_absolute():
        raise ProductionPreparationError("Keychain executable must be exact /usr/bin/security")
    info = path.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode)
            or info.st_uid != 0 or info.st_mode & 0o022 or not os.access(path, os.X_OK)):
        raise ProductionPreparationError("Keychain executable is unsafe or unavailable")
    return path.resolve(strict=True)


def parse_request_url(encoded: bytes) -> str:
    if not encoded or len(encoded) > MAX_REQUEST_URL_BYTES or b"\x00" in encoded:
        raise ProductionPreparationError("Keychain request capability is invalid")
    try:
        value = encoded.decode("utf-8")
    except UnicodeError as error:
        raise ProductionPreparationError("Keychain request capability is invalid") from error
    if value.endswith("\n"):
        value = value[:-1]
    if "\n" in value or "\r" in value or not preparation.valid_request_url(value):
        raise ProductionPreparationError("Keychain request capability is invalid")
    return value


def keychain_request_url(
        security: Path,
        runner: Callable[[list[str], dict[str, str]], tuple[int, bytes]] | None = None) -> str:
    executable = require_security(security)
    arguments = [str(executable), "find-generic-password", "-a", KEYCHAIN_ACCOUNT,
                 "-s", KEYCHAIN_SERVICE, "-w"]
    environment = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"}
    if runner is None:
        def runner(command: list[str], env: dict[str, str]) -> tuple[int, bytes]:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
            assert process.stdout is not None
            encoded = bytearray()
            deadline = time.monotonic() + 10
            selector = selectors.DefaultSelector()
            try:
                selector.register(process.stdout, selectors.EVENT_READ)
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired(command, 10)
                    events = selector.select(remaining)
                    if not events:
                        raise subprocess.TimeoutExpired(command, 10)
                    chunk = os.read(process.stdout.fileno(),
                                    MAX_REQUEST_URL_BYTES + 1 - len(encoded))
                    if not chunk:
                        break
                    encoded.extend(chunk)
                    if len(encoded) > MAX_REQUEST_URL_BYTES:
                        process.kill()
                        break
                remaining = max(0.001, deadline - time.monotonic())
                code = process.wait(timeout=remaining)
            except Exception:
                process.kill()
                process.wait()
                raise
            finally:
                selector.close(); process.stdout.close()
            return code, bytes(encoded)
    code, encoded = runner(arguments, environment)
    if code != 0:
        raise ProductionPreparationError("Keychain request capability is unavailable")
    return parse_request_url(encoded)


def prepare_generation_1(*, approve: bool, paths: ProductionPaths,
                         published_at: str, expires_at: str,
                         request_provider: Callable[[Path], str] = keychain_request_url
                         ) -> ProductionPreparationResult:
    openssl = preparation.require_openssl(paths.openssl)
    private_key = preparation.require_file(paths.private_key, "private signing key", 0o400)
    signing_metadata = preparation.require_file(paths.signing_metadata, "signing metadata")
    bundle_metadata = preparation.require_file(paths.bundle_metadata, "bundle metadata")
    request_url = request_provider(paths.security)
    signing_key_id, expected_public = preparation.signing_metadata(signing_metadata)
    bundle_key_id, _ = preparation.bundle_metadata(bundle_metadata)
    code, public_der = preparation.bounded_tool_output(
        [str(openssl), "pkey", "-in", str(private_key), "-pubout", "-outform", "DER"],
        preparation.MAX_TOOL_OUTPUT_BYTES)
    if code != 0 or public_der != preparation.ED25519_SPKI_PREFIX + expected_public:
        raise ProductionPreparationError("private key does not match signing metadata")
    manifest = preparation.build_manifest(
        generation=1, published_at=published_at, expires_at=expires_at,
        status="active", minimum_client_protocol=1,
        minimum_upload_version="3.2.0", request_url=request_url,
        release_url="https://github.com/WsprryPi/WsprryPi/releases/latest",
        user_message=None, bundle_key_id=bundle_key_id)
    digest = hashlib.sha256(manifest).hexdigest()
    if not approve:
        if paths.staging_root.exists() or paths.staging_root.is_symlink():
            staging = preparation.require_staging(paths.staging_root)
            if any(staging.iterdir()):
                raise ProductionPreparationError("manifest staging root is not empty")
        return ProductionPreparationResult(ProductionPreparationStatus.proposed, 1,
                                           signing_key_id, bundle_key_id, digest)
    staging = preparation.require_staging(paths.staging_root)
    if any(staging.iterdir()):
        raise ProductionPreparationError("manifest staging root is not empty")
    prepared = preparation.prepare(
        openssl=openssl, private_key=private_key,
        signing_metadata_path=signing_metadata, bundle_metadata_path=bundle_metadata,
        staging_directory=staging, generation=1, published_at=published_at,
        expires_at=expires_at, status="active", minimum_client_protocol=1,
        minimum_upload_version="3.2.0", request_url=request_url,
        release_url="https://github.com/WsprryPi/WsprryPi/releases/latest",
        user_message=None)
    status = (ProductionPreparationStatus.committed
              if prepared.status is preparation.PreparationStatus.committed
              else ProductionPreparationStatus.committed_sync_uncertain)
    return ProductionPreparationResult(status, prepared.generation,
                                       prepared.signing_key_id, prepared.bundle_key_id,
                                       prepared.manifest_sha256)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--approve", action="store_true")
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--expires-at", required=True)
    arguments = parser.parse_args(argv)
    try:
        result = prepare_generation_1(
            approve=arguments.approve, paths=production_paths(),
            published_at=arguments.published_at, expires_at=arguments.expires_at)
    except (OSError, subprocess.SubprocessError, preparation.PreparationError,
            ProductionPreparationError):
        print("production manifest preparation failed", file=sys.stderr)
        return 1
    print(f"status: {result.status.value}")
    print(f"generation: {result.generation}")
    print(f"signing key ID: {result.signing_key_id}")
    print(f"bundle key ID: {result.bundle_key_id}")
    print(f"manifest SHA-256: {result.manifest_sha256}")
    return 0 if result.status is not ProductionPreparationStatus.committed_sync_uncertain else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
