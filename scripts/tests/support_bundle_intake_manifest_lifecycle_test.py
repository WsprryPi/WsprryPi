#!/usr/bin/env python3

from __future__ import annotations

import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/maintainer"))
import manage_support_bundle_intake_manifest as lifecycle
import prepare_support_bundle_intake_manifest as preparation


RAW_PUBLIC = bytes(range(32))
SIGNATURE = bytes(range(64))
SIGNING_ID = "wsprrypi-intake-2099-01"
BUNDLE_ID = "wsprrypi-bundle-2099-01"


def encode_recipient(payload: bytes) -> str:
    accumulator = bits = 0
    values = []
    for byte in payload:
        accumulator = (accumulator << 8) | byte; bits += 8
        while bits >= 5:
            bits -= 5; values.append((accumulator >> bits) & 31)
    if bits: values.append((accumulator << (5 - bits)) & 31)
    expanded = [ord(c) >> 5 for c in "age"] + [0] + [ord(c) & 31 for c in "age"]
    polymod = preparation.bech32_polymod(expanded + values + [0] * 6) ^ 1
    checksum = [(polymod >> (5 * (5 - i))) & 31 for i in range(6)]
    return "age1" + "".join(preparation.BECH32_CHARSET[v] for v in values + checksum)


RECIPIENT = encode_recipient(bytes(reversed(range(32))))


def fake_openssl(root: Path, mode: str = "success") -> Path:
    path = root / f"openssl-{mode}"
    path.write_text(f'''#!/usr/bin/env python3
import sys
mode={mode!r}
if sys.argv[1] == "pkey":
 sys.stdout.buffer.write({preparation.ED25519_SPKI_PREFIX + RAW_PUBLIC!r}); raise SystemExit(0)
if sys.argv[1] == "pkeyutl" and "-sign" in sys.argv:
 sys.stdout.buffer.write({SIGNATURE!r}); raise SystemExit(0)
if sys.argv[1] == "pkeyutl" and "-verify" in sys.argv:
 raise SystemExit(23 if mode == "verify-failure" else 0)
raise SystemExit(64)
''', encoding="utf-8")
    path.chmod(0o755)
    return path


class LifecycleTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-lifecycle-")
        self.root = Path(self.temporary.name)
        self.staging = self.root / "staging"; self.staging.mkdir(mode=0o700)
        self.private = self.root / "private.pem"; self.private.write_text("test private\n"); self.private.chmod(0o400)
        self.signing = self.root / "signing.json"
        self.bundle = self.root / "bundle.json"
        self.openssl = fake_openssl(self.root)
        self.write_metadata(RAW_PUBLIC)
        self.prepare_initial()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_metadata(self, raw_public: bytes) -> None:
        encoded = base64.urlsafe_b64encode(raw_public).decode().rstrip("=")
        self.signing.write_text(json.dumps({
            "schema_version": 1, "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing", "algorithm": "Ed25519",
            "key_id": SIGNING_ID, "public_key": {"encoding": "base64url", "value": encoded},
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(raw_public).hexdigest()},
        }) + "\n")
        self.bundle.write_text(json.dumps({
            "schema_version": 1, "project_id": "wsprrypi", "purpose": "support_bundle_encryption",
            "algorithm": "age-x25519", "key_id": BUNDLE_ID, "recipient": RECIPIENT,
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(RECIPIENT.encode()).hexdigest()},
        }) + "\n")

    def prepare_initial(self) -> None:
        preparation.prepare(openssl=self.openssl, private_key=self.private,
            signing_metadata_path=self.signing, bundle_metadata_path=self.bundle,
            staging_directory=self.staging, generation=1,
            published_at="2099-01-01T01:00:00Z", expires_at="2099-04-01T01:00:00Z",
            status="active", minimum_client_protocol=1, minimum_upload_version="1.3.0",
            request_url="https://www.dropbox.com/request/initial-test",
            release_url="https://github.com/WsprryPi/WsprryPi/releases/latest", user_message=None)

    def manage(self, operation: str, approve: bool = False, **changes):
        values = dict(operation=operation, approve=approve, openssl=self.openssl,
            staging_root=self.staging, signing_metadata_path=self.signing,
            bundle_metadata_path=self.bundle, private_key=self.private,
            published_at="2099-02-01T01:00:00Z", expires_at="2099-07-01T01:00:00Z",
            request_url=None, minimum_upload_version=None, user_message=None)
        values.update(changes)
        if operation == "inspect":
            values.update(bundle_metadata_path=None, private_key=None, published_at=None,
                          expires_at=None, request_url=None, minimum_upload_version=None,
                          user_message=None)
            values.update(changes)
        return lifecycle.manage(**values)

    def test_inspect_authenticates_without_private_and_discloses_no_routing(self) -> None:
        before = (self.staging / "generation-1/intake.json").read_bytes()
        result = lifecycle.manage(operation="inspect", approve=False, openssl=self.openssl,
            staging_root=self.staging, signing_metadata_path=self.signing)
        self.assertEqual(result.status, lifecycle.LifecycleStatus.inspected)
        self.assertEqual(result.generation, 1)
        self.assertEqual((self.staging / "generation-1/intake.json").read_bytes(), before)
        self.assertEqual(len(list(self.staging.iterdir())), 1)

    def test_rotate_dry_run_then_approved_preserves_predecessor(self) -> None:
        predecessor = (self.staging / "generation-1/intake.json").read_bytes()
        proposed = self.manage("rotate", request_url="https://www.dropbox.com/request/replacement-test",
                               minimum_upload_version="1.4.0", user_message="Use the replacement")
        self.assertEqual(proposed.status, lifecycle.LifecycleStatus.proposed)
        self.assertFalse((self.staging / "generation-2").exists())
        committed = self.manage("rotate", True,
            request_url="https://www.dropbox.com/request/replacement-test",
            minimum_upload_version="1.4.0", user_message="Use the replacement")
        self.assertEqual(committed.status, lifecycle.LifecycleStatus.committed)
        successor = json.loads((self.staging / "generation-2/intake.json").read_text())
        self.assertEqual(successor["generation"], 2)
        self.assertEqual(successor["minimum_upload_version"], "1.4.0")
        self.assertEqual((self.staging / "generation-1/intake.json").read_bytes(), predecessor)

    def test_disable_and_renew_own_only_documented_fields(self) -> None:
        disabled = self.manage("disable", True, user_message="Uploads temporarily disabled")
        self.assertEqual(disabled.intake_status, "disabled")
        value2 = json.loads((self.staging / "generation-2/intake.json").read_text())
        self.assertNotIn("request_url", value2)
        self.assertEqual(value2["release_url"], "https://github.com/WsprryPi/WsprryPi/releases/latest")
        renewed = self.manage("renew", True, published_at="2099-03-01T01:00:00Z",
                              expires_at="2099-10-01T01:00:00Z")
        self.assertEqual(renewed.generation, 3)
        value3 = json.loads((self.staging / "generation-3/intake.json").read_text())
        for field in ("status", "minimum_client_protocol", "minimum_upload_version",
                      "release_url", "user_message", "bundle_encryption_key_id"):
            self.assertEqual(value3[field], value2[field])

    def test_rotate_reenables_a_disabled_intake(self) -> None:
        self.manage("disable", True, user_message="Uploads temporarily disabled")
        rotated = self.manage("rotate", True, published_at="2099-03-01T01:00:00Z",
                              expires_at="2099-10-01T01:00:00Z",
                              request_url="https://www.dropbox.com/request/restored-test")
        self.assertEqual(rotated.generation, 3)
        self.assertEqual(rotated.intake_status, "active")
        value = json.loads((self.staging / "generation-3/intake.json").read_text())
        self.assertEqual(value["request_url"], "https://www.dropbox.com/request/restored-test")

    def test_approval_and_monotonic_time_rules_fail_closed(self) -> None:
        with self.assertRaises(lifecycle.LifecycleError):
            self.manage("disable", True, private_key=None, user_message="Disabled")
        with self.assertRaises(lifecycle.LifecycleError):
            self.manage("renew", published_at="2098-01-01T00:00:00Z")
        with self.assertRaises(lifecycle.LifecycleError):
            self.manage("renew", expires_at="2099-03-01T00:00:00Z")
        with self.assertRaises(lifecycle.LifecycleError):
            self.manage("disable", request_url="https://www.dropbox.com/request/ignored",
                        user_message="Disabled")
        with self.assertRaises(lifecycle.LifecycleError):
            lifecycle.manage(operation="inspect", approve=True, openssl=self.openssl,
                staging_root=self.staging, signing_metadata_path=self.signing)
        self.assertFalse((self.staging / "generation-2").exists())

    def test_inventory_rejects_partial_unexpected_gap_and_unsafe_mode(self) -> None:
        unexpected = self.staging / "note"; unexpected.write_text("x")
        with self.assertRaises(lifecycle.LifecycleError): self.manage("inspect")
        unexpected.unlink()
        partial = self.staging / ".generation-2.partial"; partial.mkdir()
        with self.assertRaises(lifecycle.LifecycleError): self.manage("inspect")
        partial.rmdir()
        generation1 = self.staging / "generation-1"; generation2 = self.staging / "generation-2"
        generation1.rename(generation2)
        with self.assertRaises(lifecycle.LifecycleError): self.manage("inspect")
        generation2.rename(generation1)
        generation1.chmod(0o755)
        with self.assertRaises(lifecycle.LifecycleError): self.manage("inspect")

    def test_signature_mutation_and_wrong_verifier_fail_before_disclosure(self) -> None:
        manifest = self.staging / "generation-1/intake.json"
        manifest.write_bytes(manifest.read_bytes().replace(b'"generation": 1', b'"generation": 9'))
        with self.assertRaises(lifecycle.LifecycleError): self.manage("inspect")
        shutil.rmtree(self.staging); self.staging.mkdir(mode=0o700); self.prepare_initial()
        with self.assertRaises(lifecycle.LifecycleError):
            self.manage("inspect", openssl=fake_openssl(self.root, "verify-failure"))

    def test_delegated_uncertain_status_propagates(self) -> None:
        fake_result = preparation.PreparationResult(preparation.PreparationStatus.committed_sync_uncertain,
            self.staging / "generation-2/intake.json", self.staging / "generation-2/intake.json.sig",
            2, SIGNING_ID, BUNDLE_ID, "a" * 64)
        with mock.patch.object(lifecycle.preparation, "prepare", return_value=fake_result):
            result = self.manage("disable", True, user_message="Disabled")
        self.assertEqual(result.status, lifecycle.LifecycleStatus.committed_sync_uncertain)

    def test_cli_inspect_and_proposal_are_non_disclosing_and_immutable(self) -> None:
        tool = ROOT / "scripts/maintainer/manage_support_bundle_intake_manifest.py"
        request_url = "https://www.dropbox.com/request/cli-distinct-test"
        message = "distinct lifecycle private message"
        common = ["python3", str(tool), "--openssl", str(self.openssl),
                  "--staging-root", str(self.staging), "--signing-metadata", str(self.signing)]
        inspected = subprocess.run([common[0], common[1], "inspect", *common[2:]],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   check=False, timeout=30)
        self.assertEqual(inspected.returncode, 0)
        current_bytes = (self.staging / "generation-1/intake.json").read_bytes()
        proposed = subprocess.run([common[0], common[1], "rotate", *common[2:],
            "--published-at", "2099-02-01T01:00:00Z", "--expires-at", "2099-07-01T01:00:00Z",
            "--request-url", request_url, "--user-message", message],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False, timeout=30)
        self.assertEqual(proposed.returncode, 0)
        disclosed = inspected.stdout + inspected.stderr + proposed.stdout + proposed.stderr
        self.assertNotIn(request_url, disclosed)
        self.assertNotIn(message, disclosed)
        self.assertNotIn(base64.urlsafe_b64encode(SIGNATURE).decode().rstrip("="), disclosed)
        self.assertFalse((self.staging / "generation-2").exists())
        self.assertEqual((self.staging / "generation-1/intake.json").read_bytes(), current_bytes)

    @unittest.skipUnless(os.environ.get("WSPRRYPI_REAL_OPENSSL"), "real OpenSSL not requested")
    def test_real_openssl_authenticates_and_rotates_exact_successor(self) -> None:
        executable = Path(os.environ["WSPRRYPI_REAL_OPENSSL"])
        shutil.rmtree(self.staging); self.staging.mkdir(mode=0o700)
        self.private.unlink()
        subprocess.run([str(executable), "genpkey", "-algorithm", "ED25519", "-out", str(self.private)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        self.private.chmod(0o400)
        der = subprocess.run([str(executable), "pkey", "-in", str(self.private), "-pubout", "-outform", "DER"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout
        self.write_metadata(der[len(preparation.ED25519_SPKI_PREFIX):])
        self.openssl = executable; self.prepare_initial()
        predecessor = (self.staging / "generation-1/intake.json").read_bytes()
        result = self.manage("rotate", True, request_url="https://www.dropbox.com/request/real-fixture")
        self.assertEqual(result.status, lifecycle.LifecycleStatus.committed)
        self.assertEqual((self.staging / "generation-1/intake.json").read_bytes(), predecessor)
        current = lifecycle.authenticate_current(self.staging, executable, self.signing)
        self.assertEqual(current.value["generation"], 2)
        successor_path = self.staging / "generation-2/intake.json"
        exact = successor_path.read_bytes()
        successor_path.write_bytes(exact.replace(b"real-fixture", b"evil-fixture"))
        with self.assertRaises(lifecycle.LifecycleError):
            lifecycle.authenticate_current(self.staging, executable, self.signing)
        successor_path.write_bytes(exact)


if __name__ == "__main__":
    unittest.main()
