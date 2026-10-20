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
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/provision_support_bundle_intake_signing_key.py"
SPEC = importlib.util.spec_from_file_location("intake_signing_key_provisioning", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

KEY_ID = "wsprrypi-intake-2026-01"
RAW_PUBLIC = bytes(range(32))
PUBLIC_DER = MODULE.ED25519_SPKI_PREFIX + RAW_PUBLIC
PRIVATE = "-----BEGIN PRIVATE KEY-----\nTEST-PRIVATE-CONTENT\n-----END PRIVATE KEY-----\n"


def make_openssl(directory: Path, mode: str = "success") -> Path:
    path = directory / f"openssl-{mode}"
    source = f'''#!/usr/bin/env python3
import pathlib, sys
mode = {mode!r}
if len(sys.argv) >= 2 and sys.argv[1] == "genpkey":
    output = pathlib.Path(sys.argv[sys.argv.index("-out") + 1])
    output.write_text({PRIVATE!r}, encoding="ascii")
    raise SystemExit(23 if mode == "generate-failure" else 0)
if len(sys.argv) >= 2 and sys.argv[1] == "pkey":
    if mode == "derive-failure":
        raise SystemExit(24)
    value = {PUBLIC_DER!r}
    if mode == "wrong-algorithm":
        value = bytes.fromhex("302a300506032b6571032100") + {RAW_PUBLIC!r}
    if mode == "trailing-data":
        value += b"x"
    if mode == "oversized":
        value = b"x" * 257
    sys.stdout.buffer.write(value)
    raise SystemExit(0)
raise SystemExit(64)
'''
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)
    return path


class ProvisioningTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-signing-provisioning-")
        self.root = Path(self.temporary.name)
        self.private = self.root / "private"
        self.private.mkdir(mode=0o700)
        self.public = self.root / "public.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def provision(self, executable: Path | None = None):
        return MODULE.provision(executable or make_openssl(self.root), self.private,
                                self.public, KEY_ID, "2026-08-17T12:00:00Z")

    def test_success_permissions_and_exact_public_metadata(self) -> None:
        identity, public = self.provision()
        self.assertEqual(stat.S_IMODE(identity.stat().st_mode), 0o400)
        self.assertEqual(stat.S_IMODE(public.stat().st_mode), 0o600)
        self.assertEqual(identity.read_text(encoding="ascii"), PRIVATE)
        encoded = public.read_text(encoding="utf-8")
        self.assertNotIn("TEST-PRIVATE-CONTENT", encoded)
        metadata = json.loads(encoded)
        expected_public = base64.urlsafe_b64encode(RAW_PUBLIC).decode("ascii").rstrip("=")
        self.assertEqual(metadata, {
            "schema_version": 1,
            "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing",
            "algorithm": "Ed25519",
            "key_id": KEY_ID,
            "public_key": {"encoding": "base64url", "value": expected_public},
            "created_at_utc": "2026-08-17T12:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(RAW_PUBLIC).hexdigest()},
        })
        self.assertEqual(base64.urlsafe_b64decode(expected_public + "="), RAW_PUBLIC)

    def test_command_output_contains_paths_but_no_private_content(self) -> None:
        completed = subprocess.run(
            ["python3", str(MODULE_PATH), "--openssl", str(make_openssl(self.root)),
             "--private-directory", str(self.private), "--public-output", str(self.public),
             "--key-id", KEY_ID, "--created-at", "2026-08-17T12:00:00Z"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False, timeout=30,
        )
        self.assertEqual(completed.returncode, 0)
        self.assertIn("private signing key created:", completed.stdout)
        self.assertIn("public metadata created:", completed.stdout)
        self.assertNotIn("TEST-PRIVATE-CONTENT", completed.stdout + completed.stderr)

    def test_rejects_failures_and_noncanonical_public_der_without_outputs(self) -> None:
        for mode in ("generate-failure", "derive-failure", "wrong-algorithm",
                     "trailing-data", "oversized"):
            with self.subTest(mode=mode):
                with self.assertRaises(MODULE.ProvisioningError):
                    self.provision(make_openssl(self.root, mode))
                self.assertFalse(any(self.private.iterdir()))
                self.assertFalse(self.public.exists())
                self.assertFalse(any(p.name.endswith(".partial") for p in self.root.rglob("*")))

    def test_rejects_unsafe_inputs_and_collisions(self) -> None:
        with self.assertRaises(MODULE.ProvisioningError):
            MODULE.provision(make_openssl(self.root), self.private, self.public,
                             "wsprrypi-bundle-2026-01", "2026-08-17T12:00:00Z")
        executable = make_openssl(self.root)
        link = self.root / "openssl-link"
        link.symlink_to(executable)
        with self.assertRaises(MODULE.ProvisioningError):
            self.provision(link)
        self.private.chmod(0o755)
        with self.assertRaises(MODULE.ProvisioningError):
            self.provision(executable)
        self.private.chmod(0o700)
        identity, public = self.provision(executable)
        before = (identity.read_bytes(), public.read_bytes())
        with self.assertRaises(MODULE.ProvisioningError):
            self.provision(executable)
        self.assertEqual((identity.read_bytes(), public.read_bytes()), before)

    def test_every_publication_step_rolls_back(self) -> None:
        real_link = MODULE.os.link
        real_unlink = Path.unlink
        identity = self.private / f"{KEY_ID}.ed25519-private.pem"
        identity_partial = f".{KEY_ID}.ed25519-private.pem.partial"
        public_partial = f".{self.public.name}.partial"

        for target in (identity.name, self.public.name):
            with self.subTest(link=target):
                def fail_link(source, destination, target=target):
                    if Path(destination).name == target:
                        raise OSError("injected link failure")
                    return real_link(source, destination)
                with mock.patch.object(MODULE.os, "link", side_effect=fail_link):
                    with self.assertRaises(OSError):
                        self.provision()
                self.assertFalse(identity.exists())
                self.assertFalse(self.public.exists())
                self.assertFalse(any(p.name.endswith(".partial") for p in self.root.rglob("*")))

        for target in (identity_partial, public_partial):
            with self.subTest(unlink=target):
                failed = False
                def fail_unlink(path, *args, target=target, **kwargs):
                    nonlocal failed
                    if Path(path).name == target and not failed:
                        failed = True
                        raise OSError("injected unlink failure")
                    return real_unlink(path, *args, **kwargs)
                with mock.patch.object(Path, "unlink", new=fail_unlink):
                    with self.assertRaises(OSError):
                        self.provision()
                self.assertFalse(identity.exists())
                self.assertFalse(self.public.exists())
                self.assertFalse(any(p.name.endswith(".partial") for p in self.root.rglob("*")))

    def test_rollback_cleanup_continues_after_one_error(self) -> None:
        real_unlink = Path.unlink
        entered = False
        cleanup_failed = False
        identity = self.private / f"{KEY_ID}.ed25519-private.pem"
        def fail_operation_then_cleanup(path, *args, **kwargs):
            nonlocal entered, cleanup_failed
            candidate = Path(path)
            if candidate.name == f".{self.public.name}.partial" and not entered:
                entered = True
                raise OSError("enter rollback")
            if candidate.name == self.public.name and entered:
                cleanup_failed = True
                raise OSError("persistent public cleanup failure")
            return real_unlink(path, *args, **kwargs)
        with mock.patch.object(Path, "unlink", new=fail_operation_then_cleanup):
            with self.assertRaises(OSError):
                self.provision()
        self.assertTrue(cleanup_failed)
        self.assertTrue(self.public.exists())
        self.assertFalse(identity.exists(), "private cleanup must continue")
        self.public.unlink()

    @unittest.skipUnless(os.environ.get("WSPRRYPI_REAL_OPENSSL"), "real OpenSSL not requested")
    def test_real_openssl_public_metadata_matches_private_key(self) -> None:
        executable = Path(os.environ["WSPRRYPI_REAL_OPENSSL"])
        identity, public = self.provision(executable)
        metadata = json.loads(public.read_text(encoding="utf-8"))
        derived = subprocess.run(
            [str(executable), "pkey", "-in", str(identity), "-pubout", "-outform", "DER"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            check=True, timeout=30,
        ).stdout
        raw = MODULE.raw_ed25519_public_key(derived)
        self.assertEqual(metadata["public_key"]["value"],
                         base64.urlsafe_b64encode(raw).decode("ascii").rstrip("="))
        self.assertEqual(metadata["fingerprint"]["value"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(stat.S_IMODE(identity.stat().st_mode), 0o400)
        self.assertNotIn("PRIVATE KEY", public.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
