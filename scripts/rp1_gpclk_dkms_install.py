#!/usr/bin/env python3
"""Resolve and install the independently owned RP1-GPCLK-DKMS provider.

This helper deliberately owns only WsprryPi's orchestration and acceptance
policy.  Package and exact-source lifecycle mechanics remain upstream.
"""

from __future__ import annotations

import argparse
import ast
import base64
import bz2
import copy
import gzip
import hashlib
import json
import lzma
import os
import pathlib
import platform
import re
import secrets
import shlex
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any, Iterable, Mapping, Sequence


REPOSITORY = "WsprryPi/RP1-GPCLK-DKMS"
REPOSITORY_URL = "https://github.com/WsprryPi/RP1-GPCLK-DKMS.git"
API_BASE = f"https://api.github.com/repos/{REPOSITORY}"
MANIFEST_NAME = "rp1-gpclk-dkms-installation-manifest-v1.json"
CHECKSUMS_NAME = "SHA256SUMS"
MANIFEST_SCHEMA = "rp1-gpclk-dkms-installation-manifest-v1"
ADMIN_PROTOCOL = "rp1-gpclk-route-manager"
PACKAGE_NAME = "rp1-gpclk-dkms"
DKMS_NAME = "rp1-gpclk-dkms"
MODULE_NAME = "rp1_gpclk_dkms"
CONTROLLER_MODULE_NAME = "rp1_route_controller"
LEGACY_COMPATIBILITY_IDENTITY = "v0.9.0-pi5"
CORRECTED_RUNTIME_COMMIT = "840afebc667cf9202701246b84d437d95581cdd3"
LEGACY_RECORD_SCHEMA = "wsprrypi-rp1-gpclk-dkms-installation-v1"
RECORD_SCHEMA = "wsprrypi-rp1-gpclk-dkms-ownership-v2"
RUNTIME_RECORD_SCHEMA = "wsprrypi-rp1-gpclk-dkms-ownership-v3"
STATE_SCHEMA = "wsprrypi-rp1-gpclk-dkms-plan-v1"
RUNTIME_READINESS_CONTRACT = "rp1-gpclk-runtime-readiness-v1"
RUNTIME_BINDING_CONTRACT = "rp1-gpclk-runtime-binding-v3"
RUNTIME_ROUTE_CONTRACT = "rp1-gpclk-route-manager-runtime"
RUNTIME_PROVIDER = pathlib.Path("/usr/lib/rp1-gpclk-dkms/runtime_provider.py")
RUNTIME_ROUTE_CLIENT = pathlib.Path("/usr/lib/rp1-gpclk-dkms/runtime_route_client.py")
RUNTIME_COMPANION = pathlib.Path("/usr/local/lib/wsprrypi/route_application.py")
SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
SEMVER = re.compile(r"^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SAFE_VERSION = re.compile(r"^[0-9A-Za-z.+:~-]+$")
MAX_API_BYTES = 16 * 1024 * 1024
MAX_METADATA_BYTES = 2 * 1024 * 1024
MAX_PACKAGE_BYTES = 256 * 1024 * 1024
MAX_PACKAGE_MEMBERS = 20000
MAX_PACKAGE_MEMBER_BYTES = 64 * 1024 * 1024
MAX_EXPANDED_TAR_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_PATH_BYTES = 1024
MAX_RECORD_BYTES = 4 * 1024 * 1024


class ContractError(RuntimeError):
    """A fail-closed contract refusal."""


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def decompressed_module_sha256(path: pathlib.Path, compression: str) -> str:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        require(stat.S_ISREG(os.fstat(descriptor).st_mode),
                "runtime module prerequisite is not a regular file")
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            raw = stream.read(64 * 1024 * 1024 + 1)
    finally:
        os.close(descriptor)
    require(len(raw) <= 64 * 1024 * 1024,
            "runtime module prerequisite exceeds read bound")
    try:
        if compression == "none":
            payload = raw
        elif compression == "xz":
            payload = lzma.decompress(raw)
        elif compression == "gz":
            payload = gzip.decompress(raw)
        elif compression == "bz2":
            payload = bz2.decompress(raw)
        elif compression == "zst":
            result = subprocess.run(
                ["/usr/bin/zstd", "-q", "-d", "-c"], input=raw,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            require(result.returncode == 0, "runtime zstd module decompression failed")
            payload = result.stdout
        else:
            raise ContractError("runtime module compression is unsupported")
    except (OSError, EOFError, ValueError, lzma.LZMAError) as error:
        raise ContractError("runtime module decompression failed") from error
    require(len(payload) <= 64 * 1024 * 1024,
            "decompressed runtime module exceeds read bound")
    return sha256_bytes(payload)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(message)


def require_exact_keys(value: Mapping[str, Any], required: Iterable[str], where: str) -> None:
    required_set = set(required)
    missing = sorted(required_set - set(value))
    require(not missing, f"{where} is missing required fields: {', '.join(missing)}")
    unexpected = sorted(set(value) - required_set)
    require(not unexpected, f"{where} contains unsupported fields: {', '.join(unexpected)}")


def json_result(result: CommandResult, operation: str) -> dict[str, Any]:
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ContractError(f"RP1 runtime {operation} returned malformed JSON") from error
    require(isinstance(value, dict), f"RP1 runtime {operation} did not return an object")
    return value


def parse_semver(tag: str) -> tuple[int, int, int]:
    match = SEMVER.fullmatch(tag)
    if not match:
        raise ContractError(f"release tag is not an immutable semantic version: {tag!r}")
    return tuple(int(match.group(index)) for index in range(1, 4))


def normalize_repository_url(url: str) -> str:
    if url in {
        REPOSITORY_URL,
        "https://github.com/WsprryPi/RP1-GPCLK-DKMS",
        "git@github.com:WsprryPi/RP1-GPCLK-DKMS.git",
        "ssh://git@github.com/WsprryPi/RP1-GPCLK-DKMS.git",
    }:
        return REPOSITORY_URL
    return url


@dataclass(frozen=True)
class CommandResult:
    stdout: str
    stderr: str
    returncode: int


class RuntimeOperationError(ContractError):
    """Structured failure returned by the RP1 runtime provider."""

    def __init__(
        self,
        operation: str,
        value: Mapping[str, Any],
        result: CommandResult,
        message: str,
    ) -> None:
        super().__init__(message)
        self.operation = operation
        self.value = dict(value)
        self.result = result


class Runner:
    def __init__(self, debug: bool = False):
        self.debug = debug

    def trace(self, argv: Sequence[str], cwd: pathlib.Path | None = None) -> None:
        if self.debug:
            location = f" (cwd={cwd})" if cwd is not None else ""
            print(f"[RP1 DEBUG] command{location}: {shlex.join(str(item) for item in argv)}", file=sys.stderr)

    def run(
        self,
        argv: Sequence[str],
        *,
        check: bool = True,
        cwd: pathlib.Path | None = None,
        passthrough: bool = False,
    ) -> CommandResult:
        self.trace(argv, cwd)
        try:
            if passthrough:
                process = subprocess.run(list(argv), cwd=cwd, text=True, check=False)
            else:
                process = subprocess.run(
                    list(argv), cwd=cwd, text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False,
                )
        except FileNotFoundError as error:
            result = CommandResult("", f"required command is unavailable: {argv[0]}", 127)
            if check:
                raise ContractError(result.stderr) from error
            return result
        result = CommandResult(process.stdout or "", process.stderr or "", process.returncode)
        if self.debug and not passthrough:
            if result.stdout:
                print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
            if result.stderr:
                print(result.stderr, end="" if result.stderr.endswith("\n") else "\n", file=sys.stderr)
        if check and process.returncode:
            if passthrough:
                detail = "see command output above"
            else:
                detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
            raise ContractError(f"command failed ({argv[0]}): {detail}")
        return result


def read_device_tree_text(path: pathlib.Path) -> list[str]:
    try:
        return [item.decode("utf-8", "strict") for item in path.read_bytes().split(b"\0") if item]
    except (OSError, UnicodeError):
        return []


def detect_platform(model_file: pathlib.Path, compatible_file: pathlib.Path) -> dict[str, Any]:
    models = read_device_tree_text(model_file)
    compatibles = read_device_tree_text(compatible_file)
    model = models[0] if models else "unknown"
    pi5_model = bool(
        re.fullmatch(r"Raspberry Pi 5(?: Model B)?(?: Rev .+)?", model)
        or re.fullmatch(r"Raspberry Pi Compute Module 5(?: Rev .+)?", model)
    )
    pi5_compatible = any(
        item in {"raspberrypi,5-model-b", "raspberrypi,5-compute-module"}
        for item in compatibles
    )
    bcm2712 = any(item in {"brcm,bcm2712", "brcm,bcm2712d0"} for item in compatibles)
    positive = pi5_model and pi5_compatible and bcm2712
    return {
        "model": model,
        "compatible": compatibles,
        "classification": "raspberry-pi-5-family" if positive else "other-or-unknown",
        "automaticEligible": positive,
    }


def installation_decision(requested: str, detected: Mapping[str, Any]) -> dict[str, Any]:
    require(requested in {"auto", "true", "false"}, "INSTALL_RP1_GPCLK_DKMS must be auto, true, or false")
    eligible = detected.get("automaticEligible") is True
    if requested == "false":
        return {"install": False, "reason": "explicit operator opt-out", "platformOverride": False}
    if requested == "true":
        return {
            "install": True,
            "reason": "explicit operator installation request",
            "platformOverride": not eligible,
        }
    return {
        "install": eligible,
        "reason": "positively identified Raspberry Pi 5-family system" if eligible else "automatic selection skipped other or unknown system",
        "platformOverride": False,
    }


def wsprry_channel(source: str) -> str:
    require(bool(source) and source not in {"HEAD", "detached", "unknown"}, "WsprryPi source channel is ambiguous; set RP1_GPCLK_DKMS_SOURCE explicitly")
    if source in {"main", "master", "release"} or SEMVER.fullmatch(source):
        return "production"
    require(re.fullmatch(r"[A-Za-z0-9._/-]+", source) is not None, "WsprryPi source channel contains unsupported characters")
    return "development"


def source_selector(requested: str, channel: str) -> tuple[str, str | None]:
    if requested == "auto":
        return ("release", None) if channel == "production" else ("devel", None)
    if requested in {"release", "devel"}:
        return requested, None
    if requested.startswith("commit:"):
        commit = requested.removeprefix("commit:")
        require(SHA40.fullmatch(commit) is not None, "commit source requires a full lowercase 40-character SHA")
        return "commit", commit
    if requested.startswith("checkout:"):
        path = requested.removeprefix("checkout:")
        require(pathlib.Path(path).is_absolute(), "checkout source requires an absolute path")
        return "checkout", path
    raise ContractError("RP1_GPCLK_DKMS_SOURCE must be auto, release, devel, commit:FULL_SHA, or checkout:/absolute/path")


def release_assets(release: Mapping[str, Any]) -> dict[str, str]:
    assets: dict[str, str] = {}
    for asset in release.get("assets", []):
        if not isinstance(asset, dict):
            continue
        name, url = asset.get("name"), asset.get("browser_download_url")
        if isinstance(name, str) and isinstance(url, str) and asset.get("state") == "uploaded":
            require(name not in assets, f"release contains duplicate asset name: {name}")
            assets[name] = url
    return assets


def release_candidate(release: Mapping[str, Any]) -> bool:
    if release.get("draft") is not False or release.get("prerelease") is not False:
        return False
    if release.get("immutable") is not True:
        return False
    if not isinstance(release.get("published_at"), str) or not release.get("published_at"):
        return False
    tag = release.get("tag_name")
    if not isinstance(tag, str) or SEMVER.fullmatch(tag) is None:
        return False
    assets = release_assets(release)
    return MANIFEST_NAME in assets and CHECKSUMS_NAME in assets and any(
        re.fullmatch(r"rp1-gpclk-dkms_[0-9A-Za-z.+:~-]+_all\.deb", name) for name in assets
    )


def select_release(releases: Sequence[Mapping[str, Any]]) -> Mapping[str, Any]:
    candidates = [release for release in releases if release_candidate(release)]
    require(bool(candidates), "no eligible RP1-GPCLK-DKMS release is available")
    return max(candidates, key=lambda item: parse_semver(str(item["tag_name"])))


def authoritative_asset_url(tag: str, filename: str, url: str) -> None:
    parsed = urllib.parse.urlsplit(url)
    require(parsed.scheme == "https" and parsed.netloc == "github.com", f"release asset URL is not authoritative HTTPS: {url}")
    expected = f"/{REPOSITORY}/releases/download/{urllib.parse.quote(tag, safe='')}/{urllib.parse.quote(filename, safe='._+-~') }"
    require(parsed.path == expected and not parsed.query and not parsed.fragment, f"release asset URL does not match repository, tag, and filename: {url}")


def parse_checksums(data: bytes) -> dict[str, str]:
    try:
        text = data.decode("ascii")
    except UnicodeError as error:
        raise ContractError("SHA256SUMS is not ASCII") from error
    result: dict[str, str] = {}
    for line in text.splitlines():
        if not line:
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._+:~-]*)", line)
        require(match is not None, "SHA256SUMS contains a malformed or unsafe line")
        digest, filename = match.groups()
        require(filename not in result, f"SHA256SUMS repeats {filename}")
        result[filename] = digest
    return result


def validate_manifest(manifest: Any, *, tag: str, tag_commit: str) -> dict[str, Any]:
    require(isinstance(manifest, dict), "installation manifest must be a JSON object")
    required = {
        "schema", "schemaVersion", "repository", "releaseTag", "sourceCommit",
        "productVersion", "debianVersion", "package", "dkmsModule", "kernelModule",
        "uapi", "administrationProtocol", "packageInventory", "packageInventorySha256",
        "installationBehavior", "releaseChannel",
    }
    require_exact_keys(manifest, required, "installation manifest")
    require(manifest["schema"] == MANIFEST_SCHEMA and type(manifest["schemaVersion"]) is int and manifest["schemaVersion"] == 1, "unsupported installation manifest schema")
    require(manifest["repository"] == REPOSITORY, "installation manifest repository substitution")
    require(manifest["releaseTag"] == tag, "manifest release tag differs from selected release")
    require(manifest["sourceCommit"] == tag_commit and SHA40.fullmatch(tag_commit) is not None, "manifest source commit differs from immutable tag commit")
    version = tag.removeprefix("v")
    require(manifest["productVersion"] == version, "manifest product version differs from release tag")
    require(isinstance(manifest["debianVersion"], str) and SAFE_VERSION.fullmatch(manifest["debianVersion"]), "manifest Debian version is invalid")
    require(manifest["dkmsModule"] == DKMS_NAME and manifest["kernelModule"] == MODULE_NAME, "manifest module identity is incompatible")
    require(manifest["administrationProtocol"] == ADMIN_PROTOCOL, "manifest administration protocol is incompatible")
    require(manifest["releaseChannel"] == "release", "manifest is not classified for the release channel")
    behavior = manifest["installationBehavior"]
    require(isinstance(behavior, dict), "manifest installation behavior must be an object")
    require_exact_keys(
        behavior,
        {"routeNeutral", "outputDisabled", "loadsModule", "appliesOverlay", "editsBootConfiguration", "operatesServices"},
        "manifest installation behavior",
    )
    require(
        behavior == {
            "routeNeutral": True,
            "outputDisabled": True,
            "loadsModule": False,
            "appliesOverlay": False,
            "editsBootConfiguration": False,
            "operatesServices": False,
        },
        "manifest does not declare the required inactive installation behavior",
    )
    package = manifest["package"]
    require(isinstance(package, dict), "manifest package must be an object")
    require_exact_keys(package, {"name", "filename", "sha256"}, "manifest package")
    require(package["name"] == PACKAGE_NAME, "manifest package name is incompatible")
    expected_filename = f"{PACKAGE_NAME}_{manifest['debianVersion']}_all.deb"
    require(package["filename"] == expected_filename, "manifest package filename and Debian version differ")
    require(isinstance(package["sha256"], str) and SHA256.fullmatch(package["sha256"]), "manifest package SHA-256 is invalid")
    uapi = manifest["uapi"]
    require(isinstance(uapi, dict), "manifest UAPI must be an object")
    require_exact_keys(uapi, {"sha256", "path"}, "manifest UAPI")
    require(isinstance(uapi["sha256"], str) and SHA256.fullmatch(uapi["sha256"]), "manifest UAPI SHA-256 is invalid")
    require(isinstance(uapi["path"], str) and safe_archive_path(uapi["path"]), "manifest UAPI path is unsafe")
    inventory = manifest["packageInventory"]
    require(isinstance(inventory, list) and bool(inventory), "manifest package inventory is empty or invalid")
    normalized = validate_inventory(inventory)
    require(isinstance(manifest["packageInventorySha256"], str) and SHA256.fullmatch(manifest["packageInventorySha256"]), "manifest package inventory SHA-256 is invalid")
    require(sha256_bytes(canonical(normalized)) == manifest["packageInventorySha256"], "manifest package inventory hash is inconsistent")
    matches = [item for item in normalized if item["path"] == uapi["path"]]
    require(len(matches) == 1 and matches[0]["type"] == "file" and matches[0]["sha256"] == uapi["sha256"], "manifest UAPI is not bound to the package inventory")
    result = dict(manifest)
    result["packageInventory"] = normalized
    return result


def safe_archive_path(value: str) -> bool:
    if not value or "\0" in value or value.startswith("/"):
        return False
    parts = pathlib.PurePosixPath(value).parts
    return all(part not in {"", ".", ".."} for part in parts)


def validate_inventory(inventory: Sequence[Any]) -> list[dict[str, Any]]:
    require(len(inventory) <= MAX_PACKAGE_MEMBERS, "package inventory exceeds the bounded member limit")
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    for raw in inventory:
        require(isinstance(raw, dict), "package inventory member must be an object")
        require_exact_keys(raw, {"path", "type", "mode", "sha256"}, "package inventory member")
        path, kind, mode, digest = raw["path"], raw["type"], raw["mode"], raw["sha256"]
        require(isinstance(path, str) and len(path.encode("utf-8")) <= MAX_ARCHIVE_PATH_BYTES, "package inventory path exceeds the bounded length")
        require(isinstance(path, str) and safe_archive_path(path), "package inventory contains an unsafe path")
        require(path not in seen, f"package inventory repeats path: {path}")
        require(kind in {"file", "directory", "symlink"}, f"package inventory contains unsupported type: {kind!r}")
        require(isinstance(mode, str) and re.fullmatch(r"0[0-7]{3}", mode) is not None, "package inventory mode is invalid")
        require(isinstance(digest, str) and SHA256.fullmatch(digest) is not None, "package inventory SHA-256 is invalid")
        seen.add(path)
        result.append({"path": path, "type": kind, "mode": mode, "sha256": digest})
    return sorted(result, key=lambda item: item["path"])


def tar_inventory(tar_path: pathlib.Path) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    try:
        archive = tarfile.open(tar_path, "r:*")
    except (tarfile.TarError, OSError) as error:
        raise ContractError("Debian data archive is unreadable or uses unsupported compression") from error
    with archive:
        for member_index, member in enumerate(archive, start=1):
            require(member_index <= MAX_PACKAGE_MEMBERS, "package inventory exceeds the bounded member limit")
            path = member.name.removeprefix("./")
            if not path or path == ".":
                continue
            require(safe_archive_path(path), f"package contains unsafe archive path: {member.name!r}")
            require(len(path.encode("utf-8")) <= MAX_ARCHIVE_PATH_BYTES, f"package path exceeds the bounded length: {path!r}")
            require(path not in seen, f"package repeats archive path: {path}")
            seen.add(path)
            mode = f"0{stat.S_IMODE(member.mode):03o}"
            if member.isfile():
                require(0 <= member.size <= MAX_PACKAGE_MEMBER_BYTES, f"package member exceeds the bounded size limit: {path}")
                stream = archive.extractfile(member)
                require(stream is not None, f"package member cannot be read: {path}")
                digest = hashlib.sha256(stream.read()).hexdigest()
                kind = "file"
            elif member.isdir():
                digest = sha256_bytes(b"")
                kind = "directory"
            elif member.issym():
                require(not member.linkname.startswith("/"), f"package symlink has an absolute target: {path}")
                target_parts = pathlib.PurePosixPath(path).parent.joinpath(member.linkname).parts
                depth = 0
                for part in target_parts:
                    if part == "..":
                        depth -= 1
                    elif part not in {"", "."}:
                        depth += 1
                    require(depth >= 0, f"package symlink escapes package root: {path}")
                digest = sha256_bytes(member.linkname.encode())
                kind = "symlink"
            else:
                raise ContractError(f"package contains unsupported special or hard-linked member: {path}")
            result.append({"path": path, "type": kind, "mode": mode, "sha256": digest})
    return sorted(result, key=lambda item: item["path"])


def parse_debian_control(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    current: str | None = None
    for line in text.splitlines():
        if line.startswith((" ", "\t")):
            require(current is not None, "Debian control continuation has no field")
            fields[current] += "\n" + line[1:]
            continue
        if not line:
            continue
        require(":" in line, "Debian control contains malformed field")
        key, value = line.split(":", 1)
        require(re.fullmatch(r"[A-Za-z0-9-]+", key) is not None and key not in fields, "Debian control contains invalid or duplicate field")
        fields[key] = value.strip()
        current = key
    return fields


def validate_package(path: pathlib.Path, manifest: Mapping[str, Any], runner: Runner) -> None:
    package = manifest["package"]
    require(path.name == package["filename"], "downloaded package filename differs from manifest")
    require(sha256_file(path) == package["sha256"], "downloaded package SHA-256 differs from manifest")
    control = [
        runner.run(["dpkg-deb", "-f", str(path), field]).stdout.strip()
        for field in ("Package", "Version", "Architecture")
    ]
    require(control == [PACKAGE_NAME, manifest["debianVersion"], "all"], "Debian control identity differs from manifest")
    tar_path = path.parent / "data.tar"
    fsys_command = ["dpkg-deb", "--fsys-tarfile", str(path)]
    runner.trace(fsys_command)
    process = subprocess.Popen(
        fsys_command,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    expanded = 0
    with tar_path.open("wb") as stream:
        assert process.stdout is not None
        for chunk in iter(lambda: process.stdout.read(1024 * 1024), b""):
            expanded += len(chunk)
            if expanded > MAX_EXPANDED_TAR_BYTES:
                process.kill()
                process.wait()
                tar_path.unlink(missing_ok=True)
                raise ContractError("Debian data archive exceeds the bounded expanded size limit")
            stream.write(chunk)
    assert process.stderr is not None
    stderr = process.stderr.read().decode(errors="replace").strip()
    returncode = process.wait()
    if runner.debug and stderr:
        print(stderr, file=sys.stderr)
    require(returncode == 0, f"dpkg-deb could not expose package inventory: {stderr}")
    try:
        actual = tar_inventory(tar_path)
    finally:
        tar_path.unlink(missing_ok=True)
    require(actual == manifest["packageInventory"], "package contents differ from checksummed manifest inventory")


class GitHubClient:
    def __init__(self, token: str | None = None):
        self.headers = {"Accept": "application/vnd.github+json", "User-Agent": "WsprryPi-installer"}
        if token:
            self.headers["Authorization"] = f"Bearer {token}"

    def bytes(self, url: str, *, limit: int = MAX_API_BYTES, allow_asset_redirect: bool = False) -> bytes:
        headers = self.headers if not allow_asset_redirect else {
            "Accept": "application/octet-stream", "User-Agent": "WsprryPi-installer"
        }
        request = urllib.request.Request(url, headers=headers, method="GET")
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                final = response.geturl()
                if final != url:
                    parsed = urllib.parse.urlsplit(final)
                    require(
                        allow_asset_redirect
                        and parsed.scheme == "https"
                        and parsed.netloc in {"release-assets.githubusercontent.com", "objects.githubusercontent.com"},
                        f"unexpected redirect while retrieving {url}",
                    )
                length = response.headers.get("Content-Length")
                if length is not None:
                    require(length.isdigit() and int(length) <= limit, f"remote content exceeds the bounded size limit: {url}")
                data = response.read(limit + 1)
                require(len(data) <= limit, f"remote content exceeds the bounded size limit: {url}")
                return data
        except (urllib.error.URLError, TimeoutError) as error:
            raise ContractError(f"failed to retrieve {url}: {error}") from error

    def json(self, url: str) -> Any:
        try:
            return json.loads(self.bytes(url, limit=MAX_API_BYTES))
        except json.JSONDecodeError as error:
            raise ContractError(f"remote JSON is malformed: {url}") from error

    def releases(self) -> list[Mapping[str, Any]]:
        result: list[Mapping[str, Any]] = []
        for page in range(1, 21):
            payload = self.json(f"{API_BASE}/releases?per_page=100&page={page}")
            require(isinstance(payload, list), "GitHub releases response is not an array")
            result.extend(item for item in payload if isinstance(item, dict))
            if len(payload) < 100:
                return result
        raise ContractError("GitHub release enumeration exceeded the bounded page limit")

    def tag_commit(self, tag: str) -> str:
        payload = self.json(f"{API_BASE}/git/ref/tags/{urllib.parse.quote(tag, safe='')}")
        require(isinstance(payload, dict) and isinstance(payload.get("object"), dict), "Git tag reference response is malformed")
        obj = payload["object"]
        for _ in range(8):
            kind, sha = obj.get("type"), obj.get("sha")
            require(isinstance(sha, str) and SHA40.fullmatch(sha), "Git tag object SHA is malformed")
            if kind == "commit":
                return sha
            require(kind == "tag", "Git tag does not resolve to a commit")
            annotated = self.json(f"{API_BASE}/git/tags/{sha}")
            require(isinstance(annotated, dict) and isinstance(annotated.get("object"), dict), "annotated Git tag response is malformed")
            obj = annotated["object"]
        raise ContractError("Git tag indirection exceeds the bounded resolution limit")


def download_release(state_dir: pathlib.Path, client: GitHubClient, runner: Runner) -> dict[str, Any]:
    release = select_release(client.releases())
    tag = str(release["tag_name"])
    assets = release_assets(release)
    package_names = sorted(name for name in assets if re.fullmatch(r"rp1-gpclk-dkms_[0-9A-Za-z.+:~-]+_all\.deb", name))
    require(len(package_names) == 1, "selected release must contain exactly one RP1-GPCLK-DKMS package")
    package_name = package_names[0]
    for name in (MANIFEST_NAME, CHECKSUMS_NAME, package_name):
        authoritative_asset_url(tag, name, assets[name])
    manifest_bytes = client.bytes(assets[MANIFEST_NAME], limit=MAX_METADATA_BYTES, allow_asset_redirect=True)
    checksums_bytes = client.bytes(assets[CHECKSUMS_NAME], limit=MAX_METADATA_BYTES, allow_asset_redirect=True)
    asset_objects = {
        item["name"]: item for item in release.get("assets", [])
        if isinstance(item, dict) and isinstance(item.get("name"), str)
    }
    for name, data in ((MANIFEST_NAME, manifest_bytes), (CHECKSUMS_NAME, checksums_bytes)):
        api_digest = asset_objects[name].get("digest")
        if api_digest is not None:
            require(api_digest == f"sha256:{sha256_bytes(data)}", f"GitHub asset digest differs for {name}")
    checksums = parse_checksums(checksums_bytes)
    require(checksums.get(MANIFEST_NAME) == sha256_bytes(manifest_bytes), "SHA256SUMS does not bind the installation manifest")
    try:
        manifest_raw = json.loads(manifest_bytes)
    except json.JSONDecodeError as error:
        raise ContractError("selected release installation manifest is malformed") from error
    tag_commit = client.tag_commit(tag)
    manifest = validate_manifest(manifest_raw, tag=tag, tag_commit=tag_commit)
    require(manifest["package"]["filename"] == package_name, "selected package asset differs from manifest")
    require(checksums.get(package_name) == manifest["package"]["sha256"], "SHA256SUMS package identity differs from manifest")
    package_path = state_dir / package_name
    package_bytes = client.bytes(assets[package_name], limit=MAX_PACKAGE_BYTES, allow_asset_redirect=True)
    api_package_digest = asset_objects[package_name].get("digest")
    if api_package_digest is not None:
        require(api_package_digest == f"sha256:{sha256_bytes(package_bytes)}", "GitHub asset digest differs for the package")
    package_path.write_bytes(package_bytes)
    os.chmod(package_path, 0o600)
    validate_package(package_path, manifest, runner)
    (state_dir / MANIFEST_NAME).write_bytes(canonical(manifest) + b"\n")
    os.chmod(state_dir / MANIFEST_NAME, 0o600)
    checkout = clone_exact(state_dir, "commit", tag_commit, runner)
    source = pathlib.Path(checkout["path"])
    require_compatible_runtime_source(source, tag_commit, runner)
    runtime_source_interface(source, runner)
    upstream_identity = development_identity(source, runner)
    require(
        upstream_identity["version"] == manifest["productVersion"],
        "release package and exact runtime source product versions differ",
    )
    runtime_source_identity(source, upstream_identity["version"])
    require(manifest["uapi"]["sha256"] == sha256_file(source / "include/uapi/linux/rp1_gpclk.h")
            and manifest["uapi"]["path"] ==
                f"usr/src/{PACKAGE_NAME}-{manifest['productVersion']}/include/uapi/linux/rp1_gpclk.h",
            "release package UAPI differs from exact runtime source")
    return {
        "channel": "release", "tag": tag, "commit": tag_commit,
        "manifest": manifest, "packagePath": str(package_path),
        "checkout": checkout, "runtimeVersion": upstream_identity["version"],
    }


def git_output(runner: Runner, source: pathlib.Path, *args: str) -> str:
    return runner.run(["git", "-C", str(source), *args]).stdout.strip()


def require_compatible_runtime_source(
    source: pathlib.Path, commit: str, runner: Runner
) -> None:
    require(SHA40.fullmatch(commit) is not None, "selected runtime source commit is invalid")
    available = runner.run(
        ["git", "-C", str(source), "cat-file", "-e", f"{CORRECTED_RUNTIME_COMMIT}^{{commit}}"],
        check=False,
    )
    require(
        available.returncode == 0,
        "selected source cannot prove the corrected RP1 runtime ancestry",
    )
    compatible = runner.run(
        ["git", "-C", str(source), "merge-base", "--is-ancestor", CORRECTED_RUNTIME_COMMIT, commit],
        check=False,
    )
    require(
        compatible.returncode == 0,
        "selected RP1-GPCLK-DKMS source predates the corrected runtime lifecycle contract",
    )


def validate_checkout(source_text: str, runner: Runner) -> dict[str, Any]:
    requested = pathlib.Path(source_text)
    require(requested.is_absolute(), "checkout source requires an absolute path")
    require(requested.exists() and requested.is_dir(), "checkout source is not an existing directory")
    absolute = pathlib.Path(os.path.abspath(requested))
    resolved = requested.resolve(strict=True)
    require(absolute == resolved, "checkout source contains a symlink or path substitution")
    top = pathlib.Path(git_output(runner, resolved, "rev-parse", "--show-toplevel"))
    require(top.resolve(strict=True) == resolved, "checkout path is not the repository root")
    remote = git_output(runner, resolved, "remote", "get-url", "origin")
    require(normalize_repository_url(remote) == REPOSITORY_URL, "checkout is not the authoritative RP1-GPCLK-DKMS repository")
    commit = git_output(runner, resolved, "rev-parse", "HEAD")
    require(SHA40.fullmatch(commit) is not None, "checkout HEAD is not a full commit SHA")
    status_text = git_output(runner, resolved, "status", "--porcelain=v1", "--untracked-files=all")
    require(not status_text, "checkout has tracked or untracked changes")
    flags = git_output(runner, resolved, "ls-files", "-v").splitlines()
    require(all(line.startswith("H ") for line in flags), "checkout uses assume-unchanged, skip-worktree, or unsupported tracked-file state")
    tree = git_output(runner, resolved, "rev-parse", "HEAD^{tree}")
    require(SHA40.fullmatch(tree) is not None, "checkout tree identity is invalid")
    root_stat, git_stat = resolved.stat(), pathlib.Path(git_output(runner, resolved, "rev-parse", "--absolute-git-dir")).resolve(strict=True).stat()
    return {
        "path": str(resolved), "commit": commit, "tree": tree,
        "rootDevice": root_stat.st_dev, "rootInode": root_stat.st_ino,
        "gitDevice": git_stat.st_dev, "gitInode": git_stat.st_ino,
    }


def clone_exact(state_dir: pathlib.Path, selector: str, value: str | None, runner: Runner) -> dict[str, Any]:
    source = state_dir / "source"
    runner.run(["git", "clone", "--no-checkout", "--origin", "origin", REPOSITORY_URL, str(source)])
    if selector == "devel":
        result = runner.run(["git", "-C", str(source), "rev-parse", "--verify", "origin/devel^{commit}"], check=False)
        require(result.returncode == 0, "authoritative RP1-GPCLK-DKMS origin/devel is unavailable")
        commit = result.stdout.strip()
    else:
        require(value is not None and SHA40.fullmatch(value) is not None, "internal exact-commit resolution error")
        result = runner.run(["git", "-C", str(source), "cat-file", "-e", f"{value}^{{commit}}"], check=False)
        require(result.returncode == 0, "requested RP1-GPCLK-DKMS commit does not exist in the authoritative repository")
        commit = value
    require(SHA40.fullmatch(commit) is not None, "resolved development commit is not immutable")
    runner.run(["git", "-C", str(source), "checkout", "--detach", commit])
    identity = validate_checkout(str(source), runner)
    require(identity["commit"] == commit, "development checkout changed after immutable resolution")
    return identity


def development_identity(source: pathlib.Path, runner: Runner) -> dict[str, str]:
    preflight = source / "scripts" / "development-preflight"
    require(preflight.is_file() and os.access(preflight, os.X_OK), "development source lacks the maintained preflight")
    result = runner.run(
        [str(preflight), "--source", str(source), "--kernel", platform.release()],
        cwd=source,
    )
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ContractError("upstream development preflight returned malformed identity") from error
    require(isinstance(payload, dict), "upstream development preflight identity is not an object")
    version = payload.get("moduleVersion")
    version_source = payload.get("versionSource")
    version_sha256 = payload.get("versionSourceSha256")
    require(payload.get("classification") == "source-development", "upstream development preflight classification differs")
    require(payload.get("moduleName") == MODULE_NAME, "upstream development preflight module identity differs")
    require(isinstance(version, str) and SAFE_VERSION.fullmatch(version), "upstream development preflight has an invalid version")
    require(version_source == "include/rp1_gpclk/version.h", "upstream development preflight version source differs")
    path = source / version_source
    require(path.is_file() and not path.is_symlink(), "upstream canonical development version source is missing or substituted")
    require(version_sha256 == sha256_file(path), "upstream development version identity differs from the selected source")
    return {"version": version, "versionSource": version_source, "versionSourceSha256": version_sha256}


def route_neutral_interface(source: pathlib.Path, runner: Runner) -> tuple[str, list[str]]:
    installer = source / "scripts" / "development-install"
    preflight = source / "scripts" / "development-preflight"
    require(installer.is_file() and os.access(installer, os.X_OK) and preflight.is_file() and os.access(preflight, os.X_OK), "development source lacks maintained executable lifecycle tools")
    help_result = runner.run([str(installer), "--help"], cwd=source)
    help_text = help_result.stdout + help_result.stderr
    require("--runtime-controller" in help_text,
            "upstream development-install lacks the single-authority runtime-controller profile")
    if "--route-neutral" in help_text:
        return "route-neutral-flag", ["--route-neutral"]
    if re.search(r"--route[^\n]*route-neutral", help_text):
        return "route-choice", ["--route", "route-neutral"]
    raise ContractError("upstream development-install has no reviewed route-neutral installation interface; no development mutation was attempted")


def runtime_source_interface(source: pathlib.Path, runner: Runner) -> None:
    required = (
        "Makefile",
        "scripts/build_runtime_bundle.py",
        "scripts/runtime_provider.py",
        "scripts/runtime_activation.py",
        "scripts/runtime_deployment.py",
        "scripts/runtime_binding.py",
        "scripts/runtime_layout.py",
        "schema/rp1-gpclk-runtime-readiness-v1.schema.json",
        "include/uapi/linux/rp1_gpclk.h",
        "include/uapi/linux/rp1_route_admin.h",
        "systemd/rp1-gpclk-route-manager.socket",
        "systemd/rp1-gpclk-route-manager@.service",
        "systemd/95-runtime-controller.conf",
    )
    for relative in required:
        path = source / relative
        require(path.is_file() and not path.is_symlink(), f"selected source lacks runtime administration input: {relative}")
    # Probe the selected source itself before any package or application effect.
    provider = source / "scripts/runtime_provider.py"
    reply = runner.run(["python3", str(provider), "capabilities"], check=False)
    require(reply.returncode == 0,
            "selected source lacks rp1-gpclk-runtime-update-v1; choose a compatible provider source")
    validate_runtime_update_capabilities(reply)


def runtime_source_identity(source: pathlib.Path, product: str) -> None:
    try:
        layout = (source / "scripts/runtime_layout.py").read_text(encoding="utf-8")
        binding = (source / "scripts/runtime_binding.py").read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ContractError("selected runtime source identity is unreadable") from error
    product_match = re.search(r"(?m)^PRODUCT_VERSION\s*=\s*(['\"])([^'\"]+)\1\s*$", binding)
    contract_match = re.search(r"(?m)^CONTRACT\s*=\s*(['\"])([^'\"]+)\1\s*$", binding)
    compatibility_match = re.search(r"(?m)^COMPATIBILITY\s*=\s*(\{[^\n]+\})\s*$", binding)
    require(re.search(r"(?m)^KERNEL\s*=", layout) is None,
            "selected runtime source hard-codes a kernel instead of using the build binding")
    require(product_match is not None and product_match.group(2) == product, "selected runtime source product version differs")
    require(contract_match is not None and contract_match.group(2) == RUNTIME_BINDING_CONTRACT, "selected runtime binding contract differs")
    try:
        compatibility = ast.literal_eval(compatibility_match.group(1)) if compatibility_match else None
    except (SyntaxError, ValueError) as error:
        raise ContractError("selected runtime compatibility identity is malformed") from error
    require(
        compatibility == {
            "gpio4": f"v{product}-rp1-gpio4",
            "gpio20": f"v{product}-rp1-gpio20",
        },
        "selected runtime route compatibility identities differ",
    )


def revalidate_checkout(identity: Mapping[str, Any], runner: Runner) -> pathlib.Path:
    source = pathlib.Path(str(identity["path"]))
    try:
        current = validate_checkout(str(source), runner)
    except ContractError as error:
        raise ContractError("development checkout was dirty or replaced after preflight") from error
    for key in ("commit", "tree", "rootDevice", "rootInode", "gitDevice", "gitInode"):
        require(current[key] == identity[key], "development checkout was dirty or replaced after preflight")
    return source


def prepare_development(state_dir: pathlib.Path, selector: str, value: str | None, runner: Runner) -> dict[str, Any]:
    if selector == "checkout":
        original = validate_checkout(str(value), runner)
        snapshot = state_dir / "source"
        runner.run(["git", "clone", "--no-local", "--no-checkout", original["path"], str(snapshot)])
        runner.run(["git", "-C", str(snapshot), "remote", "set-url", "origin", REPOSITORY_URL])
        runner.run(["git", "-C", str(snapshot), "checkout", "--detach", original["commit"]])
        identity = validate_checkout(str(snapshot), runner)
        require(
            identity["commit"] == original["commit"] and identity["tree"] == original["tree"],
            "local checkout snapshot differs from the selected commit",
        )
    else:
        identity = clone_exact(state_dir, selector, value, runner)
    source = pathlib.Path(identity["path"])
    require_compatible_runtime_source(source, identity["commit"], runner)
    uapi = source / "include" / "uapi" / "linux" / "rp1_gpclk.h"
    require(uapi.is_file() and not uapi.is_symlink(), "development source lacks the canonical regular-file UAPI")
    interface, route_args = route_neutral_interface(source, runner)
    runtime_source_interface(source, runner)
    upstream_identity = development_identity(source, runner)
    runtime_source_identity(source, upstream_identity["version"])
    return {
        "channel": "development", "commit": identity["commit"], "version": upstream_identity["version"],
        "checkout": identity, "interface": interface, "routeArguments": route_args,
        "sourceTree": identity["tree"], "uapiSha256": sha256_file(uapi),
        "versionSource": upstream_identity["versionSource"],
        "versionSourceSha256": upstream_identity["versionSourceSha256"],
        "compatibilityIdentity": development_compatibility(upstream_identity["version"]),
    }


def development_compatibility(version: str) -> str:
    """Route-neutral provenance; each runtime route still has its own identity."""
    require(isinstance(version, str) and SAFE_VERSION.fullmatch(version),
            "invalid development product version")
    return canonical({route: f"v{version}-rp1-{route}"
                      for route in ("gpio4", "gpio20")}).decode()


def secure_state_dir(path: pathlib.Path) -> pathlib.Path:
    require(path.is_absolute() and path.exists() and path.is_dir(), "state directory must be an existing absolute directory")
    resolved = path.resolve(strict=True)
    require(resolved == pathlib.Path(os.path.abspath(path)), "state directory cannot contain symlinks")
    info = resolved.stat()
    require(info.st_uid == os.geteuid(), "state directory is not owned by the invoking user")
    require(stat.S_IMODE(info.st_mode) & 0o077 == 0, "state directory must not be accessible by group or others")
    return resolved


def secure_record_parent(path: pathlib.Path) -> None:
    parent = path.parent
    parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    absolute = pathlib.Path(os.path.abspath(parent))
    resolved = parent.resolve(strict=True)
    require(absolute == resolved, "installation record parent contains a symlink or path substitution")
    info = resolved.stat()
    require(info.st_uid == os.geteuid(), "installation record parent is not owned by the invoking user")
    require(stat.S_IMODE(info.st_mode) & 0o022 == 0, "installation record parent is group- or world-writable")


def atomic_json(path: pathlib.Path, value: Any, mode: int = 0o600) -> None:
    payload = canonical(value) + b"\n"
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, mode)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        parent_descriptor = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(parent_descriptor)
        finally:
            os.close(parent_descriptor)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def atomic_json_new(path: pathlib.Path, value: Any, mode: int = 0o600) -> None:
    """Publish a new ownership record without replacing any concurrent state."""
    payload = canonical(value) + b"\n"
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, mode)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path, follow_symlinks=False)
        parent_descriptor = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(parent_descriptor)
        finally:
            os.close(parent_descriptor)
    except FileExistsError as error:
        raise ContractError("installation ownership record appeared during provider installation") from error
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def record_file_identity(path: pathlib.Path) -> os.stat_result:
    require(path.is_absolute(), "installation ownership record path must be absolute")
    secure_record_parent(path)
    try:
        info = path.lstat()
    except FileNotFoundError as error:
        raise ContractError("installation ownership record is absent") from error
    require(stat.S_ISREG(info.st_mode), "installation ownership record is not a regular file")
    require(info.st_uid == os.geteuid(), "installation ownership record is not owned by the invoking user")
    require(stat.S_IMODE(info.st_mode) & 0o077 == 0, "installation ownership record is accessible by group or others")
    require(info.st_size <= MAX_RECORD_BYTES, "installation ownership record exceeds the bounded size")
    return info


def load_ownership_record(path: pathlib.Path) -> tuple[dict[str, Any] | None, os.stat_result | None, str | None]:
    """Return a validated v2 record, or a preservation reason without following unsafe state."""
    if not path.is_absolute():
        return None, None, "ownership record path is not absolute"
    if not path.exists() and not path.is_symlink():
        return None, None, "no WsprryPi ownership record exists"
    try:
        info = record_file_identity(path)
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        try:
            opened = os.fstat(descriptor)
            require(
                (opened.st_dev, opened.st_ino) == (info.st_dev, info.st_ino),
                "installation ownership record changed while opening",
            )
            with os.fdopen(descriptor, "rb", closefd=False) as stream:
                payload = stream.read(MAX_RECORD_BYTES + 1)
        finally:
            os.close(descriptor)
        require(len(payload) <= MAX_RECORD_BYTES, "installation ownership record exceeds the bounded size")
        value = json.loads(payload.decode("utf-8"))
        require(isinstance(value, dict), "installation ownership record is not an object")
        if value.get("schema") == LEGACY_RECORD_SCHEMA:
            return None, info, "legacy v1 installation record cannot prove WsprryPi ownership"
        schema = value.get("schema")
        require(schema in {RECORD_SCHEMA, RUNTIME_RECORD_SCHEMA}, "installation ownership record schema is unsupported")
        common = {
            "schema", "repository", "owner", "channel", "installationMethod",
            "routeActivation", "output", "qualificationClaim",
        }
        channel = value.get("channel")
        if channel == "release":
            required = common | {"tag", "sourceCommit", "manifest", "moduleArtifacts"}
        elif channel == "development":
            required = common | {
                "sourceCommit", "productVersion", "sourceTree", "uapiSha256",
                "versionSource", "versionSourceSha256", "targetKernel",
                "compatibilityIdentity", "upstreamEvidence", "rollbackRecord",
                "rollbackRecordSha256", "rollbackEntrypoint",
                "rollbackEntrypointSha256", "installedModulePath",
                "installedModuleSha256", "decompressedModuleSha256",
            }
        else:
            raise ContractError("installation ownership record channel is unsupported")
        if schema == RUNTIME_RECORD_SCHEMA:
            required.add("runtime")
        require_exact_keys(value, required, "installation ownership record")
        require(value["repository"] == REPOSITORY, "installation ownership repository differs")
        require(value["owner"] == "WsprryPi", "installation ownership owner differs")
        require(value["routeActivation"] == "disabled" and value["output"] == "disabled", "installation ownership record is not route-neutral")
        require(value["qualificationClaim"] is False, "installation ownership record contains an unsupported qualification claim")
        if channel == "release":
            require(isinstance(value["tag"], str) and isinstance(value["sourceCommit"], str), "release ownership identity fields are invalid")
            require(isinstance(value["manifest"], dict) and isinstance(value["moduleArtifacts"], list), "release ownership evidence fields are invalid")
        else:
            string_fields = required - common - {"runtime"}
            require(all(isinstance(value[field], str) for field in string_fields), "development ownership evidence fields are invalid")
        if schema == RUNTIME_RECORD_SCHEMA:
            validate_runtime_ownership(value["runtime"], value)
        return value, info, None
    except (ContractError, OSError, UnicodeError, json.JSONDecodeError) as error:
        return None, None, str(error)


def validate_runtime_ownership(runtime: Any, record: Mapping[str, Any]) -> dict[str, Any]:
    required = {
        "readinessContract", "bindingSha256", "artifactSetSha256",
        "sourceCommit", "productVersion", "targetKernel",
        "compatibilityIdentities", "deploymentPlanSha256",
        "activationPlanSha256", "activationRequestId", "controllerSession",
        "controllerGeneration", "state", "route", "output",
    }
    require(isinstance(runtime, dict), "installation runtime ownership is not an object")
    require_exact_keys(runtime, required, "installation runtime ownership")
    require(runtime["readinessContract"] == RUNTIME_READINESS_CONTRACT, "runtime readiness contract differs")
    for field in (
        "bindingSha256", "artifactSetSha256", "deploymentPlanSha256",
        "activationPlanSha256",
    ):
        require(isinstance(runtime[field], str) and SHA256.fullmatch(runtime[field]), f"runtime ownership has invalid {field}")
    require(isinstance(runtime["sourceCommit"], str) and SHA40.fullmatch(runtime["sourceCommit"]), "runtime source commit is invalid")
    require(runtime["sourceCommit"] == record.get("sourceCommit"), "runtime source commit differs from provider ownership")
    product = record.get("productVersion")
    if record.get("channel") == "release":
        product = record.get("manifest", {}).get("productVersion")
    require(runtime["productVersion"] == product, "runtime product version differs from provider ownership")
    require(runtime["targetKernel"] == platform.release(), "runtime ownership targets a different running kernel")
    compatibility = runtime["compatibilityIdentities"]
    require(
        isinstance(compatibility, dict)
        and set(compatibility) == {"gpio4", "gpio20"}
        and compatibility == {route: f"v{product}-rp1-{route}"
                              for route in ("gpio4", "gpio20")},
        "runtime compatibility identities are invalid",
    )
    require(isinstance(runtime["activationRequestId"], str) and len(runtime["activationRequestId"]) >= 8, "runtime activation request ID is invalid")
    require(type(runtime["controllerSession"]) is int and runtime["controllerSession"] > 0, "runtime controller session is invalid")
    require(type(runtime["controllerGeneration"]) is int and runtime["controllerGeneration"] == 0, "runtime neutral controller generation differs")
    require(runtime["state"] == "neutral_ready", "runtime ownership is not neutral-ready")
    require(runtime["route"] is None, "runtime ownership selected a GPIO route")
    require(runtime["output"] == "disabled", "runtime ownership does not retain disabled output")
    return runtime


def runtime_deployment_destinations(files: Iterable[str] = ()) -> set[str]:
    library = "/usr/lib/rp1-gpclk-dkms"
    state = "/var/lib/rp1-gpclk-dkms/runtime-admin"
    return {
        *files,
        "/etc/rp1-gpclk-dkms/runtime-controller.json",
        "/etc/systemd/system/rp1-gpclk-route-manager@.service.d/95-runtime-controller.conf",
        "/usr/lib/systemd/system/rp1-gpclk-route-manager.socket",
        "/usr/lib/systemd/system/rp1-gpclk-route-manager@.service",
        *(f"{library}/{name}" for name in (
            "runtime_controller_admin.py", "runtime_manager.py",
            "runtime_route_client.py", "runtime_layout.py",
            "runtime_deployment.py", "runtime_output.py",
            "runtime_application.py", "runtime_activation.py",
            "runtime_provider.py", "runtime_binding.py", "package_preflight.py")),
        *(f"{library}/runtime-uapi/{name}" for name in
          ("rp1_gpclk.h", "rp1_route_admin.h")),
        *(f"{library}/runtime-overlays/{route}.dtbo" for route in
          ("gpio4", "gpio20")),
        f"{library}/schema/rp1-gpclk-runtime-readiness-v1.schema.json",
        *(f"{state}/{name}" for name in
          ("transaction.json", "manager.json", "application.json",
           "activation.json")),
    }




def remove_ownership_record(path: pathlib.Path, expected: os.stat_result) -> None:
    current = record_file_identity(path)
    require(
        (current.st_dev, current.st_ino) == (expected.st_dev, expected.st_ino),
        "installation ownership record changed during provider removal",
    )
    path.unlink()
    parent_descriptor = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(parent_descriptor)
    finally:
        os.close(parent_descriptor)


def module_artifacts(root: pathlib.Path) -> list[dict[str, str]]:
    base = root_path(root, "/lib/modules")
    artifacts: list[dict[str, str]] = []
    if not base.exists():
        return artifacts
    paths = [path for name in (MODULE_NAME, CONTROLLER_MODULE_NAME)
             for path in base.glob(f"*/updates/dkms/{name}.ko*")]
    for path in sorted(paths):
        relative = pathlib.PurePosixPath("/" + str(path.relative_to(root)))
        installed = installed_member(root, str(relative).removeprefix("/"), "file")
        artifacts.append({"path": str(relative), "sha256": sha256_file(installed)})
    return artifacts


def ensure_new_ownership_record(path: pathlib.Path) -> None:
    require(path.is_absolute(), "installation ownership record path must be absolute")
    secure_record_parent(path)
    require(not path.exists() and not path.is_symlink(), "stale installation ownership state blocks a new provider installation")
    pending = removal_pending_path(path)
    require(not pending.exists() and not pending.is_symlink(),
            "pending owned-provider removal must be recovered before installation")


def removal_pending_path(path: pathlib.Path) -> pathlib.Path:
    return path.with_name(path.name + ".removal-pending")


def recover_pending_removal(path: pathlib.Path) -> bool:
    """Restore authority after interruption without replacing concurrent state."""
    pending = removal_pending_path(path)
    if not pending.exists() and not pending.is_symlink():
        return False
    value, identity, reason = load_ownership_record(pending)
    require(value is not None and identity is not None and value.get("channel") == "release",
            "invalid pending package-removal authority: " + str(reason))
    if path.exists() or path.is_symlink():
        current, _, reason = load_ownership_record(path)
        require(current == value, "pending removal conflicts with current ownership: " + str(reason))
    else:
        atomic_json_new(path, value)
    remove_ownership_record(pending, identity)
    return True


def render_plan(plan: Mapping[str, Any]) -> None:
    print("RP1-GPCLK-DKMS installation plan")
    print(f"  WsprryPi source channel: {plan['wsprryChannel']}")
    print(f"  Installation decision: {'install' if plan['decision']['install'] else 'skip'} ({plan['decision']['reason']})")
    print(f"  Detected platform: {plan['platform']['model']} [{plan['platform']['classification']}]")
    print(f"  Explicit platform override: {'yes' if plan['decision']['platformOverride'] else 'no'}")
    print(f"  Requested source selector: {plan['requestedSource']}")
    if plan.get("resolved"):
        resolved = plan["resolved"]
        print(f"  Resolved channel: {resolved['channel']}")
        print(f"  Resolved tag or commit: {resolved.get('tag') or resolved.get('commit')}")
        if resolved["channel"] == "release":
            manifest = resolved["manifest"]
            print(f"  Product/Debian version: {manifest['productVersion']} / {manifest['debianVersion']}")
            print(f"  Package SHA-256: {manifest['package']['sha256']}")
            print(f"  UAPI SHA-256: {manifest['uapi']['sha256']}")
            print("  Planned lifecycle: apt-get install of the validated local Debian package")
        else:
            print(f"  Source version: {resolved['version']}")
            print(f"  Source tree: {resolved['sourceTree']}")
            print(f"  UAPI SHA-256: {resolved['uapiSha256']}")
            print("  Module SHA-256: resolved and verified by the upstream exact-source build")
            print("  Planned lifecycle: upstream development-preflight and exact-source development-install")
    print("  Safety boundary: no route, overlay, module load, service, GPIO, transmission, or RF action is authorized.")


def prepare(args: argparse.Namespace, runner: Runner) -> dict[str, Any]:
    state_dir = secure_state_dir(args.state_dir)
    detected = detect_platform(args.model_file, args.compatible_file)
    decision = installation_decision(args.install, detected)
    explicit_source = args.source != "auto"
    try:
        channel = wsprry_channel(args.wsprry_source)
    except ContractError:
        if not explicit_source:
            raise
        channel = "explicit-source"
    plan: dict[str, Any] = {
        "schema": STATE_SCHEMA, "wsprrySource": args.wsprry_source, "wsprryChannel": channel,
        "platform": detected, "decision": decision, "requestedSource": args.source,
        "resolved": None, "dryRun": args.dry_run,
    }
    if decision["install"]:
        residue = runtime_residue_inventory(pathlib.Path("/"))
        if residue:
            owned, _, reason = load_ownership_record(args.record)
            require(
                owned is not None
                and owned.get("schema") in {RECORD_SCHEMA, RUNTIME_RECORD_SCHEMA},
                "RP1 runtime-controller residue blocks installation planning; use the owning runtime cleanup workflow: "
                + ", ".join(residue) + (f" ({reason})" if reason else ""),
            )
        selector, value = source_selector(args.source, channel)
        if selector == "release":
            plan["resolved"] = download_release(
                state_dir, GitHubClient(os.environ.get("GITHUB_TOKEN")), runner
            )
        else:
            plan["resolved"] = prepare_development(state_dir, selector, value, runner)
    atomic_json(state_dir / "plan.json", plan)
    render_plan(plan)
    return plan


def root_path(root: pathlib.Path, absolute: str) -> pathlib.Path:
    require(absolute.startswith("/"), "internal installed path must be absolute")
    return root / absolute.removeprefix("/")


def runtime_residue_inventory(root: pathlib.Path) -> list[str]:
    runtime_paths = (
        "/etc/rp1-gpclk-dkms/runtime-controller.json",
        "/etc/systemd/system/rp1-gpclk-route-manager@.service.d/95-runtime-controller.conf",
        "/etc/systemd/system/wsprrypi.service.d/90-rp1-route-inhibit.conf",
        "/etc/systemd/system/wsprrypi.service.d/91-rp1-route-idle.conf",
        "/usr/lib/systemd/system/rp1-gpclk-route-manager.socket",
        "/usr/lib/systemd/system/rp1-gpclk-route-manager@.service",
        "/var/lib/rp1-gpclk-dkms/runtime-admin",
        "/run/rp1-gpclk-dkms/route-manager.sock",
        "/usr/lib/rp1-gpclk-dkms/runtime-uapi",
        "/usr/lib/rp1-gpclk-dkms/runtime-overlays",
        "/usr/lib/rp1-gpclk-dkms/runtime_application.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_activation.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_binding.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_controller_admin.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_deployment.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_layout.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_manager.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_output.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_provider.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_route_client.py",
        "/usr/lib/rp1-gpclk-dkms/package_preflight.py",
        "/usr/lib/rp1-gpclk-dkms/schema/rp1-gpclk-runtime-readiness-v1.schema.json",
        "/dev/rp1-route-admin",
        "/dev/rp1-gpclk",
        "/sys/module/rp1_route_controller",
        "/sys/module/rp1_gpclk_dkms",
    )
    residue = []
    for path_text in runtime_paths:
        path = root_path(root, path_text)
        if path.exists() or path.is_symlink():
            residue.append(str(path))
    return sorted(set(residue))


def existing_inventory(root: pathlib.Path, runner: Runner) -> dict[str, Any]:
    package = runner.run(["dpkg-query", "-W", "-f=${Status}\n${Version}\n", PACKAGE_NAME], check=False)
    package_version = None
    if package.returncode == 0:
        lines = package.stdout.splitlines()
        require(len(lines) >= 2 and lines[0] == "install ok installed", "existing package state is ambiguous")
        package_version = lines[1]
    dkms = runner.run(["dkms", "status", "-m", DKMS_NAME], check=False)
    modules_file = root_path(root, "/proc/modules")
    active = False
    active_controller = False
    if modules_file.exists():
        active = any(line.split(maxsplit=1)[0] == MODULE_NAME for line in modules_file.read_text(errors="replace").splitlines() if line)
        active_controller = any(line.split(maxsplit=1)[0] == CONTROLLER_MODULE_NAME
            for line in modules_file.read_text(errors="replace").splitlines() if line)
    source_base = root_path(root, "/usr/src")
    sources = sorted(str(path) for path in source_base.glob(f"{PACKAGE_NAME}-*") if source_base.exists())
    module_candidates: list[str] = []
    controller_candidates: list[str] = []
    modules_base = root_path(root, "/lib/modules")
    if modules_base.exists():
        module_candidates = sorted(str(path) for path in modules_base.glob("*/updates/dkms/rp1_gpclk_dkms.ko*"))
        controller_candidates = sorted(str(path) for path in modules_base.glob("*/updates/dkms/rp1_route_controller.ko*"))
    enrollment = root_path(root, "/etc/rp1-gpclk-dkms/enrollment.json").exists()
    manager = root_path(root, "/etc/systemd/system/rp1-gpclk-route-manager@.service.d/90-source-development.conf").exists()
    overlay_paths = [
        root_path(root, "/boot/firmware/overlays/rp1-gpclk-gpio4.dtbo"),
        root_path(root, "/boot/firmware/overlays/rp1-gpclk-gpio20.dtbo"),
        root_path(root, "/usr/lib/rp1-gpclk-dkms/overlays/rp1-gpclk-gpio4.dtbo"),
        root_path(root, "/usr/lib/rp1-gpclk-dkms/overlays/rp1-gpclk-gpio20.dtbo"),
    ]
    overlays = sorted(str(path) for path in overlay_paths if path.exists() or path.is_symlink())
    configured = False
    for config in (root_path(root, "/boot/firmware/config.txt"), root_path(root, "/boot/config.txt")):
        if config.is_file() and not config.is_symlink():
            configured = configured or "dtoverlay=rp1-gpclk-" in config.read_text(errors="replace")
    return {
        "packageVersion": package_version, "dkms": dkms.stdout.strip(),
        "activeModule": active, "activeController": active_controller,
        "sourceTrees": sources, "moduleCandidates": module_candidates,
        "controllerCandidates": controller_candidates,
        "installedOverlays": overlays, "configuredRoute": configured,
        "enrollment": enrollment, "developmentManager": manager,
        "runtimeResidue": runtime_residue_inventory(root),
    }


def require_no_runtime_residue(inventory: Mapping[str, Any], operation: str) -> None:
    residue = inventory.get("runtimeResidue")
    require(isinstance(residue, list), "RP1 runtime-controller inventory is unavailable")
    require(
        not residue,
        f"RP1 runtime-controller residue blocks {operation}; use the owning runtime cleanup workflow: "
        + ", ".join(str(path) for path in residue),
    )


def installed_member(root: pathlib.Path, relative: str, expected_type: str) -> pathlib.Path:
    current = root
    parts = pathlib.PurePosixPath(relative).parts
    for index, part in enumerate(parts):
        current = current / part
        try:
            info = current.lstat()
        except FileNotFoundError as error:
            raise ContractError(f"installed package member is missing: /{relative}") from error
        if index == 0 and part == "lib" and stat.S_ISLNK(info.st_mode):
            # Debian's merged-/usr alias is the sole permitted ancestor link.
            # Walk the canonical destination again without following any other
            # symlink, including a substituted /usr/lib or module directory.
            require(info.st_uid == (0 if root == pathlib.Path("/") else os.geteuid())
                    and os.readlink(current) in ("usr/lib", "/usr/lib"),
                    f"installed package path has a substituted ancestor: /{relative}")
            return installed_member(root, str(pathlib.PurePosixPath("usr/lib", *parts[1:])), expected_type)
        final = index == len(parts) - 1
        if not final:
            require(stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode), f"installed package path has a substituted ancestor: /{relative}")
        elif expected_type == "file":
            require(stat.S_ISREG(info.st_mode), f"installed package file is substituted: /{relative}")
        elif expected_type == "directory":
            require(stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode), f"installed package directory is substituted: /{relative}")
        else:
            require(stat.S_ISLNK(info.st_mode), f"installed package symlink is substituted: /{relative}")
    return current


def release_runtime_inventory(inventory: Mapping[str, Any], root: pathlib.Path,
                              manifest: Mapping[str, Any]) -> dict[str, Any]:
    """Inactive package-owned unit fragments are distinct from deployed runtime."""
    result = dict(inventory)
    if inventory["packageVersion"] != manifest["debianVersion"]:
        return result
    units = {"usr/lib/systemd/system/rp1-gpclk-route-manager.socket",
             "usr/lib/systemd/system/rp1-gpclk-route-manager@.service"}
    passive = set()
    for member in manifest["packageInventory"]:
        if member["path"] in units and member["type"] == "file":
            path = installed_member(root, member["path"], "file")
            require(sha256_file(path) == member["sha256"], "passive package unit differs from manifest")
            passive.add(str(path))
    result["runtimeResidue"] = [path for path in inventory["runtimeResidue"] if path not in passive]
    return result


def verify_installed_release(
    root: pathlib.Path,
    manifest: Mapping[str, Any],
    runner: Runner,
    *,
    allow_owned_runtime: bool = False,
) -> None:
    inventory = release_runtime_inventory(existing_inventory(root, runner), root, manifest)
    if not allow_owned_runtime:
        require_no_runtime_residue(inventory, "release verification")
    require(inventory["packageVersion"] == manifest["debianVersion"], "installed Debian package version differs from plan")
    require(not inventory["activeModule"]
            and (allow_owned_runtime or not inventory.get("activeController", False)),
            "RP1 module/controller became active during route-neutral installation")
    require(not inventory["configuredRoute"], "RP1 GPCLK route became configured during route-neutral installation")
    require(not inventory["enrollment"] and not inventory["developmentManager"], "installed provider has unexpected development enrollment or manager binding")
    dkms_lines = [line for line in inventory["dkms"].splitlines() if line.strip()]
    expected_dkms = re.compile(rf"^{re.escape(DKMS_NAME)}/{re.escape(manifest['productVersion'])}[^\n]*installed$")
    require(bool(dkms_lines) and all(expected_dkms.fullmatch(line) for line in dkms_lines), "DKMS registration contains a missing, foreign, or non-installed version")

    expected_sources: set[str] = set()
    for member in manifest["packageInventory"]:
        parts = pathlib.PurePosixPath(member["path"]).parts
        if len(parts) >= 3 and parts[:2] == ("usr", "src") and parts[2].startswith(f"{PACKAGE_NAME}-"):
            expected_sources.add(str(root_path(root, "/" + "/".join(parts[:3]))))
    require(bool(expected_sources), "manifest package inventory does not contain the DKMS source destination")
    require(set(inventory["sourceTrees"]) == expected_sources, "installed DKMS source destinations contain missing or foreign state")

    expected_overlays = {
        str(root_path(root, "/" + member["path"]))
        for member in manifest["packageInventory"]
        if member["path"].endswith(("/rp1-gpclk-gpio4.dtbo", "/rp1-gpclk-gpio20.dtbo"))
    }
    require(set(inventory["installedOverlays"]) == expected_overlays, "installed overlay inventory contains missing or foreign state")
    for member in manifest["packageInventory"]:
        installed = installed_member(root, member["path"], member["type"])
        kind = member["type"]
        if kind == "file":
            require(sha256_file(installed) == member["sha256"], f"installed package file hash differs: /{member['path']}")
        elif kind == "directory":
            pass
        else:
            require(sha256_bytes(os.readlink(installed).encode()) == member["sha256"], f"installed package symlink differs: /{member['path']}")
        if kind != "symlink":
            actual_mode = f"0{stat.S_IMODE(installed.stat().st_mode):03o}"
            require(actual_mode == member["mode"], f"installed package member mode differs: /{member['path']}")
    artifacts = module_artifacts(root)
    validate_recorded_module_artifacts(artifacts)
    for name in (MODULE_NAME, CONTROLLER_MODULE_NAME):
        version = runner.run(["modinfo", "-k", platform.release(), "-F", "version", name]).stdout.strip()
        require(version == manifest["productVersion"], "installed module/controller metadata version differs from manifest")


def apply_release(state_dir: pathlib.Path, resolved: Mapping[str, Any], record: pathlib.Path, root: pathlib.Path, runner: Runner) -> None:
    require(not removal_pending_path(record).exists() and not removal_pending_path(record).is_symlink(),
            "pending owned-provider removal blocks release installation")
    manifest = resolved["manifest"]
    package_path = pathlib.Path(str(resolved["packagePath"]))
    require(package_path.parent == state_dir and package_path.is_file() and not package_path.is_symlink(), "prepared package was replaced before installation")
    validate_package(package_path, manifest, runner)
    inventory = release_runtime_inventory(existing_inventory(root, runner), root, manifest)
    owned, owned_identity, _ = load_ownership_record(record)
    owned_runtime = bool(
        owned is not None and owned_identity is not None
        and owned.get("schema") == RUNTIME_RECORD_SCHEMA
        and owned.get("channel") == "release"
        and owned.get("sourceCommit") == resolved.get("commit")
    )
    if owned_runtime:
        require(owned.get("tag") == resolved.get("tag") and owned.get("manifest") == manifest,
                "existing WsprryPi-owned runtime release identity differs from the selected source")
        validate_resumable_neutral_activation(owned, inventory, runner)
    if not owned_runtime:
        require_no_runtime_residue(inventory, "release installation")
    require(not inventory["activeModule"], "an active RP1 GPCLK module blocks ordinary installation")
    require(not inventory["configuredRoute"], "a configured RP1 GPCLK route blocks ordinary installation")
    require(not inventory["enrollment"] and not inventory["developmentManager"], "development enrollment or manager binding blocks release installation")
    expected = manifest["debianVersion"]
    installed_by_wsprrypi = False
    if inventory["packageVersion"] is None:
        require(
            not inventory["dkms"] and not inventory["sourceTrees"]
            and not inventory["moduleCandidates"] and not inventory.get("controllerCandidates", [])
            and not inventory["installedOverlays"],
            "foreign or mixed RP1 GPCLK installation blocks package installation",
        )
        ensure_new_ownership_record(record)
        package_preflight(resolved, "unpack", runner)
        runner.run(
            ["apt-get", "install", "--no-install-recommends", "-y", str(package_path)],
            passthrough=True,
        )
        installed_by_wsprrypi = True
    elif inventory["packageVersion"] == expected:
        pass
    else:
        raise ContractError(f"existing RP1-GPCLK-DKMS {inventory['packageVersion']} requires cleanup-first migration: retain its exact package and ownership evidence, prepare/remove its runtime through the owning provider, remove the predecessor through its owning lifecycle, and verify complete absence before installing the selected provider; direct upgrade/downgrade refused")
    verify_installed_release(root, manifest, runner, allow_owned_runtime=owned_runtime)
    if installed_by_wsprrypi:
        artifacts = module_artifacts(root)
        validate_recorded_module_artifacts(artifacts)
        atomic_json_new(record, {
            "schema": RECORD_SCHEMA, "repository": REPOSITORY, "owner": "WsprryPi",
            "channel": "release", "installationMethod": "debian-package",
            "tag": resolved["tag"], "sourceCommit": resolved["commit"],
            "manifest": manifest, "moduleArtifacts": artifacts,
            "routeActivation": "disabled", "output": "disabled",
            "qualificationClaim": False,
        })


def package_preflight(resolved: Mapping[str, Any], operation: str, runner: Runner) -> None:
    """The incoming provider owns package transition admission before APT."""
    source = revalidate_checkout(resolved["checkout"], runner)
    guard = source / "scripts/package_preflight.py"
    require(guard.is_file() and not guard.is_symlink(), "selected source lacks package preflight")
    run_package_preflight(guard, operation, runner)


def run_package_preflight(guard: pathlib.Path, operation: str, runner: Runner) -> None:
    reply = runner.run(["python3", "-I", str(guard), operation], check=False)
    value = json_result(CommandResult(reply.stdout or reply.stderr, reply.stderr, reply.returncode), "package preflight")
    require(reply.returncode == 0 and value.get("contract") == "rp1-gpclk-package-preflight-v1"
            and value.get("status") == "permitted" and value.get("operation") == operation
            and value.get("executionAuthorized") is False and value.get("routeSelected") is False,
            "provider package preflight refused: " + reply.stdout.strip() + " " + reply.stderr.strip())


def package_removal_preflight(record: Mapping[str, Any], root: pathlib.Path, runner: Runner) -> None:
    relative = "usr/libexec/rp1-gpclk-dkms/package_preflight.py"
    members = [member for member in record["manifest"]["packageInventory"]
               if member["path"] == relative and member["type"] == "file"]
    require(len(members) == 1, "owned package lacks its removal preflight")
    guard = installed_member(root, relative, "file")
    require(sha256_file(guard) == members[0]["sha256"], "owned package removal preflight changed")
    run_package_preflight(guard, "remove", runner)


def verify_development_result(
    manifest_path: pathlib.Path,
    resolved: Mapping[str, Any],
) -> dict[str, Any]:
    require(manifest_path.is_file() and not manifest_path.is_symlink(), "upstream development result manifest is missing or substituted")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError("upstream development result manifest is unreadable") from error
    require(isinstance(manifest, dict), "upstream development result manifest is not an object")
    expected = {
        "schema": "rp1-gpclk-source-development-manifest",
        "classification": "source-development",
        "qualification": False,
        "releaseQualified": False,
        "sourceCommit": resolved["commit"],
        "sourceState": "clean",
        "renderedVersion": resolved["version"],
        "dkmsName": DKMS_NAME,
        "moduleName": MODULE_NAME,
        "targetKernel": platform.release(),
    }
    for key, value in expected.items():
        require(manifest.get(key) == value, f"upstream development result has incompatible {key}")
    require(manifest.get("installationMode") == "route-neutral", "upstream development result is not route-neutral")
    version_identity = manifest.get("versionIdentity")
    require(
        isinstance(version_identity, dict)
        and version_identity.get("path") == resolved["versionSource"]
        and version_identity.get("sha256") == resolved["versionSourceSha256"]
        and version_identity.get("moduleVersion") == resolved["version"],
        "upstream development result canonical version identity differs from the selected source",
    )
    require(manifest.get("route") is None, "upstream development result selected a GPIO route")
    safety = manifest.get("routeNeutralSafety")
    require(isinstance(safety, dict) and set(safety) == {"before", "after"}, "upstream development result lacks route-neutral safety observations")
    for stage in ("before", "after"):
        observation = safety.get(stage)
        require(isinstance(observation, dict) and observation, f"upstream development result has invalid {stage} route-neutral observation")
        require(not any(bool(value) for value in observation.values()), f"upstream development result reports a {stage} route-neutral blocker")
    uapi = manifest.get("uapiIdentity")
    require(isinstance(uapi, dict) and uapi.get("sha256") == resolved["uapiSha256"], "upstream development result UAPI differs from the selected source")
    parameters = manifest.get("parameters")
    require(isinstance(parameters, dict) and parameters.get("output_inhibit") == 1,
            "upstream route-neutral development result did not retain installation output inhibition")
    installed = manifest.get("installedModule")
    require(isinstance(installed, dict), "upstream development result lacks installed module identity")
    require(installed.get("moduleName") == MODULE_NAME and installed.get("moduleVersion") == resolved["version"], "upstream installed module identity differs from the selected source")
    require(installed.get("kernel") == platform.release(), "upstream installed module targets a different kernel")
    for field in ("installedFileSha256", "decompressedElfSha256"):
        require(isinstance(installed.get(field), str) and SHA256.fullmatch(installed[field]), f"upstream installed module lacks valid {field}")
    require(manifest.get("buildProfile") == "runtime-controller",
            "upstream development result is not the DKMS runtime-controller profile")
    installed_modules = manifest.get("installedModules")
    require(isinstance(installed_modules, dict)
            and set(installed_modules) == {MODULE_NAME, CONTROLLER_MODULE_NAME},
            "upstream development result lacks the exact runtime module set")
    for name, module in installed_modules.items():
        require(isinstance(module, dict) and module.get("moduleName") == name
                and module.get("moduleVersion") == resolved["version"]
                and module.get("kernel") == platform.release(),
                f"upstream installed {name} identity differs from selected source")
        require(isinstance(module.get("installedPath"), str)
                and isinstance(module.get("installedFileSha256"), str)
                and SHA256.fullmatch(module["installedFileSha256"])
                and isinstance(module.get("decompressedElfSha256"), str)
                and SHA256.fullmatch(module["decompressedElfSha256"]),
                f"upstream installed {name} artifact identity is invalid")
    require(installed == installed_modules[MODULE_NAME],
            "upstream canonical consumer identity differs from runtime module set")
    return manifest


def validate_runtime_development_provider(
    record: Mapping[str, Any], inventory: Mapping[str, Any], runner: Runner
) -> None:
    version = record["productVersion"]
    require(inventory["packageVersion"] is None, "a Debian-owned RP1 provider blocks runtime development reuse")
    require(not inventory["activeModule"], "the transmission consumer is active during neutral runtime reuse")
    # A WsprryPi-owned neutral runtime deliberately keeps the administration
    # controller loaded while the transmission consumer remains absent.  The
    # caller admits that state only for a v3 runtime ownership record; runtime
    # activation subsequently revalidates the bound controller and artifacts.
    require(not inventory["configuredRoute"], "a configured RP1 GPCLK route blocks neutral runtime reuse")
    require(not inventory["enrollment"] and not inventory["developmentManager"], "legacy development administration blocks neutral runtime reuse")
    lines = [line for line in inventory["dkms"].splitlines() if line.strip()]
    expected_dkms = re.compile(rf"^{re.escape(DKMS_NAME)}/{re.escape(version)}[^\n]*installed$")
    require(bool(lines) and all(expected_dkms.fullmatch(line) for line in lines), "runtime development DKMS registration differs from ownership")
    require(inventory["sourceTrees"] == [f"/usr/src/{PACKAGE_NAME}-{version}"], "runtime development source destination differs from ownership")
    require(not inventory["installedOverlays"], "packaged route overlays block neutral runtime reuse")
    require(len(inventory["moduleCandidates"]) == 1,
            "runtime consumer module destination differs from ownership")
    require(len(inventory.get("controllerCandidates", [])) == 1,
            "runtime controller module destination differs from ownership")
    require("Differences between built and installed modules" not in inventory["dkms"],
            "DKMS reports installed module drift")
    installed_version = runner.run(["modinfo", "-k", platform.release(), "-F", "version", MODULE_NAME]).stdout.strip()
    require(installed_version == version, "runtime consumer module version differs from ownership")


def validate_resumable_neutral_activation(
    record: Mapping[str, Any], inventory: Mapping[str, Any], runner: Runner
) -> dict[str, Any]:
    """Read provider-issued provenance rather than interpreting its journals."""
    if record.get("channel") == "development":
        validate_development_rollback_authority(record, pathlib.Path("/"))
    value = runtime_call(runner, RUNTIME_PROVIDER, "installation-status")
    require(type(value.get("schemaVersion")) is int
            and value.get("schemaVersion") == 1
            and value.get("contract") == RUNTIME_UPDATE_CONTRACT
            and value.get("operation") == "installation-status"
            and value.get("status") == "neutral_ready",
            "runtime installation receipt is unavailable or incompatible")
    return validate_runtime_ownership(value.get("receipt"), record)



RUNTIME_UPDATE_CONTRACT = "rp1-gpclk-runtime-update-v1"


def validate_runtime_update_capabilities(reply: CommandResult) -> None:
    capabilities = json_result(reply, "runtime update capabilities")
    require(type(capabilities.get("schemaVersion")) is int
            and capabilities.get("schemaVersion") == 1
            and capabilities.get("contract") == RUNTIME_UPDATE_CONTRACT
            and capabilities.get("operation") == "capabilities"
            and isinstance(capabilities.get("capabilities"), list)
            and {"update-plan", "update-execute", "installation-status"}.issubset(capabilities["capabilities"]),
            "runtime update interface is incompatible; upgrade the provider")


def runtime_update_provider(runner: Runner, candidate: pathlib.Path | None = None) -> pathlib.Path:
    """Negotiate a public interface; the provider owns predecessor compatibility."""
    for provider in dict.fromkeys(path for path in (RUNTIME_PROVIDER, candidate) if path is not None):
        if not provider.is_file() or provider.is_symlink():
            continue
        reply = runner.run(["python3", str(provider), "capabilities"], check=False)
        if reply.returncode != 0:
            continue
        validate_runtime_update_capabilities(reply)
        return provider
    raise ContractError("Provider lacks rp1-gpclk-runtime-update-v1; use an exact compatible provider source before updating or removing the application")


def recover_and_remove_owned_runtime(
    record_path: pathlib.Path,
    record: Mapping[str, Any],
    record_identity: os.stat_result,
    runner: Runner,
    migration_provider: pathlib.Path | None = None,
) -> None:
    provider = runtime_update_provider(runner, migration_provider)
    runtime = (validate_runtime_ownership(record.get("runtime"), record)
               if record.get("schema") == RUNTIME_RECORD_SCHEMA else {})
    identity = {"sourceCommit": record["sourceCommit"],
                "bindingSha256": runtime.get("bindingSha256"),
                "artifactSetSha256": runtime.get("artifactSetSha256"),
                "deploymentPlanSha256": runtime.get("deploymentPlanSha256")}
    arguments = ["--source-commit", identity["sourceCommit"]]
    for field, option in (("bindingSha256", "--binding-sha256"),
                          ("artifactSetSha256", "--artifact-set-sha256"),
                          ("deploymentPlanSha256", "--deployment-sha256")):
        if identity[field] is not None:
            arguments.extend([option, identity[field]])
    seen = set()
    for _ in range(8):
        require_unchanged_ownership(record_path, record, record_identity, "runtime update planning")
        planned = runtime_call(runner, provider, "update-plan", arguments)
        validate_runtime_update_envelope(planned, "update-plan", identity)
        digest = planned.get("planSha256")
        require(isinstance(digest, str) and SHA256.fullmatch(digest)
                and isinstance(planned.get("plan"), dict)
                and digest == sha256_bytes(canonical(planned["plan"])),
                "runtime update plan digest differs")
        if planned["status"] == "prepared":
            return
        require(digest not in seen, "runtime update made no progress; preserve evidence and re-plan")
        seen.add(digest)
        require_unchanged_ownership(record_path, record, record_identity, "runtime update execution")
        completed = runtime_call(runner, provider, "update-execute",
                                 [*arguments, "--plan-sha256", digest])
        validate_runtime_update_envelope(completed, "update-execute", identity)
        require(completed.get("planSha256") == digest, "runtime update result does not match reviewed plan")
        if completed["status"] == "prepared":
            return
    raise ContractError("runtime update exceeded its bounded steps; preserve evidence and re-plan")


def validate_runtime_update_envelope(value: Mapping[str, Any], operation: str,
                                     identity: Mapping[str, Any]) -> None:
    require(type(value.get("schemaVersion")) is int
            and value.get("schemaVersion") == 1
            and value.get("contract") == RUNTIME_UPDATE_CONTRACT
            and value.get("operation") == operation
            and value.get("identity") == identity
            and value.get("status") in {"planned", "prepared"}
            and isinstance(value.get("postconditions"), dict)
            and all(type(value["postconditions"].get(name)) is bool for name in
                    ("runtimeAbsent", "transmissionAuthorized"))
            and value.get("postconditions") == {
                "runtimeAbsent": value.get("status") == "prepared",
                "transmissionAuthorized": False},
            "runtime update response identity or postconditions differ")



def apply_development(resolved: Mapping[str, Any], record: pathlib.Path, runner: Runner) -> None:
    require(not removal_pending_path(record).exists() and not removal_pending_path(record).is_symlink(),
            "pending owned-provider removal blocks development installation")
    source = revalidate_checkout(resolved["checkout"], runner)
    interface, route_args = route_neutral_interface(source, runner)
    require(interface == resolved["interface"] and route_args == resolved["routeArguments"], "upstream development interface changed after preflight")
    before = existing_inventory(pathlib.Path("/"), runner)
    require(not before["configuredRoute"], "a configured RP1 GPCLK route blocks exact-source development installation")
    existing_record, record_identity, record_reason = load_ownership_record(record)
    owned_runtime = bool(
        existing_record is not None and record_identity is not None
        and existing_record.get("schema") == RUNTIME_RECORD_SCHEMA
    )
    owned_provider = bool(
        existing_record is not None and record_identity is not None
        and existing_record.get("schema") == RECORD_SCHEMA
    )
    require(
        not before.get("activeController", False) or owned_runtime or owned_provider,
        "an active RP1 route controller blocks exact-source development installation",
    )
    if not (owned_runtime or owned_provider):
        require_no_runtime_residue(before, "source-development installation")
    provider_present = bool(
        before["packageVersion"] is not None
        or before["dkms"]
        or before["sourceTrees"]
        or before["moduleCandidates"]
        or before.get("controllerCandidates", [])
        or before["installedOverlays"]
        or before["enrollment"]
        or before["developmentManager"]
    )
    if provider_present and existing_record is not None and record_identity is not None:
        require(existing_record["channel"] == "development", "an existing WsprryPi-owned release provider requires its owning migration workflow")
        expected_identity = {
            "sourceCommit": resolved["commit"],
            "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"],
            "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": platform.release(),
            "compatibilityIdentity": development_compatibility(resolved["version"]),
        }
        identity_matches = all(
            existing_record.get(name) == value
            for name, value in expected_identity.items()
        )
        profile_migration_required = bool(
            owned_runtime and (
                len(before["moduleCandidates"]) != 1
                or len(before.get("controllerCandidates", [])) != 1
                or "Differences between built and installed modules" in before["dkms"]
            )
        )
        migration_required = not identity_matches or profile_migration_required
        recoverable_runtime = bool(
            migration_required
            and before["runtimeResidue"]
            and existing_record.get("schema")
                in {RECORD_SCHEMA, RUNTIME_RECORD_SCHEMA}
        )
        require(
            not before["activeModule"] or recoverable_runtime,
            "an active RP1 GPCLK module blocks exact-source development installation",
        )
        require(
            not before.get("activeController", False)
            or owned_runtime
            or identity_matches,
            "an active RP1 route controller blocks exact-source development installation",
        )
        if (migration_required
                and existing_record.get("schema") in {RECORD_SCHEMA, RUNTIME_RECORD_SCHEMA}):
            if before["runtimeResidue"]:
                entrypoint, rollback = validate_development_rollback_authority(
                    existing_record, pathlib.Path("/")
                )
                recover_and_remove_owned_runtime(
                    record, existing_record, record_identity, runner,
                    source / "scripts/runtime_provider.py",
                )
                verified_entrypoint, verified_rollback = validate_development_removal(
                    existing_record, pathlib.Path("/"), runner
                )
                require(
                    (verified_entrypoint, verified_rollback) == (entrypoint, rollback),
                    "development rollback authority changed after runtime removal",
                )
            else:
                entrypoint, rollback = validate_development_removal(
                    existing_record, pathlib.Path("/"), runner
                )
            current_record, current_identity, current_reason = load_ownership_record(record)
            require(
                current_record == existing_record
                and current_identity is not None
                and (current_identity.st_dev, current_identity.st_ino)
                == (record_identity.st_dev, record_identity.st_ino),
                f"installation ownership changed before partial-provider migration: {current_reason or 'identity mismatch'}",
            )
            runner.run([str(entrypoint), "--record", str(rollback)],
                       cwd=entrypoint.parents[1], passthrough=True)
            verify_provider_absent(pathlib.Path("/"), runner)
            remove_ownership_record(record, record_identity)
            return apply_development(resolved, record, runner)
        require(identity_matches,
                "existing WsprryPi-owned development provider identity differs from the selected source")
        checkpoint_runtime = None
        if owned_runtime:
            validate_runtime_development_provider(existing_record, before, runner)
        elif before.get("activeController", False):
            checkpoint_runtime = validate_resumable_neutral_activation(
                existing_record, before, runner
            )
        elif before["runtimeResidue"]:
            validate_development_rollback_authority(
                existing_record, pathlib.Path("/")
            )
        else:
            validate_development_removal(existing_record, pathlib.Path("/"), runner)
        current_record, current_identity, current_reason = load_ownership_record(record)
        require(
            current_record == existing_record
            and current_identity is not None
            and (current_identity.st_dev, current_identity.st_ino)
            == (record_identity.st_dev, record_identity.st_ino),
            f"installation ownership changed during exact-provider verification: {current_reason or 'identity mismatch'}",
        )
        # Recheck the provider after the record observation so a change during
        # the first verification cannot be reported as an exact no-op.
        if owned_runtime:
            validate_runtime_development_provider(current_record, existing_inventory(pathlib.Path("/"), runner), runner)
        elif before.get("activeController", False):
            rechecked_runtime = validate_resumable_neutral_activation(
                current_record, existing_inventory(pathlib.Path("/"), runner), runner
            )
            require(
                rechecked_runtime == checkpoint_runtime,
                "interrupted runtime activation changed during verification",
            )
            replacement = copy.deepcopy(current_record)
            replacement["schema"] = RUNTIME_RECORD_SCHEMA
            replacement["runtime"] = rechecked_runtime
            validate_runtime_ownership(rechecked_runtime, replacement)
            replace_owned_record(record, current_record, current_identity, replacement)
        elif before["runtimeResidue"]:
            validate_development_rollback_authority(
                current_record, pathlib.Path("/")
            )
        else:
            validate_development_removal(current_record, pathlib.Path("/"), runner)
        print("Exact WsprryPi-owned development provider already installed; no provider mutation performed.")
        return
    require(not before["activeModule"], "an active RP1 GPCLK module blocks exact-source development installation")
    if provider_present and (existing_record is None or record_identity is None):
        require(False, f"an existing provider is not adoptable: {record_reason or 'WsprryPi ownership is unproven'}")
    if (not provider_present and existing_record is not None
            and record_identity is not None):
        # Resume an interrupted owned migration that completed provider
        # rollback but stopped before retiring the ownership record.
        verify_provider_absent(pathlib.Path("/"), runner)
        remove_ownership_record(record, record_identity)
        return apply_development(resolved, record, runner)
    require(
        before["packageVersion"] is None
        and not before["dkms"]
        and not before["sourceTrees"]
        and not before["moduleCandidates"]
        and not before.get("controllerCandidates", [])
        and not before["installedOverlays"]
        and not before["enrollment"]
        and not before["developmentManager"],
        "an existing packaged, development, foreign, or mixed RP1 GPCLK installation requires its owning migration workflow",
    )
    ensure_new_ownership_record(record)
    evidence = record.parent / f"rp1-gpclk-dkms-development-evidence.{secrets.token_hex(12)}"
    require(not evidence.exists() and not evidence.is_symlink(), "development evidence destination already exists")
    command = [
        str(source / "scripts" / "development-install"), "--source", str(source),
        "--kernel", platform.release(),
        *route_args, "--output-inhibit", "1", "--install", "--evidence-directory", str(evidence),
        "--runtime-controller",
    ]
    runner.run(command, cwd=source, passthrough=True)
    result = verify_development_result(evidence / "rendered-source" / "DEVELOPMENT_MANIFEST.json", resolved)
    after = existing_inventory(pathlib.Path("/"), runner)
    require(not after["activeModule"], "RP1 GPCLK module became active during development installation")
    require(not after.get("activeController", False), "RP1 route controller became active during development installation")
    require(not after["configuredRoute"], "RP1 GPCLK route became configured during development installation")
    installed = result["installedModule"]
    installed_modules = result["installedModules"]
    rollback_record = pathlib.Path(str(result.get("rollbackRecord", "")))
    expected_rollback = evidence / "ROLLBACK.json"
    require(rollback_record == expected_rollback and rollback_record.is_file() and not rollback_record.is_symlink(), "upstream development rollback record is missing or out of scope")
    rollback_entrypoint = evidence / "rendered-source" / "scripts" / "development-rollback"
    require(rollback_entrypoint.is_file() and not rollback_entrypoint.is_symlink(), "upstream development rollback entrypoint is missing or substituted")
    installed_path = pathlib.Path(str(installed.get("installedPath", "")))
    expected_prefix = pathlib.Path(f"/lib/modules/{platform.release()}/updates/dkms")
    require(installed_path.is_absolute() and installed_path.parent == expected_prefix, "upstream installed module path is outside the exact-kernel DKMS destination")
    controller_path = pathlib.Path(str(installed_modules[CONTROLLER_MODULE_NAME].get("installedPath", "")))
    require(controller_path.is_absolute() and controller_path.parent == expected_prefix,
            "upstream installed controller path is outside the exact-kernel DKMS destination")
    require(existing_inventory(pathlib.Path("/"), runner).get("controllerCandidates", []) == [str(controller_path)],
            "upstream runtime controller installation is not unique")
    atomic_json_new(record, {
        "schema": RECORD_SCHEMA, "repository": REPOSITORY, "owner": "WsprryPi",
        "channel": "development", "installationMethod": "upstream-development-rollback",
        "sourceCommit": resolved["commit"], "productVersion": resolved["version"],
        "sourceTree": resolved["sourceTree"], "uapiSha256": resolved["uapiSha256"],
        "versionSource": resolved["versionSource"],
        "versionSourceSha256": resolved["versionSourceSha256"],
        "installedModuleSha256": installed["installedFileSha256"],
        "decompressedModuleSha256": installed["decompressedElfSha256"],
        "installedModulePath": str(installed_path),
        "targetKernel": platform.release(),
        "compatibilityIdentity": development_compatibility(resolved["version"]), "routeActivation": "disabled",
        "output": "disabled", "qualificationClaim": False, "upstreamEvidence": str(evidence),
        "rollbackRecord": str(rollback_record), "rollbackRecordSha256": sha256_file(rollback_record),
        "rollbackEntrypoint": str(rollback_entrypoint),
        "rollbackEntrypointSha256": sha256_file(rollback_entrypoint),
    })


def require_hash(value: Any, field: str) -> str:
    require(isinstance(value, str) and SHA256.fullmatch(value) is not None, f"installation ownership record has invalid {field}")
    return value


def validate_recorded_module_artifacts(value: Any) -> list[dict[str, str]]:
    require(isinstance(value, list) and value, "installation ownership record lacks module artifacts")
    result: list[dict[str, str]] = []
    for item in value:
        require(isinstance(item, dict), "installation ownership module artifact is not an object")
        require_exact_keys(item, {"path", "sha256"}, "installation ownership module artifact")
        path = item["path"]
        require(
            isinstance(path, str)
            and re.fullmatch(r"/lib/modules/[^/]+/updates/dkms/(?:rp1_gpclk_dkms|rp1_route_controller)\.ko(?:\.(?:xz|gz|zst|bz2))?", path) is not None,
            "installation ownership module artifact path is out of scope",
        )
        result.append({"path": path, "sha256": require_hash(item["sha256"], "module artifact SHA-256")})
    require(len({item["path"] for item in result}) == len(result), "installation ownership module artifacts contain duplicate paths")
    kernels: dict[str, list[str]] = {}
    for item in result:
        parts = pathlib.PurePosixPath(item["path"]).parts
        kernels.setdefault(parts[3], []).append(parts[-1].split(".ko")[0])
    require(all(sorted(names) == sorted((MODULE_NAME, CONTROLLER_MODULE_NAME))
                for names in kernels.values()), "installation ownership requires one consumer/controller pair per kernel")
    return sorted(result, key=lambda item: item["path"])


def validate_release_removal(record: Mapping[str, Any], root: pathlib.Path, runner: Runner) -> None:
    require(record["installationMethod"] == "debian-package", "release ownership installation method differs")
    tag = record["tag"]
    commit = record["sourceCommit"]
    require(isinstance(tag, str) and SEMVER.fullmatch(tag) is not None, "release ownership tag is invalid")
    require(isinstance(commit, str) and SHA40.fullmatch(commit) is not None, "release ownership commit is invalid")
    manifest = record["manifest"]
    require(isinstance(manifest, dict), "release ownership manifest is invalid")
    validated = validate_manifest(manifest, tag=tag, tag_commit=commit)
    verify_installed_release(root, validated, runner)
    expected = {item["path"]: item["sha256"] for item in validate_recorded_module_artifacts(record["moduleArtifacts"])}
    current = {item["path"]: item["sha256"] for item in validate_recorded_module_artifacts(module_artifacts(root))}
    require(all(current.get(path) == digest for path, digest in expected.items()), "recorded DKMS module artifacts differ from WsprryPi ownership")
    for path in current:
        match = re.fullmatch(r"/lib/modules/([^/]+)/updates/dkms/(rp1_gpclk_dkms|rp1_route_controller)\.ko(?:\.(?:xz|gz|zst|bz2))?", path)
        require(match is not None, "installed DKMS module artifact path is out of scope")
        version = runner.run(["modinfo", "-k", match.group(1), "-F", "version", match.group(2)]).stdout.strip()
        require(version == validated["productVersion"], "additional-kernel DKMS module identity differs from WsprryPi ownership")


def secure_owned_path(path: pathlib.Path, parent: pathlib.Path, description: str) -> pathlib.Path:
    require(path.is_absolute(), f"{description} path is not absolute")
    absolute = pathlib.Path(os.path.abspath(path))
    parent_absolute = pathlib.Path(os.path.abspath(parent))
    require(absolute.is_relative_to(parent_absolute), f"{description} path is outside WsprryPi-owned evidence")
    try:
        resolved = absolute.resolve(strict=True)
    except OSError as error:
        raise ContractError(f"{description} is unavailable") from error
    require(resolved == absolute and resolved.is_file() and not resolved.is_symlink(), f"{description} is missing or substituted")
    info = resolved.stat()
    require(info.st_uid == os.geteuid(), f"{description} is not owned by the invoking user")
    require(stat.S_IMODE(info.st_mode) & 0o022 == 0, f"{description} is group- or world-writable")
    return resolved


def validate_development_rollback_authority(
    record: Mapping[str, Any], root: pathlib.Path
) -> tuple[pathlib.Path, pathlib.Path]:
    require(record["installationMethod"] == "upstream-development-rollback", "development ownership installation method differs")
    require(isinstance(record["sourceCommit"], str) and SHA40.fullmatch(record["sourceCommit"]) is not None, "development ownership commit is invalid")
    require(isinstance(record["productVersion"], str) and SAFE_VERSION.fullmatch(record["productVersion"]) is not None, "development ownership version is invalid")
    require(SHA40.fullmatch(record["sourceTree"]) is not None, "development ownership source tree is invalid")
    require(record["versionSource"] == "include/rp1_gpclk/version.h", "development ownership version source differs")
    require(record["compatibilityIdentity"] == development_compatibility(record["productVersion"])
            or (record["productVersion"] == "0.9.0"
                and record["compatibilityIdentity"] == LEGACY_COMPATIBILITY_IDENTITY),
            "development ownership compatibility identity differs")
    require(record["targetKernel"] == platform.release(), "development ownership targets a different running kernel")
    for field in (
        "uapiSha256", "versionSourceSha256", "rollbackRecordSha256",
        "rollbackEntrypointSha256", "installedModuleSha256", "decompressedModuleSha256",
    ):
        require_hash(record[field], field)
    evidence = pathlib.Path(record["upstreamEvidence"])
    expected_parent = pathlib.Path("/var/lib/wsprrypi")
    if root != pathlib.Path("/"):
        expected_parent = root_path(root, str(expected_parent))
    require(
        evidence.parent == expected_parent
        and re.fullmatch(r"rp1-gpclk-dkms-development-evidence\.[0-9a-f]{24}", evidence.name) is not None,
        "development ownership evidence path differs",
    )
    rollback = secure_owned_path(pathlib.Path(record["rollbackRecord"]), evidence, "development rollback record")
    entrypoint = secure_owned_path(pathlib.Path(record["rollbackEntrypoint"]), evidence, "development rollback entrypoint")
    require(rollback == evidence / "ROLLBACK.json", "development rollback record path differs")
    require(entrypoint == evidence / "rendered-source/scripts/development-rollback", "development rollback entrypoint path differs")
    require(sha256_file(rollback) == record["rollbackRecordSha256"], "development rollback record identity differs")
    require(sha256_file(entrypoint) == record["rollbackEntrypointSha256"], "development rollback entrypoint identity differs")
    return entrypoint, rollback


def validate_development_removal(
    record: Mapping[str, Any], root: pathlib.Path, runner: Runner
) -> tuple[pathlib.Path, pathlib.Path]:
    entrypoint, rollback = validate_development_rollback_authority(record, root)
    inventory = existing_inventory(root, runner)
    require_no_runtime_residue(inventory, "source-development removal")
    require(inventory["packageVersion"] is None, "a Debian-owned RP1 provider blocks development rollback")
    require(not inventory["activeModule"], "an active RP1 GPCLK module blocks owned provider removal")
    require(not inventory.get("activeController", False), "an active RP1 route controller blocks owned provider removal")
    require(not inventory["configuredRoute"], "a configured RP1 GPCLK route blocks owned provider removal")
    require(not inventory["enrollment"] and not inventory["developmentManager"], "development enrollment or manager binding blocks owned provider removal")
    version = record["productVersion"]
    dkms_lines = [line for line in inventory["dkms"].splitlines() if line.strip()]
    expected_dkms = re.compile(rf"^{re.escape(DKMS_NAME)}/{re.escape(version)}[^\n]*installed$")
    require(bool(dkms_lines) and all(expected_dkms.fullmatch(line) for line in dkms_lines), "development DKMS registration differs from WsprryPi ownership")
    require("Differences between built and installed modules" not in inventory["dkms"],
            "DKMS reports installed module drift")
    expected_source = str(root_path(root, f"/usr/src/{PACKAGE_NAME}-{version}"))
    require(inventory["sourceTrees"] == [expected_source], "development source destination differs from WsprryPi ownership")
    require(not inventory["installedOverlays"], "installed RP1 route overlays block development rollback")
    installed_path = pathlib.Path(record["installedModulePath"])
    expected_prefix = pathlib.Path(f"/lib/modules/{record['targetKernel']}/updates/dkms")
    require(installed_path.is_absolute() and installed_path.parent == expected_prefix, "development installed module path is out of scope")
    actual_installed = root_path(root, str(installed_path))
    require(inventory["moduleCandidates"] == [str(actual_installed)], "development module destination differs from WsprryPi ownership")
    require(actual_installed.is_file() and not actual_installed.is_symlink(), "development installed module is missing or substituted")
    require(sha256_file(actual_installed) == record["installedModuleSha256"], "development installed module identity differs")
    evidence = pathlib.Path(record["upstreamEvidence"])
    manifest_path = secure_owned_path(
        evidence / "rendered-source/DEVELOPMENT_MANIFEST.json", evidence,
        "development source manifest")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        rollback_value = json.loads(rollback.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError("development module-set ownership evidence is unreadable") from error
    profile = rollback_value.get("buildProfile", "ordinary")
    require(profile in {"ordinary", "runtime-controller"},
            "development rollback build profile is invalid")
    if profile == "runtime-controller":
        installed_modules = rollback_value.get("installedModules")
        require(manifest.get("buildProfile") == "runtime-controller"
                and isinstance(installed_modules, dict)
                and set(installed_modules) == {MODULE_NAME, CONTROLLER_MODULE_NAME}
                and installed_modules == manifest.get("installedModules")
                and rollback_value.get("installedManifestSha256") == sha256_file(manifest_path),
                "development rollback does not own the runtime module pair")
        controller = installed_modules[CONTROLLER_MODULE_NAME]
        controller_path = pathlib.Path(controller.get("installedPath", ""))
        require(controller_path.is_absolute() and controller_path.parent == expected_prefix,
                "development controller path is out of scope")
        actual_controller = root_path(root, str(controller_path))
        require(inventory.get("controllerCandidates", []) == [str(actual_controller)]
                and actual_controller.is_file() and not actual_controller.is_symlink()
                and sha256_file(actual_controller) == controller.get("installedFileSha256"),
                "development controller identity differs from WsprryPi ownership")
    else:
        require(not inventory.get("controllerCandidates", []),
                "legacy development rollback does not own a controller module")
    return entrypoint, rollback


def verify_provider_absent(root: pathlib.Path, runner: Runner) -> None:
    inventory = existing_inventory(root, runner)
    require_no_runtime_residue(inventory, "provider absence verification")
    require(
        inventory["packageVersion"] is None
        and not inventory["dkms"]
        and not inventory["activeModule"]
        and not inventory.get("activeController", False)
        and not inventory["sourceTrees"]
        and not inventory["moduleCandidates"]
        and not inventory.get("controllerCandidates", [])
        and not inventory["installedOverlays"]
        and not inventory["configuredRoute"]
        and not inventory["enrollment"]
        and not inventory["developmentManager"],
        "provider removal completed with residual or active RP1 state",
    )


def remove_owned_provider(args: argparse.Namespace, runner: Runner) -> None:
    if args.remove == "false":
        print("RP1-GPCLK-DKMS preserved: explicit operator opt-out.")
        return
    recovered_removal = recover_pending_removal(args.record)
    record, identity, reason = load_ownership_record(args.record)
    if record is None or identity is None:
        try:
            root = args.root.resolve(strict=True)
            verify_provider_absent(root, runner)
        except (OSError, ContractError):
            print(f"RP1-GPCLK-DKMS preserved: {reason or 'WsprryPi ownership is unproven'}.")
        else:
            print("RP1-GPCLK-DKMS absent: no provider removal was required.")
        return
    try:
        root = args.root.resolve(strict=True)
    except OSError as error:
        raise ContractError("provider inventory root is unavailable") from error
    if recovered_removal:
        try:
            verify_provider_absent(root, runner)
        except ContractError:
            pass
        else:
            remove_ownership_record(args.record, identity)
            print("Interrupted owned-package removal completed; absence verified.")
            return
    try:
        if record["schema"] == RUNTIME_RECORD_SCHEMA:
            inventory = existing_inventory(root, runner)
            if record["channel"] == "release":
                inventory = release_runtime_inventory(inventory, root, record["manifest"])
        else:
            inventory = {"runtimeResidue": []}
        if inventory["runtimeResidue"]:
            require(root == pathlib.Path("/"),
                    "runtime removal requires the production filesystem root")
            if record["channel"] == "development":
                validate_development_rollback_authority(record, root)
            recover_and_remove_owned_runtime(
                args.record, record, identity, runner)
        if record["channel"] == "release":
            validate_release_removal(record, root, runner)
            command = ["apt-get", "remove", "-y", PACKAGE_NAME]
            working_directory = None
        else:
            entrypoint, rollback = validate_development_removal(record, root, runner)
            command = [str(entrypoint), "--record", str(rollback)]
            working_directory = entrypoint.parents[1]
    except ContractError as error:
        print(f"RP1-GPCLK-DKMS preserved: installed state does not match WsprryPi ownership ({error}).")
        if args.remove == "true":
            raise ContractError(
                "explicitly requested owned-provider removal did not complete"
            ) from error
        return
    current_record, current_identity, current_reason = load_ownership_record(args.record)
    require(
        current_record == record
        and current_identity is not None
        and (current_identity.st_dev, current_identity.st_ino) == (identity.st_dev, identity.st_ino),
        f"installation ownership changed before provider removal: {current_reason or 'identity mismatch'}",
    )
    if record["channel"] == "release":
        # The provider correctly refuses an active application ownership record.
        # Retain its exact authority durably before retiring that record. This
        # journal blocks installation and permits recovery after process death.
        pending = removal_pending_path(args.record)
        atomic_json_new(pending, record)
        pending_identity = record_file_identity(pending)
        require_unchanged_ownership(args.record, record, identity, "package ownership retirement")
        remove_ownership_record(args.record, identity)
        try:
            package_removal_preflight(record, root, runner)
            runner.run(command, cwd=working_directory, passthrough=True)
            verify_provider_absent(root, runner)
            require(not args.record.exists() and not args.record.is_symlink(),
                    "concurrent ownership record appeared during package removal")
        except BaseException:
            recover_pending_removal(args.record)
            raise
        remove_ownership_record(pending, pending_identity)
    else:
        runner.run(command, cwd=working_directory, passthrough=True)
        verify_provider_absent(root, runner)
        remove_ownership_record(args.record, identity)
    print("WsprryPi-owned RP1-GPCLK-DKMS provider removed and absence verified.")


def load_plan(state_dir: pathlib.Path) -> dict[str, Any]:
    plan_path = state_dir / "plan.json"
    require(plan_path.is_file() and not plan_path.is_symlink(), "prepared plan is missing or substituted")
    try:
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError("prepared plan is unreadable") from error
    require(isinstance(plan, dict) and plan.get("schema") == STATE_SCHEMA, "prepared plan schema is invalid")
    return plan


def runtime_product_version(resolved: Mapping[str, Any]) -> str:
    if resolved.get("channel") == "release":
        value = resolved.get("manifest", {}).get("productVersion")
    else:
        value = resolved.get("version")
    require(isinstance(value, str) and SAFE_VERSION.fullmatch(value), "resolved runtime product version is invalid")
    return value


def validate_runtime_bundle(
    bundle: pathlib.Path,
    resolved: Mapping[str, Any],
    companion: pathlib.Path = RUNTIME_COMPANION,
    module_root: pathlib.Path = pathlib.Path("/"),
) -> dict[str, Any]:
    require(bundle.is_absolute() and bundle.is_dir() and not bundle.is_symlink(), "runtime bundle directory is missing or substituted")
    info = bundle.stat()
    require(info.st_uid == os.geteuid() and stat.S_IMODE(info.st_mode) & 0o077 == 0, "runtime bundle directory is not private to the invoking user")
    binding_path = bundle / "binding.json"
    require(binding_path.is_file() and not binding_path.is_symlink(), "runtime bundle binding is missing or substituted")
    try:
        binding = json.loads(binding_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError("runtime bundle binding is unreadable") from error
    required = {
        "schemaVersion", "contract", "productVersion", "compatibilityIdentities",
        "sourceCommit", "kernel", "files", "externalFiles", "modules", "uapiSha256",
        "controllerNoteSha256", "consumerNoteSha256", "artifactSetSha256",
    }
    require(isinstance(binding, dict), "runtime bundle binding is not an object")
    require_exact_keys(binding, required, "runtime bundle binding")
    product = runtime_product_version(resolved)
    compatibility = {
        "gpio4": f"v{product}-rp1-gpio4",
        "gpio20": f"v{product}-rp1-gpio20",
    }
    require(binding["schemaVersion"] == 3 and binding["contract"] == RUNTIME_BINDING_CONTRACT, "runtime bundle binding contract differs")
    require(binding["sourceCommit"] == resolved.get("commit"), "runtime bundle source commit differs from selected provider")
    require(binding["productVersion"] == product, "runtime bundle product version differs from selected provider")
    require(binding["kernel"] == platform.release(), "runtime bundle targets a different running kernel")
    require(binding["compatibilityIdentities"] == compatibility, "runtime bundle compatibility identities differ")
    files = binding["files"]
    require(isinstance(files, dict) and files, "runtime bundle file inventory is invalid")
    critical = {
        "/usr/lib/rp1-gpclk-dkms/runtime_activation.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_provider.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_deployment.py",
        "/usr/lib/rp1-gpclk-dkms/runtime_binding.py",
        "/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_gpclk.h",
        "/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_route_admin.h",
        "/usr/lib/rp1-gpclk-dkms/runtime-overlays/gpio4.dtbo",
        "/usr/lib/rp1-gpclk-dkms/runtime-overlays/gpio20.dtbo",
        "/usr/lib/rp1-gpclk-dkms/schema/rp1-gpclk-runtime-readiness-v1.schema.json",
        "/usr/lib/systemd/system/rp1-gpclk-route-manager.socket",
        "/usr/lib/systemd/system/rp1-gpclk-route-manager@.service",
        "/etc/systemd/system/rp1-gpclk-route-manager@.service.d/95-runtime-controller.conf",
    }
    require(critical <= set(files), "runtime bundle omits a required bound artifact")
    require(not any(path.startswith("/lib/modules/") for path in files),
            "runtime bundle attempts to deploy a DKMS-owned module")
    require(all(isinstance(path, str) and isinstance(value, str) and SHA256.fullmatch(value) for path, value in files.items()), "runtime bundle file digests are invalid")
    modules = binding["modules"]
    require(isinstance(modules, dict)
            and set(modules) == {CONTROLLER_MODULE_NAME, MODULE_NAME},
            "runtime bundle module prerequisite inventory differs")
    for name, note_field in ((CONTROLLER_MODULE_NAME, "controllerNoteSha256"),
                             (MODULE_NAME, "consumerNoteSha256")):
        module = modules[name]
        require(isinstance(module, dict)
                and set(module) == {"name", "path", "installedFileSha256",
                                    "decompressedElfSha256", "compression",
                                    "buildNoteSha256", "version", "kernel"},
                f"runtime bundle {name} identity is invalid")
        path = pathlib.Path(module["path"])
        require(module["name"] == name and path.is_absolute()
                and path.parent == pathlib.Path(
                    f"/lib/modules/{platform.release()}/updates/dkms")
                and re.fullmatch(rf"{re.escape(name)}\.ko(?:\.(?:xz|gz|zst|bz2))?",
                                 path.name) is not None,
                f"runtime bundle {name} path is invalid")
        actual_path = root_path(module_root, str(path))
        expected_compression = ("none" if path.name.endswith(".ko")
                                else path.suffix.removeprefix("."))
        require(actual_path.is_file() and not actual_path.is_symlink()
                and module["compression"] == expected_compression
                and sha256_file(actual_path) == module["installedFileSha256"]
                and decompressed_module_sha256(actual_path, module["compression"])
                    == module["decompressedElfSha256"],
                f"runtime bundle {name} installed artifact differs")
        require(module["version"] == product
                and module["kernel"] == platform.release()
                and module["buildNoteSha256"] == binding[note_field]
                and all(isinstance(module[field], str) and SHA256.fullmatch(module[field])
                        for field in ("installedFileSha256", "decompressedElfSha256",
                                      "buildNoteSha256")),
                f"runtime bundle {name} identity differs")
    external = binding["externalFiles"]
    expected_external = {str(companion)}
    require(isinstance(external, dict) and set(external) == expected_external, "runtime bundle external prerequisite inventory differs")
    require(companion.is_file() and not companion.is_symlink(), "installed WsprryPi route companion is missing or substituted")
    require(external[str(companion)] == sha256_file(companion), "runtime bundle WsprryPi companion identity differs")
    uapi = binding["uapiSha256"]
    require(isinstance(uapi, dict) and set(uapi) == {"consumer", "controller"}, "runtime bundle UAPI identity is invalid")
    require(
        uapi["consumer"] == files["/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_gpclk.h"]
        and uapi["controller"] == files["/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_route_admin.h"],
        "runtime bundle UAPI digests differ from bound artifacts",
    )
    for value in (
        *external.values(), *uapi.values(),
        binding["controllerNoteSha256"], binding["consumerNoteSha256"],
        binding["artifactSetSha256"],
    ):
        require(isinstance(value, str) and SHA256.fullmatch(value), "runtime bundle contains an invalid identity digest")
    identity = dict(binding)
    identity.pop("artifactSetSha256")
    require(sha256_bytes(canonical(identity)) == binding["artifactSetSha256"], "runtime bundle artifact-set digest differs")
    bootstrap = {
        "runtime_deployment.py", "runtime_controller_admin.py", "runtime_layout.py",
        "runtime_application.py", "runtime_output.py", "runtime_provider.py",
        "runtime_binding.py", "runtime_activation.py", "runtime_route_client.py",
        "runtime_manager.py", "package_preflight.py",
    }
    expected_members = {"binding.json", *bootstrap}
    for destination, expected in files.items():
        member = bundle / (sha256_bytes(destination.encode()) + ".bin")
        require(member.is_file() and not member.is_symlink(), f"runtime bundle payload is missing: {destination}")
        require(sha256_file(member) == expected, f"runtime bundle payload digest differs: {destination}")
        expected_members.add(member.name)
    actual_members = {path.name for path in bundle.iterdir()}
    require(actual_members == expected_members, "runtime bundle contains an unsupported or missing member")
    for name in bootstrap:
        member = bundle / name
        require(member.is_file() and not member.is_symlink(), f"runtime bundle bootstrap is missing: {name}")
        destination = "/usr/lib/rp1-gpclk-dkms/" + name
        require(destination in files and sha256_file(member) == files[destination], f"runtime bundle bootstrap digest differs: {name}")
    validate_bootstrap_import_closure(bundle, bootstrap)
    return {
        "binding": binding,
        "bindingSha256": sha256_file(binding_path),
        "artifactSetSha256": binding["artifactSetSha256"],
    }


def validate_bootstrap_import_closure(bundle: pathlib.Path, bootstrap: set[str]) -> None:
    """Compile bootstrap sources and require every local runtime import."""
    available = {pathlib.Path(name).stem for name in bootstrap}
    for name in sorted(bootstrap):
        member = bundle / name
        try:
            source = member.read_text(encoding="utf-8")
            tree = ast.parse(source, filename=str(member))
        except (OSError, UnicodeDecodeError, SyntaxError) as error:
            raise ContractError(f"runtime bundle bootstrap is not valid Python: {name}") from error
        imports = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imports.update(alias.name.split(".", 1)[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                imports.add(node.module.split(".", 1)[0])
        missing = sorted(module for module in imports
                         if module.startswith("runtime_") and module not in available)
        require(not missing,
                f"runtime bundle bootstrap import closure is incomplete: {name} imports {', '.join(missing)}")


def build_runtime_bundle(
    state_dir: pathlib.Path,
    resolved: Mapping[str, Any],
    runner: Runner,
) -> tuple[pathlib.Path, dict[str, Any]]:
    checkout = resolved.get("checkout")
    require(isinstance(checkout, dict), "resolved runtime source checkout is missing")
    source = revalidate_checkout(checkout, runner)
    runtime_source_interface(source, runner)
    bundle = state_dir / "runtime-bundle"
    require(not bundle.exists() and not bundle.is_symlink(), "runtime bundle destination already exists")
    module_directory = pathlib.Path(f"/lib/modules/{platform.release()}/updates/dkms")
    require(module_directory.is_dir() and not module_directory.is_symlink(),
            "DKMS runtime-profile module directory is unavailable")
    runner.run(
        ["python3", str(source / "scripts/build_runtime_bundle.py"),
         str(module_directory), str(bundle),
         "--application-companion", str(RUNTIME_COMPANION)],
        cwd=source,
        passthrough=True,
    )
    return bundle, validate_runtime_bundle(bundle, resolved)


def runtime_call(
    runner: Runner,
    provider: pathlib.Path,
    operation: str,
    arguments: Sequence[str] = (),
    allowed_results: Iterable[str] = (),
) -> dict[str, Any]:
    result = runner.run(["python3", str(provider), operation, *arguments], check=False)
    value = json_result(result, operation)
    if result.returncode != 0:
        classification = value.get("result")
        if classification not in set(allowed_results):
            details = []
            error = value.get("error")
            if isinstance(error, dict) and isinstance(error.get("detail"), str):
                details.append(error["detail"])
            conflicts = value.get("conflicts")
            if (isinstance(conflicts, list) and conflicts
                    and all(isinstance(item, str) for item in conflicts)):
                details.append("conflicts: " + ", ".join(conflicts))
            remediation = value.get("remediation")
            if (isinstance(remediation, list) and remediation
                    and all(isinstance(item, str) for item in remediation)):
                details.append("remediation: " + ", ".join(remediation))
            detail = f" ({'; '.join(details)})" if details else ""
            raise RuntimeOperationError(
                operation, value, result,
                f"RP1 runtime {operation} failed with "
                f"{classification or 'unknown state'}{detail}",
            )
    return value


def validate_readiness(value: Mapping[str, Any], expected_result: str | None = None) -> dict[str, Any]:
    require(value.get("contract") == RUNTIME_READINESS_CONTRACT, "runtime readiness contract differs")
    if expected_result is not None:
        require(value.get("result") == expected_result and value.get("state") == expected_result, f"runtime readiness did not reach {expected_result}")
    return dict(value)


def replace_owned_record(
    path: pathlib.Path,
    expected_value: Mapping[str, Any],
    expected_identity: os.stat_result,
    replacement: Mapping[str, Any],
) -> None:
    current, identity, reason = load_ownership_record(path)
    require(
        current == expected_value and identity is not None
        and (identity.st_dev, identity.st_ino) == (expected_identity.st_dev, expected_identity.st_ino),
        f"installation ownership changed before runtime evidence publication: {reason or 'identity mismatch'}",
    )
    atomic_json(path, replacement)


def require_unchanged_ownership(
    path: pathlib.Path,
    expected_value: Mapping[str, Any],
    expected_identity: os.stat_result,
    operation: str,
) -> None:
    current, identity, reason = load_ownership_record(path)
    require(
        current == expected_value and identity is not None
        and (identity.st_dev, identity.st_ino)
            == (expected_identity.st_dev, expected_identity.st_ino),
        f"installation ownership changed before {operation}: {reason or 'identity mismatch'}",
    )


def remove_owned_runtime_for_fresh_activation(
    record_path: pathlib.Path, record: Mapping[str, Any],
    record_identity: os.stat_result, runner: Runner,
    resolved: Mapping[str, Any] | None = None,
) -> None:
    if record.get("channel") == "development":
        validate_development_rollback_authority(record, pathlib.Path("/"))
    candidate = None
    if isinstance(resolved, Mapping) and isinstance(resolved.get("checkout"), dict):
        source = revalidate_checkout(resolved["checkout"], runner)
        candidate = source / "scripts/runtime_provider.py"
    recover_and_remove_owned_runtime(record_path, record, record_identity, runner, candidate)
    replacement = copy.deepcopy(record)
    replacement["schema"] = RECORD_SCHEMA
    replacement.pop("runtime", None)
    replace_owned_record(record_path, record, record_identity, replacement)


def prepare_runtime_update(args: argparse.Namespace, runner: Runner) -> None:
    """Delegate runtime preparation; retain only application-owned provenance."""
    resolved = None
    if args.command == "prepare-runtime-update":
        plan = load_plan(secure_state_dir(args.state_dir))
        require(not plan.get("dryRun"), "dry-run plan cannot prepare a runtime update")
        if not plan["decision"]["install"]:
            print("RP1-GPCLK-DKMS runtime update preparation skipped by resolved plan.")
            return
        resolved = plan.get("resolved")
        require(isinstance(resolved, dict), "prepared runtime source identity is missing")
    record, identity, reason = load_ownership_record(args.record)
    if record is None or identity is None:
        print(f"No owned RP1 runtime requires preparation: {reason or 'ownership is unproven'}.")
        return
    if record.get("schema") not in {RECORD_SCHEMA, RUNTIME_RECORD_SCHEMA}:
        print("No owned runtime administration requires preparation.")
        return
    # v2 includes the interrupted window before the application recorded a
    # neutral-activation receipt. The provider decides whether residue exists.
    remove_owned_runtime_for_fresh_activation(args.record, record, identity, runner, resolved)
    print("RP1-GPCLK-DKMS runtime preparation verified through the provider update contract.")



def activate_runtime(args: argparse.Namespace, runner: Runner) -> None:
    require(not removal_pending_path(args.record).exists() and not removal_pending_path(args.record).is_symlink(),
            "pending owned-provider removal blocks runtime deployment")
    state_dir = secure_state_dir(args.state_dir)
    plan = load_plan(state_dir)
    require(not plan.get("dryRun"), "dry-run plan cannot activate runtime administration")
    if not plan["decision"]["install"]:
        print("RP1-GPCLK-DKMS runtime administration skipped by resolved plan.")
        return
    resolved = plan.get("resolved")
    require(isinstance(resolved, dict), "prepared runtime source identity is missing")
    record, record_identity, reason = load_ownership_record(args.record)
    require(record is not None and record_identity is not None, f"WsprryPi provider ownership is required before runtime deployment: {reason or 'unknown record'}")
    require(record["schema"] in {RECORD_SCHEMA, RUNTIME_RECORD_SCHEMA}, "runtime deployment ownership schema is unsupported")
    require(record["sourceCommit"] == resolved.get("commit"), "owned provider and runtime source commits differ")
    if record["schema"] == RUNTIME_RECORD_SCHEMA:
        print("Existing WsprryPi-owned runtime record detected; revalidating before neutral activation.")
    bundle, reviewed = build_runtime_bundle(state_dir, resolved, runner)
    provider = bundle / "runtime_provider.py"
    initial = runtime_call(runner, provider, "inspect", ["--bundle", str(bundle)],
                           {"absent", "deployment_required", "activation_required", "neutral_ready"})
    validate_readiness(initial)
    require(initial.get("result") not in {"conflict", "recovery_required", "exact_ready"}, "runtime inspection is not eligible for neutral installation")
    deployment = runtime_call(runner, provider, "plan", ["--bundle", str(bundle)],
                              {"absent", "deployment_required", "activation_required", "neutral_ready"})
    validate_readiness(deployment)
    deployment_plan = deployment.get("deployment", {}).get("plan", {})
    deployment_digest = deployment_plan.get("planSha256")
    require(isinstance(deployment_digest, str) and SHA256.fullmatch(deployment_digest), "runtime deployment plan lacks a reviewed digest")
    destinations = deployment_plan.get("destinations")
    runtime_state = "/var/lib/rp1-gpclk-dkms/runtime-admin"
    expected_destinations = set(reviewed["binding"]["files"]) | {
        "/etc/rp1-gpclk-dkms/runtime-controller.json",
        *(f"{runtime_state}/{name}" for name in
          ("transaction.json", "manager.json", "application.json", "activation.json")),
    }
    require(isinstance(destinations, dict) and set(destinations) == expected_destinations, "runtime deployment plan destination inventory differs")
    runtime_call(runner, provider, "ensure",
                 ["--bundle", str(bundle), "--plan-sha256", deployment_digest])
    installed = runtime_call(runner, RUNTIME_PROVIDER, "inspect", (), {"activation_required", "neutral_ready"})
    validate_readiness(installed)
    require(installed.get("result") in {"activation_required", "neutral_ready"}, "runtime deployment did not reach neutral activation admission")
    activation = runtime_call(runner, RUNTIME_PROVIDER, "activation-plan", (), {"activation_required", "neutral_ready"})
    validate_readiness(activation)
    activation_plan = activation.get("activationPlan", {})
    activation_digest = activation_plan.get("planSha256")
    require(isinstance(activation_digest, str) and SHA256.fullmatch(activation_digest), "neutral activation plan lacks a reviewed digest")
    require(activation_plan.get("bindingSha256") == reviewed["bindingSha256"], "neutral activation plan binding differs from reviewed bundle")
    require(activation_plan.get("artifactSetSha256") == reviewed["artifactSetSha256"], "neutral activation plan artifact set differs from reviewed bundle")
    activation_result = runtime_call(
        runner, RUNTIME_PROVIDER, "activation-ensure", ["--plan-sha256", activation_digest]
    )
    require(
        activation_result.get("contract") == RUNTIME_READINESS_CONTRACT
        and activation_result.get("operation") == "activation-ensure"
        and activation_result.get("planSha256") == activation_digest,
        "neutral activation response identity differs from the reviewed plan",
    )
    runtime = validate_resumable_neutral_activation(record, {}, runner)
    require(runtime["bindingSha256"] == reviewed["bindingSha256"]
            and runtime["artifactSetSha256"] == reviewed["artifactSetSha256"]
            and runtime["deploymentPlanSha256"] == deployment_digest
            and runtime["activationPlanSha256"] == activation_digest,
            "runtime installation receipt differs from the reviewed plans")
    replacement = copy.deepcopy(record)
    replacement["schema"] = RUNTIME_RECORD_SCHEMA
    replacement["runtime"] = runtime
    validate_runtime_ownership(runtime, replacement)
    replace_owned_record(args.record, record, record_identity, replacement)
    print("RP1-GPCLK-DKMS neutral runtime administration verified; GPIO route and transmission remain unselected and disabled.")


def apply(args: argparse.Namespace, runner: Runner) -> None:
    state_dir = secure_state_dir(args.state_dir)
    plan = load_plan(state_dir)
    require(not plan.get("dryRun"), "dry-run plan cannot be applied")
    if not plan["decision"]["install"]:
        print("RP1-GPCLK-DKMS installation skipped by resolved plan.")
        return
    resolved = plan.get("resolved")
    require(isinstance(resolved, dict), "prepared installation identity is missing")
    record = args.record
    require(record.is_absolute(), "installation record path must be absolute")
    secure_record_parent(record)
    root = args.root.resolve(strict=True)
    if resolved["channel"] == "release":
        apply_release(state_dir, resolved, record, root, runner)
    elif resolved["channel"] == "development":
        apply_development(resolved, record, runner)
    else:
        raise ContractError("prepared source channel is invalid")
    print("RP1-GPCLK-DKMS package/source and DKMS installation verified; no route or transmission was activated.")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    sub = result.add_subparsers(dest="command", required=True)
    prepare_parser = sub.add_parser("prepare")
    prepare_parser.add_argument("--state-dir", type=pathlib.Path, required=True)
    prepare_parser.add_argument("--install", choices=("auto", "true", "false"), default="auto")
    prepare_parser.add_argument("--source", default="auto")
    prepare_parser.add_argument("--wsprry-source", required=True)
    prepare_parser.add_argument("--record", type=pathlib.Path, default=pathlib.Path("/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"))
    prepare_parser.add_argument("--model-file", type=pathlib.Path, default=pathlib.Path("/proc/device-tree/model"))
    prepare_parser.add_argument("--compatible-file", type=pathlib.Path, default=pathlib.Path("/proc/device-tree/compatible"))
    prepare_parser.add_argument("--dry-run", action="store_true")
    prepare_parser.add_argument("--debug", action="store_true")
    apply_parser = sub.add_parser("apply")
    apply_parser.add_argument("--state-dir", type=pathlib.Path, required=True)
    apply_parser.add_argument("--record", type=pathlib.Path, default=pathlib.Path("/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"))
    apply_parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("/"))
    apply_parser.add_argument("--debug", action="store_true")
    update_parser = sub.add_parser("prepare-runtime-update")
    update_parser.add_argument("--state-dir", type=pathlib.Path, required=True)
    update_parser.add_argument("--record", type=pathlib.Path, default=pathlib.Path("/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"))
    update_parser.add_argument("--debug", action="store_true")
    removal_update_parser = sub.add_parser("prepare-runtime-removal")
    removal_update_parser.add_argument("--record", type=pathlib.Path, default=pathlib.Path("/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"))
    removal_update_parser.add_argument("--debug", action="store_true")
    runtime_parser = sub.add_parser("activate-runtime")
    runtime_parser.add_argument("--state-dir", type=pathlib.Path, required=True)
    runtime_parser.add_argument("--record", type=pathlib.Path, default=pathlib.Path("/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"))
    runtime_parser.add_argument("--debug", action="store_true")
    remove_parser = sub.add_parser("remove")
    remove_parser.add_argument("--remove", choices=("auto", "true", "false"), default="auto")
    remove_parser.add_argument("--record", type=pathlib.Path, default=pathlib.Path("/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"))
    remove_parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("/"))
    remove_parser.add_argument("--debug", action="store_true")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "prepare":
            prepare(args, Runner(args.debug))
        elif args.command == "apply":
            apply(args, Runner(args.debug))
        elif args.command in {"prepare-runtime-update", "prepare-runtime-removal"}:
            prepare_runtime_update(args, Runner(args.debug))
        elif args.command == "activate-runtime":
            activate_runtime(args, Runner(args.debug))
        else:
            remove_owned_provider(args, Runner(args.debug))
    except ContractError as error:
        print(f"RP1-GPCLK-DKMS operation refused: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
