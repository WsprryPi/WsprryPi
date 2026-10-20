#!/usr/bin/env python3
"""Read-only preflight for a proposed WsprryPi production identity ceremony."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
import os
from pathlib import Path
import stat
import sys

import provision_support_bundle_age_key as bundle_provisioning
import provision_support_bundle_intake_signing_key as signing_provisioning


class PreflightStatus(Enum):
    ready = "ready"
    invalid_key_ids = "invalid_key_ids"
    unsafe_executable = "unsafe_executable"
    unsafe_private_storage = "unsafe_private_storage"
    unsafe_public_output = "unsafe_public_output"
    output_collision = "output_collision"
    preflight_failed = "preflight_failed"


@dataclass(frozen=True)
class PreflightResult:
    status: PreflightStatus
    signing_key_id: str = ""
    bundle_key_id: str = ""


def contained(path: Path, root: Path) -> bool:
    return path == root or path.is_relative_to(root)


def public_parent(output: Path, private_roots: tuple[Path, Path]) -> Path:
    if not output.is_absolute():
        raise ValueError("public output is not absolute")
    info = output.parent.lstat()
    if (stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode)
            or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) & 0o022):
        raise ValueError("public output directory is unsafe")
    parent = output.parent.resolve(strict=True)
    if any(contained(parent, root) for root in private_roots):
        raise ValueError("public output is inside private storage")
    return parent


def collision_paths(signing_private: Path, bundle_private: Path,
                    signing_output: Path, bundle_output: Path,
                    signing_key_id: str, bundle_key_id: str) -> tuple[Path, ...]:
    return (
        signing_private / f"{signing_key_id}.ed25519-private.pem",
        signing_private / f".{signing_key_id}.ed25519-private.pem.partial",
        bundle_private / f"{bundle_key_id}.age-identity.txt",
        bundle_private / f".{bundle_key_id}.age-identity.txt.partial",
        signing_output,
        signing_output.parent / f".{signing_output.name}.partial",
        bundle_output,
        bundle_output.parent / f".{bundle_output.name}.partial",
    )


def preflight_internal(*, age_keygen: Path, openssl: Path, bundle_private_directory: Path,
                       signing_private_directory: Path, bundle_public_output: Path,
                       signing_public_output: Path, bundle_key_id: str,
                       signing_key_id: str) -> PreflightResult:
    if (not bundle_provisioning.KEY_ID.fullmatch(bundle_key_id)
            or not signing_provisioning.KEY_ID.fullmatch(signing_key_id)):
        return PreflightResult(PreflightStatus.invalid_key_ids)
    identity = {"signing_key_id": signing_key_id, "bundle_key_id": bundle_key_id}
    try:
        bundle_provisioning.require_executable(age_keygen)
        signing_provisioning.require_executable(openssl)
    except (OSError, bundle_provisioning.ProvisioningError,
            signing_provisioning.ProvisioningError):
        return PreflightResult(PreflightStatus.unsafe_executable, **identity)
    try:
        bundle_private = bundle_provisioning.require_private_directory(
            bundle_private_directory)
        signing_private = signing_provisioning.require_private_directory(
            signing_private_directory)
    except (OSError, bundle_provisioning.ProvisioningError,
            signing_provisioning.ProvisioningError):
        return PreflightResult(PreflightStatus.unsafe_private_storage, **identity)
    try:
        signing_parent = public_parent(signing_public_output,
                                       (signing_private, bundle_private))
        bundle_parent = public_parent(bundle_public_output,
                                      (signing_private, bundle_private))
        signing_output = signing_parent / signing_public_output.name
        bundle_output = bundle_parent / bundle_public_output.name
        if signing_output == bundle_output:
            raise ValueError("public outputs are identical")
    except (OSError, ValueError):
        return PreflightResult(PreflightStatus.unsafe_public_output, **identity)
    candidates = collision_paths(signing_private, bundle_private, signing_output,
                                 bundle_output, signing_key_id, bundle_key_id)
    if any(path.exists() or path.is_symlink() for path in candidates):
        return PreflightResult(PreflightStatus.output_collision, **identity)
    return PreflightResult(PreflightStatus.ready, **identity)


def preflight(*, age_keygen: Path, openssl: Path, bundle_private_directory: Path,
              signing_private_directory: Path, bundle_public_output: Path,
              signing_public_output: Path, bundle_key_id: str,
              signing_key_id: str) -> PreflightResult:
    try:
        return preflight_internal(
            age_keygen=age_keygen, openssl=openssl,
            bundle_private_directory=bundle_private_directory,
            signing_private_directory=signing_private_directory,
            bundle_public_output=bundle_public_output,
            signing_public_output=signing_public_output,
            bundle_key_id=bundle_key_id, signing_key_id=signing_key_id)
    except Exception:
        return PreflightResult(PreflightStatus.preflight_failed)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--age-keygen", required=True, type=Path)
    parser.add_argument("--openssl", required=True, type=Path)
    parser.add_argument("--bundle-private-directory", required=True, type=Path)
    parser.add_argument("--signing-private-directory", required=True, type=Path)
    parser.add_argument("--bundle-public-output", required=True, type=Path)
    parser.add_argument("--signing-public-output", required=True, type=Path)
    parser.add_argument("--bundle-key-id", required=True)
    parser.add_argument("--signing-key-id", required=True)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    result = preflight(age_keygen=arguments.age_keygen, openssl=arguments.openssl,
                       bundle_private_directory=arguments.bundle_private_directory,
                       signing_private_directory=arguments.signing_private_directory,
                       bundle_public_output=arguments.bundle_public_output,
                       signing_public_output=arguments.signing_public_output,
                       bundle_key_id=arguments.bundle_key_id,
                       signing_key_id=arguments.signing_key_id)
    print(f"status: {result.status.value}")
    if result.signing_key_id:
        print(f"signing key ID: {result.signing_key_id}")
        print(f"bundle key ID: {result.bundle_key_id}")
    return 0 if result.status is PreflightStatus.ready else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
