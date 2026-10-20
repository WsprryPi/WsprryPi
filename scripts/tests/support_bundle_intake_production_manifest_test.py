#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/prepare_support_bundle_intake_production_manifest.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("production_manifest", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

REQUEST = "https://www.dropbox.com/request/production-test-only"


class ProductionManifestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-production-manifest-")
        self.root = Path(self.temporary.name)
        self.staging = self.root / "manifests"
        self.staging.mkdir(mode=0o700)
        self.staging.chmod(0o700)
        self.paths = MODULE.ProductionPaths(
            Path("/usr/bin/openssl"), Path("/usr/bin/security"),
            self.root / "private.pem", self.root / "signing.json",
            self.root / "bundle.json", self.staging)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_production_uses_ed25519_capable_openssl_not_macos_libressl(self) -> None:
        paths = MODULE.production_paths()
        self.assertEqual(paths.openssl,
                         Path("/opt/homebrew/opt/openssl@3/bin/openssl"))
        self.assertNotEqual(paths.openssl, Path("/usr/bin/openssl"))

    def test_keychain_uses_fixed_non_disclosing_process_contract(self) -> None:
        observed = {}

        def runner(arguments, environment):
            observed["arguments"] = arguments
            observed["environment"] = environment
            return 0, (REQUEST + "\n").encode("ascii")

        with mock.patch.object(MODULE, "require_security", return_value=Path("/usr/bin/security")):
            self.assertEqual(MODULE.keychain_request_url(Path("/usr/bin/security"), runner), REQUEST)
        self.assertEqual(observed["arguments"], [
            "/usr/bin/security", "find-generic-password", "-a",
            MODULE.KEYCHAIN_ACCOUNT, "-s", MODULE.KEYCHAIN_SERVICE, "-w"])
        self.assertEqual(observed["environment"],
                         {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"})
        self.assertNotIn(REQUEST, " ".join(observed["arguments"]))

    def test_keychain_rejects_framing_bounds_policy_and_failure(self) -> None:
        invalid = [b"", b"not-a-url", (REQUEST + "\nextra").encode(), b"\xff",
                   b"x" * (MODULE.MAX_REQUEST_URL_BYTES + 1),
                   b"https://evil.example/request/test"]
        for encoded in invalid:
            with self.assertRaises(MODULE.ProductionPreparationError):
                MODULE.parse_request_url(encoded)
        with mock.patch.object(MODULE, "require_security", return_value=Path("/usr/bin/security")):
            with self.assertRaises(MODULE.ProductionPreparationError):
                MODULE.keychain_request_url(Path("/usr/bin/security"),
                                            lambda _a, _e: (1, REQUEST.encode()))

    def preparation_mocks(self):
        public = bytes(range(32))
        return mock.patch.multiple(
            MODULE.preparation,
            require_openssl=mock.DEFAULT, require_file=mock.DEFAULT,
            signing_metadata=mock.DEFAULT, bundle_metadata=mock.DEFAULT,
            bounded_tool_output=mock.DEFAULT, build_manifest=mock.DEFAULT)

    def test_proposal_propagates_exact_policy_without_preparing(self) -> None:
        with self.preparation_mocks() as patched:
            patched["require_openssl"].return_value = self.paths.openssl
            patched["require_file"].side_effect = lambda path, *_args: path
            patched["signing_metadata"].return_value = ("wsprrypi-intake-2099-01", bytes(range(32)))
            patched["bundle_metadata"].return_value = ("wsprrypi-bundle-2099-01", "age1test")
            patched["bounded_tool_output"].return_value = (
                0, MODULE.preparation.ED25519_SPKI_PREFIX + bytes(range(32)))
            patched["build_manifest"].return_value = b"deterministic manifest\n"
            with mock.patch.object(MODULE.preparation, "prepare") as prepare:
                result = MODULE.prepare_generation_1(
                    approve=False, paths=self.paths,
                    published_at="2099-01-01T00:00:00Z",
                    expires_at="2099-04-01T00:00:00Z",
                    request_provider=lambda _path: REQUEST)
            prepare.assert_not_called()
        self.assertEqual(result.status, MODULE.ProductionPreparationStatus.proposed)
        arguments = patched["build_manifest"].call_args.kwargs
        self.assertEqual(arguments, {
            "generation": 1, "published_at": "2099-01-01T00:00:00Z",
            "expires_at": "2099-04-01T00:00:00Z", "status": "active",
            "minimum_client_protocol": 1, "minimum_upload_version": "3.2.0",
            "request_url": REQUEST,
            "release_url": "https://github.com/WsprryPi/WsprryPi/releases/latest",
            "user_message": None, "bundle_key_id": "wsprrypi-bundle-2099-01"})
        self.assertNotIn(REQUEST, repr(result))

    def test_approval_delegates_exact_policy_and_maps_durability(self) -> None:
        prepared = MODULE.preparation.PreparationResult(
            MODULE.preparation.PreparationStatus.committed,
            self.staging / "generation-1/intake.json",
            self.staging / "generation-1/intake.json.sig", 1,
            "wsprrypi-intake-2099-01", "wsprrypi-bundle-2099-01", "a" * 64)
        with self.preparation_mocks() as patched:
            patched["require_openssl"].return_value = self.paths.openssl
            patched["require_file"].side_effect = lambda path, *_args: path
            patched["signing_metadata"].return_value = ("wsprrypi-intake-2099-01", bytes(range(32)))
            patched["bundle_metadata"].return_value = ("wsprrypi-bundle-2099-01", "age1test")
            patched["bounded_tool_output"].return_value = (
                0, MODULE.preparation.ED25519_SPKI_PREFIX + bytes(range(32)))
            patched["build_manifest"].return_value = b"deterministic manifest\n"
            with mock.patch.object(MODULE.preparation, "require_staging",
                                   return_value=self.staging), \
                 mock.patch.object(MODULE.preparation, "prepare", return_value=prepared) as prepare:
                result = MODULE.prepare_generation_1(
                    approve=True, paths=self.paths,
                    published_at="2099-01-01T00:00:00Z",
                    expires_at="2099-04-01T00:00:00Z",
                    request_provider=lambda _path: REQUEST)
        self.assertEqual(result.status, MODULE.ProductionPreparationStatus.committed)
        self.assertEqual(prepare.call_args.kwargs["request_url"], REQUEST)
        self.assertEqual(prepare.call_args.kwargs["minimum_upload_version"], "3.2.0")
        self.assertNotIn(REQUEST, repr(result))

    def test_keychain_failure_precedes_manifest_construction_and_signing(self) -> None:
        with mock.patch.object(MODULE.preparation, "require_openssl",
                               return_value=self.paths.openssl), \
             mock.patch.object(MODULE.preparation, "require_file",
                               side_effect=lambda path, *_args: path), \
             mock.patch.object(MODULE.preparation, "build_manifest") as build, \
             mock.patch.object(MODULE.preparation, "prepare") as prepare:
            with self.assertRaises(MODULE.ProductionPreparationError):
                MODULE.prepare_generation_1(
                    approve=True, paths=self.paths,
                    published_at="2099-01-01T00:00:00Z",
                    expires_at="2099-04-01T00:00:00Z",
                    request_provider=lambda _path: (_ for _ in ()).throw(
                        MODULE.ProductionPreparationError("unavailable")))
        build.assert_not_called(); prepare.assert_not_called()

    def test_cli_failure_is_generic_and_non_disclosing(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.object(MODULE, "prepare_generation_1",
                               side_effect=MODULE.ProductionPreparationError(REQUEST)):
            with redirect_stdout(stdout), redirect_stderr(stderr):
                code = MODULE.main(["--published-at", "2099-01-01T00:00:00Z",
                                    "--expires-at", "2099-04-01T00:00:00Z"])
        self.assertEqual(code, 1)
        self.assertNotIn(REQUEST, stdout.getvalue() + stderr.getvalue())
        self.assertEqual(stderr.getvalue(), "production manifest preparation failed\n")


if __name__ == "__main__":
    unittest.main()
