#!/usr/bin/env python3

from __future__ import annotations

import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/prepare_support_bundle_intake_manifest.py"
SPEC = importlib.util.spec_from_file_location("intake_manifest_preparation", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

RAW_PUBLIC = bytes(range(32))
SIGNING_ID = "wsprrypi-intake-2099-01"
BUNDLE_ID = "wsprrypi-bundle-2099-01"
SIGNATURE = bytes(range(64))


def encode_recipient(payload: bytes) -> str:
    accumulator = bits = 0
    values = []
    for byte in payload:
        accumulator = (accumulator << 8) | byte; bits += 8
        while bits >= 5:
            bits -= 5; values.append((accumulator >> bits) & 31)
    if bits:
        values.append((accumulator << (5 - bits)) & 31)
    expanded = [ord(c) >> 5 for c in "age"] + [0] + [ord(c) & 31 for c in "age"]
    polymod = MODULE.bech32_polymod(expanded + values + [0] * 6) ^ 1
    checksum = [(polymod >> (5 * (5 - i))) & 31 for i in range(6)]
    return "age1" + "".join(MODULE.BECH32_CHARSET[v] for v in values + checksum)


RECIPIENT = encode_recipient(bytes(reversed(range(32))))


def make_fake_openssl(directory: Path, mode: str = "success") -> Path:
    path = directory / f"openssl-{mode}"
    source = f'''#!/usr/bin/env python3
import sys
mode = {mode!r}
if sys.argv[1] == "pkey":
    value = {MODULE.ED25519_SPKI_PREFIX + RAW_PUBLIC!r}
    if mode == "wrong-public": value = value[:-1] + b"x"
    sys.stdout.buffer.write(value); raise SystemExit(0)
if sys.argv[1] == "pkeyutl" and "-sign" in sys.argv:
    if mode == "sign-failure": raise SystemExit(23)
    value = {SIGNATURE!r}
    if mode == "short-signature": value = value[:-1]
    sys.stdout.buffer.write(value); raise SystemExit(0)
if sys.argv[1] == "pkeyutl" and "-verify" in sys.argv:
    raise SystemExit(24 if mode == "verify-failure" else 0)
raise SystemExit(64)
'''
    path.write_text(source, encoding="utf-8"); path.chmod(0o755)
    return path


class PreparationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-manifest-preparation-")
        self.root = Path(self.temporary.name)
        self.private = self.root / "private.pem"
        self.private.write_text("test private key\n", encoding="ascii"); self.private.chmod(0o400)
        self.signing = self.root / "signing.json"
        self.bundle = self.root / "bundle.json"
        self.staging = self.root / "staging"; self.staging.mkdir(mode=0o700)
        self.write_metadata()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_metadata(self) -> None:
        public_text = base64.urlsafe_b64encode(RAW_PUBLIC).decode("ascii").rstrip("=")
        self.signing.write_text(json.dumps({
            "schema_version": 1, "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing", "algorithm": "Ed25519",
            "key_id": SIGNING_ID, "public_key": {"encoding": "base64url", "value": public_text},
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(RAW_PUBLIC).hexdigest()},
        }) + "\n", encoding="utf-8")
        self.bundle.write_text(json.dumps({
            "schema_version": 1, "project_id": "wsprrypi", "purpose": "support_bundle_encryption",
            "algorithm": "age-x25519", "key_id": BUNDLE_ID, "recipient": RECIPIENT,
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(RECIPIENT.encode()).hexdigest()},
        }) + "\n", encoding="utf-8")

    def prepare(self, executable: Path | None = None, **changes):
        values = dict(openssl=executable or make_fake_openssl(self.root),
                      private_key=self.private, signing_metadata_path=self.signing,
                      bundle_metadata_path=self.bundle, staging_directory=self.staging,
                      generation=7, published_at="2099-01-01T01:00:00Z",
                      expires_at="2099-04-01T01:00:00Z", status="active",
                      minimum_client_protocol=1, minimum_upload_version="1.3.0",
                      request_url="https://www.dropbox.com/request/test-only",
                      release_url="https://github.com/WsprryPi/WsprryPi/releases/latest",
                      user_message=None)
        values.update(changes)
        return MODULE.prepare(**values)

    def test_exact_active_pair_is_signed_and_non_disclosing(self) -> None:
        result = self.prepare()
        self.assertEqual(result.status, MODULE.PreparationStatus.committed)
        manifest_bytes = result.manifest_path.read_bytes()
        self.assertTrue(manifest_bytes.endswith(b"\n"))
        manifest = json.loads(manifest_bytes)
        self.assertEqual(manifest["generation"], 7)
        self.assertEqual(manifest["bundle_encryption_key_id"], BUNDLE_ID)
        envelope = json.loads(result.signature_path.read_text())
        self.assertEqual(envelope["key_id"], SIGNING_ID)
        self.assertEqual(base64.urlsafe_b64decode(envelope["signature"] + "=="), SIGNATURE)
        self.assertEqual(result.manifest_sha256, hashlib.sha256(manifest_bytes).hexdigest())
        self.assertEqual(stat.S_IMODE(result.manifest_path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(result.signature_path.stat().st_mode), 0o600)
        self.assertNotIn("test private key", result.signature_path.read_text())

    def test_disabled_omits_request_and_active_requires_it(self) -> None:
        result = self.prepare(status="disabled", request_url=None, user_message="Temporarily unavailable")
        self.assertNotIn("request_url", json.loads(result.manifest_path.read_text()))
        other = self.root / "other"; other.mkdir(mode=0o700); self.staging = other
        with self.assertRaises(MODULE.PreparationError):
            self.prepare(request_url=None)

    def test_policy_rejects_versions_times_urls_and_messages(self) -> None:
        cases = ({"generation": 0}, {"minimum_client_protocol": 0},
                 {"minimum_upload_version": "01.2.3"},
                 {"expires_at": "2099-01-01T00:00:00Z"},
                 {"request_url": "https://evil.example/request/x"},
                 {"release_url": "https://github.com/other/releases/latest"},
                 {"user_message": "x" * 1025})
        for changes in cases:
            with self.subTest(changes=changes), self.assertRaises(MODULE.PreparationError):
                self.prepare(**changes)
            self.assertFalse(any(self.staging.iterdir()))

    def test_metadata_and_private_mismatch_fail_before_publication(self) -> None:
        metadata = json.loads(self.signing.read_text()); metadata["fingerprint"]["value"] = "0" * 64
        self.signing.write_text(json.dumps(metadata), encoding="utf-8")
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        self.write_metadata()
        with self.assertRaises(MODULE.PreparationError): self.prepare(make_fake_openssl(self.root, "wrong-public"))
        self.private.chmod(0o600)
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        self.assertFalse(any(self.staging.iterdir()))

    def test_metadata_schema_duplicate_canonicality_and_bundle_fingerprint(self) -> None:
        original_signing = self.signing.read_text()
        signing = json.loads(original_signing); signing["unknown"] = True
        self.signing.write_text(json.dumps(signing), encoding="utf-8")
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        duplicate = original_signing.replace('"schema_version": 1',
                                             '"schema_version": 1, "schema_version": 1', 1)
        self.signing.write_text(duplicate, encoding="utf-8")
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        signing = json.loads(original_signing)
        signing["public_key"]["value"] = signing["public_key"]["value"][:-1] + "9"
        self.signing.write_text(json.dumps(signing), encoding="utf-8")
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        self.write_metadata()
        bundle = json.loads(self.bundle.read_text()); bundle["fingerprint"]["value"] = "0" * 64
        self.bundle.write_text(json.dumps(bundle), encoding="utf-8")
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        self.assertFalse(any(self.staging.iterdir()))

    def test_sign_and_verify_failures_leave_no_outputs(self) -> None:
        for mode in ("sign-failure", "short-signature", "verify-failure"):
            with self.subTest(mode=mode), self.assertRaises(MODULE.PreparationError):
                self.prepare(make_fake_openssl(self.root, mode))
            self.assertFalse(any(self.staging.iterdir()))

    def test_collisions_and_publication_failure_preserve_existing_or_roll_back(self) -> None:
        existing = self.staging / "generation-7"; existing.mkdir()
        marker = existing / "keep"; marker.write_text("keep", encoding="ascii")
        with self.assertRaises(MODULE.PreparationError): self.prepare()
        self.assertEqual(marker.read_text(), "keep"); marker.unlink(); existing.rmdir()
        with mock.patch.object(MODULE.os, "rename", side_effect=OSError("injected rename failure")):
            with self.assertRaises(OSError): self.prepare()
        self.assertFalse(any(self.staging.iterdir()))

    def test_cleanup_continues_after_one_unlink_error(self) -> None:
        real_unlink = Path.unlink
        failed = False
        def fail_manifest_cleanup(path, *args, **kwargs):
            nonlocal failed
            if Path(path).name == "intake.json" and not failed:
                failed = True
                raise OSError("injected cleanup failure")
            return real_unlink(path, *args, **kwargs)
        with mock.patch.object(MODULE.os, "rename", side_effect=OSError("enter rollback")), \
             mock.patch.object(Path, "unlink", new=fail_manifest_cleanup):
            with self.assertRaises(OSError): self.prepare()
        partial = self.staging / ".generation-7.partial"
        self.assertTrue((partial / "intake.json").exists())
        self.assertFalse((partial / "intake.json.sig").exists(), "later cleanup must continue")

    def test_directory_sync_uncertainty_retains_pair(self) -> None:
        real_fsync = MODULE.os.fsync
        calls = 0
        def fail_fourth(descriptor):
            nonlocal calls
            calls += 1
            if calls == 5: raise OSError("directory sync uncertain")
            return real_fsync(descriptor)
        with mock.patch.object(MODULE.os, "fsync", side_effect=fail_fourth):
            result = self.prepare()
        self.assertEqual(result.status, MODULE.PreparationStatus.committed_sync_uncertain)
        self.assertTrue(result.manifest_path.exists() and result.signature_path.exists())

    def test_post_rename_directory_open_failure_is_sync_uncertain(self) -> None:
        def fail_staging_sync(path):
            raise OSError("staging root open uncertain")
        result = self.prepare(_root_sync=fail_staging_sync)
        self.assertEqual(result.status, MODULE.PreparationStatus.committed_sync_uncertain)
        self.assertTrue(result.manifest_path.exists() and result.signature_path.exists())

    def test_cli_output_omits_manifest_sensitive_values(self) -> None:
        request_url = "https://www.dropbox.com/request/distinct-test-only"
        user_message = "distinct private operator message"
        completed = subprocess.run([
            "python3", str(MODULE_PATH), "--openssl", str(make_fake_openssl(self.root)),
            "--private-key", str(self.private), "--signing-metadata", str(self.signing),
            "--bundle-metadata", str(self.bundle), "--staging-directory", str(self.staging),
            "--generation", "7", "--published-at", "2099-01-01T01:00:00Z",
            "--expires-at", "2099-04-01T01:00:00Z", "--status", "active",
            "--minimum-upload-version", "1.3.0", "--request-url", request_url,
            "--release-url", "https://github.com/WsprryPi/WsprryPi/releases/latest",
            "--user-message", user_message,
        ], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
           text=True, check=False, timeout=30)
        self.assertEqual(completed.returncode, 0)
        disclosed = completed.stdout + completed.stderr
        self.assertNotIn(request_url, disclosed)
        self.assertNotIn(user_message, disclosed)
        self.assertNotIn(base64.urlsafe_b64encode(SIGNATURE).decode().rstrip("="), disclosed)
        self.assertNotIn("test private key", disclosed)

    @unittest.skipUnless(os.environ.get("WSPRRYPI_REAL_OPENSSL"), "real OpenSSL not requested")
    def test_real_openssl_signature_verifies_exact_manifest(self) -> None:
        executable = Path(os.environ["WSPRRYPI_REAL_OPENSSL"])
        self.private.unlink()
        subprocess.run([str(executable), "genpkey", "-algorithm", "ED25519", "-out", str(self.private)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        self.private.chmod(0o400)
        der = subprocess.run([str(executable), "pkey", "-in", str(self.private), "-pubout", "-outform", "DER"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout
        raw = der[len(MODULE.ED25519_SPKI_PREFIX):]
        metadata = json.loads(self.signing.read_text())
        metadata["public_key"]["value"] = base64.urlsafe_b64encode(raw).decode().rstrip("=")
        metadata["fingerprint"]["value"] = hashlib.sha256(raw).hexdigest()
        self.signing.write_text(json.dumps(metadata) + "\n", encoding="utf-8")
        result = self.prepare(executable)
        envelope = json.loads(result.signature_path.read_text())
        signature = base64.urlsafe_b64decode(envelope["signature"] + "==")
        signature_path = self.root / "signature.bin"; signature_path.write_bytes(signature)
        verified = subprocess.run([str(executable), "pkeyutl", "-verify", "-rawin", "-inkey", str(self.private),
                                   "-in", str(result.manifest_path), "-sigfile", str(signature_path)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        self.assertEqual(verified.returncode, 0)


if __name__ == "__main__":
    unittest.main()
