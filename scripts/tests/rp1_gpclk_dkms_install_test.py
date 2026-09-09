#!/usr/bin/env python3
"""Offline, unprivileged contract tests for RP1 provider installation."""

from __future__ import annotations

import copy
import base64
import hashlib
import importlib.util
import io
import json
import lzma
import os
import pathlib
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "rp1_gpclk_dkms_install.py"
SPEC = importlib.util.spec_from_file_location("rp1_installer", SCRIPT)
assert SPEC and SPEC.loader
MOD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inventory_for_uapi(content: bytes = b"uapi\n") -> list[dict[str, object]]:
    return [{
        "path": "usr/src/rp1-gpclk-dkms-2.1.0/include/uapi/linux/rp1_gpclk.h",
        "type": "file", "mode": "0644", "sha256": digest(content),
    }]


def valid_manifest(version: str = "2.1.0", commit: str = "a" * 40) -> dict[str, object]:
    inventory = inventory_for_uapi()
    return {
        "schema": MOD.MANIFEST_SCHEMA,
        "schemaVersion": 1,
        "repository": MOD.REPOSITORY,
        "releaseTag": f"v{version}",
        "sourceCommit": commit,
        "productVersion": version,
        "debianVersion": f"{version}-1",
        "package": {
            "name": MOD.PACKAGE_NAME,
            "filename": f"rp1-gpclk-dkms_{version}-1_all.deb",
            "sha256": "b" * 64,
        },
        "dkmsModule": MOD.DKMS_NAME,
        "kernelModule": MOD.MODULE_NAME,
        "uapi": {
            "sha256": inventory[0]["sha256"],
            "path": inventory[0]["path"],
        },
        "administrationProtocol": MOD.ADMIN_PROTOCOL,
        "installationBehavior": {
            "routeNeutral": True,
            "outputDisabled": True,
            "loadsModule": False,
            "appliesOverlay": False,
            "editsBootConfiguration": False,
            "operatesServices": False,
        },
        "packageInventory": inventory,
        "packageInventorySha256": digest(MOD.canonical(inventory)),
        "releaseChannel": "release",
    }


def release(tag: str, *, draft: bool = False, prerelease: bool = False, complete: bool = True) -> dict[str, object]:
    names = []
    if complete:
        names = [MOD.MANIFEST_NAME, MOD.CHECKSUMS_NAME, f"rp1-gpclk-dkms_{tag.removeprefix('v')}-1_all.deb"]
    return {
        "tag_name": tag, "draft": draft, "prerelease": prerelease, "immutable": True,
        "published_at": "2026-08-31T00:00:00Z",
        "assets": [{"name": name, "state": "uploaded", "browser_download_url": f"https://github.com/{MOD.REPOSITORY}/releases/download/{tag}/{name}"} for name in names],
    }


class FakeRunner(MOD.Runner):
    def __init__(self, responses: dict[tuple[str, ...], MOD.CommandResult] | None = None):
        super().__init__()
        self.responses = responses or {}
        self.calls: list[tuple[str, ...]] = []
        self.passthrough_calls: list[tuple[str, ...]] = []

    def run(self, argv, *, check=True, cwd=None, passthrough=False):
        key = tuple(str(item) for item in argv)
        self.calls.append(key)
        if passthrough:
            self.passthrough_calls.append(key)
        result = self.responses.get(key, MOD.CommandResult("", "", 0))
        if check and result.returncode:
            raise MOD.ContractError(result.stderr or "fake command failure")
        return result


class PlatformAndSelectionTests(unittest.TestCase):
    def detected(self, model: bytes, compatible: bytes):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            (root / "model").write_bytes(model)
            (root / "compatible").write_bytes(compatible)
            return MOD.detect_platform(root / "model", root / "compatible")

    def test_auto_pi5_and_cm5(self):
        for model, compatible in (
            (b"Raspberry Pi 5 Model B Rev 1.0\0", b"raspberrypi,5-model-b\0brcm,bcm2712\0"),
            (b"Raspberry Pi Compute Module 5 Rev 1.0\0", b"raspberrypi,5-compute-module\0brcm,bcm2712d0\0"),
        ):
            detected = self.detected(model, compatible)
            self.assertTrue(detected["automaticEligible"])
            self.assertTrue(MOD.installation_decision("auto", detected)["install"])

    def test_auto_skips_non_pi5_and_unknown(self):
        for model, compatible in (
            (b"Raspberry Pi 4 Model B\0", b"raspberrypi,4-model-b\0brcm,bcm2711\0"),
            (b"", b""),
            (b"Raspberry Pi 5 Model B Rev 1.0\0", b"raspberrypi,5-model-b\0"),
        ):
            decision = MOD.installation_decision("auto", self.detected(model, compatible))
            self.assertFalse(decision["install"])
            self.assertFalse(decision["platformOverride"])

    def test_explicit_true_and_false(self):
        unknown = {"automaticEligible": False}
        enabled = MOD.installation_decision("true", unknown)
        self.assertTrue(enabled["install"])
        self.assertTrue(enabled["platformOverride"])
        self.assertFalse(MOD.installation_decision("false", {"automaticEligible": True})["install"])
        with self.assertRaises(MOD.ContractError):
            MOD.installation_decision("yes", unknown)

    def test_channel_and_source_selection(self):
        for source in ("main", "master", "v3.2.0"):
            self.assertEqual(MOD.wsprry_channel(source), "production")
            self.assertEqual(MOD.source_selector("auto", MOD.wsprry_channel(source))[0], "release")
        for source in ("devel", "codex/feature", "issue-412"):
            self.assertEqual(MOD.wsprry_channel(source), "development")
            self.assertEqual(MOD.source_selector("auto", MOD.wsprry_channel(source))[0], "devel")
        for ambiguous in ("", "HEAD", "detached", "unknown"):
            with self.assertRaises(MOD.ContractError):
                MOD.wsprry_channel(ambiguous)
        self.assertEqual(MOD.source_selector("release", "development"), ("release", None))
        self.assertEqual(MOD.source_selector("commit:" + "c" * 40, "development"), ("commit", "c" * 40))
        with self.assertRaises(MOD.ContractError):
            MOD.source_selector("commit:abc123", "development")
        with self.assertRaises(MOD.ContractError):
            MOD.source_selector("checkout:relative", "development")
        with self.assertRaises(MOD.ContractError):
            MOD.source_selector("commit:" + "a" * 40 + ";modprobe", "development")

    def test_runtime_source_requires_corrected_commit_ancestry(self):
        source = pathlib.Path("/fixture/source")
        selected = "a" * 40
        ancestor = (
            "git", "-C", str(source), "merge-base", "--is-ancestor",
            MOD.CORRECTED_RUNTIME_COMMIT, selected,
        )
        missing = FakeRunner({
            ("git", "-C", str(source), "cat-file", "-e",
             f"{MOD.CORRECTED_RUNTIME_COMMIT}^{{commit}}"): MOD.CommandResult("", "missing", 1),
        })
        with self.assertRaisesRegex(MOD.ContractError, "cannot prove the corrected"):
            MOD.require_compatible_runtime_source(source, selected, missing)
        predates = FakeRunner({ancestor: MOD.CommandResult("", "", 1)})
        with self.assertRaisesRegex(MOD.ContractError, "predates the corrected"):
            MOD.require_compatible_runtime_source(source, selected, predates)
        compatible = FakeRunner()
        MOD.require_compatible_runtime_source(source, selected, compatible)
        self.assertIn(ancestor, compatible.calls)

    def test_debug_runner_reports_argv_and_captured_output(self):
        completed = subprocess.CompletedProcess(["git", "status"], 0, "clean\n", "notice\n")
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(MOD.subprocess, "run", return_value=completed), \
             mock.patch.object(MOD.sys, "stdout", stdout), \
             mock.patch.object(MOD.sys, "stderr", stderr):
            result = MOD.Runner(debug=True).run(["git", "status"])
        self.assertEqual(result.stdout, "clean\n")
        self.assertEqual(stdout.getvalue(), "clean\n")
        self.assertIn("[RP1 DEBUG] command: git status", stderr.getvalue())
        self.assertIn("notice", stderr.getvalue())

    def test_passthrough_failure_remains_a_contract_error(self):
        completed = subprocess.CompletedProcess(["apt-get", "install"], 1, None, None)
        with mock.patch.object(MOD.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(MOD.ContractError, "see command output above"):
                MOD.Runner().run(["apt-get", "install"], passthrough=True)


class ReleaseTests(unittest.TestCase):
    def test_release_resolution_binds_exact_runtime_source_checkout(self):
        package = b"fixture package"
        manifest = valid_manifest()
        manifest["package"]["sha256"] = digest(package)
        manifest_bytes = MOD.canonical(manifest) + b"\n"
        package_name = manifest["package"]["filename"]
        checksums = (
            f"{digest(manifest_bytes)}  {MOD.MANIFEST_NAME}\n"
            f"{digest(package)}  {package_name}\n"
        ).encode()
        selected = release("v2.1.0")
        assets = {item["name"]: item["browser_download_url"] for item in selected["assets"]}

        class Client:
            def releases(self): return [selected]
            def tag_commit(self, _tag): return "a" * 40
            def bytes(self, url, **_kwargs):
                return {
                    assets[MOD.MANIFEST_NAME]: manifest_bytes,
                    assets[MOD.CHECKSUMS_NAME]: checksums,
                    assets[package_name]: package,
                }[url]

        with tempfile.TemporaryDirectory() as raw:
            state = pathlib.Path(raw).resolve()
            checkout = {"path": str(state / "source"), "commit": "a" * 40}
            with mock.patch.object(MOD, "validate_package"), \
                 mock.patch.object(MOD, "clone_exact", return_value=checkout) as clone, \
                 mock.patch.object(MOD, "runtime_source_interface") as interface, \
                 mock.patch.object(MOD, "development_identity", return_value={"version": "2.1.0"}), \
                 mock.patch.object(MOD, "runtime_source_identity") as identity:
                resolved = MOD.download_release(state, Client(), FakeRunner())
            clone.assert_called_once_with(state, "commit", "a" * 40, mock.ANY)
            interface.assert_called_once_with(state / "source", mock.ANY)
            identity.assert_called_once_with(state / "source", "2.1.0")
            self.assertEqual(resolved["checkout"], checkout)
            self.assertEqual(resolved["runtimeVersion"], "2.1.0")

    def test_semantic_ordering_and_exclusions(self):
        selected = MOD.select_release([
            release("v1.9.9"), release("v1.10.0"), release("v9.0.0", draft=True),
            release("v8.0.0", prerelease=True), release("latest"), release("v7.0.0", complete=False),
        ])
        self.assertEqual(selected["tag_name"], "v1.10.0")

    def test_release_without_required_manifest_is_not_eligible(self):
        with self.assertRaisesRegex(MOD.ContractError, "no eligible"):
            MOD.select_release([release("v2.0.0", complete=False)])

    def test_selected_highest_corruption_does_not_fallback(self):
        selected = MOD.select_release([release("v2.0.0"), release("v1.9.0")])
        self.assertEqual(selected["tag_name"], "v2.0.0")
        broken = valid_manifest("2.0.0", "d" * 40)
        broken["repository"] = "attacker/repository"
        with self.assertRaisesRegex(MOD.ContractError, "repository substitution"):
            MOD.validate_manifest(broken, tag="v2.0.0", tag_commit="d" * 40)

    def test_manifest_contract(self):
        manifest = valid_manifest()
        validated = MOD.validate_manifest(manifest, tag="v2.1.0", tag_commit="a" * 40)
        self.assertEqual(validated["uapi"]["sha256"], inventory_for_uapi()[0]["sha256"])
        mutations = [
            ("schema", "bad"), ("repository", "other/repo"), ("sourceCommit", "c" * 40),
            ("releaseChannel", "development"), ("administrationProtocol", "shell-v0"),
            ("dkmsModule", "other"),
        ]
        for field, value in mutations:
            candidate = copy.deepcopy(manifest)
            candidate[field] = value
            with self.assertRaises(MOD.ContractError, msg=field):
                MOD.validate_manifest(candidate, tag="v2.1.0", tag_commit="a" * 40)

        candidate = copy.deepcopy(manifest)
        candidate["installationBehavior"]["loadsModule"] = True
        with self.assertRaisesRegex(MOD.ContractError, "inactive installation behavior"):
            MOD.validate_manifest(candidate, tag="v2.1.0", tag_commit="a" * 40)

    def test_missing_manifest_uapi_and_inventory_hash_refused(self):
        for mutation in ("missing", "unexpected", "uapi", "inventory"):
            candidate = copy.deepcopy(valid_manifest())
            if mutation == "missing":
                del candidate["package"]
            elif mutation == "unexpected":
                candidate["alternateRepository"] = "attacker/repository"
            elif mutation == "uapi":
                candidate["uapi"]["sha256"] = "e" * 64
            else:
                candidate["packageInventorySha256"] = "f" * 64
            with self.assertRaises(MOD.ContractError, msg=mutation):
                MOD.validate_manifest(candidate, tag="v2.1.0", tag_commit="a" * 40)

    def test_checksum_parser_and_authoritative_urls(self):
        parsed = MOD.parse_checksums(("a" * 64 + "  file.deb\n").encode())
        self.assertEqual(parsed["file.deb"], "a" * 64)
        for unsafe in (
            "A" * 64 + "  file.deb\n",
            "a" * 64 + " *file.deb\n",
            "a" * 64 + "  ../file.deb\n",
            "a" * 64 + "  file.deb\n" + "b" * 64 + "  file.deb\n",
        ):
            with self.assertRaises(MOD.ContractError):
                MOD.parse_checksums(unsafe.encode())
        good = f"https://github.com/{MOD.REPOSITORY}/releases/download/v2.1.0/file.deb"
        MOD.authoritative_asset_url("v2.1.0", "file.deb", good)
        for bad in (good.replace("github.com", "evil.example"), good + "?token=1", good.replace("v2.1.0", "v2.0.0")):
            with self.assertRaises(MOD.ContractError):
                MOD.authoritative_asset_url("v2.1.0", "file.deb", bad)

    def test_asset_download_does_not_forward_api_token(self):
        class Response:
            headers = {"Content-Length": "2"}
            def __enter__(self): return self
            def __exit__(self, *_): return False
            def geturl(self): return "https://release-assets.githubusercontent.com/asset"
            def read(self, _): return b"ok"

        observed = {}
        def fake_open(request, timeout):
            observed["authorization"] = request.get_header("Authorization")
            observed["timeout"] = timeout
            return Response()

        with mock.patch.object(MOD.urllib.request, "urlopen", side_effect=fake_open):
            value = MOD.GitHubClient("secret-token").bytes(
                "https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases/download/v2.1.0/file.deb",
                allow_asset_redirect=True,
            )
        self.assertEqual(value, b"ok")
        self.assertIsNone(observed["authorization"])


class ArchiveTests(unittest.TestCase):
    def make_tar(self, members):
        temporary = tempfile.NamedTemporaryFile(suffix=".tar", delete=False)
        temporary.close()
        path = pathlib.Path(temporary.name)
        with tarfile.open(path, "w") as archive:
            for name, kind, content in members:
                info = tarfile.TarInfo(name)
                info.mode = 0o644
                if kind == "file":
                    info.size = len(content)
                    archive.addfile(info, io.BytesIO(content))
                elif kind == "symlink":
                    info.type = tarfile.SYMTYPE
                    info.linkname = content.decode()
                    archive.addfile(info)
                elif kind == "hardlink":
                    info.type = tarfile.LNKTYPE
                    info.linkname = content.decode()
                    archive.addfile(info)
        self.addCleanup(path.unlink, missing_ok=True)
        return path

    def test_safe_inventory(self):
        path = self.make_tar([("./usr/include/uapi.h", "file", b"header")])
        inventory = MOD.tar_inventory(path)
        self.assertEqual(inventory[0]["path"], "usr/include/uapi.h")
        self.assertEqual(inventory[0]["sha256"], digest(b"header"))

    def test_member_size_and_inventory_count_are_bounded(self):
        path = self.make_tar([("usr/large", "file", b"xx")])
        with mock.patch.object(MOD, "MAX_PACKAGE_MEMBER_BYTES", 1):
            with self.assertRaisesRegex(MOD.ContractError, "bounded size"):
                MOD.tar_inventory(path)
        with mock.patch.object(MOD, "MAX_PACKAGE_MEMBERS", 0):
            with self.assertRaisesRegex(MOD.ContractError, "bounded member"):
                MOD.validate_inventory(inventory_for_uapi())

    def test_path_traversal_symlink_escape_and_special_refused(self):
        for members in (
            [("../escape", "file", b"x")],
            [("usr/link", "symlink", b"../../escape")],
            [("usr/hard", "hardlink", b"usr/target")],
        ):
            with self.assertRaises(MOD.ContractError):
                MOD.tar_inventory(self.make_tar(members))


class CheckoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = pathlib.Path(self.temp.name).resolve() / "repo"
        self.repo.mkdir()
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        subprocess.run(["git", "-C", str(self.repo), "config", "user.email", "test@example.invalid"], check=True)
        subprocess.run(["git", "-C", str(self.repo), "config", "user.name", "Test"], check=True)
        (self.repo / "tracked").write_text("clean\n")
        subprocess.run(["git", "-C", str(self.repo), "add", "tracked"], check=True)
        subprocess.run(["git", "-C", str(self.repo), "commit", "-q", "-m", "fixture"], check=True)
        subprocess.run(["git", "-C", str(self.repo), "remote", "add", "origin", MOD.REPOSITORY_URL], check=True)

    def test_clean_checkout_acceptance(self):
        identity = MOD.validate_checkout(str(self.repo), MOD.Runner())
        self.assertRegex(identity["commit"], r"^[0-9a-f]{40}$")
        self.assertEqual(MOD.revalidate_checkout(identity, MOD.Runner()), self.repo)

    def test_dirty_untracked_relative_symlink_and_unrelated_refused(self):
        (self.repo / "untracked").write_text("x")
        with self.assertRaisesRegex(MOD.ContractError, "tracked or untracked"):
            MOD.validate_checkout(str(self.repo), MOD.Runner())
        (self.repo / "untracked").unlink()
        with self.assertRaises(MOD.ContractError):
            MOD.validate_checkout("relative", MOD.Runner())
        link = pathlib.Path(self.temp.name) / "link"
        link.symlink_to(self.repo, target_is_directory=True)
        with self.assertRaisesRegex(MOD.ContractError, "symlink"):
            MOD.validate_checkout(str(link), MOD.Runner())
        subprocess.run(["git", "-C", str(self.repo), "remote", "set-url", "origin", "https://example.invalid/other.git"], check=True)
        with self.assertRaisesRegex(MOD.ContractError, "authoritative"):
            MOD.validate_checkout(str(self.repo), MOD.Runner())

    def test_checkout_toctou_change_refused(self):
        identity = MOD.validate_checkout(str(self.repo), MOD.Runner())
        (self.repo / "tracked").write_text("replaced\n")
        with self.assertRaisesRegex(MOD.ContractError, "dirty or replaced"):
            MOD.revalidate_checkout(identity, MOD.Runner())

    def test_devel_and_exact_commit_resolve_once_to_detached_sha(self):
        bare = pathlib.Path(self.temp.name).resolve() / "authoritative.git"
        subprocess.run(["git", "clone", "-q", "--bare", str(self.repo), str(bare)], check=True)
        subprocess.run(["git", "-C", str(self.repo), "push", "-q", str(bare), "HEAD:refs/heads/devel"], check=True)
        commit = subprocess.run(
            ["git", "-C", str(self.repo), "rev-parse", "HEAD"],
            check=True, text=True, stdout=subprocess.PIPE,
        ).stdout.strip()
        with mock.patch.object(MOD, "REPOSITORY_URL", str(bare)):
            devel_state = pathlib.Path(self.temp.name).resolve() / "devel-state"
            devel_state.mkdir()
            devel = MOD.clone_exact(devel_state, "devel", None, MOD.Runner())
            self.assertEqual(devel["commit"], commit)
            head_name = subprocess.run(
                ["git", "-C", devel["path"], "symbolic-ref", "-q", "HEAD"],
                text=True, stdout=subprocess.PIPE,
            )
            self.assertNotEqual(head_name.returncode, 0)
            commit_state = pathlib.Path(self.temp.name).resolve() / "commit-state"
            commit_state.mkdir()
            exact = MOD.clone_exact(commit_state, "commit", commit, MOD.Runner())
            self.assertEqual(exact["commit"], commit)
            missing_state = pathlib.Path(self.temp.name).resolve() / "missing-state"
            missing_state.mkdir()
            with self.assertRaisesRegex(MOD.ContractError, "does not exist"):
                MOD.clone_exact(missing_state, "commit", "f" * 40, MOD.Runner())


class DevelopmentInterfaceTests(unittest.TestCase):
    def lifecycle_source(self, help_text: str) -> pathlib.Path:
        root = pathlib.Path(self.temp.name)
        scripts = root / "scripts"
        scripts.mkdir(exist_ok=True)
        version_source = root / "include/rp1_gpclk/version.h"
        version_source.parent.mkdir(parents=True, exist_ok=True)
        version_source.write_text('#define RP1_GPCLK_MODULE_VERSION "0.9.0"\n')
        installer = scripts / "development-install"
        installer.write_text(f"#!/bin/sh\nprintf '%s\\n' '{help_text}'\n")
        preflight = scripts / "development-preflight"
        identity = {
            "classification": "source-development", "moduleName": MOD.MODULE_NAME,
            "moduleVersion": "0.9.0", "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": digest(version_source.read_bytes()),
        }
        preflight.write_text("#!/bin/sh\nprintf '%s\\n' '" + json.dumps(identity) + "'\n")
        installer.chmod(0o755)
        preflight.chmod(0o755)
        return root

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def test_current_route_required_upstream_is_explicit_blocker(self):
        source = self.lifecycle_source("usage: development-install --route {gpio4,gpio20} --runtime-controller")
        with self.assertRaisesRegex(MOD.ContractError, "no reviewed route-neutral"):
            MOD.route_neutral_interface(source, MOD.Runner())

    def test_future_reviewed_route_neutral_shapes(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        self.assertEqual(MOD.route_neutral_interface(source, MOD.Runner())[1], ["--route-neutral"])
        source = self.lifecycle_source("usage: development-install --route {gpio4,gpio20,route-neutral} --runtime-controller")
        self.assertEqual(MOD.route_neutral_interface(source, MOD.Runner())[1], ["--route", "route-neutral"])

    def test_development_identity_comes_from_upstream_preflight(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        identity = MOD.development_identity(source, MOD.Runner())
        self.assertEqual(identity["version"], "0.9.0")
        self.assertEqual(identity["versionSource"], "include/rp1_gpclk/version.h")
        (source / "include/rp1_gpclk/version.h").write_text('#define RP1_GPCLK_MODULE_VERSION "changed"\n')
        with self.assertRaisesRegex(MOD.ContractError, "differs from the selected source"):
            MOD.development_identity(source, MOD.Runner())

    def test_runtime_source_identity_binds_product_and_both_rp1_routes(self):
        source = pathlib.Path(self.temp.name) / "runtime-source"
        scripts = source / "scripts"
        scripts.mkdir(parents=True)
        (scripts / "runtime_layout.py").write_text(
            '"""Runtime destinations; kernel identity is binding-specific."""\n'
        )
        (scripts / "runtime_binding.py").write_text(
            "CONTRACT = 'rp1-gpclk-runtime-binding-v3'\n"
            "PRODUCT_VERSION = '0.9.0'\n"
            "COMPATIBILITY = {'gpio4': 'v0.9.0-rp1-gpio4', 'gpio20': 'v0.9.0-rp1-gpio20'}\n"
        )
        MOD.runtime_source_identity(source, "0.9.0")
        (scripts / "runtime_binding.py").write_text(
            "CONTRACT = 'rp1-gpclk-runtime-binding-v3'\n"
            "PRODUCT_VERSION = '0.9.0'\n"
            "COMPATIBILITY = {'gpio4': 'v0.9.0-pi5-gpio4', 'gpio20': 'v0.9.0-pi5-gpio20'}\n"
        )
        with self.assertRaisesRegex(MOD.ContractError, "route compatibility identities differ"):
            MOD.runtime_source_identity(source, "0.9.0")
        (scripts / "runtime_binding.py").write_text(
            "CONTRACT = 'rp1-gpclk-runtime-binding-v3'\n"
            "PRODUCT_VERSION = '0.9.0'\n"
            "COMPATIBILITY = {'gpio4': 'v0.9.0-rp1-gpio4', 'gpio20': 'v0.9.0-rp1-gpio20'}\n"
        )
        (scripts / "runtime_layout.py").write_text("KERNEL = 'fixture-kernel'\n")
        with self.assertRaisesRegex(MOD.ContractError, "hard-codes a kernel"):
            MOD.runtime_source_identity(source, "0.9.0")

    def test_development_result_is_bound_before_recording(self):
        manifest_path = pathlib.Path(self.temp.name) / "DEVELOPMENT_MANIFEST.json"
        resolved = {"commit": "a" * 40, "version": "0.9.0", "uapiSha256": "b" * 64,
                    "versionSource": "include/rp1_gpclk/version.h", "versionSourceSha256": "e" * 64}
        manifest = {
            "schema": "rp1-gpclk-source-development-manifest",
            "classification": "source-development", "qualification": False,
            "releaseQualified": False, "sourceCommit": "a" * 40,
            "sourceState": "clean", "renderedVersion": "0.9.0",
            "versionIdentity": {"path": "include/rp1_gpclk/version.h", "sha256": "e" * 64,
                                "moduleVersion": "0.9.0"},
            "dkmsName": MOD.DKMS_NAME, "moduleName": MOD.MODULE_NAME,
            "targetKernel": MOD.platform.release(),
            "buildProfile": "runtime-controller",
            "installationMode": "route-neutral", "route": None,
            "routeNeutralSafety": {
                "before": {"loadedModule": False, "configuredRoutes": []},
                "after": {"loadedModule": False, "configuredRoutes": []},
            },
            "uapiIdentity": {"sha256": "b" * 64},
            "parameters": {"output_inhibit": 1},
            "installedModule": {
                "moduleName": MOD.MODULE_NAME, "moduleVersion": "0.9.0",
                "kernel": MOD.platform.release(), "installedFileSha256": "c" * 64,
                "decompressedElfSha256": "d" * 64,
                "installedPath": f"/lib/modules/{MOD.platform.release()}/updates/dkms/{MOD.MODULE_NAME}.ko",
            },
        }
        manifest["installedModules"] = {
            MOD.MODULE_NAME: manifest["installedModule"],
            MOD.CONTROLLER_MODULE_NAME: {
                "moduleName": MOD.CONTROLLER_MODULE_NAME, "moduleVersion": "0.9.0",
                "kernel": MOD.platform.release(), "installedFileSha256": "f" * 64,
                "decompressedElfSha256": "1" * 64,
                "installedPath": f"/lib/modules/{MOD.platform.release()}/updates/dkms/{MOD.CONTROLLER_MODULE_NAME}.ko",
            },
        }
        manifest_path.write_text(json.dumps(manifest))
        self.assertEqual(MOD.verify_development_result(manifest_path, resolved)["sourceState"], "clean")
        for field, value in (("route", "gpio4"), ("sourceState", "dirty-explicitly-allowed")):
            candidate = copy.deepcopy(manifest)
            candidate[field] = value
            manifest_path.write_text(json.dumps(candidate))
            with self.assertRaises(MOD.ContractError):
                MOD.verify_development_result(manifest_path, resolved)

    def test_existing_foreign_state_blocks_development_before_mutation(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "version": "0.9.0",
        }
        existing = {
            "packageVersion": None, "dkms": "rp1-gpclk-dkms/foreign, kernel: installed",
            "activeModule": False, "configuredRoute": False, "sourceTrees": [],
            "moduleCandidates": [], "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
            "runtimeResidue": [],
        }
        record = pathlib.Path(self.temp.name) / "record.json"
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=existing):
            with self.assertRaisesRegex(MOD.ContractError, "not adoptable"):
                MOD.apply_development(resolved, record, MOD.Runner())
        self.assertFalse(record.exists())

    def test_exact_owned_development_state_is_an_idempotent_noop(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None,
            "dkms": f"{MOD.DKMS_NAME}/0.9.0, {MOD.platform.release()}, arm64: installed",
            "activeModule": False, "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.9.0"],
            "moduleCandidates": [
                f"/lib/modules/{MOD.platform.release()}/updates/dkms/{MOD.MODULE_NAME}.ko"
            ],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
            "runtimeResidue": ["/var/lib/rp1-gpclk-dkms/runtime-admin"],
        }
        owned = {
            "schema": MOD.RECORD_SCHEMA,
            "channel": "development", "sourceCommit": resolved["commit"],
            "productVersion": resolved["version"], "sourceTree": resolved["sourceTree"],
            "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult("usage: development-install --route-neutral --runtime-controller\n", "", 0),
        })
        stdout = io.StringIO()
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=existing), \
             mock.patch.object(MOD, "load_ownership_record", side_effect=[
                 (owned, identity, None), (owned, identity, None),
             ]), \
             mock.patch.object(MOD, "validate_development_rollback_authority") as validate, \
             mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.apply_development(resolved, record, runner)
        self.assertEqual(validate.call_args_list, [
            mock.call(owned, pathlib.Path("/")),
            mock.call(owned, pathlib.Path("/")),
        ])
        self.assertEqual(runner.passthrough_calls, [])
        self.assertIn("no provider mutation", stdout.getvalue())

    def test_exact_owned_neutral_runtime_with_loaded_controller_is_idempotent(self):
        source = self.lifecycle_source(
            "usage: development-install --route-neutral --runtime-controller"
        )
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None,
            "dkms": (
                f"{MOD.DKMS_NAME}/0.9.0, {MOD.platform.release()}, "
                "arm64: installed"
            ),
            "activeModule": False, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.9.0"],
            "moduleCandidates": [
                f"/lib/modules/{MOD.platform.release()}/updates/dkms/"
                f"{MOD.MODULE_NAME}.ko"
            ],
            "controllerCandidates": [
                f"/lib/modules/{MOD.platform.release()}/updates/dkms/"
                f"{MOD.CONTROLLER_MODULE_NAME}.ko"
            ],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
            "runtimeResidue": ["/var/lib/rp1-gpclk-dkms/runtime-admin"],
        }
        owned = {
            "schema": MOD.RUNTIME_RECORD_SCHEMA, "channel": "development",
            "sourceCommit": resolved["commit"],
            "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"],
            "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult(
                    "usage: development-install --route-neutral "
                    "--runtime-controller\n", "", 0,
                ),
            ("modinfo", "-k", MOD.platform.release(), "-F", "version",
             MOD.MODULE_NAME): MOD.CommandResult("0.9.0\n", "", 0),
        })
        stdout = io.StringIO()
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=existing), \
             mock.patch.object(MOD, "load_ownership_record", side_effect=[
                 (owned, identity, None), (owned, identity, None),
             ]), \
             mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.apply_development(
                resolved, pathlib.Path(self.temp.name) / "record.json", runner
            )
        self.assertEqual(runner.passthrough_calls, [])
        self.assertIn("no provider mutation", stdout.getvalue())

    def test_provider_owned_neutral_checkpoint_resumes_without_reinstall(self):
        source = self.lifecycle_source(
            "usage: development-install --route-neutral --runtime-controller"
        )
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None,
            "dkms": (f"{MOD.DKMS_NAME}/0.9.0, {MOD.platform.release()}, "
                     "arm64: installed"),
            "activeModule": False, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.9.0"],
            "moduleCandidates": ["consumer.ko"],
            "controllerCandidates": ["controller.ko"],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
            "runtimeResidue": ["/var/lib/rp1-gpclk-dkms/runtime-admin"],
        }
        owned = {
            "schema": MOD.RECORD_SCHEMA, "channel": "development",
            "sourceCommit": resolved["commit"], "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"], "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult(
                    "usage: development-install --route-neutral "
                    "--runtime-controller\n", "", 0,
                ),
        })
        runtime = {
            "readinessContract": MOD.RUNTIME_READINESS_CONTRACT,
            "bindingSha256": "1" * 64, "artifactSetSha256": "2" * 64,
            "sourceCommit": owned["sourceCommit"],
            "productVersion": owned["productVersion"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentities": {
                "gpio4": "v0.9.0-rp1-gpio4",
                "gpio20": "v0.9.0-rp1-gpio20",
            },
            "deploymentPlanSha256": "3" * 64,
            "activationPlanSha256": "4" * 64,
            "activationRequestId": "request-1234",
            "controllerSession": 42, "controllerGeneration": 0,
            "state": "neutral_ready", "route": None, "output": "disabled",
        }
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=existing), \
             mock.patch.object(MOD, "load_ownership_record", side_effect=[
                 (owned, identity, None), (owned, identity, None),
             ]), \
             mock.patch.object(MOD, "validate_resumable_neutral_activation",
                               return_value=runtime) as validate, \
             mock.patch.object(MOD, "replace_owned_record") as replace:
            MOD.apply_development(
                resolved, pathlib.Path(self.temp.name) / "record.json", runner
            )
        self.assertEqual(validate.call_count, 2)
        self.assertEqual(replace.call_count, 1)
        self.assertEqual(replace.call_args.args[3]["schema"], MOD.RUNTIME_RECORD_SCHEMA)
        self.assertEqual(replace.call_args.args[3]["runtime"], runtime)
        self.assertEqual(runner.passthrough_calls, [])

    def test_neutral_runtime_reuse_still_rejects_transmission_state(self):
        inventory = {
            "packageVersion": None,
            "dkms": (
                f"{MOD.DKMS_NAME}/0.9.0, {MOD.platform.release()}, "
                "arm64: installed"
            ),
            "activeModule": False, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.9.0"],
            "moduleCandidates": ["consumer.ko"],
            "controllerCandidates": ["controller.ko"],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
        }
        runner = FakeRunner({
            ("modinfo", "-k", MOD.platform.release(), "-F", "version",
             MOD.MODULE_NAME): MOD.CommandResult("0.9.0\n", "", 0),
        })
        for field, message in (
            ("activeModule", "transmission consumer"),
            ("configuredRoute", "configured RP1 GPCLK route"),
            ("enrollment", "legacy development administration"),
        ):
            unsafe = dict(inventory)
            unsafe[field] = True
            with self.subTest(field=field), self.assertRaisesRegex(
                    MOD.ContractError, message):
                MOD.validate_runtime_development_provider(
                    {"productVersion": "0.9.0"}, unsafe, runner
                )

    def test_owned_development_identity_drift_is_not_reused(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None, "dkms": "present", "activeModule": False,
            "configuredRoute": False, "sourceTrees": [], "moduleCandidates": [],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False, "runtimeResidue": [],
        }
        owned = {
            "channel": "development", "sourceCommit": "e" * 40,
            "productVersion": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult("usage: development-install --route-neutral --runtime-controller\n", "", 0),
        })
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=existing), \
             mock.patch.object(MOD, "load_ownership_record", return_value=(owned, mock.Mock(st_dev=1, st_ino=2), None)), \
             mock.patch.object(MOD, "validate_development_removal") as validate:
            with self.assertRaisesRegex(MOD.ContractError, "identity differs"):
                MOD.apply_development(resolved, pathlib.Path(self.temp.name) / "record.json", runner)
        validate.assert_not_called()

    def test_changed_source_migrates_owned_active_runtime_before_provider_inventory(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        before = {
            "packageVersion": None,
            "dkms": f"{MOD.DKMS_NAME}/0.9.0, kernel, arm64: installed",
            "activeModule": True, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.9.0"],
            "moduleCandidates": [
                "/lib/modules/kernel/updates/dkms/rp1_gpclk_dkms.ko"
            ],
            "controllerCandidates": [
                "/lib/modules/kernel/updates/dkms/rp1_route_controller.ko"
            ],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
            "runtimeResidue": ["/var/lib/rp1-gpclk-dkms/runtime-admin"],
        }
        absent = {key: value for key, value in before.items()}
        absent.update({"dkms": "", "activeModule": False,
                       "activeController": False,
                       "sourceTrees": [], "moduleCandidates": [],
                       "controllerCandidates": [],
                       "runtimeResidue": []})
        owned = {
            "schema": MOD.RUNTIME_RECORD_SCHEMA, "channel": "development",
            "sourceCommit": "e" * 40, "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"], "uapiSha256": resolved["uapiSha256"],
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        rollback = pathlib.Path("/var/lib/wsprrypi/evidence/ROLLBACK.json")
        entrypoint = pathlib.Path("/var/lib/wsprrypi/evidence/rendered-source/scripts/development-rollback")
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult("usage: development-install --route-neutral --runtime-controller\n", "", 0),
        })
        events = []
        def authority(*unused):
            events.append("authority")
            return entrypoint, rollback
        def recover(*unused):
            events.append("runtime-remove")
        def provider(*unused):
            events.append("provider-validate")
            return entrypoint, rollback
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", side_effect=[before, absent]), \
             mock.patch.object(MOD, "load_ownership_record", side_effect=[
                 (owned, identity, None), (owned, identity, None), (None, None, "absent")]), \
             mock.patch.object(MOD, "validate_development_rollback_authority", side_effect=authority), \
             mock.patch.object(MOD, "recover_and_remove_owned_runtime", side_effect=recover), \
             mock.patch.object(MOD, "validate_development_removal", side_effect=provider), \
             mock.patch.object(MOD, "verify_provider_absent"), \
             mock.patch.object(MOD, "remove_ownership_record", side_effect=lambda *unused: events.append("record-remove")), \
             mock.patch.object(MOD, "ensure_new_ownership_record",
                               side_effect=MOD.ContractError("stop after migration")):
            with self.assertRaisesRegex(MOD.ContractError, "stop after migration"):
                MOD.apply_development(resolved, record, runner)
        self.assertEqual(events, ["authority", "runtime-remove", "provider-validate", "record-remove"])
        self.assertEqual(runner.passthrough_calls, [
            (str(entrypoint), "--record", str(rollback)),
        ])

    def test_exact_owned_runtime_does_not_adopt_an_active_consumer(self):
        source = self.lifecycle_source(
            "usage: development-install --route-neutral --runtime-controller"
        )
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        before = {
            "packageVersion": None, "dkms": "owned", "activeModule": True,
            "activeController": True, "configuredRoute": False,
            "sourceTrees": ["owned"], "moduleCandidates": ["owned"],
            "controllerCandidates": ["owned"], "installedOverlays": [],
            "enrollment": False, "developmentManager": False,
            "runtimeResidue": ["/var/lib/rp1-gpclk-dkms/runtime-admin"],
        }
        owned = {
            "schema": MOD.RUNTIME_RECORD_SCHEMA, "channel": "development",
            "sourceCommit": resolved["commit"],
            "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"],
            "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult(
                    "usage: development-install --route-neutral "
                    "--runtime-controller\n", "", 0
                ),
        })
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=before), \
             mock.patch.object(MOD, "load_ownership_record", return_value=(
                 owned, mock.Mock(st_dev=1, st_ino=2), None)), \
             mock.patch.object(MOD, "recover_and_remove_owned_runtime") as recover:
            with self.assertRaisesRegex(MOD.ContractError, "active RP1 GPCLK module"):
                MOD.apply_development(
                    resolved, pathlib.Path(self.temp.name) / "record.json", runner
                )
        recover.assert_not_called()

    def test_active_controller_requires_runtime_ownership(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        before = {
            "packageVersion": None, "dkms": "owned", "activeModule": False,
            "activeController": True, "configuredRoute": False,
            "sourceTrees": ["owned"], "moduleCandidates": ["owned"],
            "controllerCandidates": ["owned"], "installedOverlays": [],
            "enrollment": False, "developmentManager": False,
            "runtimeResidue": ["/var/lib/rp1-gpclk-dkms/runtime-admin"],
        }
        owned_provider = {"schema": MOD.RECORD_SCHEMA, "channel": "development"}
        runner = FakeRunner({
            (str(source / "scripts/development-install"), "--help"):
                MOD.CommandResult("usage: development-install --route-neutral --runtime-controller\n", "", 0),
        })
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=before), \
             mock.patch.object(MOD, "load_ownership_record", return_value=(
                 owned_provider, mock.Mock(st_dev=1, st_ino=2), None)):
            with self.assertRaisesRegex(
                    MOD.ContractError, "active RP1 route controller"):
                MOD.apply_development(
                    resolved, pathlib.Path(self.temp.name) / "record.json", runner)

    def test_runtime_migration_resumes_after_runtime_removal_checkpoint(self):
        source = pathlib.Path(self.temp.name) / "source"
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        provider = {
            "packageVersion": None, "dkms": "owned", "activeModule": False,
            "configuredRoute": False, "sourceTrees": ["owned"],
            "moduleCandidates": ["owned"], "installedOverlays": [],
            "enrollment": False, "developmentManager": False,
            "runtimeResidue": [],
        }
        absent = dict(provider, dkms="", sourceTrees=[], moduleCandidates=[])
        owned = {
            "schema": MOD.RECORD_SCHEMA, "channel": "development",
            "sourceCommit": "e" * 40, "productVersion": "0.9.0",
            "sourceTree": "f" * 40, "uapiSha256": "1" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "2" * 64,
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        rollback = pathlib.Path("/var/lib/wsprrypi/evidence/ROLLBACK.json")
        entrypoint = pathlib.Path("/var/lib/wsprrypi/evidence/rendered-source/scripts/development-rollback")
        runner = FakeRunner()
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "route_neutral_interface",
                               return_value=("route-neutral-flag", ["--route-neutral"])), \
             mock.patch.object(MOD, "existing_inventory", side_effect=[provider, absent]), \
             mock.patch.object(MOD, "load_ownership_record", side_effect=[
                 (owned, identity, None), (owned, identity, None), (None, None, "absent")]), \
             mock.patch.object(MOD, "validate_development_removal",
                               return_value=(entrypoint, rollback)) as validate, \
             mock.patch.object(MOD, "recover_and_remove_owned_runtime") as recover, \
             mock.patch.object(MOD, "verify_provider_absent"), \
             mock.patch.object(MOD, "remove_ownership_record"), \
             mock.patch.object(MOD, "ensure_new_ownership_record",
                               side_effect=MOD.ContractError("stop after migration")):
            with self.assertRaisesRegex(MOD.ContractError, "stop after migration"):
                MOD.apply_development(resolved, record, runner)
        validate.assert_called_once_with(owned, pathlib.Path("/"), runner)
        recover.assert_not_called()
        self.assertEqual(runner.passthrough_calls, [
            (str(entrypoint), "--record", str(rollback)),
        ])

    def test_runtime_migration_resumes_after_provider_removal_checkpoint(self):
        source = pathlib.Path(self.temp.name) / "source"
        source.mkdir()
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.9.0", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        absent = {
            "packageVersion": None, "dkms": "", "activeModule": False,
            "activeController": False, "configuredRoute": False,
            "sourceTrees": [], "moduleCandidates": [],
            "controllerCandidates": [], "installedOverlays": [],
            "enrollment": False, "developmentManager": False,
            "runtimeResidue": [],
        }
        owned = {"schema": MOD.RUNTIME_RECORD_SCHEMA, "channel": "development"}
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        events = []
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "route_neutral_interface",
                               return_value=("route-neutral-flag", ["--route-neutral"])), \
             mock.patch.object(MOD, "existing_inventory", return_value=absent), \
             mock.patch.object(MOD, "load_ownership_record", side_effect=[
                 (owned, identity, None), (None, None, "absent")]), \
             mock.patch.object(MOD, "verify_provider_absent",
                               side_effect=lambda *unused: events.append("verify-absent")), \
             mock.patch.object(MOD, "remove_ownership_record",
                               side_effect=lambda *unused: events.append("record-remove")), \
             mock.patch.object(MOD, "ensure_new_ownership_record",
                               side_effect=MOD.ContractError("stop after checkpoint")):
            with self.assertRaisesRegex(MOD.ContractError, "stop after checkpoint"):
                MOD.apply_development(resolved, record, FakeRunner())
        self.assertEqual(events, ["verify-absent", "record-remove"])

    def test_runtime_controller_residue_blocks_development_before_mutation(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "version": "0.9.0",
        }
        existing = {
            "packageVersion": None, "dkms": "", "activeModule": False,
            "configuredRoute": False, "sourceTrees": [], "moduleCandidates": [],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
            "runtimeResidue": [
                "/etc/systemd/system/wsprrypi.service.d/90-rp1-route-inhibit.conf"
            ],
        }
        record = pathlib.Path(self.temp.name) / "record.json"
        runner = MOD.Runner()
        with mock.patch.object(MOD, "revalidate_checkout", return_value=source), \
             mock.patch.object(MOD, "existing_inventory", return_value=existing):
            with self.assertRaisesRegex(MOD.ContractError, "runtime-controller residue.*owning runtime cleanup"):
                MOD.apply_development(resolved, record, runner)
        self.assertFalse(record.exists())

    def test_inventory_reports_runtime_files_directories_symlinks_and_controller_modules(self):
        root = pathlib.Path(self.temp.name) / "inventory-root"
        (root / "proc").mkdir(parents=True)
        (root / "proc/modules").write_text("")
        inhibitor = root / "etc/systemd/system/wsprrypi.service.d/90-rp1-route-inhibit.conf"
        inhibitor.parent.mkdir(parents=True)
        inhibitor.write_text("owned")
        runtime_library = root / "usr/lib/rp1-gpclk-dkms/runtime_provider.py"
        runtime_library.parent.mkdir(parents=True)
        runtime_library.symlink_to("missing-client")
        runtime_socket = root / "usr/lib/systemd/system/rp1-gpclk-route-manager.socket"
        runtime_socket.parent.mkdir(parents=True)
        runtime_socket.write_text("[Socket]\n")
        controller = root / "lib/modules/fixture/updates/dkms/rp1_route_controller.ko"
        controller.parent.mkdir(parents=True)
        controller.write_bytes(b"module")
        runner = FakeRunner({
            ("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME):
                MOD.CommandResult("", "not installed", 1),
        })
        inventory = MOD.existing_inventory(root, runner)
        self.assertEqual(
            inventory["runtimeResidue"],
            sorted((str(inhibitor), str(runtime_library), str(runtime_socket))),
        )
        self.assertEqual(inventory["controllerCandidates"], [str(controller)])


class ApplyPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        (self.root / "proc").mkdir()
        (self.root / "proc/modules").write_text("")
        self.state = self.root / "state"
        self.state.mkdir(mode=0o700)
        self.package = self.state / "rp1-gpclk-dkms_2.1.0-1_all.deb"
        self.package.write_bytes(b"fixture")
        self.manifest = valid_manifest()
        self.resolved = {"tag": "v2.1.0", "commit": "a" * 40, "manifest": self.manifest, "packagePath": str(self.package)}
        self.record = self.root / "record.json"

    def runtime_update_args(self, *, dry_run=False):
        MOD.atomic_json(self.state / "plan.json", {
            "schema": MOD.STATE_SCHEMA, "dryRun": dry_run,
            "decision": {"install": True},
            "resolved": {"channel": "development", "commit": "a" * 40,
                         "version": "0.9.0"},
        })
        return MOD.parser().parse_args([
            "prepare-runtime-update", "--state-dir", str(self.state),
            "--record", str(self.record),
        ])

    def runtime_update_record(self):
        return {
            "schema": MOD.RUNTIME_RECORD_SCHEMA, "channel": "development",
            "sourceCommit": "a" * 40, "productVersion": "0.9.0",
            "runtime": {
                "readinessContract": MOD.RUNTIME_READINESS_CONTRACT,
                "bindingSha256": "b" * 64, "artifactSetSha256": "c" * 64,
                "sourceCommit": "a" * 40, "productVersion": "0.9.0",
                "targetKernel": MOD.platform.release(),
                "compatibilityIdentities": {
                    "gpio4": "v0.9.0-rp1-gpio4",
                    "gpio20": "v0.9.0-rp1-gpio20",
                },
                "deploymentPlanSha256": "d" * 64,
                "activationPlanSha256": "e" * 64,
                "activationRequestId": "request-1234",
                "controllerSession": 42, "controllerGeneration": 0,
                "state": "neutral_ready", "route": None,
                "output": "disabled",
            },
        }

    def public_update(self, record, *, prepared=False, operation="update-plan", marker=1):
        runtime = record.get("runtime", {})
        identity = {"sourceCommit": record["sourceCommit"],
                    "bindingSha256": runtime.get("bindingSha256"),
                    "artifactSetSha256": runtime.get("artifactSetSha256"),
                    "deploymentPlanSha256": runtime.get("deploymentPlanSha256")}
        # Deliberately opaque: applications must not interpret provider internals.
        plan = {"providerPrivate": {"generation": marker, "futureField": ["opaque"]}}
        return {"schemaVersion": 1, "contract": MOD.RUNTIME_UPDATE_CONTRACT,
                "operation": operation, "identity": identity,
                "status": "prepared" if prepared else "planned",
                "plan": plan, "planSha256": digest(MOD.canonical(plan)),
                "postconditions": {"runtimeAbsent": prepared, "transmissionAuthorized": False}}


    def test_update_uses_only_public_envelopes_and_preserves_ownership(self):
        record = self.runtime_update_record(); identity = mock.Mock()
        first = self.public_update(record)
        executed = self.public_update(record, operation="update-execute")
        final = self.public_update(record, prepared=True, marker=2)
        with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
             mock.patch.object(MOD, "runtime_call", side_effect=[first, executed, final]) as calls, \
             mock.patch.object(MOD, "require_unchanged_ownership") as unchanged:
            MOD.recover_and_remove_owned_runtime(self.record, record, identity, FakeRunner())
        self.assertEqual([call.args[2] for call in calls.call_args_list],
                         ["update-plan", "update-execute", "update-plan"])
        self.assertEqual(unchanged.call_count, 3)
        self.assertIn(record["runtime"]["bindingSha256"], calls.call_args_list[0].args[3])


    def test_update_rejects_bad_envelopes_and_stale_results(self):
        record = self.runtime_update_record()
        for key, value in (("schemaVersion", 2), ("schemaVersion", True), ("contract", "unknown"),
                           ("identity", {}), ("status", "success"),
                           ("postconditions", {"runtimeAbsent": True}),
                           ("postconditions", {"runtimeAbsent": 0, "transmissionAuthorized": False}),
                           ("planSha256", "0" * 64)):
            with self.subTest(key=key):
                bad = self.public_update(record); bad[key] = value
                with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
                     mock.patch.object(MOD, "runtime_call", return_value=bad) as calls, \
                     mock.patch.object(MOD, "require_unchanged_ownership"):
                    with self.assertRaises(MOD.ContractError):
                        MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), FakeRunner())
                self.assertEqual(calls.call_count, 1)
        planned = self.public_update(record)
        bad = self.public_update(record, prepared=True, operation="update-execute", marker=99)
        with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
             mock.patch.object(MOD, "runtime_call", side_effect=[planned, bad]), \
             mock.patch.object(MOD, "require_unchanged_ownership"):
            with self.assertRaisesRegex(MOD.ContractError, "reviewed plan"):
                MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), FakeRunner())


    def test_update_never_repeats_effects_after_failure_or_no_progress(self):
        record = self.runtime_update_record(); planned = self.public_update(record)
        for replies in ([planned, MOD.ContractError("provider refused")],
                        [planned, self.public_update(record, operation="update-execute"), planned]):
            with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
                 mock.patch.object(MOD, "runtime_call", side_effect=replies) as calls, \
                 mock.patch.object(MOD, "require_unchanged_ownership"):
                with self.assertRaises(MOD.ContractError):
                    MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), FakeRunner())
                self.assertEqual(sum(call.args[2] == "update-execute" for call in calls.call_args_list), 1)


    def test_update_blocks_changed_application_ownership_before_execution(self):
        record = self.runtime_update_record()
        with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
             mock.patch.object(MOD, "runtime_call", return_value=self.public_update(record)) as calls, \
             mock.patch.object(MOD, "require_unchanged_ownership", side_effect=[None, MOD.ContractError("changed")]):
            with self.assertRaisesRegex(MOD.ContractError, "changed"):
                MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), FakeRunner())
        self.assertEqual(calls.call_count, 1)


    def test_update_negotiates_installed_or_exact_candidate_interface(self):
        installed = self.root / "installed.py"; installed.write_text("# fixture")
        candidate = self.root / "candidate.py"; candidate.write_text("# fixture")
        capabilities = {"schemaVersion": 1, "contract": MOD.RUNTIME_UPDATE_CONTRACT,
                        "operation": "capabilities", "capabilities":
                        ["update-plan", "update-execute", "installation-status"]}
        old = MOD.CommandResult("", "unsupported operation", 2)
        supported = MOD.CommandResult(json.dumps(capabilities), "", 0)
        with mock.patch.object(MOD, "RUNTIME_PROVIDER", installed):
            runner = FakeRunner({("python3", str(installed), "capabilities"): old,
                                 ("python3", str(candidate), "capabilities"): supported})
            self.assertEqual(MOD.runtime_update_provider(runner, candidate), candidate)
            with self.assertRaisesRegex(MOD.ContractError, "exact compatible provider source"):
                MOD.runtime_update_provider(FakeRunner({("python3", str(installed), "capabilities"): old}))
            runner = FakeRunner({("python3", str(installed), "capabilities"): supported})
            self.assertEqual(MOD.runtime_update_provider(runner, candidate), installed)
            self.assertEqual(len(runner.calls), 1)


    def test_release_receipt_does_not_require_development_rollback_metadata(self):
        record = self.runtime_update_record(); record['channel'] = 'release'
        record['manifest'] = {'productVersion': '0.9.0'}
        value = {'schemaVersion': 1, 'contract': MOD.RUNTIME_UPDATE_CONTRACT,
                 'operation': 'installation-status', 'status': 'neutral_ready',
                 'receipt': record['runtime']}
        with mock.patch.object(MOD, 'runtime_call', return_value=value), \
             mock.patch.object(MOD, 'validate_development_rollback_authority') as rollback:
            self.assertEqual(MOD.validate_resumable_neutral_activation(record, {}, FakeRunner()), record['runtime'])
        rollback.assert_not_called()

    def test_source_preflight_rejects_unsupported_candidate_before_effects(self):
        candidate = self.root / "scripts/runtime_provider.py"
        runner = FakeRunner({("python3", str(candidate), "capabilities"):
                             MOD.CommandResult("", "unsupported", 2)})
        with mock.patch.object(pathlib.Path, "is_file", return_value=True), \
             mock.patch.object(pathlib.Path, "is_symlink", return_value=False):
            with self.assertRaisesRegex(MOD.ContractError, "selected source lacks rp1-gpclk-runtime-update-v1"):
                MOD.runtime_source_interface(self.root, runner)
        self.assertEqual(len(runner.calls), 1)

    def test_update_preparation_keeps_dkms_record_after_public_absence(self):
        record = self.runtime_update_record(); identity = mock.Mock()
        for source in (record["sourceCommit"], "f" * 40):
            args = self.runtime_update_args()
            plan = json.loads((self.state / "plan.json").read_text())
            plan["resolved"]["commit"] = source
            MOD.atomic_json(self.state / "plan.json", plan)
            with mock.patch.object(MOD, "load_ownership_record", return_value=(record, identity, None)), \
                 mock.patch.object(MOD, "recover_and_remove_owned_runtime") as prepare, \
                 mock.patch.object(MOD, "validate_development_rollback_authority"), \
                 mock.patch.object(MOD, "replace_owned_record") as replace:
                MOD.prepare_runtime_update(args, FakeRunner())
            prepare.assert_called_once()
            replacement = replace.call_args.args[3]
            self.assertEqual(replacement["schema"], MOD.RECORD_SCHEMA)
            self.assertNotIn("runtime", replacement)


    def test_update_checks_rollback_authority_before_provider_effects(self):
        with mock.patch.object(MOD, 'validate_development_rollback_authority',
                               side_effect=MOD.ContractError('rollback changed')), \
             mock.patch.object(MOD, 'recover_and_remove_owned_runtime') as effects:
            with self.assertRaisesRegex(MOD.ContractError, 'rollback changed'):
                MOD.remove_owned_runtime_for_fresh_activation(self.record,
                    self.runtime_update_record(), mock.Mock(), FakeRunner())
        effects.assert_not_called()

    def test_runtime_call_reports_provider_failure_detail(self):
        provider = self.root / "runtime_provider.py"
        response = {
            "result": "conflict",
            "error": {"detail": "application restoration verification failed"},
            "conflicts": ["application-not-neutral"],
            "remediation": ["inspect wsprrypi.service"],
        }
        runner = FakeRunner({
            ("python3", str(provider), "remove"):
                MOD.CommandResult(json.dumps(response), "", 12),
        })
        with self.assertRaisesRegex(
                MOD.RuntimeOperationError,
                "application restoration verification failed.*"
                "application-not-neutral.*inspect wsprrypi.service") as caught:
            MOD.runtime_call(runner, provider, "remove")
        self.assertEqual(caught.exception.operation, "remove")
        self.assertEqual(caught.exception.value, response)


    def test_runtime_update_rejects_dry_run_plan_without_provider_call(self):
        args = self.runtime_update_args(dry_run=True)
        with mock.patch.object(MOD, "runtime_call") as calls:
            with self.assertRaisesRegex(MOD.ContractError, "dry-run"):
                MOD.prepare_runtime_update(args, FakeRunner())
        calls.assert_not_called()


    def responses(self, installed_version=None, dkms=""):
        dpkg = MOD.CommandResult("", "missing", 1)
        if installed_version:
            dpkg = MOD.CommandResult(f"install ok installed\n{installed_version}\n", "", 0)
        return {
            ("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME): dpkg,
            ("dkms", "status", "-m", MOD.DKMS_NAME): MOD.CommandResult(dkms, "", 0),
        }

    def test_foreign_and_mixed_state_refused_before_apt(self):
        source = self.root / "usr/src/rp1-gpclk-dkms-foreign"
        source.mkdir(parents=True)
        runner = FakeRunner(self.responses())
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "foreign or mixed"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))

    def test_different_installed_version_requires_owner_migration(self):
        runner = FakeRunner(self.responses("9.9.9-1"))
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "owning package migration"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))

    def test_active_module_and_development_manager_refused(self):
        (self.root / "proc/modules").write_text(f"{MOD.MODULE_NAME} 1 0 - Live 0x0\n")
        runner = FakeRunner(self.responses())
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "active"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))

    def test_exact_installation_is_idempotent(self):
        member = self.manifest["packageInventory"][0]
        installed = self.root / member["path"]
        installed.parent.mkdir(parents=True)
        installed.write_bytes(b"uapi\n")
        installed.chmod(int(member["mode"], 8))
        responses = self.responses(
            "2.1.0-1",
            "rp1-gpclk-dkms/2.1.0, fixture-kernel, arm64: installed\n",
        )
        responses[("modinfo", "-k", MOD.platform.release(), "-F", "version", MOD.MODULE_NAME)] = MOD.CommandResult("2.1.0\n", "", 0)
        runner = FakeRunner(responses)
        with mock.patch.object(MOD, "validate_package"):
            MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))
        self.assertFalse(self.record.exists(), "pre-existing exact providers must not become WsprryPi-owned")

    def test_exact_installation_rejects_foreign_same_version_state(self):
        member = self.manifest["packageInventory"][0]
        installed = self.root / member["path"]
        installed.parent.mkdir(parents=True)
        installed.write_bytes(b"uapi\n")
        (self.root / "usr/src/rp1-gpclk-dkms-foreign").mkdir()
        runner = FakeRunner(self.responses(
            "2.1.0-1",
            "rp1-gpclk-dkms/2.1.0, fixture-kernel, arm64: installed\n",
        ))
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "source destinations"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)

    def test_fresh_release_uses_normal_debian_path(self):
        runner = FakeRunner(self.responses())
        artifacts = [{
            "path": "/lib/modules/fixture/updates/dkms/rp1_gpclk_dkms.ko",
            "sha256": digest(b"module"),
        }]
        with mock.patch.object(MOD, "validate_package"), \
             mock.patch.object(MOD, "verify_installed_release"), \
             mock.patch.object(MOD, "module_artifacts", return_value=artifacts):
            MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        apt_calls = [call for call in runner.calls if call and call[0] == "apt-get"]
        self.assertEqual(apt_calls, [("apt-get", "install", "--no-install-recommends", "-y", str(self.package))])
        self.assertEqual(runner.passthrough_calls, apt_calls)
        recorded = json.loads(self.record.read_text())
        self.assertEqual(recorded["schema"], MOD.RECORD_SCHEMA)
        self.assertEqual(recorded["owner"], "WsprryPi")
        self.assertEqual(recorded["moduleArtifacts"][0]["sha256"], digest(b"module"))

    def test_dry_run_plan_cannot_be_applied(self):
        model = self.root / "model"
        compatible = self.root / "compatible"
        model.write_bytes(b"Unknown\0")
        compatible.write_bytes(b"")
        args = MOD.parser().parse_args([
            "prepare", "--state-dir", str(self.state), "--install", "false",
            "--source", "auto", "--wsprry-source", "main",
            "--model-file", str(model), "--compatible-file", str(compatible), "--dry-run",
        ])
        plan = MOD.prepare(args, FakeRunner())
        self.assertTrue(plan["dryRun"])
        apply_args = MOD.parser().parse_args([
            "apply", "--state-dir", str(self.state), "--record", str(self.record), "--root", str(self.root),
        ])
        with self.assertRaisesRegex(MOD.ContractError, "dry-run"):
            MOD.apply(apply_args, FakeRunner())

    def test_prepare_rejects_runtime_residue_before_source_resolution(self):
        model = self.root / "model"
        compatible = self.root / "compatible"
        model.write_bytes(b"Raspberry Pi 5 Model B Rev 1.0\0")
        compatible.write_bytes(b"raspberrypi,5-model-b\0brcm,bcm2712\0")
        args = MOD.parser().parse_args([
            "prepare", "--state-dir", str(self.state), "--install", "auto",
            "--source", "devel", "--wsprry-source", "devel",
            "--model-file", str(model), "--compatible-file", str(compatible),
        ])
        inventory = {
            "runtimeResidue": [
                "/etc/systemd/system/wsprrypi.service.d/90-rp1-route-inhibit.conf"
            ]
        }
        with mock.patch.object(MOD, "runtime_residue_inventory", return_value=inventory["runtimeResidue"]), \
             mock.patch.object(MOD, "existing_inventory") as full_inventory, \
             mock.patch.object(MOD, "prepare_development") as resolve:
            with self.assertRaisesRegex(MOD.ContractError, "residue blocks installation planning"):
                MOD.prepare(args, FakeRunner())
        full_inventory.assert_not_called()
        resolve.assert_not_called()
        self.assertFalse((self.state / "plan.json").exists())

    def test_prepare_allows_runtime_retry_for_owned_provider(self):
        model = self.root / "model"
        compatible = self.root / "compatible"
        model.write_bytes(b"Raspberry Pi 5 Model B Rev 1.0\0")
        compatible.write_bytes(b"raspberrypi,5-model-b\0brcm,bcm2712\0")
        args = MOD.parser().parse_args([
            "prepare", "--state-dir", str(self.state), "--install", "auto",
            "--source", "devel", "--wsprry-source", "devel",
            "--model-file", str(model), "--compatible-file", str(compatible),
        ])
        owned = {"schema": MOD.RECORD_SCHEMA}
        resolved = {"channel": "development", "commit": "a" * 40,
                    "version": "1.2.3", "sourceTree": "b" * 40,
                    "uapiSha256": "c" * 64}
        with mock.patch.object(
                MOD, "runtime_residue_inventory",
                return_value=["/var/lib/rp1-gpclk-dkms/runtime-admin"]), \
             mock.patch.object(MOD, "load_ownership_record",
                               return_value=(owned, mock.sentinel.identity, None)), \
             mock.patch.object(MOD, "prepare_development",
                               return_value=resolved) as resolve:
            plan = MOD.prepare(args, FakeRunner())
        resolve.assert_called_once()
        self.assertEqual(plan["resolved"], resolved)

    def test_runtime_residue_inventory_covers_complete_activation_surface(self):
        source = SCRIPT.read_text()
        for path in (
            "/usr/lib/rp1-gpclk-dkms/runtime_activation.py",
            "/dev/rp1-route-admin", "/dev/rp1-gpclk",
            "/sys/module/rp1_route_controller", "/sys/module/rp1_gpclk_dkms",
            "/run/rp1-gpclk-dkms/route-manager.sock",
            "/etc/systemd/system/wsprrypi.service.d/90-rp1-route-inhibit.conf",
            "/etc/systemd/system/wsprrypi.service.d/91-rp1-route-idle.conf",
            "/var/lib/rp1-gpclk-dkms/runtime-admin",
        ):
            self.assertIn(path, source)


    def test_runtime_bundle_requires_self_contained_bootstrap_and_bound_units(self):
        bundle = self.root / "bundle"
        bundle.mkdir(mode=0o700)
        companion = self.root / "route_application.py"
        companion.write_bytes(b"companion\n")
        kernel = "fixture-kernel"
        bootstrap = {
            "runtime_deployment.py", "runtime_controller_admin.py", "runtime_layout.py",
            "runtime_application.py", "runtime_output.py", "runtime_provider.py",
            "runtime_binding.py", "runtime_activation.py", "runtime_route_client.py",
            "runtime_manager.py",
        }
        files = {
            "/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_gpclk.h",
            "/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_route_admin.h",
            "/usr/lib/rp1-gpclk-dkms/runtime-overlays/gpio4.dtbo",
            "/usr/lib/rp1-gpclk-dkms/runtime-overlays/gpio20.dtbo",
            "/usr/lib/rp1-gpclk-dkms/schema/rp1-gpclk-runtime-readiness-v1.schema.json",
            "/usr/lib/systemd/system/rp1-gpclk-route-manager.socket",
            "/usr/lib/systemd/system/rp1-gpclk-route-manager@.service",
            "/etc/systemd/system/rp1-gpclk-route-manager@.service.d/95-runtime-controller.conf",
            *{"/usr/lib/rp1-gpclk-dkms/" + name for name in bootstrap},
        }
        file_digests = {}
        for destination in files:
            name = pathlib.Path(destination).name
            payload = (("# fixture: " if name in bootstrap else "payload:") +
                       destination + "\n").encode()
            file_digests[destination] = digest(payload)
            (bundle / (digest(destination.encode()) + ".bin")).write_bytes(payload)
        for name in bootstrap:
            destination = "/usr/lib/rp1-gpclk-dkms/" + name
            (bundle / name).write_bytes(("# fixture: " + destination + "\n").encode())
        binding = {
            "schemaVersion": 3, "contract": MOD.RUNTIME_BINDING_CONTRACT,
            "productVersion": "0.9.0",
            "compatibilityIdentities": {
                "gpio4": "v0.9.0-rp1-gpio4", "gpio20": "v0.9.0-rp1-gpio20"},
            "sourceCommit": "a" * 40, "kernel": kernel, "files": file_digests,
            "externalFiles": {str(companion): digest(companion.read_bytes())},
            "uapiSha256": {
                "consumer": file_digests["/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_gpclk.h"],
                "controller": file_digests["/usr/lib/rp1-gpclk-dkms/runtime-uapi/rp1_route_admin.h"]},
            "controllerNoteSha256": "b" * 64, "consumerNoteSha256": "c" * 64,
        }
        modules = {}
        for name, note in ((MOD.CONTROLLER_MODULE_NAME, "b" * 64),
                           (MOD.MODULE_NAME, "c" * 64)):
            compression = "xz" if name == MOD.CONTROLLER_MODULE_NAME else "none"
            suffix = ".ko.xz" if compression == "xz" else ".ko"
            path = f"/lib/modules/{kernel}/updates/dkms/{name}{suffix}"
            actual = MOD.root_path(self.root, path)
            actual.parent.mkdir(parents=True, exist_ok=True)
            payload = b"\x7fELF" + name.encode()
            actual.write_bytes(lzma.compress(payload) if compression == "xz" else payload)
            modules[name] = {
                "name": name, "path": path,
                "installedFileSha256": digest(actual.read_bytes()),
                "decompressedElfSha256": digest(payload),
                "compression": compression, "buildNoteSha256": note,
                "version": "0.9.0", "kernel": kernel,
            }
        binding["modules"] = modules
        binding["artifactSetSha256"] = digest(MOD.canonical(binding))
        (bundle / "binding.json").write_text(json.dumps(binding))
        resolved = {"channel": "development", "commit": "a" * 40, "version": "0.9.0"}
        with mock.patch.object(MOD.platform, "release", return_value=kernel):
            result = MOD.validate_runtime_bundle(bundle, resolved, companion, self.root)
        self.assertEqual(result["artifactSetSha256"], binding["artifactSetSha256"])

        legacy_binding = dict(binding)
        legacy_binding["compatibilityIdentities"] = {
            "gpio4": "v0.9.0-pi5-gpio4", "gpio20": "v0.9.0-pi5-gpio20",
        }
        (bundle / "binding.json").write_text(json.dumps(legacy_binding))
        with mock.patch.object(MOD.platform, "release", return_value=kernel):
            with self.assertRaisesRegex(MOD.ContractError, "compatibility identities differ"):
                MOD.validate_runtime_bundle(bundle, resolved, companion, self.root)

        (bundle / "binding.json").write_text(json.dumps(binding))
        (bundle / "runtime_route_client.py").unlink()
        with mock.patch.object(MOD.platform, "release", return_value=kernel):
            with self.assertRaisesRegex(MOD.ContractError, "unsupported or missing member"):
                MOD.validate_runtime_bundle(bundle, resolved, companion, self.root)

    def test_runtime_bundle_import_closure_rejects_missing_local_module(self):
        bundle = self.root / "bootstrap"
        bundle.mkdir()
        (bundle / "runtime_provider.py").write_text("import runtime_missing\n")
        with self.assertRaisesRegex(MOD.ContractError, "import closure is incomplete"):
            MOD.validate_bootstrap_import_closure(
                bundle, {"runtime_provider.py"})

    def test_neutral_runtime_activation_records_reviewed_identity(self):
        resolved = {"channel": "development", "commit": "a" * 40, "version": "0.9.0"}
        MOD.atomic_json(self.state / "plan.json", {
            "schema": MOD.STATE_SCHEMA, "dryRun": False,
            "decision": {"install": True}, "resolved": resolved,
        })
        owned = {
            "schema": MOD.RECORD_SCHEMA, "channel": "development",
            "sourceCommit": "a" * 40, "productVersion": "0.9.0",
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        binding = {
            "artifactSetSha256": "b" * 64,
            "compatibilityIdentities": {
                "gpio4": "v0.9.0-rp1-gpio4", "gpio20": "v0.9.0-rp1-gpio20",
            },
            "files": {"/bound/runtime": "c" * 64},
        }
        readiness = lambda state: {
            "contract": MOD.RUNTIME_READINESS_CONTRACT, "result": state, "state": state,
        }
        deployment = readiness("deployment_required")
        deployment["deployment"] = {"plan": {
            "planSha256": "d" * 64,
            "destinations": {path: {"before": None, "after": "c" * 64} for path in (
                "/bound/runtime", "/etc/rp1-gpclk-dkms/runtime-controller.json",
                "/var/lib/rp1-gpclk-dkms/runtime-admin/transaction.json",
                "/var/lib/rp1-gpclk-dkms/runtime-admin/manager.json",
                "/var/lib/rp1-gpclk-dkms/runtime-admin/application.json",
                "/var/lib/rp1-gpclk-dkms/runtime-admin/activation.json",
            )},
        }}
        activation = readiness("activation_required")
        activation["activationPlan"] = {
            "planSha256": "e" * 64, "bindingSha256": "f" * 64,
            "artifactSetSha256": "b" * 64,
        }
        receipt = self.runtime_update_record()["runtime"]
        receipt.update(bindingSha256="f" * 64, artifactSetSha256="b" * 64)
        final = {"schemaVersion": 1, "contract": MOD.RUNTIME_UPDATE_CONTRACT,
                 "operation": "installation-status", "status": "neutral_ready", "receipt": receipt}
        replies = [
            readiness("absent"), deployment, {"operation": "ensure"},
            readiness("activation_required"), activation,
            {"contract": MOD.RUNTIME_READINESS_CONTRACT,
             "operation": "activation-ensure", "planSha256": "e" * 64,
             "response": {"journal": {"requestId": "00000000-0000-0000-0000-000000000001"}}},
            final,
        ]
        args = MOD.parser().parse_args([
            "activate-runtime", "--state-dir", str(self.state), "--record", str(self.record),
        ])
        captured = {}
        def replace(path, expected, expected_identity, replacement):
            captured.update(replacement)
        with mock.patch.object(MOD, "load_ownership_record", return_value=(owned, identity, None)), \
             mock.patch.object(MOD, "build_runtime_bundle", return_value=(self.state / "bundle", {
                 "binding": binding, "bindingSha256": "f" * 64,
                 "artifactSetSha256": "b" * 64,
             })), \
             mock.patch.object(MOD, "runtime_call", side_effect=replies) as calls, \
             mock.patch.object(MOD, "validate_development_rollback_authority"), \
             mock.patch.object(MOD, "replace_owned_record", side_effect=replace):
            MOD.activate_runtime(args, FakeRunner())
        self.assertEqual(calls.call_count, 7)
        self.assertEqual(captured["schema"], MOD.RUNTIME_RECORD_SCHEMA)
        runtime = captured["runtime"]
        self.assertEqual(runtime["deploymentPlanSha256"], "d" * 64)
        self.assertEqual(runtime["activationPlanSha256"], "e" * 64)
        self.assertEqual(runtime["controllerSession"], 42)
        self.assertEqual(runtime["controllerGeneration"], 0)
        self.assertIsNone(runtime["route"])
        self.assertEqual(runtime["output"], "disabled")


    def test_selected_shell_dry_run_never_invokes_python_or_resolution_tools(self):
        install_script = ROOT / "scripts" / "install.sh"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python3 >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
curl() { printf curl >"$SENTINEL"; return 99; }
mktemp() { printf mktemp >"$SENTINEL"; return 99; }
git() { printf git >"$SENTINEL"; return 99; }
apt-get() { printf apt-get >"$SENTINEL"; return 99; }
dpkg-deb() { printf dpkg-deb >"$SENTINEL"; return 99; }
DRY_RUN=true
ACTION=install
INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_SOURCE="$SOURCE_SELECTOR"
REPO_BRANCH="$WSPRRYPi_BRANCH"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
prepare_rp1_gpclk_dkms_installation debug
apply_rp1_gpclk_dkms_installation debug
prepare_rp1_gpclk_runtime_update debug
activate_rp1_gpclk_runtime_administration debug
[[ ! -e "$SENTINEL" ]]
cleanup_rp1_gpclk_dkms_state
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" && -z "$RP1_GPCLK_DKMS_HELPER" ]]
'''
        for selector, branch in (("release", "main"), ("devel", "codex/issue-412-test")):
            sentinel = self.root / f"{selector}.invoked"
            environment = os.environ.copy()
            environment.update({
                "INSTALL_SCRIPT": str(install_script),
                "SENTINEL": str(sentinel),
                "SOURCE_SELECTOR": selector,
                "WSPRRYPi_BRANCH": branch,
            })
            result = subprocess.run(
                ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=environment, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(sentinel.exists())
            self.assertEqual(result.stdout.count("Complete: (dry)"), 6)
            self.assertIn(
                "[INFO ] Resolve RP1-GPCLK-DKMS installation plan.",
                result.stdout,
            )
            self.assertNotIn(
                "Complete: (dry) Resolve RP1-GPCLK-DKMS installation plan",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Apply RP1-GPCLK-DKMS installation plan",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Prepare neutral RP1 runtime for application update",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Activate neutral RP1-GPCLK-DKMS administration",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Reload systemd after RP1 runtime activation",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Start WsprryPi after RP1 runtime activation",
                result.stdout,
            )
            self.assertIn("Name:    Resolve RP1-GPCLK-DKMS installation plan", result.stderr)
            self.assertIn("Name:    Apply RP1-GPCLK-DKMS installation plan", result.stderr)
            helper = install_script.parent / "rp1_gpclk_dkms_install.py"
            prepare_command = (
                f"Command: python3 {helper} prepare --state-dir dry-run:no-state-created "
                f"--install true --source {selector} --wsprry-source {branch} --dry-run --debug"
            )
            apply_command = (
                f"Command: python3 {helper} apply --state-dir dry-run:no-state-created --debug"
            )
            update_command = (
                f"Command: python3 {helper} prepare-runtime-update "
                "--state-dir dry-run:no-state-created --debug"
            )
            activation_command = (
                f"Command: python3 {helper} activate-runtime "
                "--state-dir dry-run:no-state-created --debug"
            )
            self.assertIn(prepare_command, result.stderr)
            self.assertIn(apply_command, result.stderr)
            self.assertIn(update_command, result.stderr)
            self.assertIn(activation_command, result.stderr)

    def test_non_rp1_auto_and_explicit_false_render_no_provider_steps(self):
        install_script = ROOT / "scripts" / "install.sh"
        shell = r'''
source "$INSTALL_SCRIPT"
is_rp1_system() { return "$RP1_STATUS"; }
python3() { printf python3 >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
DRY_RUN=true
ACTION=install
INSTALL_RP1_GPCLK_DKMS="$SELECTION"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
prepare_rp1_gpclk_dkms_installation debug
apply_rp1_gpclk_dkms_installation debug
prepare_rp1_gpclk_runtime_update debug
activate_rp1_gpclk_runtime_administration debug
[[ ! -e "$SENTINEL" ]]
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" && -z "$RP1_GPCLK_DKMS_HELPER" ]]
'''
        for selection, rp1_status in (("auto", "1"), ("false", "0")):
            sentinel = self.root / f"{selection}.invoked"
            result = subprocess.run(
                ["bash", "-c", shell],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env={
                    **os.environ,
                    "INSTALL_SCRIPT": str(install_script),
                    "SENTINEL": str(sentinel),
                    "SELECTION": selection,
                    "RP1_STATUS": rp1_status,
                },
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertNotIn("RP1-GPCLK-DKMS", result.stderr)
            self.assertFalse(sentinel.exists())

    def test_auto_pi5_shell_path_still_reaches_provider_planner(self):
        install_script = ROOT / "scripts" / "install.sh"
        result = subprocess.run(
            [
                "bash",
                "-c",
                r'''
source "$INSTALL_SCRIPT"
is_rp1_system() { return 0; }
python3() { printf invoked >"$SENTINEL"; return 99; }
logI() { printf '[INFO ] %s\n' "$1"; }
ACTION=install
DRY_RUN=true
INSTALL_RP1_GPCLK_DKMS=auto
prepare_rp1_gpclk_dkms_installation
cleanup_rp1_gpclk_dkms_state
[[ ! -e "$SENTINEL" ]]
''',
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={
                **os.environ,
                "INSTALL_SCRIPT": str(install_script),
                "SENTINEL": str(self.root / "auto-pi5-planner"),
            },
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Resolve RP1-GPCLK-DKMS installation plan", result.stdout)
        self.assertFalse((self.root / "auto-pi5-planner").exists())

    def test_shell_rp1_identity_requires_pi5_model_and_bcm2712(self):
        install_script = ROOT / "scripts" / "install.sh"
        compatible = self.root / "compatible"
        scenarios = (
            (b"raspberrypi,5-model-b\0brcm,bcm2712\0", True),
            (b"raspberrypi,5-compute-module\0brcm,bcm2712\0", True),
            (b"raspberrypi,5-model-b\0", False),
            (b"raspberrypi,4-model-b\0brcm,bcm2711\0", False),
            (b"brcm,bcm2712\0", False),
        )
        for payload, expected in scenarios:
            compatible.write_bytes(payload)
            result = subprocess.run(
                [
                    "bash",
                    "-c",
                    'source "$INSTALL_SCRIPT"; is_rp1_system "$COMPATIBLE"',
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env={
                    **os.environ,
                    "INSTALL_SCRIPT": str(install_script),
                    "COMPATIBLE": str(compatible),
                },
                check=False,
            )
            self.assertEqual(result.returncode == 0, expected, payload)

    def test_exec_command_preserves_internal_debug_argv(self):
        install_script = ROOT / "scripts" / "install.sh"
        capture = self.root / "argv.txt"
        shell = r'''
source "$INSTALL_SCRIPT"
probe() { printf '%s\n' "$@" >"$CAPTURE"; }
logD() { :; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
exec_command "argv fidelity" probe before debug after debug
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "CAPTURE": str(capture)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(capture.read_text().splitlines(), ["before", "debug", "after"])

    def test_empty_log_message_fails_without_unbound_argument_abort(self):
        install_script = ROOT / "scripts" / "install.sh"
        result = subprocess.run(
            ["bash", "-c", 'source "$INSTALL_SCRIPT"; logI ""'],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "INSTALL_SCRIPT": str(install_script)},
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("Message is required.", result.stderr)
        self.assertNotIn("unbound variable", result.stderr)

    def test_exec_command_retains_bounded_failure_details(self):
        install_script = ROOT / "scripts/install.sh"
        failure_output = self.root / "apply-output.log"
        shell = r'''
source "$INSTALL_SCRIPT"
failing_helper() {
    local number
    printf '%20000s\n' x
    for number in {1..25}; do
        printf '\033[31mdetail-%02d\033[0m\n' "$number"
    done
    return 7
}
warn() { printf '[WARN] %s\n' "$1"; }
logW() { printf '[LOG] %s\n' "$1"; }
logD() { :; }
DRY_RUN=false
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
EXEC_COMMAND_FAILURE_OUTPUT_FILE="$FAILURE_OUTPUT" \
    exec_command "fixture apply" failing_helper || status=$?
[[ "$status" -eq 7 ]]
'''
        environment = os.environ.copy()
        environment.update({
            "INSTALL_SCRIPT": str(install_script),
            "FAILURE_OUTPUT": str(failure_output),
        })
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "Failure details from fixture apply (last 20 lines, 16 KiB maximum)",
            result.stdout,
        )
        self.assertNotIn("detail-05", result.stdout)
        self.assertIn("detail-06", result.stdout)
        self.assertIn("detail-25", result.stdout)
        self.assertNotIn("\x1b", result.stdout)
        self.assertLessEqual(failure_output.stat().st_size, 16384)
        self.assertTrue(failure_output.read_text().endswith("detail-25\x1b[0m\n"))
        self.assertEqual(stat.S_IMODE(failure_output.stat().st_mode), 0o600)

    def test_shell_propagates_debug_to_all_helper_invocations(self):
        install_script = ROOT / "scripts" / "install.sh"
        capture = self.root / "helper-argv.txt"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() {
    {
        printf 'CALL\n'
        printf 'ARG=%s\n' "$@"
    } >>"$CAPTURE"
}
systemctl() { :; }
logI() { printf '[INFO ] %s\n' "$1"; }
logD() { :; }
DRY_RUN=false
ACTION=install
INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_SOURCE=release
REPO_BRANCH=main
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
prepare_rp1_gpclk_dkms_installation debug
apply_rp1_gpclk_dkms_installation debug
prepare_rp1_gpclk_runtime_update debug
activate_rp1_gpclk_runtime_administration debug
cleanup_rp1_gpclk_dkms_state
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "CAPTURE": str(capture)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = capture.read_text().splitlines()
        self.assertEqual(arguments.count("CALL"), 5)
        self.assertEqual(arguments.count("ARG=--debug"), 4)
        self.assertIn("ARG=/usr/local/lib/wsprrypi/runtime_reconcile.py", arguments)

    def test_selected_uninstall_dry_run_reports_check_without_invoking_python(self):
        install_script = ROOT / "scripts" / "install.sh"
        sentinel = self.root / "uninstall-python-invoked"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python3 >"$SENTINEL"; return 99; }
curl() { printf curl >"$SENTINEL"; return 99; }
mktemp() { printf mktemp >"$SENTINEL"; return 99; }
DRY_RUN=true
ACTION=uninstall
REPO_ORG=WsprryPi
REPO_NAME=WsprryPi
REPO_BRANCH=devel
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
remove_owned_rp1_gpclk_dkms_provider debug
[[ ! -e "$SENTINEL" ]]
cleanup_rp1_gpclk_dkms_state
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "SENTINEL": str(sentinel)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(sentinel.exists())
        self.assertEqual(result.stdout.count("Complete: (dry)"), 1)
        self.assertIn("Complete: (dry) Check owned RP1 provider.", result.stdout)
        self.assertLessEqual(max(map(len, result.stdout.splitlines())), 80)
        helper = install_script.parent / "rp1_gpclk_dkms_install.py"
        self.assertIn(
            f"Command: python3 {helper} remove --remove auto --debug",
            result.stderr,
        )

    def test_uninstall_opt_out_never_prepares_or_invokes_helper(self):
        install_script = ROOT / "scripts" / "install.sh"
        sentinel = self.root / "opt-out-helper-activity"
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python3 >"$SENTINEL"; return 99; }
curl() { printf curl >"$SENTINEL"; return 99; }
mktemp() { printf mktemp >"$SENTINEL"; return 99; }
logI() { :; }
DRY_RUN=false
ACTION=uninstall
REMOVE_RP1_GPCLK_DKMS=false
remove_owned_rp1_gpclk_dkms_provider debug
[[ ! -e "$SENTINEL" ]]
[[ -z "$RP1_GPCLK_DKMS_STATE_DIR" && -z "$RP1_GPCLK_DKMS_HELPER" ]]
'''
        environment = os.environ.copy()
        environment.update({"INSTALL_SCRIPT": str(install_script), "SENTINEL": str(sentinel)})
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(sentinel.exists())

    def test_forbidden_runtime_operations_absent_from_installer_paths(self):
        install_source = (ROOT / "scripts/install.sh").read_text()
        helper_source = SCRIPT.read_text()
        self.assertIn('[[ "$ACTION" == "install" ]] || return 0', install_source)
        self.assertIn('*route_args, "--output-inhibit", "1", "--install"', helper_source)
        self.assertNotIn('"--live-output"', helper_source)
        self.assertNotRegex(helper_source, r'\["(?:dtoverlay|modprobe|insmod|rmmod|systemctl|service|reboot|shutdown)"')
        self.assertNotRegex(helper_source, r"config\.txt[^\n]*(?:write|replace|unlink)")

    def test_prepare_precedes_packages_and_runtime_recovery_precedes_apply(self):
        source = (ROOT / "scripts/install.sh").read_text()
        prepare = source.index('prepare_rp1_gpclk_dkms_installation "$debug" || return 1')
        packages = source.index('handle_apt_packages "$debug" || return 1')
        dependencies = source.index('validate_rp1_gpclk_build_dependencies "$debug" || return 1')
        apply = source.index('apply_rp1_gpclk_dkms_installation "$debug" || return 1')
        update = source.index('prepare_rp1_gpclk_runtime_update "$debug" || return 1')
        package_notice = source.index(
            'logI "Updating and managing required packages (this may take a few minutes)."'
        )
        application = source.index('manage_wsprry_pi "$debug"')
        self.assertLess(prepare, update)
        self.assertLess(package_notice, update)
        self.assertLess(update, packages)
        self.assertLess(packages, dependencies)
        self.assertLess(dependencies, apply)
        self.assertLess(update, application)
        group = source[source.index("local install_group=("):]
        companion = group.index('"manage_route_application"')
        service = group.index('"manage_service')
        activation = group.index('"activate_rp1_gpclk_runtime_administration"')
        website = group.index('"manage_web"')
        readiness = group.index('"validate_wsprrypi_runtime"')
        self.assertLess(companion, activation)
        self.assertLess(service, activation)
        self.assertLess(activation, website)
        self.assertLess(website, readiness)

        package_handler_start = source.index("handle_apt_packages() {")
        package_handler_end = source.index("\n}\n", package_handler_start)
        self.assertNotIn(
            "Updating and managing required packages",
            source[package_handler_start:package_handler_end],
        )

    def test_runtime_activation_refreshes_systemd_before_service_validation(self):
        source = (ROOT / "scripts/install.sh").read_text()
        function = source[
            source.index("activate_rp1_gpclk_runtime_administration()"):
            source.index("prepare_owned_rp1_gpclk_runtime_removal()")
        ]
        activation = function.index(
            'exec_command "Activate neutral RP1-GPCLK-DKMS administration"'
        )
        reload = function.index(
            'exec_command "Reload systemd after RP1 runtime activation"'
        )
        start = function.index(
            'exec_command "Start WsprryPi after RP1 runtime activation"'
        )
        self.assertLess(activation, reload)
        self.assertLess(reload, start)
        restoration = function.index('exec_command "Restore configured RP1 route with transmission disabled"')
        self.assertLess(start, restoration)
        self.assertIn('route-restoration-output.log', function)
        self.assertIn('setup cannot confirm transmitter readiness', function)

    def test_route_restoration_failure_stops_installer_activation(self):
        shell = r'''
source "$INSTALL_SCRIPT"
ACTION=install DRY_RUN=false INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_STATE_DIR=/fixture RP1_GPCLK_DKMS_HELPER=/fixture/helper.py
logD() { :; }
warn() { printf '%s\n' "$1"; }
exec_command() {
    printf '%s\n' "$1"
    if [[ "$1" == "Restore configured RP1 route with transmission disabled" ]]; then
        [[ "$EXEC_COMMAND_FAILURE_OUTPUT_FILE" == /fixture/route-restoration-output.log ]] || return 99
        [[ "$2" == python3 && "$3" == /usr/local/lib/wsprrypi/runtime_reconcile.py && "$4" == install ]] || return 98
        return "$ROUTE_RESULT"
    fi
}
activate_rp1_gpclk_runtime_administration || exit 1
printf 'SETUP_CONTINUES\n'
'''
        for status in (0, 12):
            result = subprocess.run(['bash', '-c', shell], text=True, capture_output=True,
                env={**os.environ, 'INSTALL_SCRIPT': str(ROOT/'scripts/install.sh'),
                     'ROUTE_RESULT': str(status)})
            self.assertEqual(result.returncode, 0 if status == 0 else 1, result.stderr)
            self.assertEqual('SETUP_CONTINUES' in result.stdout, status == 0)
            self.assertEqual('cannot confirm transmitter readiness' in result.stdout, status != 0)
            self.assertLess(result.stdout.index('Activate neutral'), result.stdout.index('Restore configured'))

    def test_runtime_readiness_summary_follows_execution_loop(self):
        source = (ROOT / "scripts/install.sh").read_text()
        validator_start = source.index("validate_wsprrypi_runtime() {")
        validator_end = source.index("\n}\n", validator_start)
        validator = source[validator_start:validator_end]
        self.assertNotIn('logI "WsprryPi runtime readiness verified."', validator)

        manager_start = source.index("manage_wsprry_pi() {")
        manager_end = source.index("\n}\n", manager_start)
        manager = source[manager_start:manager_end]
        execution_loop = manager.index('for func in "${group_to_execute[@]}"; do')
        loop_end = manager.index("\n    done", execution_loop)
        summary = manager.index('logI "WsprryPi runtime readiness verified."')
        reboot_notice = manager.index('flag_need_reboot "$debug"')
        self.assertIn('"$DRY_RUN" != "true"', manager[loop_end:summary])
        self.assertLess(loop_end, summary)
        self.assertLess(summary, reboot_notice)

    def test_helper_invocations_use_exec_command_and_propagate_debug(self):
        source = (ROOT / "scripts/install.sh").read_text()
        self.assertNotRegex(source, r'if ! python3 "\$RP1_GPCLK_DKMS_HELPER"')
        self.assertEqual(source.count('exec_command "Resolve RP1-GPCLK-DKMS installation plan"'), 1)
        self.assertEqual(source.count('exec_command "Apply RP1-GPCLK-DKMS installation plan"'), 1)
        self.assertEqual(source.count('exec_command "Prepare neutral RP1 runtime for application update"'), 1)
        self.assertEqual(source.count('exec_command "Activate neutral RP1-GPCLK-DKMS administration"'), 1)
        self.assertIn('EXEC_COMMAND_STATUS_MODE=info', source)
        self.assertGreaterEqual(source.count('args+=(--debug)'), 4)
        self.assertIn('runtime-update-output.log', source)
        self.assertIn('runtime-removal-output.log', source)
        self.assertIn('activation-output.log', source)

    def test_explicit_true_reaches_planner_without_raspberry_pi_identity(self):
        source = (ROOT / "scripts/install.sh").read_text()

        def function_body(name):
            start = source.index(f"{name}() {{")
            return source[start:source.index("\n}\n", start) + 3]

        for name in ("validate_sys_accs", "print_model_name", "check_arch"):
            body = function_body(name)
            self.assertIn('INSTALL_RP1_GPCLK_DKMS" == "true"', body)
            self.assertIn("explicitly requested", body)
        self.assertIn('"$file" == "/proc/device-tree/compatible"', function_body("validate_sys_accs"))

    def test_temporary_cleanup_is_scoped_and_uninstall_uses_owned_provider_step(self):
        source = (ROOT / "scripts/install.sh").read_text()
        cleanup_start = source.index("cleanup_rp1_gpclk_dkms_state() {")
        cleanup = source[cleanup_start:source.index("\n}\n", cleanup_start) + 3]
        self.assertIn("/tmp/wsprrypi-rp1-gpclk-dkms.*", cleanup)
        self.assertIn("Refusing to remove unexpected", cleanup)
        self.assertIn('"prepare_owned_rp1_gpclk_runtime_removal"\n            "remove_owned_rp1_gpclk_dkms_provider"', source)

    def run_uninstall_orchestration(self, failing_step="", provider_unproven=False):
        install_script = ROOT / "scripts" / "install.sh"
        capture = self.root / f"uninstall-{failing_step or 'success'}.txt"
        shell = r'''
source "$INSTALL_SCRIPT"
ACTION=uninstall
DRY_RUN=false
NO_WEB=false

record_step() {
    printf '%s\n' "$1" >>"$CAPTURE"
    [[ "$1" != "$FAILING_STEP" ]]
}
manage_apache() { record_step manage_apache; }
manage_web() { record_step manage_web; }
manage_service() { record_step manage_service; }
manage_i2c() { record_step manage_i2c; }
manage_config() { record_step manage_config; }
manage_route_application() { record_step manage_route_application; }
manage_support_bundle_runtime() { record_step manage_support_bundle_runtime; }
manage_exe() { record_step manage_exe; }
manage_sound() { record_step manage_sound; }
prepare_owned_rp1_gpclk_runtime_removal() { record_step prepare_owned_rp1_gpclk_runtime_removal; }
remove_owned_rp1_gpclk_dkms_provider() {
    record_step remove_owned_rp1_gpclk_dkms_provider || return $?
    if [[ "$PROVIDER_UNPROVEN" == "true" ]]; then
        RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN=true
    fi
}
flag_need_reboot() { record_step flag_need_reboot; }
eval "$(declare -f finish_script | sed '1s/finish_script/original_finish_script/')"
finish_script() {
    printf 'finish_status=%s\n' "$1" >>"$CAPTURE"
    original_finish_script "$@"
}
logE() { :; }
warn() { printf 'warning=%s\n' "$*" >>"$CAPTURE"; }

status=0
manage_wsprry_pi || status=$?
printf 'return_status=%s\n' "$status" >>"$CAPTURE"
'''
        environment = os.environ.copy()
        environment.update({
            "INSTALL_SCRIPT": str(install_script),
            "CAPTURE": str(capture),
            "FAILING_STEP": failing_step,
            "PROVIDER_UNPROVEN": "true" if provider_unproven else "false",
        })
        result = subprocess.run(
            ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=environment, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return capture.read_text().splitlines(), result.stdout

    def test_provider_removal_precedes_teardown_and_later_failures_fail_uninstall(self):
        expected_teardown = {
            "manage_apache", "manage_web", "manage_service", "manage_i2c",
            "manage_config", "manage_route_application",
            "manage_support_bundle_runtime", "manage_exe", "manage_sound",
            "flag_need_reboot", "prepare_owned_rp1_gpclk_runtime_removal",
        }
        for failing_step in ("manage_service", "manage_route_application", "manage_exe"):
            with self.subTest(failing_step=failing_step):
                lines, stdout = self.run_uninstall_orchestration(failing_step)
                self.assertTrue(expected_teardown.issubset(lines))
                self.assertIn("remove_owned_rp1_gpclk_dkms_provider", lines)
                self.assertLess(
                    lines.index("remove_owned_rp1_gpclk_dkms_provider"),
                    lines.index(failing_step),
                )
                self.assertIn("finish_status=1", lines)
                self.assertIn("return_status=1", lines)
                self.assertNotIn("Uninstallation successful", stdout)
                self.assertNotIn("has been uninstalled", stdout)

    def test_successful_teardown_removes_owned_provider_before_application(self):
        lines, stdout = self.run_uninstall_orchestration()
        provider = lines.index("remove_owned_rp1_gpclk_dkms_provider")
        self.assertGreater(provider, lines.index("prepare_owned_rp1_gpclk_runtime_removal"))
        self.assertLess(provider, lines.index("manage_service"))
        self.assertLess(provider, lines.index("manage_exe"))
        self.assertIn("finish_status=0", lines)
        self.assertIn("return_status=0", lines)
        self.assertIn("Uninstallation successful", stdout)
        self.assertNotIn("RP1-GPCLK-DKMS provider was not removed", stdout)

    def test_successful_uninstall_reports_unproven_provider_preservation(self):
        lines, stdout = self.run_uninstall_orchestration(provider_unproven=True)
        self.assertIn("finish_status=0", lines)
        self.assertIn(
            "The RP1-GPCLK-DKMS provider was not removed, as I was unable to prove\n"
            "WsprryPi's secure ownership of the module.",
            stdout,
        )

    def test_provider_helper_output_drives_only_unproven_summary_state(self):
        install_script = ROOT / "scripts" / "install.sh"
        shell = r'''
source "$INSTALL_SCRIPT"
ACTION=uninstall
DRY_RUN=false
REMOVE_RP1_GPCLK_DKMS=auto
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
python3() {
    printf '%s\n' "$HELPER_OUTPUT"
}
remove_owned_rp1_gpclk_dkms_provider
printf '%s\n' "$RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN"
'''
        outcomes = {
            "WsprryPi-owned RP1-GPCLK-DKMS provider removed and absence verified.": "false",
            "RP1-GPCLK-DKMS absent: no provider removal was required.": "false",
            "RP1-GPCLK-DKMS preserved: WsprryPi ownership is unproven.": "true",
        }
        for helper_output, expected in outcomes.items():
            with self.subTest(helper_output=helper_output):
                environment = os.environ.copy()
                environment.update({
                    "INSTALL_SCRIPT": str(install_script),
                    "HELPER_OUTPUT": helper_output,
                })
                result = subprocess.run(
                    ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, env=environment, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip().splitlines()[-1], expected)

    def test_owned_provider_removal_failure_fails_uninstall(self):
        lines, stdout = self.run_uninstall_orchestration("remove_owned_rp1_gpclk_dkms_provider")
        self.assertIn("remove_owned_rp1_gpclk_dkms_provider", lines)
        self.assertNotIn("manage_service", lines)
        self.assertNotIn("manage_exe", lines)
        self.assertIn("finish_status=1", lines)
        self.assertIn("return_status=1", lines)
        self.assertNotIn("Uninstallation successful", stdout)
        self.assertNotIn("has been uninstalled", stdout)

    def test_runtime_recovery_failure_stops_before_application_teardown(self):
        lines, stdout = self.run_uninstall_orchestration(
            "prepare_owned_rp1_gpclk_runtime_removal"
        )
        self.assertIn("prepare_owned_rp1_gpclk_runtime_removal", lines)
        self.assertNotIn("manage_service", lines)
        self.assertNotIn("manage_exe", lines)
        self.assertNotIn("remove_owned_rp1_gpclk_dkms_provider", lines)
        self.assertIn("finish_status=1", lines)
        self.assertIn("return_status=1", lines)
        self.assertNotIn("Uninstallation successful", stdout)


class RemovalPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.record = self.root / "ownership.json"

    def release_record(self) -> dict[str, object]:
        return {
            "schema": MOD.RECORD_SCHEMA, "repository": MOD.REPOSITORY,
            "owner": "WsprryPi", "channel": "release",
            "installationMethod": "debian-package", "tag": "v2.1.0",
            "sourceCommit": "a" * 40, "manifest": valid_manifest(),
            "moduleArtifacts": [{
                "path": "/lib/modules/fixture/updates/dkms/rp1_gpclk_dkms.ko",
                "sha256": "c" * 64,
            }],
            "routeActivation": "disabled", "output": "disabled",
            "qualificationClaim": False,
        }

    def development_record(self) -> dict[str, object]:
        return {
            "schema": MOD.RECORD_SCHEMA, "repository": MOD.REPOSITORY,
            "owner": "WsprryPi", "channel": "development",
            "installationMethod": "upstream-development-rollback",
            "sourceCommit": "a" * 40, "productVersion": "0.9.0",
            "sourceTree": "b" * 40, "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.COMPATIBILITY_IDENTITY,
            "upstreamEvidence": "/var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence",
            "rollbackRecord": "/var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence/ROLLBACK.json",
            "rollbackRecordSha256": "e" * 64,
            "rollbackEntrypoint": "/var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence/rendered-source/scripts/development-rollback",
            "rollbackEntrypointSha256": "f" * 64,
            "installedModulePath": f"/lib/modules/{MOD.platform.release()}/updates/dkms/rp1_gpclk_dkms.ko",
            "installedModuleSha256": "1" * 64,
            "decompressedModuleSha256": "2" * 64,
            "routeActivation": "disabled", "output": "disabled",
            "qualificationClaim": False,
        }

    def write_record(self, value: dict[str, object]) -> None:
        self.record.write_text(json.dumps(value))
        self.record.chmod(0o600)

    def args(self):
        return MOD.parser().parse_args(["remove", "--record", str(self.record), "--root", str(self.root)])

    def test_untrusted_records_report_absence_when_no_provider_exists(self):
        runner = FakeRunner({
            ("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME):
                MOD.CommandResult("", "", 1),
        })
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertIn("absent", stdout.getvalue())

        for value in (
            {"schema": MOD.LEGACY_RECORD_SCHEMA},
            {"schema": MOD.RECORD_SCHEMA, "owner": "attacker"},
            {**self.release_record(), "tag": 210},
        ):
            self.write_record(value)
            stdout = io.StringIO()
            with mock.patch.object(MOD.sys, "stdout", stdout):
                MOD.remove_owned_provider(self.args(), runner)
            self.assertIn("absent", stdout.getvalue())
            self.assertTrue(self.record.exists())
            self.record.unlink()

        target = self.root / "target.json"
        target.write_text(json.dumps(self.release_record()))
        self.record.symlink_to(target)
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertIn("absent", stdout.getvalue())
        self.assertTrue(target.exists())

    def test_missing_ownership_preserves_an_existing_provider(self):
        source = self.root / "usr/src/rp1-gpclk-dkms-0.9.0"
        source.mkdir(parents=True)
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), FakeRunner())
        self.assertIn("preserved", stdout.getvalue())
        self.assertTrue(source.exists())

    def test_explicit_opt_out_preserves_even_a_valid_owned_provider(self):
        self.write_record(self.release_record())
        args = MOD.parser().parse_args([
            "remove", "--remove", "false", "--record", str(self.record),
            "--root", str(self.root),
        ])
        runner = FakeRunner()
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(args, runner)
        self.assertIn("explicit operator opt-out", stdout.getvalue())
        self.assertTrue(self.record.exists())
        self.assertEqual(runner.calls, [])

    def test_owned_release_uses_package_removal_then_deletes_record(self):
        self.write_record(self.release_record())
        runner = FakeRunner()
        with mock.patch.object(MOD, "validate_release_removal"), \
             mock.patch.object(MOD, "verify_provider_absent"):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertEqual(runner.passthrough_calls, [("apt-get", "remove", "-y", MOD.PACKAGE_NAME)])
        self.assertFalse(self.record.exists())

    def test_identity_drift_preserves_record_without_package_mutation(self):
        self.write_record(self.release_record())
        runner = FakeRunner()
        stdout = io.StringIO()
        with mock.patch.object(MOD, "validate_release_removal", side_effect=MOD.ContractError("artifact drift")), \
             mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertIn("artifact drift", stdout.getvalue())
        self.assertEqual(runner.calls, [])
        self.assertTrue(self.record.exists())

    def test_release_allows_only_same_version_additional_kernel_artifacts(self):
        record = self.release_record()
        artifacts = [
            *record["moduleArtifacts"],
            {
                "path": "/lib/modules/new-kernel/updates/dkms/rp1_gpclk_dkms.ko.xz",
                "sha256": "d" * 64,
            },
        ]
        responses = {
            ("modinfo", "-k", "fixture", "-F", "version", MOD.MODULE_NAME): MOD.CommandResult("2.1.0\n", "", 0),
            ("modinfo", "-k", "new-kernel", "-F", "version", MOD.MODULE_NAME): MOD.CommandResult("2.1.0\n", "", 0),
        }
        runner = FakeRunner(responses)
        with mock.patch.object(MOD, "verify_installed_release"), \
             mock.patch.object(MOD, "module_artifacts", return_value=artifacts):
            MOD.validate_release_removal(record, self.root, runner)
        runner.responses[("modinfo", "-k", "new-kernel", "-F", "version", MOD.MODULE_NAME)] = MOD.CommandResult("9.9.9\n", "", 0)
        with mock.patch.object(MOD, "verify_installed_release"), \
             mock.patch.object(MOD, "module_artifacts", return_value=artifacts):
            with self.assertRaisesRegex(MOD.ContractError, "additional-kernel"):
                MOD.validate_release_removal(record, self.root, runner)

    def test_package_removal_failure_retains_record_and_fails(self):
        self.write_record(self.release_record())
        failed = MOD.CommandResult("", "package busy", 1)
        runner = FakeRunner({("apt-get", "remove", "-y", MOD.PACKAGE_NAME): failed})
        with mock.patch.object(MOD, "validate_release_removal"):
            with self.assertRaises(MOD.ContractError):
                MOD.remove_owned_provider(self.args(), runner)
        self.assertTrue(self.record.exists())

    def test_record_change_after_preflight_blocks_package_mutation(self):
        self.write_record(self.release_record())
        runner = FakeRunner()

        def change_record(*_):
            changed = self.release_record()
            changed["sourceCommit"] = "b" * 40
            self.write_record(changed)

        with mock.patch.object(MOD, "validate_release_removal", side_effect=change_record):
            with self.assertRaisesRegex(MOD.ContractError, "ownership changed"):
                MOD.remove_owned_provider(self.args(), runner)
        self.assertEqual(runner.calls, [])
        self.assertTrue(self.record.exists())

    def test_owned_development_uses_captured_upstream_rollback(self):
        self.write_record(self.development_record())
        evidence = self.root / "evidence/rendered-source/scripts"
        evidence.mkdir(parents=True)
        entrypoint = evidence / "development-rollback"
        entrypoint.write_text("#!/bin/sh\n")
        entrypoint.chmod(0o700)
        rollback = self.root / "evidence/ROLLBACK.json"
        rollback.write_text("{}")
        rollback.chmod(0o600)
        runner = FakeRunner()
        with mock.patch.object(MOD, "validate_development_removal", return_value=(entrypoint, rollback)), \
             mock.patch.object(MOD, "verify_provider_absent"):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertEqual(runner.passthrough_calls, [(str(entrypoint), "--record", str(rollback))])
        self.assertFalse(self.record.exists())

    def test_owned_runtime_uninstall_recovers_before_dkms_rollback(self):
        record = self.development_record()
        record["schema"] = MOD.RUNTIME_RECORD_SCHEMA
        record["runtime"] = {
            "readinessContract": MOD.RUNTIME_READINESS_CONTRACT,
            "bindingSha256": "3" * 64, "artifactSetSha256": "4" * 64,
            "sourceCommit": record["sourceCommit"],
            "productVersion": record["productVersion"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentities": {"gpio4": "gpio4-id", "gpio20": "gpio20-id"},
            "deploymentPlanSha256": "5" * 64, "activationPlanSha256": "6" * 64,
            "activationRequestId": "request-1234", "controllerSession": 1,
            "controllerGeneration": 0, "state": "neutral_ready", "route": None,
            "output": "disabled",
        }
        self.write_record(record)
        args = MOD.parser().parse_args(["remove", "--record", str(self.record)])
        entrypoint = self.root / "evidence/rendered-source/scripts/development-rollback"
        entrypoint.parent.mkdir(parents=True)
        entrypoint.write_text("#!/bin/sh\n")
        entrypoint.chmod(0o700)
        rollback = self.root / "evidence/ROLLBACK.json"
        rollback.parent.mkdir(parents=True, exist_ok=True)
        rollback.write_text("{}")
        rollback.chmod(0o600)
        events = []
        runner = FakeRunner()
        with mock.patch.object(MOD, "existing_inventory",
                               return_value={"runtimeResidue": ["owned"]}), \
             mock.patch.object(MOD, "validate_development_rollback_authority",
                               side_effect=lambda *_: events.append("authority")), \
             mock.patch.object(MOD, "recover_and_remove_owned_runtime",
                               side_effect=lambda *_: events.append("runtime-remove")), \
             mock.patch.object(MOD, "validate_development_removal",
                               side_effect=lambda *_: (events.append("provider-validate")
                                                       or (entrypoint, rollback))), \
             mock.patch.object(MOD, "verify_provider_absent"):
            MOD.remove_owned_provider(args, runner)
        self.assertEqual(events, ["authority", "runtime-remove", "provider-validate"])
        self.assertEqual(runner.passthrough_calls,
                         [(str(entrypoint), "--record", str(rollback))])
        self.assertFalse(self.record.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
