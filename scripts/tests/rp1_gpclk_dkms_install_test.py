#!/usr/bin/env python3
"""Offline, unprivileged contract tests for RP1 provider installation."""

from __future__ import annotations

import copy
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
from fixture_commands import Command, StrictCommands
from fixture_filesystem import temporary_root


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "rp1_gpclk_dkms_install.py"
SPEC = importlib.util.spec_from_file_location("rp1_installer", SCRIPT)
assert SPEC and SPEC.loader
MOD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inventory_for_uapi(content: bytes = b"uapi\n", version: str = "2.1.0") -> list[dict[str, object]]:
    return [{
        "path": f"usr/src/rp1-gpclk-dkms-{version}/include/uapi/linux/rp1_gpclk.h",
        "type": "file", "mode": "0644", "sha256": digest(content),
    }]


def valid_manifest(version: str = "2.1.0", commit: str = "a" * 40) -> dict[str, object]:
    inventory = inventory_for_uapi(version=version)
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


class RunnerFixtures:
    def setUp(self):
        super().setUp()
        # Historical fixtures explicitly select their recorded authority.
        self.addCleanup(MOD.configure_provider_repository, MOD.REPOSITORY)
        MOD.configure_provider_repository(MOD.LEGACY_REPOSITORY)

    def runner(self, responses=None, *, steps=()):
        runner = StrictCommands({key: value for key, value in (responses or {}).items()
                                 if (key.argv if isinstance(key, Command) else key) not in {step.argv for step in steps}},
            steps=steps, responses={(key.argv if isinstance(key, Command) else key): value
                                   for key, value in (responses or {}).items()
                                   if (key.argv if isinstance(key, Command) else key) in {step.argv for step in steps}},
            result=lambda: MOD.CommandResult("", "", 0), error=MOD.ContractError)
        self.addCleanup(runner.assert_complete)
        return runner


class PlatformAndSelectionTests(RunnerFixtures, unittest.TestCase):
    def test_strict_fixture_cannot_hide_missing_mutations_or_caught_refusals(self):
        operation = Command(("apt-get", "remove", "-y", MOD.PACKAGE_NAME), passthrough=True, stage="removal")
        runner = StrictCommands(steps=[operation])
        with self.assertRaisesRegex(AssertionError, "required fixture commands"):
            runner.assert_complete()
        with self.assertRaisesRegex(AssertionError, "unexpected fixture command"):
            runner.run(["reboot"])
        with self.assertRaisesRegex(AssertionError, "caught by production"):
            runner.assert_complete()
        with self.assertRaises(ValueError):
            StrictCommands({operation.argv: MOD.CommandResult("", "", 0)}, steps=[operation])
        with self.assertRaises(TypeError):
            StrictCommands(steps=[operation], responses={operation: MOD.CommandResult("", "refused", 19)})
        runner = StrictCommands(steps=[operation], responses={operation.argv: MOD.CommandResult("", "refused", 19)}, error=MOD.ContractError)
        with self.assertRaisesRegex(MOD.ContractError, "refused"):
            runner.run(operation.argv, passthrough=True)
        runner.assert_complete()

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
            self.assertEqual(MOD.source_selector("auto", MOD.wsprry_channel(source))[0], "release")
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
        missing = self.runner({
            ("git", "-C", str(source), "cat-file", "-e",
             f"{MOD.CORRECTED_RUNTIME_COMMIT}^{{commit}}"): MOD.CommandResult("", "missing", 1),
        })
        with self.assertRaisesRegex(MOD.ContractError, "cannot prove the corrected"):
            MOD.require_compatible_runtime_source(source, selected, missing)
        predates = self.runner({("git", "-C", str(source), "cat-file", "-e", f"{MOD.CORRECTED_RUNTIME_COMMIT}^{{commit}}"): MOD.CommandResult("", "", 0), ancestor: MOD.CommandResult("", "", 1)})
        with self.assertRaisesRegex(MOD.ContractError, "predates the corrected"):
            MOD.require_compatible_runtime_source(source, selected, predates)
        compatible = self.runner({("git", "-C", str(source), "cat-file", "-e", f"{MOD.CORRECTED_RUNTIME_COMMIT}^{{commit}}"): MOD.CommandResult("", "", 0), ancestor: MOD.CommandResult("", "", 0)})
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


class MigrationRepositoryTests(RunnerFixtures, unittest.TestCase):
    def setUp(self):
        super().setUp()
        previous = MOD.REPOSITORY
        self.addCleanup(MOD.configure_provider_repository, previous)

    def select_new(self):
        MOD.configure_provider_repository(MOD.MIGRATION_REPOSITORY)

    def test_default_and_explicit_authority_are_separate(self):
        parsed = MOD.parser().parse_args(["remove"])
        self.assertEqual(parsed.provider_repository, MOD.MIGRATION_REPOSITORY)
        self.select_new()
        self.assertEqual(MOD.normalize_repository_url("git@github.com:WsprryPi/RP1-DKMS.git"), MOD.REPOSITORY_URL)
        self.assertNotEqual(MOD.normalize_repository_url("git@github.com:WsprryPi/RP1-GPCLK-DKMS.git"), MOD.REPOSITORY_URL)
        with self.assertRaises(MOD.ContractError): MOD.configure_provider_repository("attacker/provider")

    def test_original_ownership_is_preserved_under_new_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory).resolve()/"ownership.json"
            record = development_record()
            write_record(path, record)
            original = path.read_bytes()
            self.select_new()
            value, identity, reason = MOD.load_ownership_record(path)
            self.assertIsNone(value)
            self.assertIsNone(identity)
            self.assertIn("repository differs", reason)
            self.assertEqual(path.read_bytes(), original)

    def test_current_version_matches_cpp_and_rejects_previous_versions(self):
        source = (ROOT / 'src/WSPR-Transmitter/src/rp1_gpclk_consumer_contract.hpp').read_text()
        self.assertIn('version = "' + MOD.CONSUMER_PROVIDER_VERSION + '"', source)
        self.select_new()
        MOD.validate_consumer_provider({'channel': 'development', 'version': '0.0.1'})
        for previous in ('0.99.0', '1.0.1', '1.0.2'):
            with self.assertRaises(MOD.ContractError):
                MOD.validate_consumer_provider({'channel': 'development', 'version': previous})

    def test_ordinary_releases_and_incomplete_inputs(self):
        self.select_new()
        for value in (None, False, True):
            item = release("v1.0.0")
            if value is None: item.pop("immutable")
            else: item["immutable"] = value
            self.assertTrue(MOD.release_candidate(item))
        for item in (release("v1.0.0", draft=True), release("v1.0.0", prerelease=True),
                     release("v1.0.0", complete=False)):
            self.assertFalse(MOD.release_candidate(item))
        with self.assertRaises(MOD.ContractError): MOD.select_release([])
        duplicate = release("v1.0.0")
        duplicate["assets"].append(duplicate["assets"][0])
        with self.assertRaises(MOD.ContractError): MOD.release_candidate(duplicate)

    def test_contract_replaces_old_ancestry_without_fallback(self):
        self.select_new()
        source = pathlib.Path("/fixture/source"); commit = "a"*40
        command = ("git", "-C", str(source), "show", commit+":release/wsprrypi-consumer-contract.json")
        good = {"providerRepository": MOD.MIGRATION_REPOSITORY,
                "runtimeBinding": MOD.RUNTIME_BINDING_CONTRACT,
                "runtimeReadiness": MOD.RUNTIME_READINESS_CONTRACT,
                "runtimeUpdate": MOD.RUNTIME_UPDATE_CONTRACT}
        runner = self.runner({command: MOD.CommandResult(json.dumps(good), "", 0)})
        MOD.require_compatible_runtime_source(source, commit, runner)
        for field in good:
            changed = dict(good); changed[field] = "wrong"
            runner = self.runner({command: MOD.CommandResult(json.dumps(changed), "", 0)})
            with self.assertRaises(MOD.ContractError): MOD.require_compatible_runtime_source(source, commit, runner)
        for raw in ("[]", "invalid"):
            runner = self.runner({command: MOD.CommandResult(raw, "", 0)})
            with self.assertRaises(MOD.ContractError): MOD.require_compatible_runtime_source(source, commit, runner)
        duplicate = json.dumps(good)[:-1] + ', "runtimeBinding": "' + MOD.RUNTIME_BINDING_CONTRACT + '"}'
        runner = self.runner({command: MOD.CommandResult(duplicate, "", 0)})
        with self.assertRaisesRegex(MOD.ContractError, "duplicate fields"):
            MOD.require_compatible_runtime_source(source, commit, runner)

    def test_manifest_and_plan_cannot_cross_repository_authority(self):
        self.select_new()
        manifest = valid_manifest()
        MOD.validate_manifest(manifest, tag="v2.1.0", tag_commit="a"*40)
        manifest["repository"] = MOD.LEGACY_REPOSITORY
        with self.assertRaises(MOD.ContractError): MOD.validate_manifest(manifest, tag="v2.1.0", tag_commit="a"*40)
        with tempfile.TemporaryDirectory() as directory:
            state = pathlib.Path(directory)
            plan = {"schema": MOD.STATE_SCHEMA, "providerRepository": MOD.MIGRATION_REPOSITORY}
            (state/"plan.json").write_text(json.dumps(plan))
            self.assertEqual(MOD.load_plan(state), plan)
            MOD.configure_provider_repository(MOD.LEGACY_REPOSITORY)
            with self.assertRaises(MOD.ContractError): MOD.load_plan(state)
            plan.pop("providerRepository"); (state/"plan.json").write_text(json.dumps(plan))
            self.assertEqual(MOD.load_plan(state), plan)


class CurrentDefaultTests(unittest.TestCase):
    def test_operational_cli_refuses_old_repository(self):
        with mock.patch.object(MOD.sys, 'stderr', io.StringIO()), self.assertRaises(SystemExit) as stopped:
            MOD.parser().parse_args(['--provider-repository', MOD.LEGACY_REPOSITORY, 'remove'])
        self.assertEqual(stopped.exception.code, 2)

    def test_default_development_install_resolves_new_repository_without_overrides(self):
        with tempfile.TemporaryDirectory() as raw:
            state = pathlib.Path(raw).resolve()
            state.chmod(0o700)
            selected = {'channel': 'development', 'commit': 'a'*40, 'version': '0.0.1'}
            with mock.patch.object(MOD, 'detect_platform', return_value={'automaticEligible': True}), \
                 mock.patch.object(MOD, 'runtime_residue_inventory', return_value=[]), \
                 mock.patch.object(MOD, 'prepare_development', return_value=selected) as source, \
                 mock.patch.object(MOD, 'download_release') as release_download, \
                 mock.patch.object(MOD, 'render_plan'):
                self.assertEqual(MOD.main(['prepare', '--state-dir', str(state),
                    '--wsprry-source', 'devel', '--install', 'true']), 0)
            source.assert_called_once()
            self.assertEqual(source.call_args.args[1:3], ('devel', None))
            release_download.assert_not_called()
            plan = MOD.load_plan(state)
            self.assertEqual(plan['providerRepository'], MOD.MIGRATION_REPOSITORY)
            self.assertEqual(plan['resolved'], selected)
            self.assertEqual(MOD.REPOSITORY, MOD.MIGRATION_REPOSITORY)
            self.assertEqual(MOD.REPOSITORY_URL, 'https://github.com/WsprryPi/RP1-DKMS.git')
            self.assertEqual(MOD.source_selector('auto', 'production'), ('release', None))

    def test_missing_production_release_does_not_fall_back_to_development(self):
        with tempfile.TemporaryDirectory() as raw:
            state = pathlib.Path(raw).resolve()
            state.chmod(0o700)
            with mock.patch.object(MOD, 'detect_platform', return_value={'automaticEligible': True}), \
                 mock.patch.object(MOD, 'runtime_residue_inventory', return_value=[]), \
                 mock.patch.object(MOD, 'download_release', side_effect=MOD.ContractError('no eligible release')), \
                 mock.patch.object(MOD, 'prepare_development') as source:
                self.assertEqual(MOD.main(['prepare', '--state-dir', str(state),
                    '--wsprry-source', 'main', '--install', 'true']), 1)
            source.assert_not_called()
            self.assertFalse((state/'plan.json').exists())

    def test_old_provider_requires_manual_removal_without_adoption_or_recovery_retention(self):
        with tempfile.TemporaryDirectory() as raw:
            state = pathlib.Path(raw).resolve()
            state.chmod(0o700)
            record = state/'old-ownership.json'
            old = development_record('1.0.1')
            old['repository'] = MOD.LEGACY_REPOSITORY
            write_record(record, old)
            original = record.read_bytes()
            with mock.patch.object(MOD, 'detect_platform', return_value={'automaticEligible': True}), \
                 mock.patch.object(MOD, 'runtime_residue_inventory', return_value=['old runtime']), \
                 mock.patch.object(MOD, 'prepare_development') as source, \
                 mock.patch.object(MOD, 'retain_provider_migration') as retain:
                self.assertEqual(MOD.main(['prepare', '--state-dir', str(state),
                    '--record', str(record), '--wsprry-source', 'devel', '--install', 'true']), 1)
            source.assert_not_called()
            retain.assert_not_called()
            self.assertEqual(record.read_bytes(), original)
            self.assertEqual(set(state.iterdir()), {record})


class ReleaseTests(RunnerFixtures, unittest.TestCase):
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
            uapi = state / "source/include/uapi/linux/rp1_gpclk.h"
            uapi.parent.mkdir(parents=True)
            uapi.write_bytes(b"uapi\n")
            with mock.patch.object(MOD, "validate_package"), \
                 mock.patch.object(MOD, "clone_exact", return_value=checkout) as clone, \
                 mock.patch.object(MOD, "runtime_source_interface") as interface, \
                 mock.patch.object(MOD, "development_identity", return_value={"version": "2.1.0"}), \
                 mock.patch.object(MOD, "runtime_source_identity") as identity:
                resolved = MOD.download_release(state, Client(), self.runner({
                    ("git", "-C", str(state / "source"), "cat-file", "-e", f"{MOD.CORRECTED_RUNTIME_COMMIT}^{{commit}}"): MOD.CommandResult("", "", 0),
                    ("git", "-C", str(state / "source"), "merge-base", "--is-ancestor", MOD.CORRECTED_RUNTIME_COMMIT, "a" * 40): MOD.CommandResult("", "", 0)}))
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


class ArchiveTests(RunnerFixtures, unittest.TestCase):
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


class CheckoutTests(RunnerFixtures, unittest.TestCase):
    def setUp(self):
        super().setUp()
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


class DevelopmentInterfaceTests(RunnerFixtures, unittest.TestCase):
    def lifecycle_source(self, help_text: str) -> pathlib.Path:
        root = pathlib.Path(self.temp.name)
        scripts = root / "scripts"
        scripts.mkdir(exist_ok=True)
        version_source = root / "include/rp1_gpclk/version.h"
        version_source.parent.mkdir(parents=True, exist_ok=True)
        version_source.write_text('#define RP1_GPCLK_MODULE_VERSION "0.0.1"\n')
        installer = scripts / "development-install"
        installer.write_text(f"#!/bin/sh\nprintf '%s\\n' '{help_text}'\n")
        preflight = scripts / "development-preflight"
        identity = {
            "classification": "source-development", "moduleName": MOD.MODULE_NAME,
            "moduleVersion": "0.0.1", "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": digest(version_source.read_bytes()),
        }
        preflight.write_text("#!/bin/sh\nprintf '%s\\n' '" + json.dumps(identity) + "'\n")
        installer.chmod(0o755)
        preflight.chmod(0o755)
        return root

    def setUp(self):
        super().setUp()
        self.addCleanup(MOD.configure_provider_repository, MOD.REPOSITORY)
        MOD.configure_provider_repository(MOD.MIGRATION_REPOSITORY)
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
        self.assertEqual(identity["version"], "0.0.1")
        self.assertEqual(identity["versionSource"], "include/rp1_gpclk/version.h")
        (source / "include/rp1_gpclk/version.h").write_text('#define RP1_GPCLK_MODULE_VERSION "changed"\n')
        with self.assertRaisesRegex(MOD.ContractError, "differs from the selected source"):
            MOD.development_identity(source, MOD.Runner())

    def test_prepared_development_uses_its_actual_version_and_both_routes(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        uapi = source/'include/uapi/linux/rp1_gpclk.h'
        uapi.parent.mkdir(parents=True); uapi.write_bytes(b'current UAPI')
        identity = {'path': str(source), 'commit': 'a'*40, 'tree': 'b'*40}
        with mock.patch.object(MOD, 'clone_exact', return_value=identity), \
             mock.patch.object(MOD, 'require_compatible_runtime_source'), \
             mock.patch.object(MOD, 'runtime_source_interface'), \
             mock.patch.object(MOD, 'runtime_source_identity'):
            result = MOD.prepare_development(source, 'commit', 'a'*40, MOD.Runner())
        self.assertEqual(result['version'], '0.0.1')
        self.assertEqual(json.loads(result['compatibilityIdentity']),
                         {'gpio4': 'v0.0.1-rp1-gpio4', 'gpio20': 'v0.0.1-rp1-gpio20'})

    def test_runtime_source_identity_binds_product_and_both_rp1_routes(self):
        source = pathlib.Path(self.temp.name) / "runtime-source"
        scripts = source / "scripts"
        scripts.mkdir(parents=True)
        (scripts / "runtime_layout.py").write_text(
            '"""Runtime destinations; kernel identity is binding-specific."""\n'
        )
        (scripts / "runtime_binding.py").write_text(
            "CONTRACT = 'rp1-gpclk-runtime-binding-v1'\n"
            "PRODUCT_VERSION = '0.0.1'\n"
            "COMPATIBILITY = {'gpio4': 'v0.0.1-rp1-gpio4', 'gpio20': 'v0.0.1-rp1-gpio20'}\n"
        )
        MOD.runtime_source_identity(source, "0.0.1")
        (scripts / "runtime_binding.py").write_text(
            "CONTRACT = 'rp1-gpclk-runtime-binding-v1'\n"
            "PRODUCT_VERSION = '0.0.1'\n"
            "COMPATIBILITY = {'gpio4': 'v0.0.1-pi5-gpio4', 'gpio20': 'v0.0.1-pi5-gpio20'}\n"
        )
        with self.assertRaisesRegex(MOD.ContractError, "route compatibility identities differ"):
            MOD.runtime_source_identity(source, "0.0.1")
        (scripts / "runtime_binding.py").write_text(
            "CONTRACT = 'rp1-gpclk-runtime-binding-v1'\n"
            "PRODUCT_VERSION = '0.0.1'\n"
            "COMPATIBILITY = {'gpio4': 'v0.0.1-rp1-gpio4', 'gpio20': 'v0.0.1-rp1-gpio20'}\n"
        )
        (scripts / "runtime_layout.py").write_text("KERNEL = 'fixture-kernel'\n")
        with self.assertRaisesRegex(MOD.ContractError, "hard-codes a kernel"):
            MOD.runtime_source_identity(source, "0.0.1")

    def test_development_result_is_bound_before_recording(self):
        manifest_path = pathlib.Path(self.temp.name) / "DEVELOPMENT_MANIFEST.json"
        resolved = {"commit": "a" * 40, "version": "0.0.1", "uapiSha256": "b" * 64,
                    "versionSource": "include/rp1_gpclk/version.h", "versionSourceSha256": "e" * 64}
        manifest = {
            "schema": "rp1-gpclk-source-development-manifest",
            "classification": "source-development", "qualification": False,
            "releaseQualified": False, "sourceCommit": "a" * 40,
            "sourceState": "clean", "renderedVersion": "0.0.1",
            "versionIdentity": {"path": "include/rp1_gpclk/version.h", "sha256": "e" * 64,
                                "moduleVersion": "0.0.1"},
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
                "moduleName": MOD.MODULE_NAME, "moduleVersion": "0.0.1",
                "kernel": MOD.platform.release(), "installedFileSha256": "c" * 64,
                "decompressedElfSha256": "d" * 64,
                "installedPath": f"/lib/modules/{MOD.platform.release()}/updates/dkms/{MOD.MODULE_NAME}.ko",
            },
        }
        manifest["installedModules"] = {
            MOD.MODULE_NAME: manifest["installedModule"],
            MOD.CONTROLLER_MODULE_NAME: {
                "moduleName": MOD.CONTROLLER_MODULE_NAME, "moduleVersion": "0.0.1",
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
            "routeArguments": ["--route-neutral"], "version": "0.0.1",
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
            "version": "0.0.1", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None,
            "dkms": f"{MOD.DKMS_NAME}/0.0.1, {MOD.platform.release()}, arm64: installed",
            "activeModule": False, "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.0.1"],
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
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
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
            "version": "0.0.1", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None,
            "dkms": (
                f"{MOD.DKMS_NAME}/0.0.1, {MOD.platform.release()}, "
                "arm64: installed"
            ),
            "activeModule": False, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.0.1"],
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
            "schema": MOD.RECORD_SCHEMA, "runtime": {}, "channel": "development",
            "sourceCommit": resolved["commit"],
            "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"],
            "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
                MOD.CommandResult(
                    "usage: development-install --route-neutral "
                    "--runtime-controller\n", "", 0,
                ),
            ("modinfo", "-k", MOD.platform.release(), "-F", "version",
             MOD.MODULE_NAME): MOD.CommandResult("0.0.1\n", "", 0),
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
            "version": "0.0.1", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        existing = {
            "packageVersion": None,
            "dkms": (f"{MOD.DKMS_NAME}/0.0.1, {MOD.platform.release()}, "
                     "arm64: installed"),
            "activeModule": False, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.0.1"],
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
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
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
                "gpio4": "v0.0.1-rp1-gpio4",
                "gpio20": "v0.0.1-rp1-gpio20",
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
        self.assertEqual(replace.call_args.args[3]["schema"], MOD.RECORD_SCHEMA)
        self.assertEqual(replace.call_args.args[3]["runtime"], runtime)
        self.assertEqual(runner.passthrough_calls, [])

    def test_neutral_runtime_reuse_still_rejects_transmission_state(self):
        inventory = {
            "packageVersion": None,
            "dkms": (
                f"{MOD.DKMS_NAME}/0.0.1, {MOD.platform.release()}, "
                "arm64: installed"
            ),
            "activeModule": False, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.0.1"],
            "moduleCandidates": ["consumer.ko"],
            "controllerCandidates": ["controller.ko"],
            "installedOverlays": [], "enrollment": False,
            "developmentManager": False,
        }
        runner = self.runner({
            ("modinfo", "-k", MOD.platform.release(), "-F", "version",
             MOD.MODULE_NAME): MOD.CommandResult("0.0.1\n", "", 0),
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
                    {"productVersion": "0.0.1"}, unsafe, runner
                )

    def test_owned_development_identity_drift_is_not_reused(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "commit": "a" * 40,
            "version": "0.0.1", "sourceTree": "b" * 40,
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
            "productVersion": "0.0.1", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
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
            "version": "0.0.1", "sourceTree": "b" * 40,
            "uapiSha256": "c" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "d" * 64,
        }
        before = {
            "packageVersion": None,
            "dkms": f"{MOD.DKMS_NAME}/0.0.1, kernel, arm64: installed",
            "activeModule": True, "activeController": True,
            "configuredRoute": False,
            "sourceTrees": [f"/usr/src/{MOD.PACKAGE_NAME}-0.0.1"],
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
            "schema": MOD.RECORD_SCHEMA, "runtime": {}, "channel": "development",
            "sourceCommit": "e" * 40, "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"], "uapiSha256": resolved["uapiSha256"],
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        rollback = pathlib.Path("/var/lib/wsprrypi/evidence/ROLLBACK.json")
        entrypoint = pathlib.Path("/var/lib/wsprrypi/evidence/rendered-source/scripts/development-rollback")
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
                MOD.CommandResult("usage: development-install --route-neutral --runtime-controller\n", "", 0),
        }, steps=[Command(("/var/lib/wsprrypi/evidence/rendered-source/scripts/development-rollback", "--record", "/var/lib/wsprrypi/evidence/ROLLBACK.json"), cwd="/var/lib/wsprrypi/evidence/rendered-source", passthrough=True, stage="rollback")])
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
            "version": "0.0.1", "sourceTree": "b" * 40,
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
            "schema": MOD.RECORD_SCHEMA, "runtime": {}, "channel": "development",
            "sourceCommit": resolved["commit"],
            "productVersion": resolved["version"],
            "sourceTree": resolved["sourceTree"],
            "uapiSha256": resolved["uapiSha256"],
            "versionSource": resolved["versionSource"],
            "versionSourceSha256": resolved["versionSourceSha256"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
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
            "version": "0.0.1", "sourceTree": "b" * 40,
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
        runner = self.runner({
            Command((str(source / "scripts/development-install"), "--help"), cwd=str(source)):
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
            "version": "0.0.1", "sourceTree": "b" * 40,
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
            "sourceCommit": "e" * 40, "productVersion": "0.0.1",
            "sourceTree": "f" * 40, "uapiSha256": "1" * 64,
            "versionSource": "include/rp1_gpclk/version.h",
            "versionSourceSha256": "2" * 64,
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentity": MOD.development_compatibility("0.0.1"),
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        record = pathlib.Path(self.temp.name) / "record.json"
        rollback = pathlib.Path("/var/lib/wsprrypi/evidence/ROLLBACK.json")
        entrypoint = pathlib.Path("/var/lib/wsprrypi/evidence/rendered-source/scripts/development-rollback")
        runner = self.runner(steps=[Command((str(entrypoint), "--record", str(rollback)), cwd=str(entrypoint.parent.parent), passthrough=True, stage="rollback")])
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
            "version": "0.0.1", "sourceTree": "b" * 40,
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
        owned = {"schema": MOD.RECORD_SCHEMA, "runtime": {}, "channel": "development"}
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
                MOD.apply_development(resolved, record, self.runner())
        self.assertEqual(events, ["verify-absent", "record-remove"])

    def test_runtime_controller_residue_blocks_development_before_mutation(self):
        source = self.lifecycle_source("usage: development-install --route-neutral --runtime-controller")
        resolved = {
            "checkout": {"path": str(source)}, "interface": "route-neutral-flag",
            "routeArguments": ["--route-neutral"], "version": "0.0.1",
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
        runner = self.runner({
            ("dkms", "status", "-m", MOD.DKMS_NAME): MOD.CommandResult("", "", 0),
            ("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME):
                MOD.CommandResult("", "not installed", 1),
        })
        inventory = MOD.existing_inventory(root, runner)
        self.assertEqual(
            inventory["runtimeResidue"],
            sorted((str(inhibitor), str(runtime_library), str(runtime_socket))),
        )
        self.assertEqual(inventory["controllerCandidates"], [str(controller)])


class ApplyPolicyTests(RunnerFixtures, unittest.TestCase):
    def setUp(self):
        super().setUp()
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
                         "version": MOD.CONSUMER_PROVIDER_VERSION},
        })
        return MOD.parser().parse_args([
            "prepare-runtime-update", "--state-dir", str(self.state),
            "--record", str(self.record),
        ])

    def runtime_update_record(self):
        return {
            "schema": MOD.RECORD_SCHEMA, "channel": "development",
            "sourceCommit": "a" * 40, "productVersion": "1.0.0",
            "runtime": {
                "readinessContract": MOD.RUNTIME_READINESS_CONTRACT,
                "bindingSha256": "b" * 64, "artifactSetSha256": "c" * 64,
                "sourceCommit": "a" * 40, "productVersion": "1.0.0",
                "targetKernel": MOD.platform.release(),
                "compatibilityIdentities": {
                    "gpio4": "v1.0.0-rp1-gpio4",
                    "gpio20": "v1.0.0-rp1-gpio20",
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
            MOD.recover_and_remove_owned_runtime(self.record, record, identity, self.runner())
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
                        MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), self.runner())
                self.assertEqual(calls.call_count, 1)
        planned = self.public_update(record)
        bad = self.public_update(record, prepared=True, operation="update-execute", marker=99)
        with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
             mock.patch.object(MOD, "runtime_call", side_effect=[planned, bad]), \
             mock.patch.object(MOD, "require_unchanged_ownership"):
            with self.assertRaisesRegex(MOD.ContractError, "reviewed plan"):
                MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), self.runner())


    def test_update_never_repeats_effects_after_failure_or_no_progress(self):
        record = self.runtime_update_record(); planned = self.public_update(record)
        for replies in ([planned, MOD.ContractError("provider refused")],
                        [planned, self.public_update(record, operation="update-execute"), planned]):
            with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
                 mock.patch.object(MOD, "runtime_call", side_effect=replies) as calls, \
                 mock.patch.object(MOD, "require_unchanged_ownership"):
                with self.assertRaises(MOD.ContractError):
                    MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), self.runner())
                self.assertEqual(sum(call.args[2] == "update-execute" for call in calls.call_args_list), 1)


    def test_update_blocks_changed_application_ownership_before_execution(self):
        record = self.runtime_update_record()
        with mock.patch.object(MOD, "runtime_update_provider", return_value=pathlib.Path("/provider")), \
             mock.patch.object(MOD, "runtime_call", return_value=self.public_update(record)) as calls, \
             mock.patch.object(MOD, "require_unchanged_ownership", side_effect=[None, MOD.ContractError("changed")]):
            with self.assertRaisesRegex(MOD.ContractError, "changed"):
                MOD.recover_and_remove_owned_runtime(self.record, record, mock.Mock(), self.runner())
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
            runner = self.runner({("python3", str(installed), "capabilities"): old,
                                 ("python3", str(candidate), "capabilities"): supported})
            self.assertEqual(MOD.runtime_update_provider(runner, candidate), candidate)
            with self.assertRaisesRegex(MOD.ContractError, "exact compatible provider source"):
                MOD.runtime_update_provider(self.runner({("python3", str(installed), "capabilities"): old}))
            runner = self.runner({("python3", str(installed), "capabilities"): supported})
            self.assertEqual(MOD.runtime_update_provider(runner, candidate), installed)
            self.assertEqual(len(runner.calls), 1)


    def test_release_receipt_does_not_require_development_rollback_metadata(self):
        record = self.runtime_update_record(); record['channel'] = 'release'
        record['manifest'] = {'productVersion': '1.0.0'}
        value = {'schemaVersion': 1, 'contract': MOD.RUNTIME_UPDATE_CONTRACT,
                 'operation': 'installation-status', 'status': 'neutral_ready',
                 'receipt': record['runtime']}
        with mock.patch.object(MOD, 'runtime_call', return_value=value), \
             mock.patch.object(MOD, 'validate_development_rollback_authority') as rollback:
            self.assertEqual(MOD.validate_resumable_neutral_activation(record, {}, self.runner()), record['runtime'])
        rollback.assert_not_called()

    def test_source_preflight_rejects_unsupported_candidate_before_effects(self):
        candidate = self.root / "scripts/runtime_provider.py"
        runner = self.runner({("python3", str(candidate), "capabilities"):
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
                 mock.patch.object(MOD, "validate_candidate_before_update"), \
                 mock.patch.object(MOD, "retain_provider_migration"), \
                 mock.patch.object(MOD, "validate_development_rollback_authority"), \
                 mock.patch.object(MOD, "replace_owned_record") as replace:
                MOD.prepare_runtime_update(args, self.runner())
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
                    self.runtime_update_record(), mock.Mock(), self.runner())
        effects.assert_not_called()

    def test_runtime_call_reports_provider_failure_detail(self):
        provider = self.root / "runtime_provider.py"
        response = {
            "result": "conflict",
            "error": {"detail": "application restoration verification failed"},
            "conflicts": ["application-not-neutral"],
            "remediation": ["inspect wsprrypi.service"],
        }
        runner = self.runner({
            ("python3", str(provider), "remove"):
                MOD.CommandResult(json.dumps(response), "", 12),
        }, steps=[Command(("python3", str(provider), "remove"), stage="runtime removal")])
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
                MOD.prepare_runtime_update(args, self.runner())
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
        runner = self.runner(self.responses())
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "foreign or mixed"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))

    def test_different_installed_version_requires_owner_migration(self):
        runner = self.runner(self.responses("9.9.9-1"))
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "cleanup-first migration"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))

    def test_active_module_and_development_manager_refused(self):
        (self.root / "proc/modules").write_text(f"{MOD.MODULE_NAME} 1 0 - Live 0x0\n")
        runner = self.runner(self.responses())
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
        responses[("modinfo", "-k", MOD.platform.release(), "-F", "version", MOD.CONTROLLER_MODULE_NAME)] = MOD.CommandResult("2.1.0\n", "", 0)
        for name in (MOD.MODULE_NAME, MOD.CONTROLLER_MODULE_NAME):
            module = self.root / f"lib/modules/fixture/updates/dkms/{name}.ko"
            module.parent.mkdir(parents=True, exist_ok=True)
            module.write_bytes(name.encode())
        runner = self.runner(responses)
        with mock.patch.object(MOD, "validate_package"):
            MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))
        self.assertFalse(self.record.exists(), "pre-existing exact providers must not become WsprryPi-owned")

        (self.root / "proc/modules").write_text(f"{MOD.CONTROLLER_MODULE_NAME} 1 0 - Live 0x0\n")
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "module/controller"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        owned = {"schema": MOD.RECORD_SCHEMA, "runtime": {}, "channel": "release",
                 "sourceCommit": self.resolved["commit"], "tag": self.resolved["tag"],
                 "manifest": self.manifest}
        with mock.patch.object(MOD, "validate_package"), \
             mock.patch.object(MOD, "load_ownership_record", return_value=(owned, mock.Mock(), None)), \
             mock.patch.object(MOD, "validate_resumable_neutral_activation") as receipt:
            MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
            receipt.assert_called_once()
        with mock.patch.object(MOD, "validate_package"), \
             mock.patch.object(MOD, "load_ownership_record", return_value=(owned, mock.Mock(), None)), \
             mock.patch.object(MOD, "validate_resumable_neutral_activation", side_effect=MOD.ContractError("neutral receipt mismatch")):
            with self.assertRaisesRegex(MOD.ContractError, "neutral receipt mismatch"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)
        self.assertFalse(any(call and call[0] == "apt-get" for call in runner.calls))

    def test_exact_installation_rejects_foreign_same_version_state(self):
        member = self.manifest["packageInventory"][0]
        installed = self.root / member["path"]
        installed.parent.mkdir(parents=True)
        installed.write_bytes(b"uapi\n")
        (self.root / "usr/src/rp1-gpclk-dkms-foreign").mkdir()
        runner = self.runner(self.responses(
            "2.1.0-1",
            "rp1-gpclk-dkms/2.1.0, fixture-kernel, arm64: installed\n",
        ))
        with mock.patch.object(MOD, "validate_package"):
            with self.assertRaisesRegex(MOD.ContractError, "source destinations"):
                MOD.apply_release(self.state, self.resolved, self.record, self.root, runner)

    def test_fresh_release_uses_normal_debian_path(self):
        runner = self.runner(self.responses(), steps=[Command(("apt-get", "install", "--no-install-recommends", "-y", str(self.package)), passthrough=True, stage="installation")])
        artifacts = [{
            "path": f"/lib/modules/fixture/updates/dkms/{name}.ko",
            "sha256": digest(b"module"),
        } for name in (MOD.MODULE_NAME, MOD.CONTROLLER_MODULE_NAME)]
        with mock.patch.object(MOD, "validate_package"), \
             mock.patch.object(MOD, "verify_installed_release"), \
             mock.patch.object(MOD, "module_artifacts", return_value=artifacts), \
             mock.patch.object(MOD, "package_preflight") as preflight:
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
        plan = MOD.prepare(args, self.runner())
        self.assertTrue(plan["dryRun"])
        apply_args = MOD.parser().parse_args([
            "apply", "--state-dir", str(self.state), "--record", str(self.record), "--root", str(self.root),
        ])
        with self.assertRaisesRegex(MOD.ContractError, "dry-run"):
            MOD.apply(apply_args, self.runner())

    def test_prepare_rejects_runtime_residue_before_source_resolution(self):
        model = self.root / "model"
        compatible = self.root / "compatible"
        model.write_bytes(b"Raspberry Pi 5 Model B Rev 1.0\0")
        compatible.write_bytes(b"raspberrypi,5-model-b\0brcm,bcm2712\0")
        args = MOD.parser().parse_args([
            "prepare", "--state-dir", str(self.state), "--record", str(self.record), "--install", "auto",
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
                MOD.prepare(args, self.runner())
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
                    "version": MOD.CONSUMER_PROVIDER_VERSION, "sourceTree": "b" * 40,
                    "uapiSha256": "c" * 64}
        with mock.patch.object(
                MOD, "runtime_residue_inventory",
                return_value=["/var/lib/rp1-gpclk-dkms/runtime-admin"]), \
             mock.patch.object(MOD, "load_ownership_record",
                               return_value=(owned, mock.sentinel.identity, None)), \
             mock.patch.object(MOD, "prepare_development",
                               return_value=resolved) as resolve:
            plan = MOD.prepare(args, self.runner())
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
            "runtime_manager.py", "package_preflight.py",
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
            "schemaVersion": 1, "contract": MOD.RUNTIME_BINDING_CONTRACT,
            "productVersion": "1.0.0",
            "compatibilityIdentities": {
                "gpio4": "v1.0.0-rp1-gpio4", "gpio20": "v1.0.0-rp1-gpio20"},
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
                "version": "1.0.0", "kernel": kernel,
            }
        binding["modules"] = modules
        binding["artifactSetSha256"] = digest(MOD.canonical(binding))
        (bundle / "binding.json").write_text(json.dumps(binding))
        resolved = {"channel": "development", "commit": "a" * 40, "version": "1.0.0"}
        with mock.patch.object(MOD.platform, "release", return_value=kernel):
            result = MOD.validate_runtime_bundle(bundle, resolved, companion, self.root)
        self.assertEqual(result["artifactSetSha256"], binding["artifactSetSha256"])

        legacy_binding = dict(binding)
        legacy_binding["compatibilityIdentities"] = {
            "gpio4": "v1.0.0-pi5-gpio4", "gpio20": "v1.0.0-pi5-gpio20",
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
        resolved = {"channel": "development", "commit": "a" * 40, "version": "1.0.0"}
        MOD.atomic_json(self.state / "plan.json", {
            "schema": MOD.STATE_SCHEMA, "dryRun": False,
            "decision": {"install": True}, "resolved": resolved,
        })
        owned = {
            "schema": MOD.RECORD_SCHEMA, "channel": "development",
            "sourceCommit": "a" * 40, "productVersion": "1.0.0",
        }
        identity = mock.Mock(st_dev=1, st_ino=2)
        binding = {
            "artifactSetSha256": "b" * 64,
            "compatibilityIdentities": {
                "gpio4": "v1.0.0-rp1-gpio4", "gpio20": "v1.0.0-rp1-gpio20",
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
            MOD.activate_runtime(args, self.runner())
        self.assertEqual(calls.call_count, 7)
        self.assertEqual(captured["schema"], MOD.RECORD_SCHEMA)
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
        for selector, branch, repository in (
            ("release", "main", MOD.MIGRATION_REPOSITORY),
            ("devel", "codex/issue-412-test", MOD.MIGRATION_REPOSITORY),
            ("commit:" + "a"*40, "devel", MOD.MIGRATION_REPOSITORY),
        ):
            sentinel = self.root / f"{selector}.invoked"
            environment = os.environ.copy()
            environment.update({
                "INSTALL_SCRIPT": str(install_script),
                "SENTINEL": str(sentinel),
                "SOURCE_SELECTOR": selector,
                "RP1_GPCLK_DKMS_REPOSITORY": repository,
                "WSPRRYPi_BRANCH": branch,
            })
            result = subprocess.run(
                ["bash", "-c", shell], text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=environment, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(sentinel.exists())
            self.assertEqual(result.stdout.count("Complete: (dry)"), 6)
            self.assertNotIn("[INFO ]", result.stdout)
            self.assertNotIn("Resolve RP1-DKMS installation plan", result.stdout)
            self.assertIn(
                "Complete: (dry) Apply RP1-DKMS installation plan",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Prepare neutral RP1 runtime for application update",
                result.stdout,
            )
            self.assertIn(
                "Complete: (dry) Activate neutral RP1-DKMS administration",
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
            self.assertIn("Name:    Resolve RP1-DKMS installation plan", result.stderr)
            self.assertIn("Name:    Apply RP1-DKMS installation plan", result.stderr)
            helper = install_script.parent / "rp1_gpclk_dkms_install.py"
            prepare_command = (
                f"Command: python3 {helper} --provider-repository {repository} prepare --state-dir dry-run:no-state-created "
                f"--install true --source {selector} --wsprry-source {branch} --dry-run --debug"
            )
            apply_command = (
                f"Command: python3 {helper} --provider-repository {repository} apply --state-dir dry-run:no-state-created --debug"
            )
            update_command = (
                f"Command: python3 {helper} --provider-repository {repository} prepare-runtime-update "
                "--state-dir dry-run:no-state-created --debug"
            )
            activation_command = (
                f"Command: python3 {helper} --provider-repository {repository} activate-runtime "
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
            self.assertNotIn("RP1-DKMS", result.stderr)
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
        self.assertNotIn("Resolve RP1-DKMS installation plan", result.stdout)
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
        self.assertNotIn("[INFO ]", result.stdout)
        self.assertNotIn("Resolve RP1-DKMS installation plan", result.stdout)
        arguments = capture.read_text().splitlines()
        self.assertEqual(arguments.count("CALL"), 5)
        self.assertEqual(arguments.count("ARG=--debug"), 4)
        self.assertIn("ARG=/usr/local/lib/wsprrypi/runtime_reconcile.py", arguments)

    def test_repository_selection_reaches_every_lifecycle_operation_and_retry(self):
        shell = r'''
set -e
source "$INSTALL_SCRIPT"
python3() {
    { printf '%s\t' "$@"; printf '\n'; } >>"$CAPTURE"
    local arg
    for arg in "$@"; do
        if [[ "$arg" == remove ]]; then
            printf 'RP1-DKMS absent: no provider removal was required.\n'
        fi
    done
    return 0
}
systemctl() { :; }
logI() { :; }
logD() { :; }
ACTION=install DRY_RUN=false INSTALL_RP1_GPCLK_DKMS=true
RP1_GPCLK_DKMS_SOURCE=commit:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
REPO_BRANCH=devel
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
for pass in first repeat; do
    prepare_rp1_gpclk_dkms_installation debug
    prepare_rp1_gpclk_runtime_update debug
    apply_rp1_gpclk_dkms_installation debug
    activate_rp1_gpclk_runtime_administration debug
    cleanup_rp1_gpclk_dkms_state
done
ACTION=uninstall
prepare_owned_rp1_gpclk_runtime_removal debug
remove_owned_rp1_gpclk_dkms_provider debug
'''
        for repository in (None, MOD.MIGRATION_REPOSITORY):
            with self.subTest(repository=repository):
                capture = self.root / 'repository-argv.txt'
                capture.unlink(missing_ok=True)
                environment = {**os.environ, 'INSTALL_SCRIPT': str(ROOT/'scripts/install.sh'),
                               'CAPTURE': str(capture)}
                if repository is None:
                    environment.pop('RP1_GPCLK_DKMS_REPOSITORY', None)
                else:
                    environment['RP1_GPCLK_DKMS_REPOSITORY'] = repository
                result = subprocess.run(['bash', '-c', shell], text=True, capture_output=True,
                                        env=environment)
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = [line.split() for line in capture.read_text().splitlines()]
                helpers = [args for args in calls if args[0].endswith('rp1_gpclk_dkms_install.py')]
                self.assertEqual(len(helpers), 10)
                for args in helpers:
                    self.assertEqual(args[1:3], ['--provider-repository', MOD.MIGRATION_REPOSITORY])
                    self.assertIn('--debug', args)
                    # Parse actual shell arguments with the real helper CLI.
                    parsed = MOD.parser().parse_args(args[1:])
                    self.assertEqual(parsed.provider_repository, MOD.MIGRATION_REPOSITORY)
                self.assertEqual([args[3] for args in helpers],
                    ['prepare', 'prepare-runtime-update', 'apply', 'activate-runtime']*2 +
                    ['prepare-runtime-removal', 'remove'])
                self.assertEqual(len(calls)-len(helpers), 2, 'separate startup route-restoration calls')

    def test_invalid_repository_stops_before_helper_staging_or_dependency_resolution(self):
        shell = r'''
source "$INSTALL_SCRIPT"
python3() { printf python >>"$CAPTURE"; return 99; }
curl() { printf curl >>"$CAPTURE"; return 99; }
mktemp() { printf staging >>"$CAPTURE"; return 99; }
uname() { printf kernel >>"$CAPTURE"; return 99; }
warn() { printf '%s\n' "$*"; }
ACTION=install DRY_RUN=false INSTALL_RP1_GPCLK_DKMS=true
[[ "$OPERATION" != *removal && "$OPERATION" != remove_owned* ]] || ACTION=uninstall
"$OPERATION" && exit 98
[[ ! -e "$CAPTURE" ]]
'''
        operations = ('resolve_apt_package_list', 'prepare_rp1_gpclk_dkms_installation',
            'prepare_rp1_gpclk_runtime_update', 'apply_rp1_gpclk_dkms_installation',
            'activate_rp1_gpclk_runtime_administration', 'prepare_owned_rp1_gpclk_runtime_removal',
            'remove_owned_rp1_gpclk_dkms_provider')
        for repository in ('', MOD.LEGACY_REPOSITORY, 'attacker/provider', 'WsprryPi/RP1-DKMS;false'):
            for operation in operations:
                with self.subTest(repository=repository, operation=operation):
                    capture = self.root / 'invalid-repository-effects'
                    result = subprocess.run(['bash', '-c', shell], text=True, capture_output=True,
                        env={**os.environ, 'INSTALL_SCRIPT': str(ROOT/'scripts/install.sh'),
                             'CAPTURE': str(capture), 'OPERATION': operation,
                             'RP1_GPCLK_DKMS_REPOSITORY': repository})
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('RP1_GPCLK_DKMS_REPOSITORY must be', result.stdout)
                    self.assertFalse(capture.exists())

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
            f"Command: python3 {helper} --provider-repository WsprryPi/RP1-DKMS remove --remove auto --debug",
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

    def test_packages_precede_planning_and_runtime_recovery_precedes_apply(self):
        source = (ROOT / "scripts/install.sh").read_text()
        prepare = source.index('prepare_rp1_gpclk_dkms_installation "$debug" || return 1')
        packages = source.index('handle_apt_packages "$debug" || return 1')
        dependencies = source.index('validate_rp1_gpclk_build_dependencies "$debug" || return 1')
        apply = source.index('apply_rp1_gpclk_dkms_installation "$debug" || return 1')
        update = source.index('prepare_rp1_gpclk_runtime_update "$debug" || return 1')
        package_notice = source.index(
            'logI "Managing required packages (this may take a few minutes)."'
        )
        application = source.index('manage_wsprry_pi "$debug"')
        self.assertLess(source.index('print_version "$debug"'), package_notice)
        self.assertLess(source.index('display_compilation_resource_notes "$debug"'), package_notice)
        self.assertLess(source.index('detect_apache_web_mode "$debug"'), package_notice)
        self.assertLess(package_notice, packages)
        self.assertLess(packages, prepare)
        self.assertLess(prepare, update)
        self.assertLess(update, dependencies)
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
            "Managing required packages",
            source[package_handler_start:package_handler_end],
        )

    def test_runtime_activation_refreshes_systemd_before_service_validation(self):
        source = (ROOT / "scripts/install.sh").read_text()
        function = source[
            source.index("activate_rp1_gpclk_runtime_administration()"):
            source.index("prepare_owned_rp1_gpclk_runtime_removal()")
        ]
        activation = function.index(
            'exec_command "Activate neutral RP1-DKMS administration"'
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

    def test_readiness_keeps_existing_command_results_without_late_info(self):
        source = (ROOT / "scripts/install.sh").read_text()
        manager_start = source.index("manage_wsprry_pi() {")
        manager = source[manager_start:source.index("\n}\n", manager_start)]
        self.assertNotIn('logI ', manager)
        execution_loop = manager.index('for func in "${group_to_execute[@]}"; do')
        loop_end = manager.index("\n    done", execution_loop)
        self.assertLess(loop_end, manager.index('finish_script "$overall_status" "$debug"'))

    def test_helper_invocations_use_exec_command_and_propagate_debug(self):
        source = (ROOT / "scripts/install.sh").read_text()
        self.assertNotRegex(source, r'if ! python3 "\$RP1_GPCLK_DKMS_HELPER"')
        self.assertEqual(source.count('exec_command "Resolve RP1-DKMS installation plan"'), 1)
        self.assertEqual(source.count('exec_command "Apply RP1-DKMS installation plan"'), 1)
        self.assertEqual(source.count('exec_command "Prepare neutral RP1 runtime for application update"'), 1)
        self.assertEqual(source.count('exec_command "Activate neutral RP1-DKMS administration"'), 1)
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
manage_udp_logging() { record_step manage_udp_logging; }
manage_i2c() { record_step manage_i2c; }
manage_config() { record_step manage_config; }
manage_route_application() { record_step manage_route_application; }
manage_support_bundle_runtime() { record_step manage_support_bundle_runtime; }
manage_pico_usb_runtime() { record_step manage_pico_usb_runtime; }
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
            "manage_apache", "manage_web", "manage_service", "manage_udp_logging", "manage_i2c",
            "manage_config", "manage_route_application",
            "manage_support_bundle_runtime", "manage_pico_usb_runtime",
            "manage_exe", "manage_sound",
            "flag_need_reboot", "prepare_owned_rp1_gpclk_runtime_removal",
        }
        for failing_step in (
            "manage_service", "manage_udp_logging", "manage_route_application", "manage_pico_usb_runtime", "manage_exe",
        ):
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
        self.assertLess(provider, lines.index("manage_udp_logging"))
        self.assertLess(lines.index("manage_udp_logging"), lines.index("manage_service"))
        self.assertLess(provider, lines.index("manage_service"))
        self.assertLess(provider, lines.index("manage_exe"))
        self.assertIn("finish_status=0", lines)
        self.assertIn("return_status=0", lines)
        self.assertIn("Uninstallation successful", stdout)
        self.assertNotIn("RP1-DKMS provider was not removed", stdout)

    def test_successful_uninstall_reports_unproven_provider_preservation(self):
        lines, stdout = self.run_uninstall_orchestration(provider_unproven=True)
        self.assertIn("finish_status=0", lines)
        self.assertIn(
            "The RP1-DKMS provider was not removed, as I was unable to prove\n"
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
            "WsprryPi-owned RP1-DKMS provider removed and absence verified.": "false",
            "RP1-DKMS absent: no provider removal was required.": "false",
            "RP1-DKMS preserved: WsprryPi ownership is unproven.": "true",
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


def write_record(path, value):
    path.write_text(json.dumps(value))
    path.chmod(0o600)


def development_record(version: str = "1.0.0") -> dict[str, object]:
    return {
        "schema": MOD.RECORD_SCHEMA, "repository": MOD.REPOSITORY,
        "owner": "WsprryPi", "channel": "development",
        "installationMethod": "upstream-development-rollback",
        "sourceCommit": "a" * 40, "productVersion": version,
        "sourceTree": "b" * 40, "uapiSha256": "c" * 64,
        "versionSource": "include/rp1_gpclk/version.h",
        "versionSourceSha256": "d" * 64,
        "targetKernel": MOD.platform.release(),
        "compatibilityIdentity": MOD.development_compatibility(version),
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

def release_record() -> dict[str, object]:
    return {
        "schema": MOD.RECORD_SCHEMA, "repository": MOD.REPOSITORY,
        "owner": "WsprryPi", "channel": "release",
        "installationMethod": "debian-package", "tag": "v2.1.0",
        "sourceCommit": "a" * 40, "manifest": valid_manifest(),
        "moduleArtifacts": [{
            "path": f"/lib/modules/fixture/updates/dkms/{name}.ko",
            "sha256": "c" * 64,
        } for name in (MOD.MODULE_NAME, MOD.CONTROLLER_MODULE_NAME)],
        "routeActivation": "disabled", "output": "disabled",
        "qualificationClaim": False,
    }

class OwnershipFormatTests(unittest.TestCase):
    def setUp(self):
        self.root = temporary_root(self, "rp1-ownership-v1-").resolve()
        self.record = self.root / "ownership.json"

    def owned(self, channel, with_runtime):
        record = release_record() if channel == "release" else development_record()
        if with_runtime:
            runtime = ApplyPolicyTests.runtime_update_record(self)["runtime"]
            version = (record["manifest"]["productVersion"] if channel == "release"
                       else record["productVersion"])
            runtime["productVersion"] = version
            runtime["compatibilityIdentities"] = {
                route: f"v{version}-rp1-{route}" for route in ("gpio4", "gpio20")
            }
            record["runtime"] = runtime
        return record

    def test_one_v1_format_preserves_both_channels_and_receipt_states(self):
        self.assertEqual(MOD.RECORD_SCHEMA, "wsprrypi-rp1-gpclk-dkms-ownership-v1")
        for channel in ("release", "development"):
            for with_runtime in (False, True):
                with self.subTest(channel=channel, with_runtime=with_runtime):
                    record = self.owned(channel, with_runtime)
                    write_record(self.record, record)
                    original = self.record.read_bytes()
                    loaded, identity, reason = MOD.load_ownership_record(self.record)
                    self.assertEqual(loaded, record)
                    self.assertIsNotNone(identity)
                    self.assertIsNone(reason)
                    self.assertEqual(self.record.read_bytes(), original)

    def test_superseded_formats_and_invalid_receipts_are_rejected_unchanged(self):
        for channel in ("release", "development"):
            valid = self.owned(channel, True)
            invalid = []
            for schema in ("wsprrypi-rp1-gpclk-dkms-ownership-v2",
                           "wsprrypi-rp1-gpclk-dkms-ownership-v3",
                           "wsprrypi-rp1-gpclk-dkms-installation-v1"):
                invalid.append({**valid, "schema": schema})
            invalid.extend({**valid, "runtime": runtime} for runtime in (None, {}, []))
            invalid.append({**valid, "unexpected": True})
            changed = copy.deepcopy(valid)
            changed["runtime"]["sourceCommit"] = "0" * 40
            invalid.append(changed)
            for record in invalid:
                with self.subTest(channel=channel, record=record):
                    write_record(self.record, record)
                    original = self.record.read_bytes()
                    loaded, identity, reason = MOD.load_ownership_record(self.record)
                    self.assertIsNone(loaded)
                    self.assertIsNone(identity)
                    self.assertIsNotNone(reason)
                    self.assertEqual(self.record.read_bytes(), original)


class RemovalPolicyTests(RunnerFixtures, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.root = temporary_root(self, "rp1-removal-").resolve()
        self.record = self.root / "ownership.json"


    def args(self):
        return MOD.parser().parse_args(["remove", "--record", str(self.record), "--root", str(self.root)])

    def preparation_args(self):
        return MOD.parser().parse_args([
            "prepare-runtime-removal", "--record", str(self.record),
            "--root", str(self.root),
        ])

    def absent_runner(self, *, package=None, dkms=None):
        return self.runner({
            ("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME):
                package or MOD.CommandResult("", "not installed", 1),
            ("dkms", "status", "-m", MOD.DKMS_NAME):
                dkms or MOD.CommandResult("", "", 0),
        })

    def test_stale_owned_records_allow_both_uninstall_steps_without_provider(self):
        for channel in ("release", "development"):
            for with_runtime in (False, True):
                with self.subTest(channel=channel, with_runtime=with_runtime):
                    record = release_record() if channel == "release" else development_record()
                    if with_runtime:
                        record["runtime"] = ApplyPolicyTests.runtime_update_record(self)["runtime"]
                        record["runtime"]["productVersion"] = (
                            record["manifest"]["productVersion"] if channel == "release"
                            else record["productVersion"])
                        version = record["runtime"]["productVersion"]
                        record["runtime"]["compatibilityIdentities"] = {
                            route: f"v{version}-rp1-{route}" for route in ("gpio4", "gpio20")
                        }
                    write_record(self.record, record)
                    runner = self.absent_runner()
                    output = io.StringIO()
                    with mock.patch.object(MOD.sys, "stdout", output), \
                         mock.patch.object(MOD, "remove_owned_runtime_for_fresh_activation") as runtime:
                        MOD.prepare_runtime_update(self.preparation_args(), runner)
                        self.assertFalse(self.record.exists())
                        MOD.remove_owned_provider(self.args(), runner)
                    runtime.assert_not_called()
                    self.assertIn("stale ownership record retired", output.getvalue())
                    self.assertIn("absent: no provider removal was required", output.getvalue())
                    self.assertEqual(runner.passthrough_calls, [])

    def test_partial_or_uncertain_provider_cannot_retire_stale_record(self):
        cases = {
            "package": {"package": MOD.CommandResult("install ok installed\n1.0.0\n", "", 0)},
            "dkms": {"dkms": MOD.CommandResult("rp1-gpclk-dkms/1.0.0: added\n", "", 0)},
            "package-error": {"package": MOD.CommandResult("", "query failed", 2)},
            "dkms-error": {"dkms": MOD.CommandResult("", "query failed", 1)},
        }
        paths = (
            "usr/lib/rp1-gpclk-dkms/runtime_provider.py",
            "usr/src/rp1-gpclk-dkms-1.0.0/dkms.conf",
            "lib/modules/fixture/updates/dkms/rp1_gpclk_dkms.ko",
            "lib/modules/fixture/updates/dkms/rp1_route_controller.ko",
            "boot/firmware/overlays/rp1-gpclk-gpio4.dtbo",
            "boot/firmware/config.txt",
            "etc/rp1-gpclk-dkms/enrollment.json",
            "proc/modules",
        )
        cases.update({path: {} for path in paths})
        for case, replies in cases.items():
            with self.subTest(case=case):
                path = self.root / case if case in paths else None
                if path is not None:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("dtoverlay=rp1-gpclk-gpio4\nrp1_route_controller 0 0 - Live 0\n")
                write_record(self.record, release_record())
                original = self.record.read_bytes()
                with mock.patch.object(MOD, "remove_owned_runtime_for_fresh_activation",
                                       side_effect=MOD.ContractError("owning recovery required")) as runtime:
                    with self.assertRaisesRegex(MOD.ContractError, "owning recovery required"):
                        MOD.prepare_runtime_update(self.preparation_args(), self.absent_runner(**replies))
                runtime.assert_called_once()
                self.assertEqual(self.record.read_bytes(), original)
                if path is not None:
                    path.unlink()
                    # Empty provider source directories also count as residue.
                    if case.startswith("usr/src/"):
                        path.parent.rmdir()

    def test_stale_record_retirement_refuses_concurrent_ownership_change(self):
        write_record(self.record, release_record())
        changed = release_record()
        changed["sourceCommit"] = "b" * 40
        verify = MOD.verify_provider_absent

        def replace_after_inventory(root, runner):
            verify(root, runner)
            write_record(self.record, changed)

        with mock.patch.object(MOD, "verify_provider_absent", side_effect=replace_after_inventory):
            with self.assertRaisesRegex(MOD.ContractError, "ownership changed"):
                MOD.prepare_runtime_update(self.preparation_args(), self.absent_runner())
        self.assertEqual(json.loads(self.record.read_bytes()), changed)

    def test_dangling_provider_links_do_not_prove_absence(self):
        for relative in (
            "etc/rp1-gpclk-dkms/enrollment.json",
            "etc/systemd/system/rp1-gpclk-route-manager@.service.d/90-source-development.conf",
            "usr/lib/rp1-gpclk-dkms/runtime_provider.py",
            "boot/firmware/config.txt",
        ):
            with self.subTest(path=relative):
                path = self.root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to("missing-target")
                write_record(self.record, release_record())
                with mock.patch.object(MOD, "remove_owned_runtime_for_fresh_activation",
                                       side_effect=MOD.ContractError("owning recovery required")):
                    with self.assertRaisesRegex(MOD.ContractError, "owning recovery required"):
                        MOD.prepare_runtime_update(self.preparation_args(), self.absent_runner())
                self.assertTrue(self.record.exists())
                self.assertTrue(path.is_symlink())
                path.unlink()

    def test_merged_usr_module_inventory_only_accepts_the_standard_alias(self):
        modules = self.root / "usr/lib/modules/fixture/updates/dkms"
        modules.mkdir(parents=True)
        for name in (MOD.MODULE_NAME, MOD.CONTROLLER_MODULE_NAME):
            (modules / (name + ".ko")).write_bytes(name.encode())
        alias = self.root / "lib"
        alias.symlink_to("usr/lib")
        artifacts = MOD.module_artifacts(self.root)
        self.assertEqual(len(artifacts), 2)
        self.assertTrue(all(item["path"].startswith("/lib/modules/") for item in artifacts))
        alias.unlink(); alias.symlink_to("usr/../usr/lib")
        with self.assertRaisesRegex(MOD.ContractError, "substituted ancestor"):
            MOD.module_artifacts(self.root)
        alias.unlink(); alias.symlink_to("usr/lib")
        (modules / (MOD.MODULE_NAME + ".ko")).unlink()
        (modules / (MOD.MODULE_NAME + ".ko")).symlink_to(modules / (MOD.CONTROLLER_MODULE_NAME + ".ko"))
        with self.assertRaisesRegex(MOD.ContractError, "substituted"):
            MOD.module_artifacts(self.root)

    def test_legacy_compatibility_is_rollback_authority_only_for_exact_predecessor(self):
        record = development_record()
        evidence = self.root / ("var/lib/wsprrypi/rp1-gpclk-dkms-development-evidence." + "a" * 24)
        entrypoint = evidence / "rendered-source/scripts/development-rollback"
        entrypoint.parent.mkdir(parents=True)
        entrypoint.write_bytes(b"#!/bin/sh\nexit 0\n"); entrypoint.chmod(0o700)
        rollback = evidence / "ROLLBACK.json"
        rollback.write_bytes(b"{}\n"); rollback.chmod(0o600)
        record.update(productVersion="0.9.0", compatibilityIdentity="v0.9.0-pi5",
                      upstreamEvidence=str(evidence), rollbackRecord=str(rollback),
                      rollbackEntrypoint=str(entrypoint), rollbackRecordSha256=digest(rollback.read_bytes()),
                      rollbackEntrypointSha256=digest(entrypoint.read_bytes()))
        self.assertEqual(MOD.validate_development_rollback_authority(record, self.root), (entrypoint, rollback))
        record["productVersion"] = "1.0.0"
        with self.assertRaisesRegex(MOD.ContractError, "compatibility identity"):
            MOD.validate_development_rollback_authority(record, self.root)

    def test_untrusted_records_report_absence_when_no_provider_exists(self):
        runner = self.runner({
            ("dkms", "status", "-m", MOD.DKMS_NAME): MOD.CommandResult("", "", 0),
            ("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME):
                MOD.CommandResult("", "", 1),
        })
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertIn("absent", stdout.getvalue())

        for value in (
            {"schema": "wsprrypi-rp1-gpclk-dkms-installation-v1"},
            {"schema": MOD.RECORD_SCHEMA, "owner": "attacker"},
            {**release_record(), "tag": 210},
        ):
            write_record(self.record, value)
            stdout = io.StringIO()
            with mock.patch.object(MOD.sys, "stdout", stdout):
                MOD.remove_owned_provider(self.args(), runner)
            self.assertIn("absent", stdout.getvalue())
            self.assertTrue(self.record.exists())
            self.record.unlink()

        target = self.root / "target.json"
        target.write_text(json.dumps(release_record()))
        self.record.symlink_to(target)
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertIn("absent", stdout.getvalue())
        self.assertTrue(target.exists())

    def test_missing_ownership_preserves_an_existing_provider(self):
        source = self.root / "usr/src/rp1-gpclk-dkms-1.0.0"
        source.mkdir(parents=True)
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), self.runner({("dpkg-query", "-W", "-f=${Status}\n${Version}\n", MOD.PACKAGE_NAME): MOD.CommandResult("", "missing", 1), ("dkms", "status", "-m", MOD.DKMS_NAME): MOD.CommandResult("", "", 0)}))
        self.assertIn("preserved", stdout.getvalue())
        self.assertTrue(source.exists())

    def test_explicit_opt_out_preserves_even_a_valid_owned_provider(self):
        write_record(self.record, release_record())
        args = MOD.parser().parse_args([
            "remove", "--remove", "false", "--record", str(self.record),
            "--root", str(self.root),
        ])
        runner = self.runner()
        stdout = io.StringIO()
        with mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(args, runner)
        self.assertIn("explicit operator opt-out", stdout.getvalue())
        self.assertTrue(self.record.exists())
        self.assertEqual(runner.calls, [])

    def test_owned_release_uses_package_removal_then_deletes_record(self):
        write_record(self.record, release_record())
        runner = self.runner(steps=[Command(("apt-get", "remove", "-y", MOD.PACKAGE_NAME), passthrough=True, stage="removal")])
        def guard(*_):
            self.assertFalse(self.record.exists())
            pending = MOD.removal_pending_path(self.record)
            self.assertEqual(json.loads(pending.read_bytes()), release_record())
            with self.assertRaisesRegex(MOD.ContractError, "pending"):
                MOD.ensure_new_ownership_record(self.record)
        with mock.patch.object(MOD, "validate_release_removal"), \
             mock.patch.object(MOD, "verify_provider_absent"), \
             mock.patch.object(MOD, "package_removal_preflight", side_effect=guard) as preflight:
            MOD.remove_owned_provider(self.args(), runner)
        preflight.assert_called_once()
        self.assertEqual(runner.passthrough_calls, [("apt-get", "remove", "-y", MOD.PACKAGE_NAME)])
        self.assertFalse(self.record.exists())
        self.assertFalse(MOD.removal_pending_path(self.record).exists())

    def test_passive_packaged_units_do_not_repeat_runtime_removal(self):
        record = release_record()
        record["runtime"] = ApplyPolicyTests.runtime_update_record(self)["runtime"]
        version = record["manifest"]["productVersion"]
        record["runtime"]["productVersion"] = version
        record["runtime"]["compatibilityIdentities"] = {
            route: f"v{version}-rp1-{route}" for route in ("gpio4", "gpio20")
        }
        residue = []
        for name in ("rp1-gpclk-route-manager.socket", "rp1-gpclk-route-manager@.service"):
            relative = "usr/lib/systemd/system/" + name
            unit = self.root / relative
            unit.parent.mkdir(parents=True, exist_ok=True)
            unit.write_bytes(b"passive packaged unit")
            record["manifest"]["packageInventory"].append({
                "path": relative, "type": "file", "mode": "0644", "sha256": digest(unit.read_bytes())})
            residue.append(str(unit))
        write_record(self.record, record)
        identity = self.record.stat()
        runner = self.runner(steps=[Command(("apt-get", "remove", "-y", MOD.PACKAGE_NAME), passthrough=True, stage="removal")])
        with mock.patch.object(MOD, "load_ownership_record", return_value=(record, identity, None)), \
             mock.patch.object(MOD, "existing_inventory", return_value={"packageVersion": "2.1.0-1", "runtimeResidue": residue}), \
             mock.patch.object(MOD, "recover_and_remove_owned_runtime") as runtime_remove, \
             mock.patch.object(MOD, "validate_release_removal"), \
             mock.patch.object(MOD, "package_removal_preflight"), \
             mock.patch.object(MOD, "verify_provider_absent"):
            MOD.remove_owned_provider(self.args(), runner)
        runtime_remove.assert_not_called()
        self.assertEqual(runner.passthrough_calls, [("apt-get", "remove", "-y", MOD.PACKAGE_NAME)])
        self.assertFalse(self.record.exists())

    def test_identity_drift_preserves_record_without_package_mutation(self):
        write_record(self.record, release_record())
        runner = self.runner()
        stdout = io.StringIO()
        with mock.patch.object(MOD, "validate_release_removal", side_effect=MOD.ContractError("artifact drift")), \
             mock.patch.object(MOD.sys, "stdout", stdout):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertIn("artifact drift", stdout.getvalue())
        self.assertEqual(runner.calls, [])
        self.assertTrue(self.record.exists())

    def test_release_allows_only_same_version_additional_kernel_artifacts(self):
        record = release_record()
        artifacts = [
            *record["moduleArtifacts"],
            {
                "path": "/lib/modules/new-kernel/updates/dkms/rp1_gpclk_dkms.ko.xz",
                "sha256": "d" * 64,
            },
            {
                "path": "/lib/modules/new-kernel/updates/dkms/rp1_route_controller.ko.xz",
                "sha256": "e" * 64,
            },
        ]
        responses = {
            ("modinfo", "-k", "fixture", "-F", "version", MOD.MODULE_NAME): MOD.CommandResult("2.1.0\n", "", 0),
            ("modinfo", "-k", "new-kernel", "-F", "version", MOD.MODULE_NAME): MOD.CommandResult("2.1.0\n", "", 0),
        }
        runner = self.runner(responses)
        for kernel in ("fixture", "new-kernel"):
            runner.allow_read(("modinfo", "-k", kernel, "-F", "version", MOD.CONTROLLER_MODULE_NAME), MOD.CommandResult("2.1.0\n", "", 0))
        with mock.patch.object(MOD, "verify_installed_release"), \
             mock.patch.object(MOD, "module_artifacts", return_value=artifacts), \
             mock.patch.object(MOD, "package_preflight") as preflight:
            MOD.validate_release_removal(record, self.root, runner)
        runner.allow_read(("modinfo", "-k", "new-kernel", "-F", "version", MOD.MODULE_NAME), MOD.CommandResult("9.9.9\n", "", 0))
        with mock.patch.object(MOD, "verify_installed_release"), \
             mock.patch.object(MOD, "module_artifacts", return_value=artifacts), \
             mock.patch.object(MOD, "package_preflight") as preflight:
            with self.assertRaisesRegex(MOD.ContractError, "additional-kernel"):
                MOD.validate_release_removal(record, self.root, runner)

    def test_package_removal_failure_retains_record_and_fails(self):
        write_record(self.record, release_record())
        failed = MOD.CommandResult("", "package busy", 1)
        runner = self.runner({("apt-get", "remove", "-y", MOD.PACKAGE_NAME): failed}, steps=[Command(("apt-get", "remove", "-y", MOD.PACKAGE_NAME), passthrough=True, stage="removal")])
        with mock.patch.object(MOD, "validate_release_removal"), \
             mock.patch.object(MOD, "package_removal_preflight"):
            with self.assertRaises(MOD.ContractError):
                MOD.remove_owned_provider(self.args(), runner)
        self.assertTrue(self.record.exists())

    def test_guard_refusal_restores_ownership_without_apt(self):
        write_record(self.record, release_record())
        runner = self.runner()
        with mock.patch.object(MOD, "validate_release_removal"), \
             mock.patch.object(MOD, "package_removal_preflight", side_effect=MOD.ContractError("foreign overlay preserved")):
            with self.assertRaisesRegex(MOD.ContractError, "foreign overlay preserved"):
                MOD.remove_owned_provider(self.args(), runner)
        self.assertEqual(json.loads(self.record.read_bytes()), release_record())
        self.assertFalse(MOD.removal_pending_path(self.record).exists())
        self.assertEqual(runner.calls, [])

    def test_interrupted_package_removal_recovers_exact_authority(self):
        value = release_record()
        MOD.atomic_json_new(MOD.removal_pending_path(self.record), value)
        with mock.patch.object(MOD, "verify_provider_absent"):
            MOD.remove_owned_provider(self.args(), self.runner())
        self.assertFalse(self.record.exists())
        self.assertFalse(MOD.removal_pending_path(self.record).exists())

    def test_pending_removal_never_overwrites_concurrent_record(self):
        value = release_record()
        MOD.atomic_json_new(MOD.removal_pending_path(self.record), value)
        other = {**value, "sourceCommit": "b" * 40}
        write_record(self.record, other)
        with self.assertRaisesRegex(MOD.ContractError, "conflicts"):
            MOD.remove_owned_provider(self.args(), self.runner())
        self.assertEqual(json.loads(self.record.read_bytes()), other)
        self.assertEqual(json.loads(MOD.removal_pending_path(self.record).read_bytes()), value)

    def test_record_change_after_preflight_blocks_package_mutation(self):
        write_record(self.record, release_record())
        runner = self.runner()

        def change_record(*_):
            changed = release_record()
            changed["sourceCommit"] = "b" * 40
            write_record(self.record, changed)

        with mock.patch.object(MOD, "validate_release_removal", side_effect=change_record):
            with self.assertRaisesRegex(MOD.ContractError, "ownership changed"):
                MOD.remove_owned_provider(self.args(), runner)
        self.assertEqual(runner.calls, [])
        self.assertTrue(self.record.exists())

    def test_owned_development_uses_captured_upstream_rollback(self):
        write_record(self.record, development_record())
        evidence = self.root / "evidence/rendered-source/scripts"
        evidence.mkdir(parents=True)
        entrypoint = evidence / "development-rollback"
        entrypoint.write_text("#!/bin/sh\n")
        entrypoint.chmod(0o700)
        rollback = self.root / "evidence/ROLLBACK.json"
        rollback.write_text("{}")
        rollback.chmod(0o600)
        runner = self.runner(steps=[Command((str(entrypoint), "--record", str(rollback)), cwd=str(entrypoint.parent.parent), passthrough=True, stage="rollback")])
        with mock.patch.object(MOD, "validate_development_removal", return_value=(entrypoint, rollback)), \
             mock.patch.object(MOD, "verify_provider_absent"):
            MOD.remove_owned_provider(self.args(), runner)
        self.assertEqual(runner.passthrough_calls, [(str(entrypoint), "--record", str(rollback))])
        self.assertFalse(self.record.exists())

    def test_owned_runtime_uninstall_recovers_before_dkms_rollback(self):
        record = development_record()
        record["schema"] = MOD.RECORD_SCHEMA
        record["runtime"] = {
            "readinessContract": MOD.RUNTIME_READINESS_CONTRACT,
            "bindingSha256": "3" * 64, "artifactSetSha256": "4" * 64,
            "sourceCommit": record["sourceCommit"],
            "productVersion": record["productVersion"],
            "targetKernel": MOD.platform.release(),
            "compatibilityIdentities": {r: f"v{record['productVersion']}-rp1-{r}" for r in ("gpio4", "gpio20")},
            "deploymentPlanSha256": "5" * 64, "activationPlanSha256": "6" * 64,
            "activationRequestId": "request-1234", "controllerSession": 1,
            "controllerGeneration": 0, "state": "neutral_ready", "route": None,
            "output": "disabled",
        }
        write_record(self.record, record)
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
        runner = self.runner(steps=[Command((str(entrypoint), "--record", str(rollback)), cwd=str(entrypoint.parent.parent), passthrough=True, stage="rollback")])
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


    def test_rp1_dkms_001_runtime_removal_uses_its_owning_rollback(self):
        self.addCleanup(MOD.configure_provider_repository, MOD.REPOSITORY)
        MOD.configure_provider_repository(MOD.MIGRATION_REPOSITORY)
        record = development_record('0.0.1')
        with mock.patch(__name__ + '.development_record', return_value=record):
            self.test_owned_runtime_uninstall_recovers_before_dkms_rollback()


class InstallerMigrationTests(RunnerFixtures, unittest.TestCase):
    # Reuse the existing strict ownership fixtures, not alternate production views.

    def setUp(self):
        super().setUp()
        self.root = temporary_root(self, "rp1-migration-").resolve()
        self.record = self.root / "ownership.json"
        self.state = self.root / 'state'; self.state.mkdir(mode=0o700)
        self.selected = {'channel': 'development', 'commit': 'd' * 40,
                         'version': MOD.CONSUMER_PROVIDER_VERSION,
                         'checkout': {'commit': 'd' * 40, 'path': str(self.root / 'candidate')}}
        self.old = release_record()
        self.payload = b'exact predecessor package'
        self.old['manifest']['package']['sha256'] = digest(self.payload)

    def plan(self, selected=None):
        MOD.atomic_json(self.state / 'plan.json', {'schema': MOD.STATE_SCHEMA,
            'dryRun': False, 'decision': {'install': True}, 'resolved': selected or self.selected,
            'providerRepository': MOD.REPOSITORY})
        return MOD.parser().parse_args(['apply', '--state-dir', str(self.state),
                                      '--record', str(self.record), '--root', str(self.root)])

    def test_consumer_version_matches_cpp_and_current_source(self):
        source = (ROOT / 'src/WSPR-Transmitter/src/rp1_gpclk_consumer_contract.hpp').read_text()
        self.assertIn('version = "' + MOD.CONSUMER_PROVIDER_VERSION + '"', source)
        self.assertEqual(MOD.CONSUMER_PROVIDER_VERSION, '0.0.1')

    def test_explicit_development_remains_explicit(self):
        self.assertEqual(MOD.source_selector('auto', 'development'), ('release', None))
        self.assertEqual(MOD.source_selector('devel', 'development'), ('devel', None))

    def test_incompatible_candidate_fails_before_ownership_or_runtime_effects(self):
        selected = dict(self.selected, version='1.0.0'); args = self.plan(selected)
        args.command = 'prepare-runtime-update'
        with mock.patch.object(MOD, 'load_ownership_record') as ownership, \
             mock.patch.object(MOD, 'recover_and_remove_owned_runtime') as effects:
            with self.assertRaisesRegex(MOD.ContractError, 'not accepted'):
                MOD.prepare_runtime_update(args, self.runner())
        ownership.assert_not_called(); effects.assert_not_called()

    def test_unknown_candidate_channel_refused_before_removal(self):
        args = self.plan(dict(self.selected, channel='unknown'))
        with mock.patch.object(MOD, 'remove_owned_provider') as remove:
            with self.assertRaisesRegex(MOD.ContractError, 'channel is invalid'):
                MOD.apply(args, self.runner())
        remove.assert_not_called()

    def test_source_binding_checked_before_runtime_update(self):
        selected = copy.deepcopy(self.selected); selected['checkout']['commit'] = 'e' * 40
        with self.assertRaisesRegex(MOD.ContractError, 'commits differ'):
            MOD.validate_candidate_before_update(selected, self.runner())

    def test_source_product_checked_again_before_runtime_update(self):
        with mock.patch.object(MOD, 'revalidate_checkout', return_value=self.root), \
             mock.patch.object(MOD, 'runtime_source_interface'), \
             mock.patch.object(MOD, 'runtime_source_identity', side_effect=MOD.ContractError('source version differs')):
            with self.assertRaisesRegex(MOD.ContractError, 'source version differs'):
                MOD.validate_candidate_before_update(self.selected, self.runner())

    def test_rollback_is_retained_and_verified_before_removal(self):
        write_record(self.record, self.old); args = self.plan(); events = []
        def remove(arguments, runner):
            directories = list(self.root.glob('rp1-provider-migration.*'))
            self.assertEqual(len(directories), 1)
            self.assertEqual((directories[0] / self.old['manifest']['package']['filename']).read_bytes(), self.payload)
            self.assertEqual(json.loads((directories[0] / 'previous-installation.json').read_text()), self.old)
            self.assertEqual(arguments.remove, 'true')
            events.append('remove'); self.record.unlink()
        with mock.patch.object(MOD, 'validate_candidate_before_update', side_effect=lambda *_:events.append('candidate')), \
             mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package'), \
             mock.patch.object(MOD, 'remove_owned_provider', side_effect=remove), \
             mock.patch.object(MOD, 'verify_provider_absent', side_effect=lambda *_:events.append('absence')), \
             mock.patch.object(MOD, 'apply_development', side_effect=lambda *_:events.append('install')):
            MOD.apply(args, self.runner())
        self.assertEqual(events, ['candidate', 'remove', 'absence', 'install'])

    def test_corrupt_rollback_package_preserves_installed_provider(self):
        write_record(self.record, self.old); args = self.plan()
        with mock.patch.object(MOD, 'validate_candidate_before_update'), \
             mock.patch.object(MOD.GitHubClient, 'bytes', return_value=b'corrupt'), \
             mock.patch.object(MOD, 'remove_owned_provider') as remove, \
             mock.patch.object(MOD, 'apply_development') as install:
            with self.assertRaisesRegex(MOD.ContractError, 'rollback package SHA-256'):
                MOD.apply(args, self.runner())
        self.assertEqual(json.loads(self.record.read_text()), self.old)
        remove.assert_not_called(); install.assert_not_called()

    def test_failed_owned_removal_never_installs_candidate(self):
        write_record(self.record, self.old); args = self.plan()
        with mock.patch.object(MOD, 'validate_candidate_before_update'), \
             mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package'), \
             mock.patch.object(MOD, 'remove_owned_provider', side_effect=MOD.ContractError('cleanup refused')), \
             mock.patch.object(MOD, 'apply_development') as install:
            with self.assertRaisesRegex(MOD.ContractError, 'cleanup refused'):
                MOD.apply(args, self.runner())
        install.assert_not_called()
        self.assertTrue(self.record.exists())
        self.assertTrue(list(self.root.glob('rp1-provider-migration.*/previous-installation.json')))

    def test_retained_package_is_reused_offline_and_corruption_is_refused(self):
        with mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload) as download, \
             mock.patch.object(MOD, 'validate_package'):
            directory = MOD.retain_provider_migration(self.record, self.old, self.selected, self.runner())
            MOD.retain_provider_migration(self.record, self.old, self.selected, self.runner())
            self.assertEqual(download.call_count, 1)
            (directory / self.old['manifest']['package']['filename']).write_bytes(b'bad cached input')
            with self.assertRaisesRegex(MOD.ContractError, 'retained predecessor rollback package differs'):
                MOD.retain_provider_migration(self.record, self.old, self.selected, self.runner())
            self.assertEqual(download.call_count, 1)

    def test_retained_package_symlink_is_preserved_and_refused(self):
        with mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package'):
            directory = MOD.retain_provider_migration(self.record, self.old, self.selected, self.runner())
            package = directory / self.old['manifest']['package']['filename']; package.unlink()
            target = self.root / 'foreign'; target.write_bytes(self.payload); package.symlink_to(target)
            with self.assertRaisesRegex(MOD.ContractError, 'rollback package differs'):
                MOD.retain_provider_migration(self.record, self.old, self.selected, self.runner())
            self.assertTrue(package.is_symlink()); self.assertEqual(target.read_bytes(), self.payload)

    def test_same_provider_does_not_create_migration_or_remove_it(self):
        self.assertFalse(MOD.provider_migration_required(self.old, {
            'channel':'release', 'commit':self.old['sourceCommit']}))
        self.assertTrue(MOD.provider_migration_required(self.old, {'channel':'release','commit':'f'*40}))
        self.assertTrue(MOD.provider_migration_required({'channel':'development','sourceCommit':'a'*40},
                                                       {'channel':'release','commit':'a'*40}))

    def test_empty_owned_dropin_removed_but_foreign_content_is_preserved(self):
        directory = self.root / 'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.mkdir(parents=True); MOD.remove_empty_runtime_dropin(self.root)
        self.assertFalse(directory.exists())
        directory.mkdir(); foreign = directory / 'operator.conf'; foreign.write_text('keep')
        with self.assertRaisesRegex(MOD.ContractError, 'preserve its contents'):
            MOD.remove_empty_runtime_dropin(self.root)
        self.assertEqual(foreign.read_text(), 'keep')

    def test_runtime_dropin_symlink_and_parent_substitution_are_refused(self):
        directory = self.root / 'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.parent.mkdir(parents=True); foreign = self.root / 'foreign'; foreign.mkdir()
        directory.symlink_to(foreign)
        with self.assertRaisesRegex(MOD.ContractError, 'substituted'):
            MOD.remove_empty_runtime_dropin(self.root)
        self.assertTrue(foreign.is_dir()); self.assertTrue(directory.is_symlink())
        directory.unlink(); directory.parent.rmdir(); directory.parent.symlink_to(foreign)
        (foreign / directory.name).mkdir()
        with self.assertRaisesRegex(MOD.ContractError, 'substituted'):
            MOD.remove_empty_runtime_dropin(self.root)
        self.assertTrue((foreign / directory.name).is_dir())


    def release_candidate(self):
        manifest = valid_manifest(MOD.CONSUMER_PROVIDER_VERSION, 'd' * 40)
        package = self.state / manifest['package']['filename']; package.write_bytes(b'candidate package')
        manifest['package']['sha256'] = digest(package.read_bytes())
        return dict(self.selected, channel='release', manifest=manifest,
                    tag=manifest['releaseTag'], packagePath=str(package))

    def test_corrupt_candidate_package_does_not_start_runtime_cleanup(self):
        selected = self.release_candidate(); args = self.plan(selected)
        args.command = 'prepare-runtime-update'
        with mock.patch.object(MOD, 'validate_package', side_effect=MOD.ContractError('candidate corrupt')), \
             mock.patch.object(MOD, 'recover_and_remove_owned_runtime') as cleanup, \
             mock.patch.object(MOD, 'load_ownership_record') as owned:
            with self.assertRaisesRegex(MOD.ContractError, 'candidate corrupt'):
                MOD.prepare_runtime_update(args, self.runner())
        cleanup.assert_not_called(); owned.assert_not_called()

    def test_release_version_migration_verifies_candidate_before_owned_removal(self):
        write_record(self.record, self.old); selected = self.release_candidate(); args = self.plan(selected); events=[]
        def remove(arguments, runner):events.append('remove'); self.record.unlink()
        def package(path, *_):events.append('candidate' if path.parent==self.state else 'rollback')
        with mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package', side_effect=package), \
             mock.patch.object(MOD, 'revalidate_checkout', return_value=self.root), \
             mock.patch.object(MOD, 'runtime_source_interface'), \
             mock.patch.object(MOD, 'runtime_source_identity'), \
             mock.patch.object(MOD, 'remove_owned_provider', side_effect=remove), \
             mock.patch.object(MOD, 'verify_provider_absent', side_effect=lambda *_:events.append('absence')), \
             mock.patch.object(MOD, 'apply_release', side_effect=lambda *_:events.append('install')):
            MOD.apply(args, self.runner())
        self.assertEqual(events, ['candidate','rollback','remove','absence','install'])

    def development_recovery_inputs(self):
        previous=development_record()
        entrypoint=self.root/'development-rollback'; entrypoint.write_bytes(b'original owning rollback entrypoint')
        rollback=self.root/'ROLLBACK.json'; rollback.write_bytes(b'{"checkpoint":"installed"}')
        previous.update(rollbackRecord=str(rollback),rollbackEntrypoint=str(entrypoint),
                        rollbackRecordSha256=digest(rollback.read_bytes()),
                        rollbackEntrypointSha256=digest(entrypoint.read_bytes()))
        return previous,entrypoint,rollback

    def test_development_to_release_retains_exact_rollback_authority(self):
        previous,entrypoint,rollback_input=self.development_recovery_inputs(); write_record(self.record, previous)
        args=self.plan(self.release_candidate())
        with mock.patch.object(MOD, 'validate_candidate_before_update'), \
             mock.patch.object(MOD, 'validate_development_rollback_authority', return_value=(entrypoint,rollback_input)) as rollback, \
             mock.patch.object(MOD, 'remove_owned_provider', side_effect=lambda *_:self.record.unlink()), \
             mock.patch.object(MOD, 'verify_provider_absent'), \
             mock.patch.object(MOD, 'apply_release') as install:
            MOD.apply(args, self.runner())
        rollback.assert_called_once_with(previous, pathlib.Path('/')); install.assert_called_once()
        retained=list(self.root.glob('rp1-provider-migration.*/previous-installation.json'))
        self.assertEqual(json.loads(retained[0].read_text()), previous)

    def test_concurrent_ownership_change_after_retention_stops_removal(self):
        write_record(self.record, self.old); args=self.plan()
        changed=copy.deepcopy(self.old); changed['sourceCommit']='e'*40
        with mock.patch.object(MOD, 'validate_candidate_before_update'), \
             mock.patch.object(MOD, 'retain_provider_migration', side_effect=lambda *_:write_record(self.record, changed)), \
             mock.patch.object(MOD, 'remove_owned_provider') as remove, \
             mock.patch.object(MOD, 'apply_development') as install:
            with self.assertRaisesRegex(MOD.ContractError, 'ownership changed'):
                MOD.apply(args, self.runner())
        remove.assert_not_called(); install.assert_not_called()
        self.assertEqual(json.loads(self.record.read_text()), changed)

    def test_changed_retained_ownership_stops_retry(self):
        with mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package'):
            directory=MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())
            path=directory/'previous-installation.json'; value=json.loads(path.read_text())
            value['moduleArtifacts'][0]['sha256']='e'*64; MOD.atomic_json(path,value)
            with self.assertRaisesRegex(MOD.ContractError,'retained predecessor ownership differs'):
                MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())

    def test_dangling_runtime_dropin_symlink_is_preserved(self):
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.parent.mkdir(parents=True); directory.symlink_to(self.root/'missing')
        with self.assertRaisesRegex(MOD.ContractError,'substituted'):
            MOD.remove_empty_runtime_dropin(self.root)
        self.assertTrue(directory.is_symlink())


    def test_reinstalled_same_source_has_independent_recovery_inputs(self):
        with mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package'):
            first=MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())
            rebuilt=copy.deepcopy(self.old); rebuilt['moduleArtifacts'][0]['sha256']='e'*64
            second=MOD.retain_provider_migration(self.record,rebuilt,self.selected,self.runner())
        self.assertNotEqual(first,second)
        self.assertEqual(json.loads((first/'previous-installation.json').read_text()),self.old)
        self.assertEqual(json.loads((second/'previous-installation.json').read_text()),rebuilt)

    def test_malformed_retained_ownership_stops_retry(self):
        with mock.patch.object(MOD.GitHubClient, 'bytes', return_value=self.payload), \
             mock.patch.object(MOD, 'validate_package'):
            directory=MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())
            (directory/'previous-installation.json').write_text('[]')
            with self.assertRaisesRegex(MOD.ContractError,'retained predecessor ownership is unreadable'):
                MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())


    def test_predecessor_rollback_recreated_dropin_is_removed_before_candidate(self):
        write_record(self.record, self.old); args=self.plan()
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        def remove(*_):
            self.record.unlink(); directory.mkdir(parents=True)
        def install(*_):self.assertFalse(directory.exists())
        with mock.patch.object(MOD,'validate_candidate_before_update'), \
             mock.patch.object(MOD.GitHubClient,'bytes',return_value=self.payload), \
             mock.patch.object(MOD,'validate_package'), \
             mock.patch.object(MOD,'remove_owned_provider',side_effect=remove), \
             mock.patch.object(MOD,'verify_provider_absent'), \
             mock.patch.object(MOD,'apply_development',side_effect=install) as candidate:
            MOD.apply(args,self.runner())
        candidate.assert_called_once()

    def test_interrupted_post_removal_retry_uses_bound_evidence(self):
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.mkdir(parents=True)
        with mock.patch.object(MOD.GitHubClient,'bytes',return_value=self.payload), \
             mock.patch.object(MOD,'validate_package'):
            evidence=MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())
            with mock.patch.object(MOD,'verify_provider_absent') as absence:
                MOD.resume_removed_provider_migration(self.record,self.selected,self.root,self.runner())
        self.assertFalse(directory.exists()); self.assertTrue(evidence.is_dir()); absence.assert_called_once()

    def test_empty_foreign_dropin_without_migration_proof_is_preserved(self):
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.mkdir(parents=True)
        MOD.resume_removed_provider_migration(self.record,self.selected,self.root,self.runner())
        self.assertTrue(directory.is_dir())

    def test_post_removal_retry_refuses_foreign_or_active_provider_state(self):
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.mkdir(parents=True)
        with mock.patch.object(MOD.GitHubClient,'bytes',return_value=self.payload), \
             mock.patch.object(MOD,'validate_package'):
            MOD.retain_provider_migration(self.record,self.old,self.selected,self.runner())
            with mock.patch.object(MOD,'verify_provider_absent',side_effect=MOD.ContractError('foreign active provider')):
                with self.assertRaisesRegex(MOD.ContractError,'foreign active provider'):
                    MOD.resume_removed_provider_migration(self.record,self.selected,self.root,self.runner())
        self.assertTrue(directory.exists())


    def test_unrecognized_retained_artifact_does_not_authorize_cleanup(self):
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'
        directory.mkdir(parents=True)
        evidence=self.root/'rp1-provider-migration.foreign'; evidence.mkdir(mode=0o700)
        MOD.atomic_json(evidence/'identity.json',{'selectedCommit':self.selected['commit']})
        MOD.resume_removed_provider_migration(self.record,self.selected,self.root,self.runner())
        self.assertTrue(directory.exists()); self.assertTrue(evidence.exists())

    def test_unproven_current_ownership_blocks_migration_resumption(self):
        self.record.write_text('[]')
        with self.assertRaisesRegex(MOD.ContractError,'unproven installation ownership'):
            MOD.resume_removed_provider_migration(self.record,self.selected,self.root,self.runner())
        self.assertEqual(self.record.read_text(),'[]')


    def test_development_retry_uses_original_receipt_after_owning_rollback_changes_it(self):
        previous,entrypoint,rollback=self.development_recovery_inputs()
        selected=self.release_candidate()
        with mock.patch.object(MOD,'validate_development_rollback_authority',return_value=(entrypoint,rollback)) as authority:
            evidence=MOD.retain_provider_migration(self.record,previous,selected,self.runner())
            rollback.write_text('{"checkpoint":"rolled-back"}')
            MOD.retain_provider_migration(self.record,previous,selected,self.runner())
        authority.assert_called_once()
        self.assertEqual(digest((evidence/'previous-development-rollback.json').read_bytes()),previous['rollbackRecordSha256'])
        directory=self.root/'etc/systemd/system/rp1-gpclk-route-manager@.service.d'; directory.mkdir(parents=True)
        with mock.patch.object(MOD,'verify_provider_absent') as absence:
            MOD.resume_removed_provider_migration(self.record,selected,self.root,self.runner())
        absence.assert_called_once(); self.assertFalse(directory.exists())

    def test_changed_retained_development_receipt_is_not_accepted(self):
        previous,entrypoint,rollback=self.development_recovery_inputs(); selected=self.release_candidate()
        with mock.patch.object(MOD,'validate_development_rollback_authority',return_value=(entrypoint,rollback)):
            evidence=MOD.retain_provider_migration(self.record,previous,selected,self.runner())
        (evidence/'previous-development-rollback.json').write_text('changed')
        with self.assertRaisesRegex(MOD.ContractError,'retained development recovery input differs'):
            MOD.retain_provider_migration(self.record,previous,selected,self.runner())


    def test_same_version_development_source_change_retains_immutable_recovery(self):
        self.check_same_version_source_update(MOD.CONSUMER_PROVIDER_VERSION)

    def test_rp1_dkms_001_source_update_retains_recovery_before_removal(self):
        self.addCleanup(MOD.configure_provider_repository, MOD.REPOSITORY)
        MOD.configure_provider_repository(MOD.MIGRATION_REPOSITORY)
        self.selected['version'] = '0.0.1'
        self.check_same_version_source_update('0.0.1')

    def check_same_version_source_update(self, version):
        previous,entrypoint,rollback=self.development_recovery_inputs()
        previous['productVersion']=version
        previous['compatibilityIdentity']=MOD.development_compatibility(version)
        write_record(self.record, previous); args=self.plan()
        def remove(*_):
            evidence=list(self.root.glob('rp1-provider-migration.*/previous-development-rollback.json'))
            self.assertEqual(len(evidence),1)
            self.assertEqual(digest(evidence[0].read_bytes()),previous['rollbackRecordSha256'])
            self.record.unlink()
        with mock.patch.object(MOD,'validate_candidate_before_update'), \
             mock.patch.object(MOD,'validate_development_rollback_authority',return_value=(entrypoint,rollback)), \
             mock.patch.object(MOD,'remove_owned_provider',side_effect=remove) as owned_remove, \
             mock.patch.object(MOD,'verify_provider_absent'), \
             mock.patch.object(MOD,'apply_development') as install:
            MOD.apply(args,self.runner())
        owned_remove.assert_called_once(); install.assert_called_once()


if __name__ == "__main__":
    unittest.main(verbosity=2)
