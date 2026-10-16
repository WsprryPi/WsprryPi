#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Non-installing acceptance of the locked published RP1 1.0.0 release.

This maintainer entry point validates assets without installing them.
Publication verification is an explicit, read-only network option.
The provider's independent validator and the production consumer parsers both
inspect a fresh reproducible build. All generated files stay outside Git.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = ROOT / "scripts/tests/fixtures/rp1-release-1.0.0.json"
spec = importlib.util.spec_from_file_location("rp1_installer", ROOT / "scripts/rp1_gpclk_dkms_install.py")
installer = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = installer
spec.loader.exec_module(installer)
require = installer.require


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON field: " + key)
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs)


def snapshot(candidate: Path, lock: dict) -> dict[str, bytes]:
    require(candidate.is_absolute() and candidate.resolve(strict=True) == candidate,
            "release asset path must be absolute and nonsymlink")
    descriptor = os.open(candidate, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        require(set(os.listdir(descriptor)) == set(lock["assets"]), "missing or unexpected release assets")
        result = {}
        for name, expected in lock["assets"].items():
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=descriptor)
            try:
                info = os.fstat(fd)
                require(stat.S_ISREG(info.st_mode) and info.st_size <= 64 * 1024 * 1024,
                        "unsafe release asset: " + name)
                with os.fdopen(fd, "rb", closefd=False) as stream:
                    data = stream.read(64 * 1024 * 1024 + 1)
                require(installer.sha256_bytes(data) == expected, "release asset checksum differs: " + name)
                result[name] = data
            finally:
                os.close(fd)
        return result
    finally:
        os.close(descriptor)


def validate_assets(candidate: Path, source: Path, lock: dict) -> dict:
    runner = installer.Runner()
    checkout = installer.validate_checkout(str(source), runner)
    require(checkout["commit"] == lock["sourceCommit"], "wrong release commit")
    require(checkout["tree"] == lock["sourceTree"], "wrong release source tree")
    require(runner.run(["git", "-C", str(source), "symbolic-ref", "-q", "HEAD"], check=False).returncode == 1,
            "release checkout must be detached")
    return validate_snapshot(snapshot(candidate, lock), candidate, lock, runner)


def validate_snapshot(assets: dict[str, bytes], candidate: Path, lock: dict,
                      runner: installer.Runner) -> dict:
    require(set(assets) == set(lock["assets"]), "missing or unexpected release assets")
    for name, expected in lock["assets"].items():
        require(installer.sha256_bytes(assets[name]) == expected, "release asset checksum differs: " + name)
    sums = installer.parse_checksums(assets[installer.CHECKSUMS_NAME])
    require(sums == {name: digest for name, digest in lock["assets"].items()
                     if name != installer.CHECKSUMS_NAME}, "release checksum inventory differs")
    metadata = strict_json(assets["release-metadata.json"])
    for name in ("sourceCommit", "sourceTree", "productVersion", "debianVersion", "classification", "published", "releaseTag", "releaseUrl"):
        require(metadata.get(name) == lock[name], "release metadata differs: " + name)
    require(metadata.get("published") is True and metadata.get("classification") == "release",
            "release profile must match the published lock")
    require(metadata.get("qualification") == {name: False for name in
        ("hardware", "lifecycle", "product", "rf", "wsprrypiAcceptance")}, "release claims qualification")
    for name, expected in lock["runtime"].items():
        require(metadata["runtime"][name] == expected, "release runtime interface differs: " + name)
    for route, identity in lock["compatibilityIdentities"].items():
        require(metadata["routes"][route]["compatibilityId"] == identity
                and metadata["routes"][route]["dtboSha256"] == lock["overlaySha256"][route],
                "release route identity differs: " + route)
    for name, file in (("consumer", "rp1_gpclk.h"), ("controller", "rp1_route_admin.h")):
        require(metadata["uapi"][file]["sha256"] == lock["uapiSha256"][name],
                "release UAPI differs: " + name)
    manifest = installer.validate_manifest(strict_json(assets[installer.MANIFEST_NAME]),
        tag="v" + lock["productVersion"], tag_commit=lock["sourceCommit"])
    require(manifest["debianVersion"] == lock["debianVersion"]
            and manifest["uapi"]["sha256"] == lock["uapiSha256"]["consumer"], "release manifest identity differs")
    # Work from captured bytes, never mutable user input during package parsing.
    with tempfile.TemporaryDirectory(prefix="rp1-consumer-package-") as raw:
        package = Path(raw) / manifest["package"]["filename"]
        package.write_bytes(assets[package.name])
        installer.validate_package(package, manifest, runner)
    return {"schema": "wsprrypi-rp1-release-acceptance-v1", "status": "PASS",
            "providerSelection": "release", "published": False, "publicationVerified": False,
            "releaseProfileVerified": True, "qualification": False,
            "sourceCommit": lock["sourceCommit"], "sourceTree": lock["sourceTree"],
            "productVersion": lock["productVersion"], "debianVersion": lock["debianVersion"],
            "assets": lock["assets"], "compatibilityIdentities": lock["compatibilityIdentities"],
            "uapiSha256": lock["uapiSha256"], "runtime": lock["runtime"],
            "productionSelectable": False, "installationPerformed": False}


def validate_publication(client, lock: dict) -> dict:
    """Use production selection rules plus the exact reviewed release lock."""
    tag = lock["releaseTag"]
    release = client.json(installer.API_BASE + "/releases/tags/" + tag)
    require(installer.select_release([release])["tag_name"] == tag,
            "published release tag differs from lock")
    require(release.get("html_url") == lock["releaseUrl"], "release URL differs from lock")
    require(client.tag_commit(tag) == lock["sourceCommit"], "published tag commit differs from lock")
    assets = installer.release_assets(release)
    require(set(assets) == set(lock["assets"]), "published asset set differs from lock")
    for name, url in assets.items():
        installer.authoritative_asset_url(tag, name, url)
    for item in release["assets"]:
        require(item.get("digest") == "sha256:" + lock["assets"][item["name"]],
                "published asset digest differs: " + item["name"])
    return {"published": True, "publicationVerified": True, "productionSelectable": True,
            "releaseTag": tag, "releaseUrl": lock["releaseUrl"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new external directory")
    parser.add_argument("--compare-retained", type=Path)
    parser.add_argument("--verify-publication", action="store_true",
                        help="read-only GitHub verification of the immutable tag and asset digests")
    args = parser.parse_args()
    lock = strict_json(LOCK_PATH.read_bytes())
    source, output = args.provider_source, args.output
    runner = installer.Runner()
    identity = installer.validate_checkout(str(source), runner)
    require(identity["commit"] == lock["sourceCommit"] and identity["tree"] == lock["sourceTree"],
            "provider source is not the locked release")
    require(runner.run(["git", "-C", str(source), "symbolic-ref", "-q", "HEAD"], check=False).returncode == 1,
            "release source must be detached")
    require(output.is_absolute() and output == output.resolve() and not output.exists(), "new canonical output path required")
    require(runner.run(["git", "-C", str(output.parent), "rev-parse", "--show-toplevel"], check=False).returncode != 0,
            "acceptance output must be outside Git")
    output.mkdir(mode=0o700)
    candidate = output / "release"
    with (output / "build.log").open("wb") as log:
        subprocess.run([str(source / "scripts/build-release-candidate"), "--version", lock["productVersion"],
            "--source-commit", lock["sourceCommit"], "--output", str(candidate), "--release"],
            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
    validated = subprocess.run([str(source / "scripts/validate-release-candidate"), "--candidate", str(candidate),
        "--source-commit", lock["sourceCommit"], "--release"], text=True, capture_output=True, check=True, timeout=600)
    validation = strict_json(validated.stdout)
    require(validation == {"status": "PASS", "classification": "release", "independentlyRebuilt": True,
        "published": False, "sourceCommit": lock["sourceCommit"], "sourceTree": lock["sourceTree"]},
        "independent provider validation result differs")
    (output / "provider-validation.json").write_text(validated.stdout)
    result = validate_assets(candidate, source, lock)
    installer.require_compatible_runtime_source(source, lock["sourceCommit"], runner)
    installer.runtime_source_interface(source, runner)
    installer.runtime_source_identity(source, lock["productVersion"])
    # Exercise real exact-source development selection as a separate policy.
    # Its identity/help probes are read-only; no install/apply entry point runs.
    with tempfile.TemporaryDirectory(prefix="development-selection-", dir=output) as raw:
        development = installer.prepare_development(Path(raw), "checkout", str(source), runner)
        require(development["commit"] == lock["sourceCommit"] and development["sourceTree"] == lock["sourceTree"]
                and development["version"] == lock["productVersion"]
                and development["compatibilityIdentity"] == installer.development_compatibility(lock["productVersion"]),
                "exact-source development selection differs from release")
    result["sourceDevelopmentSelectionValidated"] = True
    if args.compare_retained:
        require(snapshot(args.compare_retained, lock) == snapshot(candidate, lock), "retained release differs from fresh build")
    if args.verify_publication:
        result.update(validate_publication(installer.GitHubClient(), lock))
    result["independentlyRebuilt"] = True
    result["wsprrypiCommit"] = installer.git_output(runner, ROOT, "rev-parse", "HEAD")
    result["wsprrypiWorkingTreeClean"] = not bool(installer.git_output(
        runner, ROOT, "status", "--porcelain=v1", "--untracked-files=all"))
    result["acceptanceToolSha256"] = installer.sha256_file(Path(__file__))
    result["releaseLockSha256"] = installer.sha256_file(LOCK_PATH)
    result["installerSha256"] = installer.sha256_file(ROOT / "scripts/rp1_gpclk_dkms_install.py")
    (output / "acceptance.json").write_bytes(installer.canonical(result) + b"\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, installer.ContractError, subprocess.SubprocessError) as error:
        print(json.dumps({"status": "FAIL", "error": str(error)}), file=sys.stderr)
        raise SystemExit(1)
