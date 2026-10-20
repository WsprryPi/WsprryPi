#!/usr/bin/env python3
"""Build, validate, and transactionally publish the installed WsprryPi UI."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import pwd
import grp
import shutil
import stat
import subprocess
import sys
import tempfile
from typing import Callable

MANIFEST_FILENAME = "ui-manifest.json"


class UiPublicationError(RuntimeError):
    """Raised when a UI artifact cannot be staged, validated, or published."""

    def __init__(self, message: str, result: dict | None = None):
        super().__init__(message)
        self.result = result


def _write_json_atomic(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_name, path)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _new_result() -> dict:
    return {
        "schema_version": 1,
        "prior_state": "not_installed",
        "modified_files": [],
        "added_files": [],
        "missing_files": [],
        "prior_manifest_backup": None,
        "modification_report_path": None,
        "backup_directory": None,
        "backup_verified": None,
        "replacement_completed": False,
        "final_installed_state": None,
        "final_installed_ui_build_id": None,
        "error": None,
    }


def get_git_root() -> Path | None:
    script_dir = Path(__file__).resolve().parent
    try:
        return Path(subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"], cwd=script_dir, text=True
        ).strip())
    except subprocess.CalledProcessError:
        return None


def get_source_commit(git_root: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=git_root, text=True
        ).strip()
    except subprocess.CalledProcessError as error:
        raise UiPublicationError("unable to determine the UI source commit") from error


def get_application_version(git_root: Path) -> str:
    version_script = git_root / "scripts" / "get_semantic_version.py"
    try:
        version = subprocess.check_output(
            [sys.executable, str(version_script)], cwd=git_root, text=True
        ).strip()
    except subprocess.CalledProcessError as error:
        raise UiPublicationError("unable to determine the application version") from error
    if not version:
        raise UiPublicationError("application version is empty")
    return version


def load_manifest_module(git_root: Path):
    module_path = git_root / "WsprryPi-UI" / "scripts" / "generate_ui_manifest.py"
    if not module_path.is_file():
        raise UiPublicationError(f"manifest generator not found: {module_path}")
    spec = importlib.util.spec_from_file_location("wsprrypi_ui_manifest", module_path)
    if spec is None or spec.loader is None:
        raise UiPublicationError(f"unable to load manifest generator: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _safe_publication_paths(source: Path, target: Path) -> tuple[Path, Path]:
    if target.is_symlink():
        raise UiPublicationError(f"UI publication target must not be a symlink: {target}")
    source = source.resolve(strict=True)
    target = target.resolve(strict=False)
    if not source.is_dir():
        raise UiPublicationError(f"UI source directory not found: {source}")
    if target == Path(target.anchor) or target.parent == target:
        raise UiPublicationError(f"unsafe UI publication target: {target}")
    if target == source or source in target.parents or target in source.parents:
        raise UiPublicationError("UI source and publication target must not overlap")
    if target.exists() and not target.is_dir():
        raise UiPublicationError(f"UI publication target is not a directory: {target}")
    return source, target


def _set_tree_permissions(root: Path, owner: str | None, group: str | None) -> None:
    if (owner or group) and os.geteuid() != 0:
        raise UiPublicationError(
            "root privileges are required to make the UI immutable to the runtime account"
        )
    uid = pwd.getpwnam(owner).pw_uid if owner else -1
    gid = grp.getgrnam(group).gr_gid if group else -1
    static_uid = 0 if os.geteuid() == 0 else uid
    static_gid = 0 if os.geteuid() == 0 else gid
    for directory, child_directories, files in os.walk(root):
        directory_path = Path(directory)
        os.chmod(directory_path, 0o755)
        if owner or group:
            os.chown(directory_path, static_uid, static_gid)
        for name in child_directories:
            child = directory_path / name
            if child.is_symlink():
                raise UiPublicationError(f"symbolic links are not permitted: {child}")
        for name in files:
            child = directory_path / name
            if child.is_symlink():
                raise UiPublicationError(f"symbolic links are not permitted: {child}")
            os.chmod(child, 0o644)
            if owner or group:
                os.chown(child, static_uid, static_gid)

    for mutable_name in ("cache", "backups"):
        mutable_root = root / mutable_name
        if not mutable_root.is_dir() or not (owner or group):
            continue
        for directory, _, files in os.walk(mutable_root):
            directory_path = Path(directory)
            os.chown(directory_path, uid, gid)
            for name in files:
                os.chown(directory_path / name, uid, gid)

    manifest = root / MANIFEST_FILENAME
    os.chmod(manifest, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
    if os.geteuid() == 0:
        os.chown(manifest, 0, 0)


def _copy_verified(source_root: Path, backup_root: Path, paths: list[str]) -> None:
    for relative in paths:
        source = source_root / relative
        destination = backup_root / "files" / relative
        if not source.is_file() or source.is_symlink():
            raise UiPublicationError(f"unable to back up covered UI file: {relative}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        if _sha256(source) != _sha256(destination):
            raise UiPublicationError(f"backup verification failed for: {relative}")


def _create_modification_backup(
    target: Path,
    classification: dict,
    manifest_module,
    backup_parent: Path,
    result: dict,
) -> None:
    backup_parent = backup_parent.resolve(strict=False)
    if backup_parent == target or backup_parent in target.parents or target in backup_parent.parents:
        raise UiPublicationError("UI backup directory must be outside the live web root")
    backup_parent.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = Path(tempfile.mkdtemp(prefix=f"ui-backup-{timestamp}-", dir=backup_parent))
    result["backup_directory"] = str(backup)
    try:
        if classification["installed_state"] == "unknown":
            paths = [record["path"] for record in manifest_module.collect_file_records(target)]
        else:
            paths = sorted(
                set(classification["modified_files"] + classification["added_files"]),
                key=lambda value: value.encode("utf-8"),
            )
        _copy_verified(target, backup, paths)

        prior_manifest = target / MANIFEST_FILENAME
        if prior_manifest.is_file() and not prior_manifest.is_symlink():
            manifest_backup = backup / "prior-manifest.json"
            shutil.copy2(prior_manifest, manifest_backup)
            if _sha256(prior_manifest) != _sha256(manifest_backup):
                raise UiPublicationError("prior manifest backup verification failed")
            result["prior_manifest_backup"] = str(manifest_backup)

        report_path = backup / "modification-report.json"
        result["modification_report_path"] = str(report_path)
        result["backup_verified"] = True
        _write_json_atomic(report_path, result)

        for relative in paths:
            if _sha256(target / relative) != _sha256(backup / "files" / relative):
                raise UiPublicationError(f"final backup verification failed for: {relative}")
        if result["prior_manifest_backup"] and (
            _sha256(prior_manifest) != _sha256(Path(result["prior_manifest_backup"]))
        ):
            raise UiPublicationError("final prior manifest verification failed")
        confirmed = manifest_module.classify_installed_ui(
            target, target / MANIFEST_FILENAME
        )
        for field in (
            "installed_state",
            "installed_ui_build_id",
            "packaged_ui_build_id",
            "modified_files",
            "added_files",
            "missing_files",
        ):
            if confirmed.get(field) != classification.get(field):
                raise UiPublicationError(
                    "live UI changed while its modification backup was being verified"
                )
    except Exception as error:
        result["backup_verified"] = False
        result["error"] = str(error)
        if result["modification_report_path"]:
            try:
                _write_json_atomic(Path(result["modification_report_path"]), result)
            except Exception:
                pass
        raise


def publish_ui(
    source: Path,
    target: Path,
    *,
    source_commit: str,
    application_version: str,
    manifest_module,
    owner: str | None = None,
    group: str | None = None,
    backup_parent: Path = Path("/var/backups/wsprrypi/ui"),
    fail_on_ui_modifications: bool = False,
    before_publish: Callable[[Path], None] | None = None,
) -> dict:
    """Publish one validated UI directory tree without exposing partial staging."""
    source, target = _safe_publication_paths(source, target)
    target.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=f".{target.name}.stage.", dir=target.parent))
    previous = target.parent / f".{target.name}.previous.{os.getpid()}"
    previous_created = False
    result = _new_result()

    try:
        shutil.copytree(source, stage, dirs_exist_ok=True, symlinks=True)
        manifest_path = stage / MANIFEST_FILENAME
        manifest = manifest_module.build_manifest(
            stage,
            source_commit,
            application_version,
        )
        manifest_module.write_atomic(
            manifest_path, manifest_module.serialize_manifest(manifest)
        )
        validated = manifest_module.load_manifest(manifest_path)
        installed = manifest_module.classify_installed_ui(stage, manifest_path)
        if (
            installed.get("installed_state") != "packaged"
            or installed.get("packaged_ui_build_id") != installed.get("installed_ui_build_id")
            or installed.get("installed_ui_build_id") != validated["packaged_ui_build_id"]
        ):
            raise UiPublicationError("staged UI identity does not match its manifest")

        if target.exists():
            classification = manifest_module.classify_installed_ui(
                target, target / MANIFEST_FILENAME
            )
            result["prior_state"] = classification["installed_state"]
            for field in ("modified_files", "added_files", "missing_files"):
                result[field] = list(classification[field])
            requires_backup = classification["installed_state"] in {
                "locally_modified", "unknown"
            }
            if fail_on_ui_modifications and requires_backup:
                raise UiPublicationError(
                    f"existing UI state is {classification['installed_state']}; replacement refused",
                    result,
                )
            if requires_backup:
                _create_modification_backup(
                    target, classification, manifest_module, backup_parent, result
                )

        _set_tree_permissions(stage, owner, group)
        if before_publish:
            before_publish(stage)
        final_status = manifest_module.classify_installed_ui(stage, manifest_path)
        if (
            final_status.get("installed_state") != "packaged"
            or final_status.get("installed_ui_build_id")
            != installed.get("installed_ui_build_id")
        ):
            raise UiPublicationError("staged UI changed after manifest validation")

        if previous.exists():
            raise UiPublicationError(f"stale publication rollback path exists: {previous}")
        if target.exists():
            os.replace(target, previous)
            previous_created = True
        try:
            os.replace(stage, target)
        except Exception:
            if previous_created:
                os.replace(previous, target)
                previous_created = False
            raise
        final_status = manifest_module.classify_installed_ui(
            target, target / MANIFEST_FILENAME
        )
        result["replacement_completed"] = True
        result["final_installed_state"] = final_status["installed_state"]
        result["final_installed_ui_build_id"] = final_status["installed_ui_build_id"]
        if previous_created:
            shutil.rmtree(previous)
            previous_created = False
        if final_status["installed_state"] != "packaged":
            raise UiPublicationError("published UI did not classify as packaged", result)
        if result["modification_report_path"]:
            _write_json_atomic(Path(result["modification_report_path"]), result)
        return result
    except UiPublicationError as error:
        if error.result is None:
            error.result = result
        result["error"] = str(error)
        raise
    except Exception as error:
        result["error"] = str(error)
        raise UiPublicationError(str(error), result) from error
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if previous_created and previous.exists() and not target.exists():
            os.replace(previous, target)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--target", type=Path, default=Path("/var/www/html/wsprrypi"))
    parser.add_argument("--source-commit")
    parser.add_argument("--application-version")
    parser.add_argument("--owner", default="www-data")
    parser.add_argument("--group", default="www-data")
    parser.add_argument("--backup-directory", type=Path, default=Path("/var/backups/wsprrypi/ui"))
    parser.add_argument("--fail-on-ui-modifications", action="store_true")
    parser.add_argument("--result-file", type=Path)
    parser.add_argument("--render-result", type=Path)
    return parser.parse_args(argv)


def render_result(path: Path) -> int:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print("\nUI replacement issue")
        print(f"  Publication details are unavailable: {error}")
        return 1

    if not isinstance(result, dict) or any(
        not isinstance(result.get(field, []), list)
        for field in ("modified_files", "added_files", "missing_files")
    ):
        print("\nUI replacement issue")
        print("  Publication details are unavailable: the result file has an invalid format.")
        return 1

    error = result.get("error")
    backup_directory = result.get("backup_directory")
    if not error and not backup_directory:
        return 0

    counts = []
    for label, field in (
        ("modified", "modified_files"),
        ("added", "added_files"),
        ("missing", "missing_files"),
    ):
        count = len(result.get(field) or [])
        if count:
            counts.append(f"{count} {label}")

    if error:
        print("\nUI replacement issue")
        print(f"  UI publication failed: {error}")
        if counts:
            print(f"  Existing changes: {', '.join(counts)}")
        if backup_directory:
            verification = "verified" if result.get("backup_verified") else "not verified"
            print(f"  Backup ({verification}): {backup_directory}")
        if result.get("modification_report_path"):
            print(f"  Details: {result['modification_report_path']}")
        if result.get("replacement_completed"):
            print("  Stock UI installation completed before this error.")
        else:
            print("  Stock UI installation did not complete.")
        return 0

    if result.get("prior_state") == "locally_modified":
        print("\nCustomized UI backed up")
        print("  The installer saved your UI changes and installed the stock UI.")
    else:
        print("\nExisting UI backed up")
        print("  The installer could not verify the prior UI, so it saved the existing files")
        print("  before installing the stock UI.")
    if counts:
        print(f"  Changes: {', '.join(counts)}")
    print(f"  Backup: {backup_directory}")
    if result.get("modification_report_path"):
        print(f"  Details: {result['modification_report_path']}")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.render_result:
        return render_result(args.render_result)
    git_root = get_git_root()
    if git_root is None:
        print("Error: Not inside a Git repository.", file=sys.stderr)
        return 1
    source = args.source or git_root / "WsprryPi-UI" / "data"
    result = _new_result()
    try:
        source_commit = args.source_commit or get_source_commit(git_root)
        application_version = args.application_version or get_application_version(git_root)
        result = publish_ui(
            source,
            args.target,
            source_commit=source_commit,
            application_version=application_version,
            manifest_module=load_manifest_module(git_root),
            owner=args.owner or None,
            group=args.group or None,
            backup_parent=args.backup_directory,
            fail_on_ui_modifications=args.fail_on_ui_modifications,
        )
    except (OSError, KeyError, UiPublicationError) as error:
        if isinstance(error, UiPublicationError) and error.result is not None:
            result = error.result
        result["error"] = str(error)
        if args.result_file:
            try:
                _write_json_atomic(args.result_file, result)
            except OSError as report_error:
                print(f"Error: unable to write UI result: {report_error}", file=sys.stderr)
        print(f"Error: UI publication failed: {error}", file=sys.stderr)
        return 1
    if args.result_file:
        _write_json_atomic(args.result_file, result)
    print(
        "Deployment successful: "
        f"{result['final_installed_ui_build_id']} ({result['final_installed_state']})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
