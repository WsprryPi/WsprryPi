#!/usr/bin/env python3
"""Generate and validate a deterministic manifest for packaged WsprryPi UI files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


SCHEMA_VERSION = 1
MANIFEST_FILENAME = "ui-manifest.json"
EXCLUDED_TOP_LEVEL_DIRECTORIES = frozenset({"cache", "backups"})
EXCLUDED_FILENAMES = frozenset({MANIFEST_FILENAME, ".DS_Store"})
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
SOURCE_COMMIT_PATTERN = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")


class ManifestError(ValueError):
    """Raised when UI files or manifest data violate the schema contract."""


def normalized_relative_path(ui_root: Path, candidate: Path) -> str:
    """Return a portable, safe path for a file beneath *ui_root*."""
    try:
        relative = candidate.relative_to(ui_root)
    except ValueError as error:
        raise ManifestError(f"UI file is outside the UI root: {candidate}") from error

    path = relative.as_posix()
    pure = PurePosixPath(path)
    if (
        not path
        or path.startswith("/")
        or "\\" in path
        or pure.is_absolute()
        or any(part in {"", ".", ".."} for part in pure.parts)
    ):
        raise ManifestError(f"unsafe UI manifest path: {path!r}")
    return path


def is_covered_path(relative_path: str) -> bool:
    """Return whether a normalized installed-UI path belongs in the identity."""
    pure = PurePosixPath(relative_path)
    if not pure.parts or any(part in {"", ".", ".."} for part in pure.parts):
        return False
    if pure.parts[0] in EXCLUDED_TOP_LEVEL_DIRECTORIES:
        return False
    if pure.name in EXCLUDED_FILENAMES:
        return False
    return True


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def collect_file_records(ui_root: Path) -> list[dict[str, str]]:
    """Hash covered regular files below *ui_root* in normalized path order."""
    root = ui_root.resolve()
    if not root.is_dir():
        raise ManifestError(f"UI root is not a directory: {ui_root}")

    def raise_walk_error(error: OSError) -> None:
        raise error

    records: list[dict[str, str]] = []
    for current, directory_names, file_names in os.walk(
        root, topdown=True, onerror=raise_walk_error, followlinks=False
    ):
        current_path = Path(current)
        if current_path == root:
            directory_names[:] = [
                name for name in directory_names
                if name not in EXCLUDED_TOP_LEVEL_DIRECTORIES
            ]
        for directory_name in directory_names:
            candidate = current_path / directory_name
            if candidate.is_symlink():
                raise ManifestError(
                    f"symbolic links are not supported in UI artifacts: {candidate}"
                )
        directory_names.sort(key=lambda name: name.encode("utf-8"))
        for file_name in sorted(file_names, key=lambda name: name.encode("utf-8")):
            candidate = current_path / file_name
            if candidate.is_symlink():
                raise ManifestError(
                    f"symbolic links are not supported in UI artifacts: {candidate}"
                )
            if not candidate.is_file():
                continue
            relative = normalized_relative_path(root, candidate)
            if is_covered_path(relative):
                records.append({"path": relative, "sha256": sha256_file(candidate)})

    records.sort(key=lambda record: record["path"].encode("utf-8"))
    return records


def packaged_ui_build_id(records: Iterable[dict[str, str]]) -> str:
    """Derive the schema-v1 identity from normalized paths and content hashes."""
    digest = hashlib.sha256()
    digest.update(b"wsprrypi-ui-manifest-v1\0")
    for record in records:
        path = record["path"]
        content_hash = record["sha256"]
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(content_hash.encode("ascii"))
        digest.update(b"\n")
    return f"sha256:{digest.hexdigest()}"


def build_manifest(ui_root: Path, source_commit: str, application_version: str) -> dict[str, Any]:
    source_commit = source_commit.strip()
    application_version = application_version.strip()
    if not SOURCE_COMMIT_PATTERN.fullmatch(source_commit):
        raise ManifestError("source_commit must be a lowercase 40- or 64-character Git object ID")
    if not application_version:
        raise ManifestError("application_version must not be empty")

    records = collect_file_records(ui_root)
    return {
        "schema_version": SCHEMA_VERSION,
        "packaged_ui_build_id": packaged_ui_build_id(records),
        "source_commit": source_commit,
        "application_version": application_version,
        "files": records,
    }


def validate_manifest(manifest: Any) -> dict[str, Any]:
    """Validate schema-v1 structure, safe paths, ordering, and derived identity."""
    if not isinstance(manifest, dict):
        raise ManifestError("manifest must be a JSON object")
    expected_fields = {
        "schema_version", "packaged_ui_build_id", "source_commit",
        "application_version", "files",
    }
    if set(manifest) != expected_fields:
        raise ManifestError("manifest fields do not match schema version 1")
    if manifest["schema_version"] != SCHEMA_VERSION or isinstance(manifest["schema_version"], bool):
        raise ManifestError("unsupported schema_version")
    if (
        not isinstance(manifest["source_commit"], str)
        or not SOURCE_COMMIT_PATTERN.fullmatch(manifest["source_commit"])
    ):
        raise ManifestError("source_commit must be a lowercase 40- or 64-character Git object ID")
    if (
        not isinstance(manifest["application_version"], str)
        or not manifest["application_version"].strip()
    ):
        raise ManifestError("application_version must be a non-empty string")
    if not isinstance(manifest["files"], list):
        raise ManifestError("files must be an array")

    records: list[dict[str, str]] = []
    previous_path: bytes | None = None
    for entry in manifest["files"]:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256"}:
            raise ManifestError("each file entry must contain only path and sha256")
        path = entry["path"]
        content_hash = entry["sha256"]
        if not isinstance(path, str) or not is_covered_path(path):
            raise ManifestError(f"unsafe or excluded manifest path: {path!r}")
        pure = PurePosixPath(path)
        if pure.as_posix() != path or path.startswith("/") or "\\" in path:
            raise ManifestError(f"manifest path is not normalized: {path!r}")
        if not isinstance(content_hash, str) or not SHA256_PATTERN.fullmatch(content_hash):
            raise ManifestError(f"invalid SHA-256 for {path!r}")
        encoded_path = path.encode("utf-8")
        if previous_path is not None and encoded_path <= previous_path:
            raise ManifestError("file entries must have unique paths in deterministic order")
        previous_path = encoded_path
        records.append({"path": path, "sha256": content_hash})

    expected_id = packaged_ui_build_id(records)
    if manifest["packaged_ui_build_id"] != expected_id:
        raise ManifestError("packaged_ui_build_id does not match the file records")
    return manifest


def serialize_manifest(manifest: dict[str, Any]) -> bytes:
    validate_manifest(manifest)
    return (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def write_atomic(destination: Path, content: bytes) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=destination.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as temporary_file:
            temporary_file.write(content)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_name, destination)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ManifestError(f"could not read manifest: {error}") from error
    return validate_manifest(value)


def unknown_installation_classification(
    error: str,
    packaged_ui_build_id_value: str | None = None,
    installed_ui_build_id_value: str | None = None,
) -> dict[str, Any]:
    """Return a fail-closed result when either UI identity is unavailable."""
    return {
        "schema_version": SCHEMA_VERSION,
        "installed_state": "unknown",
        "packaged_ui_build_id": packaged_ui_build_id_value,
        "installed_ui_build_id": installed_ui_build_id_value,
        "modified_files": [],
        "added_files": [],
        "missing_files": [],
        "error": error,
    }


def classify_installed_ui(ui_root: Path, manifest_path: Path) -> dict[str, Any]:
    """Compare installed UI files with a validated immutable package manifest.

    Classification is read-only. Invalid or unreadable inputs produce an
    ``unknown`` result rather than treating an unverified installation as
    packaged or locally modified.
    """
    try:
        installed_records = collect_file_records(ui_root)
    except (ManifestError, OSError, UnicodeError) as error:
        return unknown_installation_classification(str(error))
    installed_id = packaged_ui_build_id(installed_records)

    try:
        manifest = load_manifest(manifest_path)
    except ManifestError as error:
        return unknown_installation_classification(
            str(error), installed_ui_build_id_value=installed_id
        )

    packaged_id = manifest["packaged_ui_build_id"]

    packaged_by_path = {
        record["path"]: record["sha256"] for record in manifest["files"]
    }
    installed_by_path = {
        record["path"]: record["sha256"] for record in installed_records
    }
    packaged_paths = set(packaged_by_path)
    installed_paths = set(installed_by_path)
    modified_files = sorted(
        (
            path for path in packaged_paths & installed_paths
            if packaged_by_path[path] != installed_by_path[path]
        ),
        key=lambda path: path.encode("utf-8"),
    )
    added_files = sorted(
        installed_paths - packaged_paths,
        key=lambda path: path.encode("utf-8"),
    )
    missing_files = sorted(
        packaged_paths - installed_paths,
        key=lambda path: path.encode("utf-8"),
    )
    installed_state = (
        "packaged"
        if installed_id == packaged_id
        and not modified_files
        and not added_files
        and not missing_files
        else "locally_modified"
    )
    return {
        "schema_version": SCHEMA_VERSION,
        "installed_state": installed_state,
        "packaged_ui_build_id": packaged_id,
        "installed_ui_build_id": installed_id,
        "modified_files": modified_files,
        "added_files": added_files,
        "missing_files": missing_files,
        "error": None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--source-commit")
    parser.add_argument("--application-version")
    parser.add_argument("--validate", type=Path)
    args = parser.parse_args()

    try:
        if args.validate is not None:
            if any(value is not None for value in (
                args.ui_root, args.output, args.source_commit, args.application_version
            )):
                parser.error("--validate cannot be combined with generation arguments")
            load_manifest(args.validate)
            print(f"UI manifest valid: {args.validate}")
            return 0

        if None in (args.ui_root, args.output, args.source_commit, args.application_version):
            parser.error(
                "generation requires --ui-root, --output, --source-commit, and --application-version"
            )
        manifest = build_manifest(
            args.ui_root, args.source_commit, args.application_version
        )
        write_atomic(args.output, serialize_manifest(manifest))
    except (ManifestError, OSError, UnicodeError) as error:
        print(f"UI manifest generation failed: {error}", file=os.sys.stderr)
        return 1

    print(f"UI manifest written: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
